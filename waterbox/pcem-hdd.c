/* pcem-hdd.c - hard disks that the guest can write to, and that leave again.
 *
 * This replaces upstream's src/hdd_file.c in the waterbox build. Upstream's is
 * stdio straight onto the user's file: fopen64(fn, "rb+"), fseeko64, fwrite.
 * That is exactly the thing every TASVideos PCem page warns about - "PCem will
 * change your files, which will cause desyncs" - and it is not available to us
 * anyway, because a project's files are mounted read-only and hash-bound.
 *
 * So, following the DOSBox-X core (waterbox/sparse-disk.h there), the slot's
 * image SEEDS a writable disk instead of being written in place:
 *
 *   - the image stays a read-only mount, opened once and read through for the
 *     life of the machine. A 4 GiB Windows XP image is never copied anywhere;
 *   - a write lands in a block held in guest memory, and later reads of that
 *     block come from it. Untouched regions cost nothing at all;
 *   - the whole disk - seed underneath, written blocks on top - is what the
 *     save-data channel hands back out, so an exported image drops straight
 *     back into the same slot and the machine carries on from it.
 *
 * Three decisions that are this file's own, and the reasons:
 *
 * 1. THE BLOCK IS 4096 BYTES, one guest page. DOSBox-X uses 64 KiB. A block is
 *    machine state and lands in every savestate, and miniBox's dirty tracking
 *    is page-granular (memblock.c), so a 64 KiB block makes a single 512-byte
 *    sector write cost sixteen dirty pages instead of one. Blocks are handed
 *    out of page-aligned slabs for the same reason: a plain malloc(4096) comes
 *    back with a header in front of it and straddles two pages, which would
 *    double the cost of every block for nothing.
 *
 * 2. A BLOCK THAT MATCHES THE SEED IS NOT HELD. This is the RPCS3 disc-mirror
 *    idea (docs/gpu-bridge, the PS3 game-install problem): bytes that are
 *    already in the read-only source do not need to be copied into the
 *    machine. An installer that rewrites a file with the same contents, or a
 *    format that writes zeros over a region that is already zero, costs
 *    nothing here.
 *
 *    The check is made only when the block is NOT yet held, and that is
 *    deliberate rather than lazy. miniBox marks a page dirty when it is
 *    written and never un-marks it on content (`set_dirty`, memblock.c:237;
 *    only stack pages are compared back against the baseline), so releasing a
 *    block that has come back to the seed would recover guest memory but NOT
 *    one byte of savestate. The saving is entirely in the allocation that
 *    never happens, so that is where the comparison goes - and the hot path,
 *    a block written over and over, does no base I/O at all.
 *
 * 3. THE OVERLAY OUTLIVES hdd_close(). PCem opens its disks from each
 *    controller's init, and resetpchard() runs those inits again - so a guest
 *    that presses Ctrl-Alt-Del re-opens every disk. A real PC's disk survives
 *    a reboot, so the overlay is keyed by file name and kept for the life of
 *    the machine; hdd_close() detaches the controller from it and nothing
 *    more.
 */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ibm.h"
#include "hdd_file.h"
#include "minivhd/minivhd.h"
#include "minivhd/minivhd_util.h"

#include "pcem-hdd.h"

#define BLK        4096
#define BLK_SHIFT  12
#define L2_SHIFT   10                    /* 1024 blocks = 4 MiB of disk per L2 */
#define L2_COUNT   (1 << L2_SHIFT)
#define SLAB_BLOCKS 256                  /* 1 MiB a slab */

typedef struct pcem_disk_t {
        int      used;
        char     name[PCEM_HDD_NAME_MAX]; /* the mount name, and the export name */
        FILE    *base;                    /* the read-only seed, raw */
        void    *vhd;                     /* MVHDMeta *, when the seed is a VHD */
        int64_t  base_len;                /* bytes the seed can answer for */
        int64_t  size;                    /* bytes the MACHINE sees */
        int64_t  nblocks;
        uint8_t ***l1;                    /* l1[i] is an L2_COUNT array of blocks */
        int64_t  nl1;
        int64_t  held;                    /* blocks in guest memory right now */
        int64_t  writes;                  /* write calls, for the measurement */
        int64_t  matched;                 /* writes that matched the seed exactly */
        int      full;                    /* the guest heap said no */
} pcem_disk;

static pcem_disk g_disks[PCEM_HDD_MAX];

/* ------------------------------------------------------- block allocation */

static uint8_t *g_slab;
static int      g_slab_used = SLAB_BLOCKS;

static uint8_t *blk_alloc(void)
{
        if (g_slab_used == SLAB_BLOCKS) {
                uint8_t *s = (uint8_t *)aligned_alloc(BLK, (size_t)BLK * SLAB_BLOCKS);
                if (!s) return NULL;
                g_slab = s;
                g_slab_used = 0;
        }
        return g_slab + (size_t)(g_slab_used++) * BLK;
}

/* ------------------------------------------------------------- the index */

static uint8_t *blk_get(pcem_disk *d, int64_t b)
{
        int64_t i = b >> L2_SHIFT;
        uint8_t **l2;
        if (i >= d->nl1 || !d->l1) return NULL;
        l2 = d->l1[i];
        return l2 ? l2[b & (L2_COUNT - 1)] : NULL;
}

static int blk_set(pcem_disk *d, int64_t b, uint8_t *blk)
{
        int64_t i = b >> L2_SHIFT;
        if (i >= d->nl1 || !d->l1) return 0;
        if (!d->l1[i]) {
                d->l1[i] = (uint8_t **)calloc(L2_COUNT, sizeof(uint8_t *));
                if (!d->l1[i]) return 0;
        }
        d->l1[i][b & (L2_COUNT - 1)] = blk;
        return 1;
}

/* ------------------------------------------------------------- the seed */

/* len bytes at off, from the seed. Anything the seed cannot answer for - past
 * its end, or a short read - is zero, which is what an unwritten sector of a
 * blank disk reads as. off and len are always sector multiples here. */
static void base_read(pcem_disk *d, int64_t off, uint8_t *dst, int32_t len)
{
        memset(dst, 0, (size_t)len);
        if (off >= d->base_len) return;
        {
                int64_t take = d->base_len - off;
                if (take > len) take = len;
                if (d->vhd) {
                        mvhd_read_sectors((MVHDMeta *)d->vhd, (int)(off / 512),
                                          (int)(take / 512), dst);
                } else if (d->base) {
                        if (fseeko64(d->base, (off64_t)off, SEEK_SET) == 0) {
                                size_t got = fread(dst, 1, (size_t)take, d->base);
                                if (got < (size_t)take)
                                        memset(dst + got, 0, (size_t)take - got);
                        }
                }
        }
}

/* ------------------------------------------------------------- read/write */

static int disk_read(pcem_disk *d, int64_t off, uint8_t *dst, int64_t len)
{
        if (!d || off < 0 || len < 0) return 0;
        if (off + len > d->size) {
                if (off >= d->size) { memset(dst, 0, (size_t)len); return 0; }
                memset(dst + (d->size - off), 0, (size_t)(off + len - d->size));
                len = d->size - off;
        }
        while (len > 0) {
                int64_t b      = off >> BLK_SHIFT;
                int32_t within = (int32_t)(off & (BLK - 1));
                int32_t take   = BLK - within;
                uint8_t *blk;
                if (take > len) take = (int32_t)len;
                blk = blk_get(d, b);
                if (blk) memcpy(dst, blk + within, (size_t)take);
                else     base_read(d, (b << BLK_SHIFT) + within, dst, take);
                off += take; dst += take; len -= take;
        }
        return 1;
}

static int disk_write(pcem_disk *d, int64_t off, const uint8_t *src, int64_t len)
{
        static uint8_t seed[BLK];
        if (!d || off < 0 || len < 0) return 0;
        if (off >= d->size) return 0;
        if (off + len > d->size) len = d->size - off;
        d->writes++;
        while (len > 0) {
                int64_t b      = off >> BLK_SHIFT;
                int32_t within = (int32_t)(off & (BLK - 1));
                int32_t take   = BLK - within;
                uint8_t *blk;
                if (take > len) take = (int32_t)len;
                blk = blk_get(d, b);
                if (!blk) {
                        /* Nothing is held for this block yet, so the seed is
                         * what the machine currently reads. If the write does
                         * not change it, hold nothing: the bytes are already
                         * in a file the savestate does not carry. */
                        base_read(d, b << BLK_SHIFT, seed, BLK);
                        if (!memcmp(seed + within, src, (size_t)take)) {
                                d->matched++;
                                off += take; src += take; len -= take;
                                continue;
                        }
                        blk = blk_alloc();
                        if (!blk) {
                                if (!d->full) {
                                        d->full = 1;
                                        fprintf(stderr, "hdd %s: out of guest memory for the write "
                                              "overlay after %lld blocks; writes are being "
                                              "LOST\n", d->name, (long long)d->held);
                                }
                                return 0;
                        }
                        /* the whole block, so the bytes this write does not
                         * touch still read back as the seed's */
                        memcpy(blk, seed, BLK);
                        if (!blk_set(d, b, blk)) return 0;
                        d->held++;
                }
                memcpy(blk + within, src, (size_t)take);
                off += take; src += take; len -= take;
        }
        return 1;
}

/* ------------------------------------------------------- open, by name */

static pcem_disk *disk_for(const char *fn)
{
        int i;
        if (!fn || !fn[0]) return NULL;
        for (i = 0; i < PCEM_HDD_MAX; i++)
                if (g_disks[i].used && !strcmp(g_disks[i].name, fn))
                        return &g_disks[i];
        for (i = 0; i < PCEM_HDD_MAX; i++)
                if (!g_disks[i].used) return &g_disks[i];
        return NULL;
}

static void disk_open_base(pcem_disk *d, const char *fn)
{
        FILE *f = fopen64(fn, "rb");
        d->base = NULL; d->vhd = NULL; d->base_len = 0;
        if (!f) return;
        if (mvhd_file_is_vhd(f)) {
                int err;
                MVHDMeta *vhdm;
                fclose(f);
                vhdm = mvhd_open(fn, true, &err);
                if (!vhdm) {
                        fprintf(stderr, "hdd %s: cannot open as VHD: %s\n", fn, mvhd_strerr(err));
                        return;
                }
                d->vhd = vhdm;
                {
                        MVHDGeom g = mvhd_get_geometry(vhdm);
                        d->base_len = (int64_t)g.spt * g.heads * g.cyl * 512;
                }
                return;
        }
        if (fseeko64(f, 0, SEEK_END) == 0) d->base_len = (int64_t)ftello64(f);
        d->base = f;
}

/* ------------------------------------------- the six upstream entry points */

void hdd_load_ext(hdd_file_t *hdd, const char *fn, int spt, int hpc, int tracks, int read_only)
{
        pcem_disk *d;
        (void)read_only;            /* every disk is writable; the overlay is where */

        if (hdd->f) return;         /* already attached */
        if (!fn || !fn[0]) return;  /* an IDE channel with nothing on it */
        d = disk_for(fn);
        if (!d) { fprintf(stderr, "hdd: no room for another disk (%s)\n", fn); return; }

        if (!d->used) {
                size_t n = strlen(fn);
                if (n >= sizeof d->name) n = sizeof d->name - 1;
                memcpy(d->name, fn, n); d->name[n] = 0;
                disk_open_base(d, fn);

                if (d->vhd) {
                        MVHDGeom g = mvhd_get_geometry((MVHDMeta *)d->vhd);
                        d->size = (int64_t)g.spt * g.heads * g.cyl * 512;
                        hdd->spt = g.spt; hdd->hpc = g.heads; hdd->tracks = g.cyl;
                } else {
                        d->size = (int64_t)spt * hpc * tracks * 512;
                        hdd->spt = spt; hdd->hpc = hpc; hdd->tracks = tracks;
                }
                if (d->size <= 0) {
                        fprintf(stderr, "hdd %s: geometry %d/%d/%d gives a disk of no size; "
                              "not attaching\n", fn, spt, hpc, tracks);
                        if (d->base) fclose(d->base);
                        if (d->vhd) mvhd_close((MVHDMeta *)d->vhd);
                        d->base = NULL; d->vhd = NULL;
                        return;
                }
                d->nblocks = (d->size + BLK - 1) >> BLK_SHIFT;
                d->nl1 = (d->nblocks + L2_COUNT - 1) >> L2_SHIFT;
                d->l1 = (uint8_t ***)calloc((size_t)d->nl1, sizeof(uint8_t **));
                if (!d->l1) { fprintf(stderr, "hdd %s: no room for the block index\n", fn); return; }
                d->used = 1;
                fprintf(stderr, "hdd %s: %lld MB (%d/%d/%d), seed %lld bytes%s\n", d->name,
                      (long long)(d->size >> 20), hdd->spt, hdd->hpc, hdd->tracks,
                      (long long)d->base_len, d->vhd ? " (VHD)" : "");
        } else {
                hdd->spt = spt; hdd->hpc = hpc; hdd->tracks = tracks;
                if (d->vhd) {
                        MVHDGeom g = mvhd_get_geometry((MVHDMeta *)d->vhd);
                        hdd->spt = g.spt; hdd->hpc = g.heads; hdd->tracks = g.cyl;
                }
        }

        hdd->img_type = HDD_IMG_RAW;
        hdd->f = (void *)d;
        hdd->sectors = (int)(d->size / 512);
        hdd->read_only = 0;
}

void hdd_load(hdd_file_t *hdd, int d, const char *fn)
{
        hdd_load_ext(hdd, fn, hdc[d].spt, hdc[d].hpc, hdc[d].tracks, 0);
}

/* Detaches the controller. The disk itself - and everything written to it -
 * stays, because resetpchard() re-runs every controller's init and a real
 * PC's disk survives a reboot. */
void hdd_close(hdd_file_t *hdd)
{
        hdd->f = NULL;
        hdd->img_type = HDD_IMG_RAW;
}

int hdd_read_sectors(hdd_file_t *hdd, int offset, int nr_sectors, void *buffer)
{
        pcem_disk *d = (pcem_disk *)hdd->f;
        int transfer = nr_sectors;
        if (!d) return 1;
        if ((hdd->sectors - offset) < transfer) transfer = hdd->sectors - offset;
        if (transfer < 0) transfer = 0;
        disk_read(d, (int64_t)offset * 512, (uint8_t *)buffer, (int64_t)transfer * 512);
        return nr_sectors != transfer;
}

int hdd_write_sectors(hdd_file_t *hdd, int offset, int nr_sectors, void *buffer)
{
        pcem_disk *d = (pcem_disk *)hdd->f;
        int transfer = nr_sectors;
        if (!d) return 1;
        if ((hdd->sectors - offset) < transfer) transfer = hdd->sectors - offset;
        if (transfer < 0) transfer = 0;
        /* A write the overlay could not hold is a write FAULT, not a quiet
         * success: the guest has to be told, or a filesystem carries on over
         * bytes that were never stored. */
        if (!disk_write(d, (int64_t)offset * 512, (const uint8_t *)buffer,
                        (int64_t)transfer * 512))
                return 1;
        return nr_sectors != transfer;
}

int hdd_format_sectors(hdd_file_t *hdd, int offset, int nr_sectors)
{
        static const uint8_t zero[BLK];
        pcem_disk *d = (pcem_disk *)hdd->f;
        int transfer = nr_sectors;
        int64_t off, left;
        if (!d) return 1;
        if ((hdd->sectors - offset) < transfer) transfer = hdd->sectors - offset;
        if (transfer < 0) transfer = 0;
        off = (int64_t)offset * 512;
        left = (int64_t)transfer * 512;
        while (left > 0) {
                int64_t n = left > BLK ? BLK : left;
                if (!disk_write(d, off, zero, n)) return 1;
                off += n; left -= n;
        }
        return nr_sectors != transfer;
}

/* --------------------------------------------------- what leaves, and how */

int pcem_hdd_count(void)
{
        int i, n = 0;
        for (i = 0; i < PCEM_HDD_MAX; i++) if (g_disks[i].used) n++;
        return n;
}

static pcem_disk *nth(int index)
{
        int i, n = 0;
        for (i = 0; i < PCEM_HDD_MAX; i++)
                if (g_disks[i].used && n++ == index) return &g_disks[i];
        return NULL;
}

const char *pcem_hdd_export_name(int index)
{
        pcem_disk *d = nth(index);
        return d ? d->name : NULL;
}

int64_t pcem_hdd_export_size(int index)
{
        pcem_disk *d = nth(index);
        return d ? d->size : 0;
}

int64_t pcem_hdd_export_read(int index, int64_t off, uint8_t *dst, int64_t len)
{
        pcem_disk *d = nth(index);
        if (!d || off < 0 || off >= d->size || len <= 0) return 0;
        if (len > d->size - off) len = d->size - off;
        disk_read(d, off, dst, len);
        return len;
}

int64_t pcem_hdd_blocks_held(int index)
{
        pcem_disk *d = nth(index);
        return d ? d->held : 0;
}

int64_t pcem_hdd_writes_matching_seed(int index)
{
        pcem_disk *d = nth(index);
        return d ? d->matched : 0;
}

int pcem_hdd_block_bytes(void) { return BLK; }

/* ---------------------------------------------------- geometry, derived
 *
 * PCem keeps C/H/S in the .cfg and NOT in the image, and nothing in the
 * emulation core derives it: hdd_load() passes hdc[d].spt/hpc/tracks straight
 * through, ide.c reports them as the drive's identity, and a zero there is a
 * drive of no sectors. Upstream derives geometry in its wxWidgets "new hard
 * drive" dialog, which is part of the platform layer this port replaces - so
 * deriving it is the driver's job now, and the Hard Disk Geometry setting's
 * "Auto" means this function.
 *
 * A wrong geometry is a disk that does not boot with no useful error
 * (PLAN.md risk 6), so guessing from the file's length alone is not good
 * enough: a 112 MB FreeDOS image written at 17/15/900 read back as 63/16/227
 * has a partition table that points at the wrong sectors. The image already
 * carries the answer, in the CHS fields of its own partition table, so that is
 * what is read; the length is only the fallback.
 */

static int chs_lba(int c, int h, int s, int spt, int hpc)
{
        return ((c * hpc) + h) * spt + (s - 1);
}

/* the 1023/254/63 saturation an MBR uses when the partition is past CHS reach */
static int chs_capped(int c, int h, int s) { return c >= 1023 || (h == 254 && s == 63); }

static int geom_score(const uint8_t *mbr, int spt, int hpc)
{
        int e, score = 0;
        for (e = 0; e < 4; e++) {
                const uint8_t *p = mbr + 0x1be + e * 16;
                uint32_t start = (uint32_t)p[8] | ((uint32_t)p[9] << 8)
                               | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
                uint32_t nsect = (uint32_t)p[12] | ((uint32_t)p[13] << 8)
                               | ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24);
                int sh = p[1], ss = p[2] & 0x3f, sc = p[3] | ((p[2] & 0xc0) << 2);
                int eh = p[5], es = p[6] & 0x3f, ec = p[7] | ((p[6] & 0xc0) << 2);
                int scap = chs_capped(sc, sh, ss), ecap = chs_capped(ec, eh, es);
                if (!p[4] || !nsect || !ss || !es) continue;
                /* A partition past CHS reach has its fields saturated at
                 * 1023/254/63 and says nothing about the geometry. Those must
                 * be SKIPPED rather than range-checked: a 4 GiB disk at 63/16
                 * has a saturated head of 254, and rejecting the candidate for
                 * it is how a 63/16/8374 Windows XP image was read as
                 * 63/255/525. */
                if (!scap) {
                        if (sh >= hpc || ss > spt) return -1;
                        if (chs_lba(sc, sh, ss, spt, hpc) == (int)start) score++;
                }
                if (!ecap) {
                        if (eh >= hpc || es > spt) return -1;
                        if (chs_lba(ec, eh, es, spt, hpc) == (int)(start + nsect - 1)) score++;
                }
        }
        return score;
}

int pcem_hdd_derive_geometry(const char *name, int *spt_out, int *hpc_out, int *tracks_out)
{
        /* in preference order: the geometries the published PCem configurations
         * use first, then the rest of what a period BIOS would translate to */
        static const int SPT[] = { 63, 17, 32, 33, 34, 26, 55 };
        static const int HPC[] = { 16, 15, 8, 4, 6, 2, 1, 32, 64, 128, 240, 255 };
        uint8_t mbr[512];
        FILE *f;
        int64_t len = 0, sectors;
        int i, j, best = 0, bspt = 0, bhpc = 0, haveMbr = 0;

        if (!name || !name[0]) return 0;
        f = fopen64(name, "rb");
        if (!f) return 0;

        if (mvhd_file_is_vhd(f)) {
                int err;
                MVHDMeta *v;
                fclose(f);
                v = mvhd_open(name, true, &err);
                if (!v) return 0;
                {
                        MVHDGeom g = mvhd_get_geometry(v);
                        *spt_out = g.spt; *hpc_out = g.heads; *tracks_out = g.cyl;
                }
                mvhd_close(v);
                return 1;
        }

        if (fseeko64(f, 0, SEEK_END) == 0) len = (int64_t)ftello64(f);
        if (fseeko64(f, 0, SEEK_SET) == 0 && fread(mbr, 1, 512, f) == 512
            && mbr[510] == 0x55 && mbr[511] == 0xaa)
                haveMbr = 1;
        fclose(f);

        sectors = len / 512;
        if (sectors <= 0) return 0;

        if (haveMbr) {
                for (i = 0; i < (int)(sizeof SPT / sizeof *SPT); i++) {
                        for (j = 0; j < (int)(sizeof HPC / sizeof *HPC); j++) {
                                int s = geom_score(mbr, SPT[i], HPC[j]);
                                /* a whole number of cylinders breaks the tie, because
                                 * that is how the image was made */
                                if (s > 0 && sectors % ((int64_t)SPT[i] * HPC[j]) == 0) s += 2;
                                if (s > best) { best = s; bspt = SPT[i]; bhpc = HPC[j]; }
                        }
                }
        }
        if (!best) {
                /* No partition table, or one whose CHS says nothing. Take the
                 * first pair that divides the image exactly; a blank disk a
                 * user made is usually a whole number of cylinders. */
                for (i = 0; i < (int)(sizeof SPT / sizeof *SPT) && !bspt; i++)
                        for (j = 0; j < (int)(sizeof HPC / sizeof *HPC) && !bspt; j++)
                                if (sectors % ((int64_t)SPT[i] * HPC[j]) == 0
                                    && sectors / ((int64_t)SPT[i] * HPC[j]) <= 65535)
                                        { bspt = SPT[i]; bhpc = HPC[j]; }
                if (!bspt) { bspt = 63; bhpc = 16; }
        }

        {
                int64_t tracks = sectors / ((int64_t)bspt * bhpc);
                if (tracks < 1) return 0;
                if (tracks > 65535) tracks = 65535;
                *spt_out = bspt; *hpc_out = bhpc; *tracks_out = (int)tracks;
        }
        return 1;
}

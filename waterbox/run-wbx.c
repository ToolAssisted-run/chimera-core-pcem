/* run-wbx.c - drives pcem.wbx through the miniBox host over the Chimera core
 * ABI, and reports a PER-FRAME digest stream.
 *
 * The stream, not an end-of-run digest, is the point. M1b's end-state digest
 * did not notice the emulated CPU being halved from a Pentium II/450 to a
 * /233, because the last screen was the same text either way (docs/M1B.md
 * section 6b). A stream notices.
 *
 * usage: run-wbx <core.wbx> <workdir> <frames> [--digests] [--every N]
 *                [--mount-path name=hostpath] ...
 *
 * Every regular file in workdir is mounted under its own name, which is what
 * the engine does with a project's slots, settings and firmware.
 */
#include "minibox.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct { FILE *f; } freader;

/* a savestate in memory, so a leg can take one and put it back */
typedef struct { uint8_t *p; size_t len, cap, pos; } membuf;
static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t s)
{
        membuf *m = (membuf *)ud;
        if (m->len + s > m->cap) {
                size_t cap = m->cap ? m->cap * 2 : (1u << 20);
                while (cap < m->len + s) cap *= 2;
                m->p = (uint8_t *)realloc(m->p, cap);
                if (!m->p) return 0;
                m->cap = cap;
        }
        memcpy(m->p + m->len, d, s);
        m->len += s;
        return 1;
}
static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t s)
{
        membuf *m = (membuf *)ud;
        size_t n = m->len - m->pos;
        if (n > s) n = s;
        memcpy(d, m->p + m->pos, n);
        m->pos += n;
        return (intptr_t)n;
}
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s)
{
        return (intptr_t)fread(d, 1, s, ((freader *)ud)->f);
}

typedef int      (MB_GUEST_ABI *intfn)(void);
typedef void     (MB_GUEST_ABI *framefn)(uint64_t);
typedef void     (MB_GUEST_ABI *setfn)(int32_t, int32_t);
typedef uint64_t (MB_GUEST_ABI *u64fn)(void);
typedef const char *(MB_GUEST_ABI *strfn)(void);
typedef uintptr_t (MB_GUEST_ABI *ptrfn)(void);
typedef const char *(MB_GUEST_ABI *strifn)(int32_t);
typedef int64_t  (MB_GUEST_ABI *i64ifn)(int32_t);
typedef int64_t  (MB_GUEST_ABI *readfn)(int32_t, int64_t, int64_t);

/* the whole exported disk, hashed through the save-data window - the same
 * route the engine exports by */
typedef int64_t (MB_GUEST_ABI *readfn2)(int32_t, int64_t, int64_t);
static uint64_t disk_hash(mb_host *h);

static double now_s(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return t.tv_sec + t.tv_nsec / 1e9;
}

static uintptr_t proc(mb_host *h, const char *name, int required)
{
        mb_return r;
        wbx_get_proc_addr(h, name, &r);
        if (r.error_message[0]) {
                fprintf(stderr, "get_proc_addr(%s): %s\n", name, r.error_message);
                exit(2);
        }
        if (!r.data && required) { fprintf(stderr, "export %s missing\n", name); exit(2); }
        return r.data;
}

static void die(mb_return *r, const char *what)
{
        if (r->error_message[0]) { fprintf(stderr, "%s: %s\n", what, r->error_message); exit(1); }
}

static uint64_t disk_hash(mb_host *h)
{
        mb_return r;
        uint64_t hash = 1469598103934665603ULL;
        int32_t k, n;
        intfn Count;
        i64ifn Size;
        readfn Read;
        ptrfn Scratch;
        wbx_get_proc_addr(h, "GetSaveDataFileCount", &r);
        if (!r.data) return 0;
        Count = (intfn)r.data;
        wbx_get_proc_addr(h, "GetSaveDataFileSize", &r);    Size = (i64ifn)r.data;
        wbx_get_proc_addr(h, "ReadSaveDataFile", &r);       Read = (readfn)r.data;
        wbx_get_proc_addr(h, "GetSaveDataScratch", &r);     Scratch = (ptrfn)r.data;
        n = (int32_t)Count();
        for (k = 0; k < n; k++) {
                int64_t sz = Size(k), done = 0;
                while (done < sz) {
                        int64_t got = Read(k, done, sz - done), b;
                        const uint8_t *win;
                        if (got <= 0) break;
                        win = (const uint8_t *)Scratch();
                        for (b = 0; b < got; b++) { hash ^= win[b]; hash *= 1099511628211ULL; }
                        done += got;
                }
        }
        return hash;
}

int main(int argc, char **argv)
{
        const char *wbx, *workdir;
        long frames;
        int digests = 0, every = 1, driveTypes = 0, hddStats = 0, dumpCfg = 0, i;
        const char *savedataDir = NULL;
        long stateAt = -1, stateEvery = 0;
        long shotFrame = -1, pressAt[32];
        int pressBtn[32], nPress = 0;
        long axisAt[16]; int axisIdx[16], axisVal[16], nAxis = 0;
        int stateHash = 0;
        const char *shotPath = NULL;
        mb_return r;
        mb_host *h;
        FILE *f;
        freader fr;
        DIR *d;
        struct dirent *de;
        mb_memory_layout_template layout = {
                .sbrk_size   = 64u   << 20,
                .sealed_size = 16u   << 20,
                .invis_size  = 320u  << 20,
                .plain_size  = 64u   << 20,
                .mmap_size   = (uintptr_t)4096 << 20,   /* matches waterbox.config; NOT 4096u<<20, which is 32-bit and comes out zero */
        };

        if (argc < 4) {
                fprintf(stderr, "usage: %s <core.wbx> <workdir> <frames> [--digests] [--every N]\n", argv[0]);
                return 2;
        }
        wbx = argv[1]; workdir = argv[2]; frames = strtol(argv[3], NULL, 10);
        for (i = 4; i < argc; i++) {
                if (!strcmp(argv[i], "--digests")) digests = 1;
                else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
                else if (!strcmp(argv[i], "--drive-types")) driveTypes = 1;
                else if (!strcmp(argv[i], "--hdd-stats")) hddStats = 1;
                else if (!strcmp(argv[i], "--dump-cfg")) dumpCfg = 1;
                else if (!strcmp(argv[i], "--state-roundtrip") && i + 1 < argc)
                        stateAt = atol(argv[++i]);
                else if (!strcmp(argv[i], "--state-every") && i + 1 < argc)
                        stateEvery = atol(argv[++i]);
                else if (!strcmp(argv[i], "--savedata-out") && i + 1 < argc) savedataDir = argv[++i];
                else if (!strcmp(argv[i], "--press") && i + 1 < argc && nPress < 32) {
                        /* <frame>=<button index>, held for a few frames - a
                         * real machine sees a key down for longer than 10 ms */
                        char *spec = argv[++i], *eq = strchr(spec, '=');
                        if (eq) { *eq = 0; pressAt[nPress] = atol(spec);
                                  pressBtn[nPress] = atoi(eq + 1); nPress++; }
                }
                else if (!strcmp(argv[i], "--axis") && i + 1 < argc && nAxis < 16) {
                        /* [<frame>:]<axis index>=<value>, held from that frame
                         * on. A POSITION axis only moves the machine when it
                         * CHANGES - the first frame deliberately places nothing
                         * - so a leg that wants movement asks for a frame. */
                        char *spec = argv[++i], *eq = strchr(spec, '='), *colon = strchr(spec, ':');
                        if (eq) {
                                *eq = 0;
                                if (colon && colon < eq) {
                                        *colon = 0;
                                        axisAt[nAxis] = atol(spec);
                                        axisIdx[nAxis] = atoi(colon + 1);
                                } else {
                                        axisAt[nAxis] = 0;
                                        axisIdx[nAxis] = atoi(spec);
                                }
                                axisVal[nAxis] = atoi(eq + 1);
                                nAxis++;
                        }
                }
                else if (!strcmp(argv[i], "--state-hash")) stateHash = 1;
                else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
                        char *spec = argv[++i], *eq = strchr(spec, '=');
                        if (eq) { *eq = 0; shotFrame = atol(spec); shotPath = eq + 1; }
                }
        }

        f = fopen(wbx, "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", wbx); return 1; }
        fr.f = f;
        wbx_create_host(&layout, "pcem.wbx", file_read, (uintptr_t)&fr, &r);
        fclose(f);
        die(&r, "create_host");
        h = (mb_host *)r.data;

        d = opendir(workdir);
        if (!d) { fprintf(stderr, "cannot open %s\n", workdir); return 1; }
        while ((de = readdir(d))) {
                char path[1024];
                struct stat st;
                FILE *mf;
                freader *mr;
                snprintf(path, sizeof path, "%s/%s", workdir, de->d_name);
                if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
                /* A disc image is gigabytes; mount anything large by path so
                 * the box streams it instead of copying it in. */
                if (st.st_size > (64L << 20)) {
                        wbx_mount_file_path(h, de->d_name, path, &r);
                        if (r.error_message[0])
                                fprintf(stderr, "mount_path %s: %s\n", de->d_name, r.error_message);
                        continue;
                }
                mf = fopen(path, "rb");
                if (!mf) continue;
                mr = malloc(sizeof *mr);
                mr->f = mf;
                wbx_mount_file(h, de->d_name, file_read, (uintptr_t)mr, false, &r);
                if (r.error_message[0])
                        fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message);
        }
        closedir(d);

        wbx_activate_host(h, &r); die(&r, "activate");

        if (!((intfn)proc(h, "Init", 1))()) {
                strfn GetLoadError = (strfn)proc(h, "GetLoadError", 0);
                const char *msg = GetLoadError ? GetLoadError() : NULL;
                char why[256];
                /* The core's own explanation first: a guest pointer is a host
                 * pointer here, so this reads straight out of guest memory.
                 * Without it a refusal is indistinguishable from a crash. */
                if (msg && *msg) fprintf(stderr, "FAIL: %s\n", msg);
                wbx_get_death(h, why, sizeof why, &r);
                if (r.data) fprintf(stderr, "FAIL: the guest died: %s\n", why);
                if (!(msg && *msg) && !r.data) fprintf(stderr, "FAIL: Init returned 0\n");
                return 1;
        }

        if (dumpCfg) {
                strfn Cfg = (strfn)proc(h, "GetComposedConfig", 1);
                printf("---- composed .cfg ----\n%s-----------------------\n", Cfg());
        }

        if (driveTypes) {
                intfn A = (intfn)proc(h, "GetDriveAType", 1);
                intfn B = (intfn)proc(h, "GetDriveBType", 1);
                printf("DRIVES a=%d b=%d\n", A(), B());
        }

        wbx_deactivate_host(h, &r); die(&r, "deactivate");
        wbx_seal(h, &r); die(&r, "seal");
        wbx_activate_host(h, &r); die(&r, "activate2");

        {
                framefn FrameAdvance = (framefn)proc(h, "FrameAdvance", 1);
                setfn SetButton = (setfn)proc(h, "SetButton", 1);
                u64fn Digest = (u64fn)proc(h, "GetFrameDigest", 1);
                u64fn Blits = (u64fn)proc(h, "GetBlitCount", 1);
                intfn W = (intfn)proc(h, "GetVideoWidth", 1);
                intfn H = (intfn)proc(h, "GetVideoHeight", 1);
                intfn Samples = (intfn)proc(h, "GetAudioSampleCount", 1);
                setfn SetAxis = (setfn)proc(h, "SetAxis", nAxis > 0);
                uint64_t stream = 1469598103934665603ULL;
                long n, audioTotal = 0;
                double t0 = now_s(), wall;
                membuf st = {0};
                uint64_t afterSave = 0, afterLoad = 0, diskAtSave = 0;

                for (n = 0; n < frames; n++) {
                        uint64_t dg;
                        int k;
                        for (k = 0; k < nPress; k++) {
                                if (n == pressAt[k]) SetButton(pressBtn[k], 1);
                                if (n == pressAt[k] + 8) SetButton(pressBtn[k], 0);
                        }
                        for (k = 0; k < nAxis; k++)
                                if (n >= axisAt[k]) SetAxis(axisIdx[k], axisVal[k]);
                        if (stateAt >= 0 && n == stateAt) {
                                diskAtSave = disk_hash(h);
                                wbx_save_state(h, mem_write, (uintptr_t)&st, &r);
                                die(&r, "save_state");
                        }
                        FrameAdvance(0);
                        dg = Digest();
                        stream ^= dg; stream *= 1099511628211ULL;
                        if (stateAt >= 0 && n >= stateAt) {
                                afterSave ^= dg; afterSave *= 1099511628211ULL;
                        }
                        audioTotal += Samples();
                        if (shotPath && n == shotFrame) {
                                /* A picture, because a stuck machine is
                                 * perfectly deterministic: a stable digest
                                 * proves nothing on its own. */
                                uint32_t *fb = (uint32_t *)((ptrfn)proc(h, "GetVideoBgra", 1))();
                                int w = W(), hh = H(), x, y;
                                FILE *o = fopen(shotPath, "wb");
                                if (o && fb && w > 0 && hh > 0) {
                                        fprintf(o, "P6\n%d %d\n255\n", w, hh);
                                        for (y = 0; y < hh; y++)
                                                for (x = 0; x < w; x++) {
                                                        uint32_t px = fb[(size_t)y * w + x];
                                                        fputc((px >> 16) & 0xff, o);
                                                        fputc((px >> 8) & 0xff, o);
                                                        fputc(px & 0xff, o);
                                                }
                                        printf("wrote %s (%dx%d)\n", shotPath, w, hh);
                                }
                                if (o) fclose(o);
                        }
                        /* what a state WEIGHS as the machine runs, beside how much
                         * of the disk it is holding - the two numbers the disk
                         * overlay's cost is made of */
                        if (stateEvery > 0 && (n % stateEvery) == 0) {
                                membuf probe = {0};
                                i64ifn Held = (i64ifn)proc(h, "GetHddBlocksHeld", 0);
                                intfn BlockBytes = (intfn)proc(h, "GetHddBlockBytes", 0);
                                wbx_save_state(h, mem_write, (uintptr_t)&probe, &r);
                                die(&r, "save_state");
                                printf("STATESIZE frame=%ld bytes=%zu held_blocks=%lld"
                                       " held_bytes=%lld\n", n, probe.len,
                                       Held ? (long long)Held(0) : -1,
                                       (Held && BlockBytes)
                                               ? (long long)Held(0) * BlockBytes() : -1);
                                fflush(stdout);
                                free(probe.p);
                        }
                        if (digests && (n % every) == 0)
                                printf("frame %6ld digest=%016llx %dx%d\n",
                                       n, (unsigned long long)dg, W(), H());
                }
                wall = now_s() - t0;

                /* THE WHOLE MACHINE, hashed. A frame digest is the picture,
                 * and some input never reaches the picture: a mouse packet
                 * sitting in a controller's buffer with no guest driver to
                 * read it changes the machine and draws nothing. This is what
                 * says that input arrived at all. */
                if (stateHash) {
                        membuf sh = {0};
                        uint64_t hv = 1469598103934665603ULL;
                        size_t b;
                        wbx_save_state(h, mem_write, (uintptr_t)&sh, &r);
                        die(&r, "save_state");
                        for (b = 0; b < sh.len; b++) { hv ^= sh.p[b]; hv *= 1099511628211ULL; }
                        printf("STATEHASH %016llx bytes=%zu\n",
                               (unsigned long long)hv, sh.len);
                        free(sh.p);
                }

                /* THE DISK IS MACHINE STATE, and this is what proves it: the
                 * disk as it stood when the state was taken, the disk at the
                 * end of the run, and the disk the instant the state is put
                 * back. If the second differs from the first (the machine
                 * really did write) and the third equals the first, then a
                 * rewind past a write took the disk back with it. */
                if (stateAt >= 0 && st.len) {
                        uint64_t diskEnd = disk_hash(h), diskLoaded;
                        st.pos = 0;
                        wbx_load_state(h, mem_read, (uintptr_t)&st, &r);
                        die(&r, "load_state");
                        {
                                uintptr_t sl = proc(h, "StateLoaded", 0);
                                if (sl) ((void (MB_GUEST_ABI *)(void))sl)();
                        }
                        diskLoaded = disk_hash(h);
                        for (n = stateAt; n < frames; n++) {
                                uint64_t dg;
                                int k2;
                                for (k2 = 0; k2 < nPress; k2++) {
                                        if (n == pressAt[k2]) SetButton(pressBtn[k2], 1);
                                        if (n == pressAt[k2] + 8) SetButton(pressBtn[k2], 0);
                                }
                                FrameAdvance(0);
                                dg = Digest();
                                afterLoad ^= dg; afterLoad *= 1099511628211ULL;
                        }
                        printf("STATE bytes=%zu disk_at_save=%016llx disk_at_end=%016llx"
                               " disk_after_load=%016llx replay=%016llx/%016llx\n",
                               st.len, (unsigned long long)diskAtSave,
                               (unsigned long long)diskEnd,
                               (unsigned long long)diskLoaded,
                               (unsigned long long)afterSave,
                               (unsigned long long)afterLoad);
                }

                /* The save-data export, through the WINDOW: there is no
                 * address at which a 4 GiB disk exists, so the host asks for
                 * it a piece at a time, exactly as the engine does
                 * (session.cpp:2193). */
                if (savedataDir || hddStats) {
                        intfn Count = (intfn)proc(h, "GetSaveDataFileCount", 1);
                        strifn Name = (strifn)proc(h, "GetSaveDataFileName", 1);
                        i64ifn Size = (i64ifn)proc(h, "GetSaveDataFileSize", 1);
                        readfn Read = (readfn)proc(h, "ReadSaveDataFile", 1);
                        ptrfn Scratch = (ptrfn)proc(h, "GetSaveDataScratch", 1);
                        i64ifn Held = (i64ifn)proc(h, "GetHddBlocksHeld", 0);
                        i64ifn Matched = (i64ifn)proc(h, "GetHddSeedMatches", 0);
                        intfn BlockBytes = (intfn)proc(h, "GetHddBlockBytes", 0);
                        int32_t nf = (int32_t)Count(), k;
                        for (k = 0; k < nf; k++) {
                                const char *nm = Name(k);
                                int64_t sz = Size(k), done = 0;
                                if (hddStats && Held && Matched && BlockBytes)
                                        printf("HDD %d %s size=%lld held_blocks=%lld"
                                               " block_bytes=%d seed_matches=%lld\n",
                                               k, nm ? nm : "?", (long long)sz,
                                               (long long)Held(k), BlockBytes(),
                                               (long long)Matched(k));
                                if (!savedataDir) continue;
                                {
                                        char path[1024];
                                        FILE *o;
                                        snprintf(path, sizeof path, "%s/%s", savedataDir,
                                                 nm ? nm : "savedata.bin");
                                        o = fopen(path, "wb");
                                        if (!o) { fprintf(stderr, "cannot write %s\n", path); return 4; }
                                        while (done < sz) {
                                                int64_t got = Read(k, done, sz - done);
                                                const uint8_t *win;
                                                if (got <= 0) break;
                                                win = (const uint8_t *)Scratch();
                                                if (!win) break;
                                                fwrite(win, 1, (size_t)got, o);
                                                done += got;
                                        }
                                        fclose(o);
                                        if (done != sz) {
                                                fprintf(stderr, "savedata %s short: %lld of %lld\n",
                                                        nm ? nm : "?", (long long)done, (long long)sz);
                                                return 4;
                                        }
                                        printf("savedata %s %lld bytes\n", path, (long long)sz);
                                }
                        }
                }

                /* Liveness before any number: a dead guest returns 0 from
                 * every call and "finishes" instantly (docs/M1B.md 6b). */
                {
                        char why[256];
                        wbx_get_death(h, why, sizeof why, &r);
                        if (r.data) { fprintf(stderr, "\nFAIL: the guest is dead: %s\n", why); return 3; }
                }
                if (!Blits()) { fprintf(stderr, "\nFAIL: the machine drew nothing\n"); return 3; }

                printf("\nRESULT frames=%ld wall_s=%.3f fps=%.1f blits=%llu %dx%d"
                       " audio_samples=%ld stream=%016llx\n",
                       frames, wall, frames / wall, (unsigned long long)Blits(),
                       W(), H(), audioTotal, (unsigned long long)stream);
        }

        wbx_deactivate_host(h, &r);
        wbx_destroy_host(h, &r);
        return 0;
}

/* M1b host: runs pcem-spike.wbx through the miniBox host and times it, so the
 * sandbox figure can be put beside M1a's native one for the same workload.
 *
 * usage: m1b-host <pcem-spike.wbx> <workdir> <emulated-ms> [--epoch-ms N]
 *
 * Every regular file in the workdir is mounted under its basename - the ROM
 * set (with PCem's directory separators flattened to '_', which is what the
 * spike's rom.c fopen shim expects) and machine.cfg.
 *
 * --epoch-ms N opens a miniBox epoch every N emulated milliseconds, which is
 * what Chimera's greenzone does per stored frame (state_history.cpp:1213).
 * That is the setting the whole RWX dirty-page question is about: without it
 * no page is ever re-held and the JIT never faults for tracking.
 */
#include "minibox.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct { uint8_t *b; size_t len, cap; } membuf;
static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t n)
{
        membuf *m = (membuf *)ud;
        if (m->len + n > m->cap) { m->cap = (m->len + n) * 2 + 64; m->b = realloc(m->b, m->cap); }
        memcpy(m->b + m->len, d, n); m->len += n; return 0;
}

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s)
{
        return (intptr_t)fread(d, 1, s, ((freader *)ud)->f);
}

typedef int      (MB_GUEST_ABI *intfn)(void);
typedef void     (MB_GUEST_ABI *runfn)(uint64_t, uint64_t);
typedef uint64_t (MB_GUEST_ABI *u64fn)(void);

static double now_s(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return t.tv_sec + t.tv_nsec / 1e9;
}

static uintptr_t proc(mb_host *h, const char *name)
{
        mb_return r;
        wbx_get_proc_addr(h, name, &r);
        if (r.error_message[0]) {
                fprintf(stderr, "get_proc_addr(%s): %s\n", name, r.error_message);
                exit(2);
        }
        if (!r.data) { fprintf(stderr, "export %s missing\n", name); exit(2); }
        return r.data;
}

static void die(mb_return *r, const char *what)
{
        if (r->error_message[0]) { fprintf(stderr, "%s: %s\n", what, r->error_message); exit(1); }
}

int main(int argc, char **argv)
{
        const char *wbx, *workdir;
        uint64_t total_ms, epoch_ms = 0, slice_ms = 10;
        mb_return r;
        mb_host *h;
        FILE *f;
        freader fr;
        DIR *d;
        struct dirent *de;
        int i;
        const char *iso = NULL;
        double t0, wall;
        /* PCem mallocs the machine's 256 MB of RAM and its lookup tables, so
         * musl routes most of that to the mmap arena; the 120 MiB recompiler
         * arena is invisible (patches/0001). */
        mb_memory_layout_template layout = {
                .sbrk_size   = 64u  << 20,
                .sealed_size = 16u  << 20,
                .invis_size  = 320u << 20,
                .plain_size  = 64u  << 20,
                .mmap_size   = 2048u << 20,
        };

        if (argc < 4) {
                fprintf(stderr, "usage: %s <core.wbx> <workdir> <emulated-ms> [--epoch-ms N] [--slice-ms N]\n", argv[0]);
                return 2;
        }
        wbx = argv[1]; workdir = argv[2];
        total_ms = strtoull(argv[3], NULL, 10);
        for (i = 4; i < argc; i++) {
                if (!strcmp(argv[i], "--epoch-ms") && i + 1 < argc)
                        epoch_ms = strtoull(argv[++i], NULL, 10);
                else if (!strcmp(argv[i], "--slice-ms") && i + 1 < argc)
                        slice_ms = strtoull(argv[++i], NULL, 10);
                else if (!strcmp(argv[i], "--iso") && i + 1 < argc)
                        iso = argv[++i];
        }

        f = fopen(wbx, "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", wbx); return 1; }
        fr.f = f;
        wbx_create_host(&layout, "pcem-spike.wbx", file_read, (uintptr_t)&fr, &r);
        fclose(f);
        die(&r, "create_host");
        h = (mb_host *)r.data;

        /* Mount before Init and before seal, so they are part of the baseline. */
        d = opendir(workdir);
        if (!d) { fprintf(stderr, "cannot open %s\n", workdir); return 1; }
        while ((de = readdir(d))) {
                char path[1024];
                struct stat st;
                FILE *mf;
                freader *mr;
                snprintf(path, sizeof path, "%s/%s", workdir, de->d_name);
                if (stat(path, &st) || !S_ISREG(st.st_mode))
                        continue;
                mf = fopen(path, "rb");
                if (!mf) continue;
                mr = malloc(sizeof *mr);
                mr->f = mf;
                wbx_mount_file(h, de->d_name, file_read, (uintptr_t)mr, false, &r);
                if (r.error_message[0])
                        fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message);
        }
        closedir(d);

        /* A CD image is gigabytes; mount it by host path so the box streams it
         * instead of copying it into guest memory (minibox.h:62-66). */
        if (iso) {
                wbx_mount_file_path(h, "cd.iso", iso, &r);
                die(&r, "mount_file_path");
                printf("m1b: mounted %s as cd.iso by path\n", iso);
        }

        wbx_activate_host(h, &r); die(&r, "activate");

        printf("m1b: calling Init\n"); fflush(stdout);
        if (!((intfn)proc(h, "Init"))()) {
                char why[256];
                wbx_get_death(h, why, sizeof why, &r);
                fprintf(stderr, "Init failed%s%s\n", r.data ? ": " : "", r.data ? why : "");
                return 1;
        }
        printf("m1b: JIT arena at 0x%llx, %llu bytes (%llu pages)\n",
               (unsigned long long)((u64fn)proc(h, "GetJitArena"))(),
               (unsigned long long)((u64fn)proc(h, "GetJitArenaSize"))(),
               (unsigned long long)((u64fn)proc(h, "GetJitArenaSize"))() / 4096);
        printf("m1b: cpu speed %d Hz\n", ((intfn)proc(h, "GetSpeedHz"))());
        fflush(stdout);

        wbx_deactivate_host(h, &r); die(&r, "deactivate");
        wbx_seal(h, &r); die(&r, "seal");
        wbx_activate_host(h, &r); die(&r, "activate2");

        {
                runfn Run = (runfn)proc(h, "Run");
                u64fn Blits = (u64fn)proc(h, "GetBlits");
                u64fn Digest = (u64fn)proc(h, "GetScreenDigest");
                intfn W = (intfn)proc(h, "GetWidth"), H = (intfn)proc(h, "GetHeight");
                uint64_t done = 0, epoch_pages_total = 0;
                long epochs = 0;
                double last = now_s(), last_done = 0;

                t0 = now_s();
                while (done < total_ms) {
                        uint64_t chunk = epoch_ms ? epoch_ms : 1000;
                        double t;
                        if (chunk > total_ms - done) chunk = total_ms - done;
                        if (epoch_ms) {
                                wbx_epoch_begin(h, &r);
                                if (r.error_message[0]) { fprintf(stderr, "epoch_begin: %s\n", r.error_message); break; }
                                epochs++;
                        }
                        Run(chunk, slice_ms);
                        if (epoch_ms) {
                                wbx_get_epoch_page_count(h, &r);
                                if (!r.error_message[0]) epoch_pages_total += r.data;
                        }
                        done += chunk;
                        t = now_s();
                        if (t - last >= 1.0) {
                                printf("  t=%6.1fs emulated=%8llu ms  window speed=%6.1f%%\n",
                                       t - t0, (unsigned long long)done,
                                       100.0 * (done - last_done) / ((t - last) * 1000.0));
                                fflush(stdout);
                                last = t; last_done = done;
                        }
                }
                wall = now_s() - t0;
                /* A dead guest returns 0 from every call, so it "runs" the
                 * whole workload instantly and reports an absurd speed - the
                 * negative control for patches/0002 printed 2339127.5%. A
                 * harness that only asserts "fast enough" goes green on a
                 * machine that never executed an instruction. Refuse that
                 * here, loudly, before any number is printed. */
                {
                        char why[256];
                        wbx_get_death(h, why, sizeof why, &r);
                        if (r.data) {
                                fprintf(stderr, "\nFAIL: the guest is dead: %s\n", why);
                                return 3;
                        }
                        if (!Blits()) {
                                fprintf(stderr, "\nFAIL: the guest drew nothing; it did not run\n");
                                return 3;
                        }
                }
                printf("\nRESULT emulated_ms=%llu wall_s=%.3f speed=%.1f%% blits=%llu %dx%d digest=%016llx\n",
                       (unsigned long long)done, wall, 100.0 * done / (wall * 1000.0),
                       (unsigned long long)Blits(), W(), H(),
                       (unsigned long long)Digest());
                if (epoch_ms)
                        printf("EPOCHS n=%ld pages_total=%llu pages_per_epoch=%.1f\n",
                               epochs, (unsigned long long)epoch_pages_total,
                               epochs ? (double)epoch_pages_total / epochs : 0.0);
        }

        {
                mb_return rr;
                wbx_get_proc_addr(h, "GetJitBlocksUsed", &rr);
                if (rr.data)
                        printf("JIT blocks_used=%llu of 131072 (%.1f%% of the arena, %.1f MiB)\n",
                               (unsigned long long)((u64fn)rr.data)(),
                               100.0 * ((u64fn)rr.data)() / 131072.0,
                               ((u64fn)rr.data)() * 960.0 / 1048576.0);
        }

        /* How big is a savestate of this machine? PCem's 120 MiB recompiler
         * arena is in it unless patches/0001 put it in invisible memory, and
         * a greenzone keeps hundreds of these. */
        {
                membuf st = { NULL, 0, 0 };
                double s0 = now_s();
                wbx_deactivate_host(h, &r);
                wbx_save_state(h, mem_write, (uintptr_t)&st, &r);
                if (r.error_message[0]) fprintf(stderr, "save_state: %s\n", r.error_message);
                else printf("STATE bytes=%zu (%.1f MiB) save_s=%.3f\n",
                            st.len, st.len / 1048576.0, now_s() - s0);
                free(st.b);
        }
        wbx_destroy_host(h, &r);
        return 0;
}

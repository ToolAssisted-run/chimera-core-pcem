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

typedef struct { uint8_t *b; size_t len, pos; } memreader;
static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t n)
{
        memreader *m = (memreader *)ud;
        uintptr_t avail = m->len - m->pos;
        if (n > avail) n = avail;
        memcpy(d, m->b + m->pos, n); m->pos += n; return (intptr_t)n;
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

/* Everything needed to build a fresh host over the same core and files.
 * --resume-at needs a SECOND one, because the point of that test is that a
 * host which never saw the machine run can finish the run from a state. */
static mb_host *make_host(const char *wbx, const char *workdir, const char *iso)
{
        mb_memory_layout_template layout = {
                .sbrk_size   = 64u  << 20,
                .sealed_size = 16u  << 20,
                .invis_size  = 320u << 20,
                .plain_size  = 64u  << 20,
                .mmap_size   = 2048u << 20,
        };
        mb_return r;
        mb_host *h;
        FILE *f;
        freader fr;
        DIR *d;
        struct dirent *de;

        f = fopen(wbx, "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", wbx); exit(1); }
        fr.f = f;
        wbx_create_host(&layout, "pcem-spike.wbx", file_read, (uintptr_t)&fr, &r);
        fclose(f);
        die(&r, "create_host");
        h = (mb_host *)r.data;

        d = opendir(workdir);
        if (!d) { fprintf(stderr, "cannot open %s\n", workdir); exit(1); }
        while ((de = readdir(d))) {
                char path[1024];
                struct stat st;
                FILE *mf;
                freader *mr;
                snprintf(path, sizeof path, "%s/%s", workdir, de->d_name);
                if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
                mf = fopen(path, "rb");
                if (!mf) continue;
                mr = malloc(sizeof *mr);
                mr->f = mf;
                wbx_mount_file(h, de->d_name, file_read, (uintptr_t)mr, false, &r);
                if (r.error_message[0])
                        fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message);
        }
        closedir(d);
        if (iso) { wbx_mount_file_path(h, "cd.iso", iso, &r); die(&r, "mount_file_path"); }
        return h;
}

int main(int argc, char **argv)
{
        const char *wbx, *workdir;
        uint64_t total_ms, epoch_ms = 0, slice_ms = 10;
        mb_return r;
        mb_host *h;
        int i;
        const char *iso = NULL;
        int roundtrip = 0;
        uint64_t resume_at = 0;
        /* The negative control for --resume-at: build the new host and DO NOT
         * load the state into it, so it carries on from reset instead of from
         * where the old one stopped. The digest must then differ. Without
         * this, "the digests matched" would be consistent with a load that
         * silently did nothing. */
        int resume_no_load = 0;
        double t0, wall;

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
                else if (!strcmp(argv[i], "--roundtrip"))
                        roundtrip = 1;
                else if (!strcmp(argv[i], "--resume-at") && i + 1 < argc)
                        resume_at = strtoull(argv[++i], NULL, 10);
                else if (!strcmp(argv[i], "--resume-no-load"))
                        resume_no_load = 1;   /* negative control, see below */
        }

        h = make_host(wbx, workdir, iso);

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
                int resumed = 0;
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

                        /* --roundtrip: save and reload the WHOLE machine
                         * around every chunk. The digests must come out the
                         * same as a run without it. With the default mmap
                         * arena the recompiler's generated code is inside the
                         * state, so this is the case that most needs a test. */
                        if (roundtrip) {
                                membuf st = { NULL, 0, 0 };
                                memreader rd;
                                wbx_deactivate_host(h, &r);
                                wbx_save_state(h, mem_write, (uintptr_t)&st, &r);
                                if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); return 1; }
                                rd.b = st.b; rd.len = st.len; rd.pos = 0;
                                wbx_load_state(h, mem_read, (uintptr_t)&rd, &r);
                                if (r.error_message[0]) { fprintf(stderr, "load_state: %s\n", r.error_message); return 1; }
                                wbx_activate_host(h, &r);
                                free(st.b);
                        }

                        /* --resume-at N: at N emulated ms, save, tear the host
                         * DOWN, build a brand new one over the same core and
                         * files, load the state into it and finish the run
                         * there. This is the reopened-project case, and it is
                         * where a host address kept in guest state shows up -
                         * the second host is at a different place in the
                         * host's address space. */
                        if (resume_at && done + chunk >= resume_at && !resumed) {
                                membuf st = { NULL, 0, 0 };
                                memreader rd;
                                resumed = 1;
                                wbx_deactivate_host(h, &r);
                                wbx_save_state(h, mem_write, (uintptr_t)&st, &r);
                                if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); return 1; }
                                wbx_destroy_host(h, &r);
                                printf("m1b: host destroyed at %llu ms; building a new one\n",
                                       (unsigned long long)(done + chunk));
                                h = make_host(wbx, workdir, iso);
                                /* A state can only be loaded into a SEALED
                                 * host, so the new one has to be brought up
                                 * the same way the first was - Init, then
                                 * seal - before the load replaces everything
                                 * that produced. */
                                wbx_activate_host(h, &r); die(&r, "activate3");
                                if (!((intfn)proc(h, "Init"))()) {
                                        fprintf(stderr, "Init failed in the new host\n");
                                        return 1;
                                }
                                wbx_deactivate_host(h, &r);
                                wbx_seal(h, &r); die(&r, "seal2");
                                wbx_activate_host(h, &r); die(&r, "activate3b");
                                wbx_deactivate_host(h, &r);
                                if (!resume_no_load) {
                                        rd.b = st.b; rd.len = st.len; rd.pos = 0;
                                        wbx_load_state(h, mem_read, (uintptr_t)&rd, &r);
                                        if (r.error_message[0]) { fprintf(stderr, "load_state into a new host: %s\n", r.error_message); return 1; }
                                } else {
                                        printf("m1b: NEGATIVE CONTROL - not loading the state\n");
                                }
                                wbx_activate_host(h, &r); die(&r, "activate4");
                                free(st.b);
                                /* every export address belongs to the old host */
                                Run = (runfn)proc(h, "Run");
                                Blits = (u64fn)proc(h, "GetBlits");
                                Digest = (u64fn)proc(h, "GetScreenDigest");
                                W = (intfn)proc(h, "GetWidth");
                                H = (intfn)proc(h, "GetHeight");
                        }
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

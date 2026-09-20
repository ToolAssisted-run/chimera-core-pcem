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

int main(int argc, char **argv)
{
        const char *wbx, *workdir;
        long frames;
        int digests = 0, every = 1, driveTypes = 0, i;
        long shotFrame = -1, pressAt[32];
        int pressBtn[32], nPress = 0;
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
                .mmap_size   = 2048u << 20,
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
                else if (!strcmp(argv[i], "--press") && i + 1 < argc && nPress < 32) {
                        /* <frame>=<button index>, held for a few frames - a
                         * real machine sees a key down for longer than 10 ms */
                        char *spec = argv[++i], *eq = strchr(spec, '=');
                        if (eq) { *eq = 0; pressAt[nPress] = atol(spec);
                                  pressBtn[nPress] = atoi(eq + 1); nPress++; }
                }
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
                uint64_t stream = 1469598103934665603ULL;
                long n, audioTotal = 0;
                double t0 = now_s(), wall;

                for (n = 0; n < frames; n++) {
                        uint64_t dg;
                        int k;
                        for (k = 0; k < nPress; k++) {
                                if (n == pressAt[k]) SetButton(pressBtn[k], 1);
                                if (n == pressAt[k] + 8) SetButton(pressBtn[k], 0);
                        }
                        FrameAdvance(0);
                        dg = Digest();
                        stream ^= dg; stream *= 1099511628211ULL;
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
                        if (digests && (n % every) == 0)
                                printf("frame %6ld digest=%016llx %dx%d\n",
                                       n, (unsigned long long)dg, W(), H());
                }
                wall = now_s() - t0;

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

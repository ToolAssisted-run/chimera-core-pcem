/* M1b guest spike: PCem's emulation core, unmodified, running inside miniBox.
 *
 * The question M1b exists to answer is whether PCem's dynamic recompiler
 * works in the sandbox and what it costs: the recompiler allocates a 120 MiB
 * RWX arena (MEM_BLOCK_NR 131072 * MEM_BLOCK_SIZE 0x3c0,
 * codegen_allocator.h:17-23) and writes generated code all over it, and
 * miniBox faults on the first write to each clean page in an epoch.
 *
 * So this is not a stand-in for the recompiler - it IS the recompiler, the
 * real arena and the real x86 code of a real machine BIOS, driven by the same
 * runpc(ms) the native M1a harness drives. Only the platform layer is the
 * driver's, and it is the same 28 definitions M1A.md enumerated.
 *
 * The guest has no clock. The host times the calls.
 */
#define _GNU_SOURCE
#include <emulibc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

#include "video.h"
#include "plat-joystick.h"

typedef enum { EMULATION_STOPPED, EMULATION_PAUSED, EMULATION_RUNNING } emulation_state_t;

extern void initpc(int argc, char *argv[]);
extern void resetpchard(void);
extern void runpc(uint64_t ms);
extern void fullspeed(void);
extern int cpu_get_speed(void);
extern void paths_init(void);
extern void sound_init(void);
extern FILE *pclogf;
extern BITMAP *buffer32;
extern void (*video_blit_memtoscreen_func)(int, int, int, int, int, int);
extern char roms_paths[4096];
extern int num_roms_paths;

/* ---------------------------------------------------------------- state */

emulation_state_t emulation_state = EMULATION_RUNNING;
int SOUNDBUFLEN = 48000 / 50;
uint64_t timer_freq = 1000000000ULL;
uint8_t pcem_key[272];
BITMAP *screen;
int mouse_buttons = 0;
joystick_t joystick_state[MAX_JOYSTICKS];
plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
int joysticks_present = 0;

static long blit_count = 0;
static int blit_w = 0, blit_h = 0;

/* The guest clock is frozen and PCem only reads it for status-bar statistics
 * (PLAN.md section 2, point 5), so a counter is a faithful stand-in and,
 * unlike a real clock, it cannot make the machine nondeterministic. */
static uint64_t fake_ticks = 0;
uint64_t timer_read(void) { return fake_ticks += 1000; }

BITMAP *create_bitmap(int w, int h)
{
        int c;
        BITMAP *b = malloc(sizeof(BITMAP) + (size_t)h * sizeof(uint8_t *));
        b->dat = malloc((size_t)w * h * 4);
        for (c = 0; c < h; c++)
                b->line[c] = b->dat + (c * w * 4);
        b->w = w; b->h = h;
        return b;
}
void destroy_bitmap(BITMAP *b) { free(b->dat); free(b); }

void hline(BITMAP *b, int x1, int y, int x2, int col)
{
        uint32_t *p = (uint32_t *)b->line[y];
        for (; x1 < x2; x1++) p[x1] = col;
}

int dir_exists(char *path) { (void)path; return 0; }

/* The sandbox VFS is flat, so the roms path is empty and every ROM is asked
 * for by the name PCem builds. rom.c is compiled -Dfopen=spike_fopen so that
 * "ga686bx/6BX.F2a" reaches the mount as "ga686bx_6BX.F2a". This is a
 * placeholder for PLAN.md section 5's real answer, which maps PCem's name
 * onto the firmware id the engine mounted. */
void get_pcem_path(char *s, int size) { strncpy(s, "/x", size - 1); s[size - 1] = 0; }

FILE *spike_fopen(const char *path, const char *mode)
{
        char flat[256];
        size_t i;
        const char *p = path;
        while (*p == '/') p++;
        for (i = 0; i < sizeof flat - 1 && p[i]; i++)
                flat[i] = (p[i] == '/') ? '_' : p[i];
        flat[i] = 0;
        return fopen(flat, mode);
}

static void spike_blit(int x, int y, int y1, int y2, int w, int h)
{
        int yy;
        (void)h;
        if (y1 == y2) return;
        for (yy = y1; yy < y2; yy++)
                if ((y + yy) >= 0 && (y + yy) < buffer32->h)
                        memcpy(screen->dat + ((size_t)yy * screen->w * 4),
                               &(((uint32_t *)buffer32->line[y + yy])[x]), (size_t)w * 4);
        blit_w = w; blit_h = h; blit_count++;
}

void startblit(void) {}
void endblit(void) {}
void updatewindowsize(int x, int y) { (void)x; (void)y; }
void set_window_title(const char *s) { (void)s; }
void givealbuffer(int32_t *b) { (void)b; }
void givealbuffer_cd(int16_t *b) { (void)b; }
void initalmain(int argc, char *argv[]) { (void)argc; (void)argv; }
void inital(void) {}
void keyboard_poll_host(void) {}
void mouse_poll_host(void) {}
void mouse_get_mickeys(int *x, int *y, int *z) { *x = *y = *z = 0; }
void joystick_poll(void) {}
void midi_write(uint8_t v) { (void)v; }
void stop_emulation_now(void) { printf("spike: guest powered off\n"); }
void warning(const char *f, ...)
{
        va_list ap; va_start(ap, f); vfprintf(stderr, f, ap); va_end(ap);
        fputc('\n', stderr);
}

/* ------------------------------------------------------------- exports */

ECL_EXPORT int Init(void)
{
        char *argv[3];
        pclogf = stderr;
        paths_init();
        /* Flat VFS: one roms path, "/". It cannot be the empty string -
         * get_roms_path() (paths.c:33-59) returns 0 without touching its
         * buffer for a zero-length path, and romfopen() (rom.c:16-21)
         * ignores that return and strcat()s onto the uninitialised stack
         * buffer. So every ROM would be looked up under garbage. */
        strcpy(roms_paths, "/");
        num_roms_paths = 1;
        screen = create_bitmap(2048, 2048);
        video_blit_memtoscreen_func = spike_blit;

        argv[0] = "pcem"; argv[1] = "--config"; argv[2] = "machine.cfg";
        initpc(3, argv);
        resetpchard();
        sound_init();
        fullspeed();
        printf("spike: cpu_get_speed = %d Hz\n", cpu_get_speed());
        return 1;
}

ECL_EXPORT void Run(uint64_t ms, uint64_t slice_ms)
{
        uint64_t done = 0;
        if (!slice_ms) slice_ms = 10;
        while (done < ms) { runpc(slice_ms); done += slice_ms; }
}

ECL_EXPORT uint64_t GetBlits(void) { return (uint64_t)blit_count; }
ECL_EXPORT int GetWidth(void) { return blit_w; }
ECL_EXPORT int GetHeight(void) { return blit_h; }
ECL_EXPORT int GetSpeedHz(void) { return cpu_get_speed(); }

/* A digest of the visible framebuffer: the host compares it against the
 * native build's to prove the two ran the same machine, not merely that both
 * ran something. */
ECL_EXPORT uint64_t GetScreenDigest(void)
{
        uint64_t h = 1469598103934665603ULL;
        int x, y;
        for (y = 0; y < blit_h; y++) {
                uint32_t *row = (uint32_t *)(screen->dat + ((size_t)y * screen->w * 4));
                for (x = 0; x < blit_w; x++) { h ^= row[x]; h *= 1099511628211ULL; }
        }
        return h;
}

/* Where the recompiler's arena landed and how big it is, so the host can say
 * whether it is inside the guest's mmap arena. patches/0001 exposes these from
 * codegen_allocator.c, which otherwise keeps the pointer in a static. */
extern void *codegen_arena_base(void);
extern unsigned long codegen_arena_size(void);
ECL_EXPORT uint64_t GetJitArena(void) { return (uint64_t)(uintptr_t)codegen_arena_base(); }
ECL_EXPORT uint64_t GetJitArenaSize(void) { return (uint64_t)codegen_arena_size(); }

/* How many of the arena's 131072 blocks are in use. The whole dirty-page
 * question turns on this: an arena that is allocated but never written costs
 * nothing, in faults or in state bytes. */
extern int codegen_allocator_usage;
ECL_EXPORT uint64_t GetJitBlocksUsed(void) { return (uint64_t)codegen_allocator_usage; }

int main(void) { return 0; }

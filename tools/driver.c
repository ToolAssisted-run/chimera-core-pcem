/* M1a measurement driver.
 *
 * Replaces PCem's wx/SDL/OpenAL platform layer -- 27 symbols in total, which
 * is the whole surface -- with stubs, and times how much wall clock it costs
 * to emulate a fixed amount of machine time.
 *
 *   speed%  = emulated_ms / wall_ms * 100
 *
 * That is the same quantity PCem's own status bar reports, computed directly
 * instead of through a GUI timer. Nothing in the emulation core is modified.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "video.h"

typedef enum { EMULATION_STOPPED, EMULATION_PAUSED, EMULATION_RUNNING } emulation_state_t;

extern void initpc(int argc, char *argv[]);
extern void resetpchard(void);
extern void runpc(uint64_t ms);
extern void closepc(void);
extern void fullspeed(void);
extern int cpu_get_speed(void);
extern void saveconfig(char *fn);
extern void paths_init(void);
extern FILE *pclogf;
extern void sound_init(void);
extern void savenvr(void);

/* ---------------------------------------------------------------- state */

emulation_state_t emulation_state = EMULATION_RUNNING;
int SOUNDBUFLEN = 48000 / 50;
uint64_t timer_freq = 1000000000ULL;
uint8_t pcem_key[272];
BITMAP *screen;
int mouse_buttons = 0;

typedef struct { int a; } dummy_joystick_t;

/* joystick_state is declared as joystick_t joystick_state[MAX_JOYSTICKS] in
 * plat-joystick.h; provide the definition through that header's own types. */
#include "plat-joystick.h"
joystick_t joystick_state[MAX_JOYSTICKS];
plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
int joysticks_present = 0;

/* ------------------------------------------------------------ platform */

uint64_t timer_read(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}

BITMAP *create_bitmap(int w, int h)
{
        int c;
        BITMAP *b = malloc(sizeof(BITMAP) + (size_t)h * sizeof(uint8_t *));
        b->dat = malloc((size_t)w * h * 4);
        for (c = 0; c < h; c++)
                b->line[c] = b->dat + (c * w * 4);
        b->w = w;
        b->h = h;
        return b;
}

void destroy_bitmap(BITMAP *b)
{
        free(b->dat);
        free(b);
}

void hline(BITMAP *b, int x1, int y, int x2, int col)
{
        uint32_t *p = (uint32_t *)b->line[y];
        int x;
        for (x = x1; x < x2; x++)
                p[x] = col;
}

int dir_exists(char *path)
{
        struct stat st;
        return stat(path, &st) == 0 && (st.st_mode & S_IFDIR);
}

void get_pcem_path(char *s, int size)
{
        const char *p = getenv("PCEM_PATH");
        if (!p)
                p = "./";
        strncpy(s, p, size - 1);
        s[size - 1] = 0;
}

/* The one function pointer the platform layer owns (video.c:733). Doing the
 * real copy here keeps the measurement honest: a Chimera driver does the same
 * work when it hands the frame over. */
extern BITMAP *buffer32;
static int blit_w = 0, blit_h = 0;
static long blit_count = 0;

static void bench_blit(int x, int y, int y1, int y2, int w, int h)
{
        int yy;
        if (y1 == y2)
                return;
        for (yy = y1; yy < y2; yy++)
                if ((y + yy) >= 0 && (y + yy) < buffer32->h)
                        memcpy(screen->dat + ((size_t)yy * screen->w * 4),
                               &(((uint32_t *)buffer32->line[y + yy])[x]), (size_t)w * 4);
        blit_w = w;
        blit_h = h;
        blit_count++;
}

/* A PPM of the last blitted frame: the only way to prove what the machine
 * actually put on screen without a window. */
static void write_shot(const char *name)
{
        FILE *f;
        int xx, yy;
        if (blit_w <= 0 || blit_h <= 0) {
                printf("bench: no frame blitted yet, no screenshot\n");
                return;
        }
        f = fopen(name, "wb");
        if (!f)
                return;
        fprintf(f, "P6\n%d %d\n255\n", blit_w, blit_h);
        for (yy = 0; yy < blit_h; yy++) {
                uint32_t *row = (uint32_t *)(screen->dat + ((size_t)yy * screen->w * 4));
                for (xx = 0; xx < blit_w; xx++) {
                        uint32_t p = row[xx];
                        fputc((p >> 16) & 0xff, f);
                        fputc((p >> 8) & 0xff, f);
                        fputc(p & 0xff, f);
                }
        }
        fclose(f);
        printf("bench: wrote %s (%dx%d, %ld blits so far)\n", name, blit_w, blit_h, blit_count);
}

void startblit(void) {}
void endblit(void) {}
void updatewindowsize(int x, int y) {}
void set_window_title(const char *s) {}

void givealbuffer(int32_t *buf) {}
void givealbuffer_cd(int16_t *buf) {}
void initalmain(int argc, char *argv[]) {}
void inital(void) {}

/* Scripted keyboard: "<ms>:<xt scancode>[:<hold ms>]" entries, comma
 * separated. pcem_key[] is indexed by XT scancode, extended keys at 0x80+code
 * (keyboard.c:421-438). The Chimera driver will set the same array from the
 * frame's input word. */
#define MAX_KEYEV 64
static struct { uint64_t at_ms, until_ms; int code; } keyev[MAX_KEYEV];
static int keyev_n = 0;
static uint64_t emu_ms_now = 0;

static void parse_keys(char *spec)
{
        char *tok, *save = NULL;
        for (tok = strtok_r(spec, ",", &save); tok && keyev_n < MAX_KEYEV;
             tok = strtok_r(NULL, ",", &save)) {
                unsigned long long at = 0, hold = 80;
                unsigned code = 0;
                if (sscanf(tok, "%llu:%x:%llu", &at, &code, &hold) >= 2) {
                        keyev[keyev_n].at_ms = at;
                        keyev[keyev_n].until_ms = at + hold;
                        keyev[keyev_n].code = code & 0x1ff;
                        keyev_n++;
                }
        }
}

void keyboard_poll_host(void)
{
        /* Build the whole desired key state first: several events may name the
         * same key, and applying them one at a time makes a later inactive
         * event cancel an earlier active one. */
        static uint8_t want[272];
        int i;
        memset(want, 0, sizeof want);
        for (i = 0; i < keyev_n; i++)
                if (emu_ms_now >= keyev[i].at_ms && emu_ms_now < keyev[i].until_ms)
                        want[keyev[i].code] = 1;
        for (i = 0; i < 272; i++) {
                if (want[i] != pcem_key[i]) {
                        pcem_key[i] = want[i];
                        printf("bench: key 0x%02x %s at %llums\n", i,
                               want[i] ? "down" : "up",
                               (unsigned long long)emu_ms_now);
                }
        }
}
void mouse_poll_host(void) {}
void mouse_get_mickeys(int *x, int *y, int *z) { *x = 0; *y = 0; *z = 0; }
void joystick_poll(void) {}

void midi_write(uint8_t val) {}

void stop_emulation_now(void)
{
        printf("\nbench: guest asked for power-off (stop_emulation_now)\n");
        fflush(stdout);
        exit(0);
}

void warning(const char *format, ...)
{
        va_list ap;
        fprintf(stderr, "warning: ");
        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);
        fprintf(stderr, "\n");
}

/* ---------------------------------------------------------------- main */

static double now_s(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char *argv[])
{
        uint64_t slice_ms = 10;
        uint64_t total_ms = 10000;
        uint64_t shot_every_ms = 0, next_shot = 0;
        const char *shot_dir = ".";
        uint64_t done = 0;
        double t0, last_report, wall;
        uint64_t last_done = 0;
        int c;
        char *pcargv[16];
        int pcargc = 0;

        pcargv[pcargc++] = argv[0];
        for (c = 1; c < argc; c++) {
                if (!strcmp(argv[c], "--bench-ms") && c + 1 < argc)
                        total_ms = strtoull(argv[++c], NULL, 10);
                else if (!strcmp(argv[c], "--slice-ms") && c + 1 < argc)
                        slice_ms = strtoull(argv[++c], NULL, 10);
                else if (!strcmp(argv[c], "--shot-every-ms") && c + 1 < argc)
                        shot_every_ms = strtoull(argv[++c], NULL, 10);
                else if (!strcmp(argv[c], "--shot-dir") && c + 1 < argc)
                        shot_dir = argv[++c];
                else if (!strcmp(argv[c], "--keys") && c + 1 < argc)
                        parse_keys(argv[++c]);
                else if (pcargc < 15)
                        pcargv[pcargc++] = argv[c];
        }
        pcargv[pcargc] = NULL;

        setvbuf(stdout, NULL, _IOLBF, 0);
        pclogf = stderr;          /* PCem's fatal() writes here unconditionally */
        paths_init();             /* normally done by wx-sdl2.c */
        screen = create_bitmap(2048, 2048);
        video_blit_memtoscreen_func = bench_blit;
        /* wx-sdl2.c's pc_main() order: paths_init, initpc, resetpchard,
         * sound_init, then the video layer. */
        initpc(pcargc, pcargv);
        resetpchard();
        sound_init();
        fullspeed();

        printf("bench: emulated CPU speed = %d Hz (%.2f MHz)\n",
               cpu_get_speed(), cpu_get_speed() / 1e6);
        printf("bench: running %llu ms of machine time in %llu ms slices\n",
               (unsigned long long)total_ms, (unsigned long long)slice_ms);
        fflush(stdout);

        t0 = now_s();
        last_report = t0;
        while (done < total_ms) {
                double t;
                emu_ms_now = done;
                runpc(slice_ms);
                done += slice_ms;
                if (shot_every_ms && done >= next_shot) {
                        char nm[512];
                        snprintf(nm, sizeof nm, "%s/shot_%06llums.ppm", shot_dir,
                                 (unsigned long long)done);
                        write_shot(nm);
                        next_shot = done + shot_every_ms;
                }
                t = now_s();
                if (t - last_report >= 1.0) {
                        printf("  t=%6.1fs emulated=%8llu ms  window speed=%6.1f%%\n",
                               t - t0, (unsigned long long)done,
                               100.0 * (double)(done - last_done) / ((t - last_report) * 1000.0));
                        fflush(stdout);
                        last_report = t;
                        last_done = done;
                }
        }
        {
                char nm[512];
                snprintf(nm, sizeof nm, "%s/shot_final.ppm", shot_dir);
                write_shot(nm);
        }
        wall = now_s() - t0;
        printf("\nRESULT emulated_ms=%llu wall_s=%.3f speed=%.1f%% effective_MHz=%.1f\n",
               (unsigned long long)done, wall,
               100.0 * (double)done / (wall * 1000.0),
               (cpu_get_speed() / 1e6) * (double)done / (wall * 1000.0));
        fflush(stdout);
        savenvr();   /* persist the CMOS so a later run keeps the boot order */
        return 0;
}

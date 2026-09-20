/* pcem-driver.c - PCem as a Chimera core.
 *
 * This is the whole platform layer. PCem's emulation core exports exactly 28
 * symbols to its host (docs/M1A.md section 1) plus one function pointer, and
 * this file defines all of them; nothing in PCem's emulation sources is
 * replaced, only surrounded.
 *
 * The machine is GENERAL. Every machine PCem supports, every video and sound
 * card, every drive, is reachable from settings; the TASVideos configurations
 * are presets over the same surface, not special cases. PCem's own settings
 * live in a .cfg file, so the driver composes one in memory from the Chimera
 * settings and hands PCem a FILE* over it - see pcem_driver_fopen.
 *
 * Built twice from the same source: once for the miniBox guest and once
 * natively, so the equivalence gate compares like with like.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "video.h"
#include "plat-joystick.h"

#include "pcem-driver.h"

/* The guest kit gives wbx_setting_long/bool/str; these are the two shapes
 * this driver wants. */
static int drv_setting_int(const char *key, int dflt)
{
        return (int)wbx_setting_long(key, dflt);
}
static const char *drv_setting_str(const char *key, const char *dflt,
                                   char *out, int outsz)
{
        if (wbx_setting_str(key, out, outsz) < 0) {
                strncpy(out, dflt, (size_t)outsz - 1);
                out[outsz - 1] = 0;
        }
        return out;
}

typedef enum { EMULATION_STOPPED, EMULATION_PAUSED, EMULATION_RUNNING } emulation_state_t;

/* ------------------------------------------------- PCem's emulation core */

extern void initpc(int argc, char *argv[]);
extern void resetpchard(void);
extern void runpc(uint64_t ms);
extern void fullspeed(void);
extern void paths_init(void);
extern void sound_init(void);
extern int  cpu_get_speed(void);
extern FILE *pclogf;
extern BITMAP *buffer32;
extern void (*video_blit_memtoscreen_func)(int, int, int, int, int, int);
extern char roms_paths[4096];
extern int num_roms_paths;
extern int SOUNDBUFLEN_unused_marker;

/* The machine tables, so the driver can validate a setting against what this
 * build of PCem actually has rather than against a list that can drift. */
extern int model_count(void);
extern int model_get_model_from_internal_name(char *name);
extern char *model_get_internal_name_ex(int model);
extern int video_get_video_from_internal_name(char *name);

/* ------------------------------------------------------------- our state */

emulation_state_t emulation_state = EMULATION_RUNNING;
int SOUNDBUFLEN = 48000 / 50;
uint64_t timer_freq = 1000000000ULL;
uint8_t pcem_key[272];
BITMAP *screen;
int mouse_buttons = 0;
joystick_t joystick_state[MAX_JOYSTICKS];
plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
int joysticks_present = 0;

static char g_loadError[512];
static int  g_width = 640, g_height = 480;
static long g_blits;
static int  g_inputRead;
static int  g_renderEnabled = 1;

/* the frame slice, from the fps setting */
static uint64_t g_frameMs = 10;

/* the composed PCem .cfg */
static char g_cfg[16384];
static int  g_cfgLen;

/* video: PCem draws into buffer32 (2048x2048) and blits a rectangle out.
 * Chimera wants a tight BGRA frame. */
static uint32_t *g_frame;

/* audio: PCem hands over SOUNDBUFLEN stereo int32 samples at 48 kHz whenever
 * its emulated timer says so; a frame's worth is whatever arrived. */
#define AUDIO_CAP 48000
static int16_t g_audio[AUDIO_CAP * 2];
static int     g_audioSamples;

/* The guest clock is frozen, and PCem reads the host clock only for the
 * status bar's blitter statistics (PLAN.md section 2, point 5). A counter is
 * faithful there and, unlike a real clock, cannot desync the machine. */
static uint64_t g_fakeTicks;

/* ------------------------------------------------- the 28 platform symbols */

uint64_t timer_read(void) { return g_fakeTicks += 1000; }

BITMAP *create_bitmap(int w, int h)
{
        int c;
        BITMAP *b = malloc(sizeof(BITMAP) + (size_t)h * sizeof(uint8_t *));
        if (!b) return NULL;
        b->dat = malloc((size_t)w * h * 4);
        for (c = 0; c < h; c++)
                b->line[c] = b->dat + (c * w * 4);
        b->w = w; b->h = h;
        return b;
}

void destroy_bitmap(BITMAP *b) { free(b->dat); free(b); }

void hline(BITMAP *b, int x1, int y, int x2, int col)
{
        uint32_t *p;
        if (y < 0 || y >= b->h) return;
        p = (uint32_t *)b->line[y];
        for (; x1 < x2; x1++) p[x1] = col;
}

int dir_exists(char *path) { (void)path; return 0; }

/* The sandbox VFS is flat, so PCem's roms path is "/" and every ROM is asked
 * for by the name PCem builds from it. rom.c, nvr.c and config.c are compiled
 * -Dfopen=pcem_driver_fopen so those names can be translated. */
void get_pcem_path(char *s, int size) { strncpy(s, "/x", size - 1); s[size - 1] = 0; }

void startblit(void) {}
void endblit(void) {}
void updatewindowsize(int x, int y) { (void)x; (void)y; }
void set_window_title(const char *s) { (void)s; }
void midi_write(uint8_t v) { (void)v; }
void initalmain(int argc, char *argv[]) { (void)argc; (void)argv; }
void inital(void) {}
void joystick_poll(void) {}
void mouse_poll_host(void) {}
void keyboard_poll_host(void) { g_inputRead = 1; }

static int g_mouseDx, g_mouseDy, g_mouseDz;
void mouse_get_mickeys(int *x, int *y, int *z)
{
        *x = g_mouseDx; *y = g_mouseDy; *z = g_mouseDz;
        g_mouseDx = g_mouseDy = g_mouseDz = 0;
        g_inputRead = 1;
}

void stop_emulation_now(void) { /* the guest asked for power-off; keep running */ }

void warning(const char *f, ...)
{
        va_list ap; va_start(ap, f); vfprintf(stderr, f, ap); va_end(ap);
        fputc('\n', stderr);
}

/* PCem's video blit: copy the machine's rectangle into the frame buffer. */
static void driver_blit(int x, int y, int y1, int y2, int w, int h)
{
        int yy;
        if (y1 == y2 || w <= 0 || h <= 0) return;
        if (w > PCEM_VIDEO_MAX_W) w = PCEM_VIDEO_MAX_W;
        if (h > PCEM_VIDEO_MAX_H) h = PCEM_VIDEO_MAX_H;
        g_width = w; g_height = h;
        /* ALWAYS draw, even when rendering is "disabled". Turbo skips the
         * READBACK, not the drawing: a PC paints its screen once and then
         * leaves it alone for minutes, so a driver that skips the copy loses
         * that picture rather than deferring it, and the frame the frontend
         * finally reads is blank. SetRenderingEnabled is recorded and
         * deliberately not acted on here.
         *
         * Rows are PACKED at the live width. The engine reads width*height
         * contiguous pixels from GetVideoBgra (session.cpp:824-853) - there is
         * no pitch in the ABI - so a fixed stride leaves it reading the gap
         * between rows and the picture comes out black. */
        for (yy = y1; yy < y2 && yy < h; yy++)
                if ((y + yy) >= 0 && (y + yy) < buffer32->h)
                        memcpy(g_frame + (size_t)yy * (size_t)w,
                               &(((uint32_t *)buffer32->line[y + yy])[x]),
                               (size_t)w * 4);
        g_blits++;
}

void givealbuffer(int32_t *buf)
{
        int c;
        for (c = 0; c < SOUNDBUFLEN * 2 && g_audioSamples < AUDIO_CAP * 2; c++) {
                int32_t v = buf[c];
                if (v < -32768) v = -32768;
                if (v > 32767) v = 32767;
                g_audio[g_audioSamples++] = (int16_t)v;
        }
}

void givealbuffer_cd(int16_t *buf)
{
        /* CD audio is already mixed with ATAPI volume before it gets here
         * (sound.c:180-221); add it into whatever the cards produced. */
        int c, n = SOUNDBUFLEN * 2;
        for (c = 0; c < n && c < g_audioSamples; c++) {
                int32_t v = g_audio[c] + buf[c];
                if (v < -32768) v = -32768;
                if (v > 32767) v = 32767;
                g_audio[c] = (int16_t)v;
        }
}

/* --------------------------------------------------------- the file shim */

/* PCem opens three kinds of file by a path it builds itself: ROMs under the
 * roms path, the NVRAM under the nvr path, and its .cfg. The sandbox VFS is
 * flat and has no directories, so a name like "ga686bx/6BX.F2a" is translated
 * to the flat mount name "ga686bx_6BX.F2a".
 *
 * Flattening to the BASENAME would not do: bios.bin alone is claimed by
 * ati28800, mach64gx, oti037 and oti067 (PLAN.md section 5), so the
 * directory has to survive into the name.
 *
 * The .cfg is not a file at all - it is composed in memory from the settings
 * and handed over with fmemopen. */
FILE *pcem_driver_fopen(const char *path, const char *mode)
{
        char flat[512];
        size_t i;
        const char *p = path;

        if (strstr(path, PCEM_CFG_NAME))
                return fmemopen(g_cfg, (size_t)g_cfgLen, "rb");

        while (*p == '/') p++;
        for (i = 0; i < sizeof flat - 1 && p[i]; i++)
                flat[i] = (p[i] == '/') ? '_' : p[i];
        flat[i] = 0;
        return fopen(flat, mode);
}

/* ------------------------------------------------- settings -> PCem .cfg */

static void cfg_add(const char *fmt, ...)
{
        va_list ap;
        int n;
        va_start(ap, fmt);
        n = vsnprintf(g_cfg + g_cfgLen, sizeof g_cfg - g_cfgLen, fmt, ap);
        va_end(ap);
        if (n > 0) g_cfgLen += n;
}

/* The engine mounts each project file under its own file name and hands the
 * guest a "slots" map of slot id -> names in swap order (project.cpp:1232).
 * So a slot is resolved to a NAME first, and the name is what PCem is told.
 * A slot the user left empty yields nothing and the corresponding PCem key is
 * then omitted rather than pointing at a file that does not exist. */
static const char *slot_file(const char *id, char *out, int outsz)
{
        const char *n = wbx_slot_first(id, out, outsz);
        if (!n || !n[0]) return NULL;
        {
                FILE *f = fopen(n, "rb");
                if (!f) return NULL;
                fclose(f);
        }
        return n;
}

static void compose_cfg(void)
{
        char buf[256];
        char slot[256];
        const char *fn;

        g_cfgLen = 0;

        cfg_add("model = %s\n", drv_setting_str("machine", "ibmat", buf, sizeof buf));
        cfg_add("cpu_manufacturer = %d\n", drv_setting_int("cpuManufacturer", 0));
        cfg_add("cpu = %d\n", drv_setting_int("cpu", 0));
        cfg_add("fpu = %s\n", drv_setting_str("fpu", "none", buf, sizeof buf));
        cfg_add("cpu_use_dynarec = %d\n", wbx_setting_bool("dynarec", 1) ? 1 : 0);
        cfg_add("cpu_waitstates = %d\n", drv_setting_int("cpuWaitStates", 0));
        cfg_add("mem_size = %d\n", drv_setting_int("memSizeKB", 4096));

        cfg_add("gfxcard = %s\n", drv_setting_str("videoCard", "vga", buf, sizeof buf));
        cfg_add("video_speed = %d\n", drv_setting_int("videoSpeed", -1));
        cfg_add("voodoo = %d\n", wbx_setting_bool("voodoo", 0) ? 1 : 0);

        cfg_add("sndcard = %s\n", drv_setting_str("soundCard", "none", buf, sizeof buf));
        cfg_add("gameblaster = %d\n", wbx_setting_bool("gameBlaster", 0) ? 1 : 0);
        cfg_add("gus = %d\n", wbx_setting_bool("gus", 0) ? 1 : 0);
        cfg_add("ssi2001 = %d\n", wbx_setting_bool("ssi2001", 0) ? 1 : 0);

        cfg_add("hdd_controller = %s\n", drv_setting_str("hddController", "none", buf, sizeof buf));

        /* Drive C: and D:. Geometry 0 means "derive from the image", which
         * PCem's own new-disk dialog does by file size. */
        if ((fn = slot_file(PCEM_SLOT_HDD, slot, sizeof slot))) {
                cfg_add("hdc_fn = %s\n", fn);
                cfg_add("hdc_sectors = %d\n", drv_setting_int("hddSectors", 0));
                cfg_add("hdc_heads = %d\n", drv_setting_int("hddHeads", 0));
                cfg_add("hdc_cylinders = %d\n", drv_setting_int("hddCylinders", 0));
        }
        if ((fn = slot_file(PCEM_SLOT_HDD2, slot, sizeof slot))) {
                cfg_add("hdd_fn = %s\n", fn);
                cfg_add("hdd_sectors = %d\n", drv_setting_int("hdd2Sectors", 0));
                cfg_add("hdd_heads = %d\n", drv_setting_int("hdd2Heads", 0));
                cfg_add("hdd_cylinders = %d\n", drv_setting_int("hdd2Cylinders", 0));
        }

        if ((fn = slot_file(PCEM_SLOT_FLOPPY_A, slot, sizeof slot))) cfg_add("disc_a = %s\n", fn);
        if ((fn = slot_file(PCEM_SLOT_FLOPPY_B, slot, sizeof slot))) cfg_add("disc_b = %s\n", fn);
        cfg_add("drive_a_type = %d\n", drv_setting_int("driveAType", 7));
        cfg_add("drive_b_type = %d\n", drv_setting_int("driveBType", 2));
        cfg_add("bpb_disable = %d\n", wbx_setting_bool("bpbDisable", 0) ? 1 : 0);

        /* The CD-ROM. cdrom_drive 200 is "an image"; 0 is "no drive". Leaving
         * the channel set with no drive crashes PCem in callbackide (XP.md
         * section 6), so the channel goes with the drive. */
        if ((fn = slot_file(PCEM_SLOT_CDROM, slot, sizeof slot))) {
                cfg_add("cdrom_drive = 200\n");
                cfg_add("cdrom_path = %s\n", fn);
                cfg_add("cdrom_channel = %d\n", drv_setting_int("cdChannel", 2));
                cfg_add("cd_speed = %d\n", drv_setting_int("cdSpeed", 24));
                cfg_add("cd_model = %s\n", drv_setting_str("cdModel", "pcemcd", buf, sizeof buf));
        } else {
                cfg_add("cdrom_drive = 0\n");
                cfg_add("cdrom_channel = -1\n");
        }
        cfg_add("zip_channel = -1\n");

        cfg_add("mouse_type = %d\n", drv_setting_int("mouseType", 0));
        cfg_add("joystick_type = %d\n", drv_setting_int("joystickType", 0));
        cfg_add("lpt1_device = %s\n", drv_setting_str("lpt1Device", "none", buf, sizeof buf));

        /* The one host-clock seam PCem has (rtc.c:227-238) is enable_sync,
         * which seeds the CMOS from the host clock at boot. A movie cannot
         * have that, so it is always off and the date is a declared setting
         * the driver writes into the CMOS itself. */
        cfg_add("enable_sync = 0\n");

        g_cfg[g_cfgLen] = 0;
}

/* ------------------------------------------------------------ the ABI */

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
        char *argv[3];
        char buf[64];
        int num, den;

        g_loadError[0] = 0;
        pclogf = stderr;

        g_frame = (uint32_t *)alloc_invisible((size_t)PCEM_VIDEO_MAX_W * PCEM_VIDEO_MAX_H * 4);
        if (!g_frame) {
                snprintf(g_loadError, sizeof g_loadError, "no room for the frame buffer");
                return 0;
        }

        paths_init();
        /* Flat VFS: one roms path, "/". It cannot be empty - get_roms_path()
         * (paths.c:33-59) returns 0 without touching its buffer for a
         * zero-length path, and romfopen() (rom.c:16-21) ignores that and
         * strcat()s onto an uninitialised stack buffer. */
        strcpy(roms_paths, "/");
        num_roms_paths = 1;

        compose_cfg();

        screen = create_bitmap(2048, 2048);
        if (!screen) {
                snprintf(g_loadError, sizeof g_loadError, "no room for the blit bitmap");
                return 0;
        }
        video_blit_memtoscreen_func = driver_blit;

        argv[0] = "pcem"; argv[1] = "--config"; argv[2] = PCEM_CFG_NAME;
        initpc(3, argv);
        resetpchard();
        sound_init();
        fullspeed();

        num = drv_setting_int("fpsNumerator", 100);
        den = drv_setting_int("fpsDenominator", 1);
        if (num <= 0) { num = 100; den = 1; }
        if (den <= 0) den = 1;
        g_frameMs = (uint64_t)(1000.0 * den / num);
        if (!g_frameMs) g_frameMs = 1;

        (void)buf;
        return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
        pcem_driver_set_button(index, state);
}

ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
        pcem_driver_set_axis(index, value, &g_mouseDx, &g_mouseDy, &g_mouseDz);
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
        int i;
        for (i = 0; i < 64 && i < PCEM_BTN_COUNT; i++)
                pcem_driver_set_packed(i, (int)((packed >> i) & 1));
        pcem_driver_apply_input();
        g_inputRead = 0;
        g_audioSamples = 0;
        runpc(g_frameMs);
}

/* Recorded for diagnostics; see driver_blit for why it does not gate the
 * copy. */
ECL_EXPORT void SetRenderingEnabled(int on) { g_renderEnabled = on != 0; }
ECL_EXPORT int GetRenderingEnabled(void) { return g_renderEnabled; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_frame; }
ECL_EXPORT int GetVideoWidth(void) { return g_width; }
ECL_EXPORT int GetVideoHeight(void) { return g_height; }

ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_audioSamples / 2; }

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }

ECL_EXPORT uint64_t GetBlitCount(void) { return (uint64_t)g_blits; }
ECL_EXPORT int GetCpuSpeedHz(void) { return cpu_get_speed(); }

/* Memory domains. Mandatory: the engine resolves all five or refuses the
 * core. PCem's RAM pointer and size are only valid after resetpchard, which
 * is why these are read live rather than captured at Init. */
extern uint8_t *ram;
extern int mem_size;              /* KB */
extern uint8_t nvrram[128];
extern int nvrmask;

ECL_EXPORT int GetMemoryDomainCount(void) { return 2; }

ECL_EXPORT const char *GetMemoryDomainName(int i)
{
        switch (i) {
        case 0: return "System RAM";
        case 1: return "CMOS/NVRAM";
        default: return NULL;
        }
}

ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i)
{
        switch (i) {
        case 0: return ram;
        case 1: return nvrram;
        default: return NULL;
        }
}

ECL_EXPORT int64_t GetMemoryDomainSize(int i)
{
        switch (i) {
        case 0: return (int64_t)mem_size * 1024;
        case 1: return nvrmask ? (int64_t)nvrmask + 1 : 128;
        default: return 0;
        }
}

ECL_EXPORT int GetMemoryDomainWritable(int i) { return i == 0 || i == 1; }

/* A per-frame digest of the visible picture. M1b's end-of-run digest did not
 * notice the emulated CPU being halved (docs/M1B.md section 6b), so the gate
 * compares a STREAM of these, one a frame, not a single value at the end. */
ECL_EXPORT uint64_t GetFrameDigest(void)
{
        uint64_t h = 1469598103934665603ULL;
        int x, y;
        for (y = 0; y < g_height; y++) {
                uint32_t *row = g_frame + (size_t)y * (size_t)g_width;
                for (x = 0; x < g_width; x++) { h ^= row[x]; h *= 1099511628211ULL; }
        }
        h ^= (uint64_t)g_width * 65537u + (uint64_t)g_height;
        h *= 1099511628211ULL;
        return h;
}

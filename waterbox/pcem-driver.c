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
#include "pcem-hdd.h"

/* The guest kit gives wbx_setting_long/bool/str; these are the two shapes
 * this driver wants. */
static int drv_setting_int(const char *key, int dflt)
{
        return (int)wbx_setting_long(key, dflt);
}
/* jsmn hands back the RAW token text, so a JSON string keeps its escapes:
 * "5.25\" 360k" arrives as 5.25\" 360k, with the backslash still in it. Every
 * floppy drive name has an inches mark in it, so without this no drive could
 * ever be matched by name and the setting was decorative. Found by the gate's
 * negative control, not by reading the code. */
static void json_unescape(char *s)
{
        char *r = s, *w = s;
        while (*r) {
                if (*r != '\\') { *w++ = *r++; continue; }
                r++;
                switch (*r) {
                case 0:    *w = 0; return;
                case 'n':  *w++ = '\n'; r++; break;
                case 't':  *w++ = '\t'; r++; break;
                case 'r':  *w++ = '\r'; r++; break;
                case 'b':  *w++ = '\b'; r++; break;
                case 'f':  *w++ = '\f'; r++; break;
                case 'u':  /* not expected in a setting value; keep it visible */
                        *w++ = '\\'; *w++ = 'u'; r++; break;
                default:   *w++ = *r++; break;   /* \" \\ \/ and anything else */
                }
        }
        *w = 0;
}

static const char *drv_setting_str(const char *key, const char *dflt,
                                   char *out, int outsz)
{
        if (wbx_setting_str(key, out, outsz) < 0) {
                strncpy(out, dflt, (size_t)outsz - 1);
                out[outsz - 1] = 0;
        } else {
                json_unescape(out);
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
extern char *mouse_get_name(int mouse);
extern char *joystick_get_name(int joystick);
#include "cpu.h"
#include "device.h"
#include "model.h"

/* PCem's per-device settings: each card's own options live in a [device name]
 * section of the .cfg, so the driver has to reach the DEVICE the settings
 * chose. All three of these read static tables and are safe before initpc. */
extern device_t *sound_card_getdevice(int card);
extern int sound_card_get_from_internal_name(char *s);
extern device_t *video_card_getdevice(int card, int romset);
extern int video_old_to_new(int card);
extern device_t voodoo_device;

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

/* The save-data export window (see the savedata group below): ECL_INVISIBLE,
 * because it is a transient view the host reads at a frame boundary and not
 * one byte of machine state. */
#define SAVEDATA_WINDOW (256 * 1024)
static uint8_t *g_saveWindow;

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

/* What a report can carry is kept, and the rest waits: pcem_take_report in
 * pcem-driver.h says why. */
void mouse_get_mickeys(int *x, int *y, int *z)
{
        *x = pcem_take_report(&g_mouseDx);
        *y = pcem_take_report(&g_mouseDy);
        *z = pcem_take_report(&g_mouseDz);
        g_inputRead = 1;
}

void stop_emulation_now(void) { /* the guest asked for power-off; keep running */ }

void warning(const char *f, ...)
{
        va_list ap; va_start(ap, f); vfprintf(stderr, f, ap); va_end(ap);
        fputc('\n', stderr);
}

/* The last rectangle PCem blitted. These are ordinary guest globals, so they
 * are machine state and a savestate carries them; the FRAME BUFFER they were
 * copied into is ECL_INVISIBLE and is not. That is the right way round - a
 * frame buffer is derivable - but it means the picture has to be rebuilt after
 * a state load, which is what StateLoaded does. Without it a loaded state
 * shows whatever the machine happened to be drawing before, possibly for
 * minutes, because a PC paints its screen once and then leaves it alone. */
static int g_lastX, g_lastY, g_lastY1, g_lastY2, g_lastW, g_lastH;

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
        g_lastX = x; g_lastY = y; g_lastY1 = y1; g_lastY2 = y2;
        g_lastW = w; g_lastH = h;
        g_blits++;
}

/* The machine's screen bitmap (buffer32) IS state - PCem mallocs it - so the
 * picture is rebuilt from it by replaying the last blit. */
ECL_EXPORT void StateLoaded(void)
{
        if (g_lastW > 0 && g_lastH > 0) {
                long blits = g_blits;
                driver_blit(g_lastX, g_lastY, g_lastY1, g_lastY2, g_lastW, g_lastH);
                g_blits = blits;     /* a redraw is not a frame the machine drew */
        }
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
extern int pcem_nvr_default(const char *name, const uint8_t **data, int *len);

FILE *pcem_driver_fopen(const char *path, const char *mode)
{
        char flat[512];
        size_t i;
        const char *p = path;

        if (strstr(path, PCEM_CFG_NAME))
                return fmemopen(g_cfg, (size_t)g_cfgLen, "rb");

        /* The CMOS default. nvrfopen() (nvr.c:33-57) asks for
         * "<config>.<machine>.nvr" first and falls back to
         * "<nvr path>/default/<machine>.nvr", and only that fallback has a
         * "default/" component in it. There is no such folder in the sandbox
         * and a project cannot supply one - the CMOS is not firmware and not
         * a user file - so the defaults travel inside the core, generated
         * from PCem's own nvr/default by tools/gen-nvr-defaults.py.
         *
         * Without this every AT-class machine stops at POST with
         * "161-System Options Not Set-(Run SETUP)" and waits for F1, which
         * is the first thing anybody trying the core would see. */
        {
                const char *d = strstr(path, "default/");
                if (d && mode[0] == 'r') {
                        const uint8_t *data;
                        int len;
                        if (pcem_nvr_default(d + 8, &data, &len))
                                return fmemopen((void *)data, (size_t)len, "rb");
                }
        }

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

/* Every enum whose value is one of PCem's internal ids is shown to the user
 * as "internal - Display", because a 93-entry list of bare ids is unreadable.
 * The id is the part before the first " - ". */
static const char *bare(const char *labelled, char *out, int outsz)
{
        const char *dash = strstr(labelled, " - ");
        int n = dash ? (int)(dash - labelled) : (int)strlen(labelled);
        if (n > outsz - 1) n = outsz - 1;
        memcpy(out, labelled, (size_t)n);
        out[n] = 0;
        return out;
}

static const char *setting_bare(const char *key, const char *dflt,
                                char *out, int outsz)
{
        char raw[256];
        drv_setting_str(key, dflt, raw, sizeof raw);
        return bare(raw, out, outsz);
}

/* Resolve a name against a list PCem itself reports, so the declared options
 * and the emulator cannot drift apart. -1 when the name is not in the list. */
static int index_of_name(char *(*get)(int), const char *want, int limit)
{
        int i;
        for (i = 0; i < limit; i++) {
                char *n = get(i);
                if (!n) break;
                if (!strcmp(n, want)) return i;
        }
        return -1;
}

/* The CPU is picked BY NAME out of every CPU PCem has. Which manufacturer
 * table of the chosen machine holds it is looked up here, so the user never
 * types an index and never picks a manufacturer. */
static int resolve_cpu(int model, const char *want, int *manufacturer, int *cpu)
{
        int m, c;
        for (m = 0; m < 4; m++) {
                CPU *list = models[model].cpu[m].cpus;
                if (!list) continue;
                for (c = 0; list[c].cpu_type != -1 && list[c].name[0]; c++) {
                        if (!strcmp(list[c].name, want)) {
                                *manufacturer = m;
                                *cpu = c;
                                return 1;
                        }
                }
        }
        return 0;
}

/* What this machine WILL take, for the error message. A list of what is
 * wrong is worth more than "invalid". */
static void cpu_choices(int model, char *out, int outsz)
{
        int m, c, n = 0;
        out[0] = 0;
        for (m = 0; m < 4; m++) {
                CPU *list = models[model].cpu[m].cpus;
                if (!list) continue;
                for (c = 0; list[c].cpu_type != -1 && list[c].name[0]; c++) {
                        int len = snprintf(out + n, (size_t)(outsz - n),
                                           "%s%s", n ? ", " : "", list[c].name);
                        if (len < 0 || n + len >= outsz - 4) { strcpy(out + n, ", ..."); return; }
                        n += len;
                }
        }
}

/* The size of a file that is mounted, or 0. */
static long file_size(const char *name)
{
        FILE *f = fopen(name, "rb");
        long n;
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fclose(f);
        return n;
}

/* "Auto" fits the drive to the disk actually in it.
 *
 * This matters more than it looks: PCem does NOT refuse an image the drive
 * cannot reach. fdd.c:91-92 clamps the head at the drive's last track, so a
 * 1.44M disk in a 360k drive mounts and then misreads, which is a confusing
 * failure to debug. Auto is the defence.
 *
 * A movie is safe to record as "Auto" because the answer is a pure function
 * of things the project already pins: the media are hashed as they are added
 * and the hash is their identity (docs/project.md), the order within a slot
 * is recorded, and the core's own version is pinned too - so the same project
 * resolves the same drive every time, and a different file is a different
 * project rather than a silent change under a movie. */
static int auto_fdd_type(const char *slot_file_name, int dflt)
{
        long n = slot_file_name ? file_size(slot_file_name) : 0;
        switch (n) {
        case 163840: case 184320: case 327680: case 368640: return 1; /* 5.25" 360k */
        case 737280:  return 4;   /* 3.5" 720k */
        case 1228800: return 2;   /* 5.25" 1.2M */
        case 1474560: return 5;   /* 3.5" 1.44M */
        case 2949120: return 7;   /* 3.5" 2.88M */
        default: break;
        }
        /* No disk, or a container that carries its own geometry (.fdi, .td0,
         * .imd, .86f) whose file length says nothing about the medium. */
        return dflt;
}

/* ------------------------------------------- PCem's PER-DEVICE settings
 *
 * A card's own options - a Sound Blaster's port, the OPL implementation, how
 * much memory a graphics card has, how many slices a Voodoo splits a triangle
 * into - are not global .cfg keys. Each lives in a [device name] section and
 * is read back through device_get_config_int, which looks the key up in THAT
 * device's own table and returns the matching number (device.c:120-133).
 *
 * Two things follow, and together they decide the shape of this code.
 *
 * The NUMBER is the device's, not the setting's. A video card's "memory" is in
 * MB on an S3 Trio64 and in kB on an AVGA2; "0x220" is 0x220 on a Sound
 * Blaster and is not in the AHA-1542C's list for the same key name at all. So
 * the Chimera setting carries the LABEL PCem shows a user - "0x220",
 * "NukedOPL", "4 MB" - and it is resolved HERE against the table of the card
 * that is actually fitted. One setting then serves 20 sound cards and 48 video
 * cards with no per-card table in the driver to drift out of date.
 *
 * And a label the fitted card does not offer writes NOTHING. An SB Pro v2
 * takes two addresses where an SB16 takes four; writing 0x260 into a Pro v2
 * would give it a port no such card ever had, and PCem does not range-check it
 * (device_get_config_int returns the file's value unvalidated). "Card default"
 * is the same path: no key written, so PCem's own default stands, which is
 * exactly what this core did before these settings existed.
 */

/* The chosen sound card's device, or NULL for None and for a card PCem has no
 * device for. */
static device_t *chosen_sound_device(void)
{
        char name[128];
        int idx;
        setting_bare("soundCard", "none", name, sizeof name);
        idx = sound_card_get_from_internal_name(name);
        if (idx < 0) return NULL;
        return sound_card_getdevice(idx);
}

/* The chosen video card's device. "builtin" is GFX_BUILTIN, and which device
 * that IS depends on the machine, so the romset has to be resolved first -
 * which it can be, because models[] is static data and compose_cfg runs before
 * initpc. */
static device_t *chosen_video_device(void)
{
        char name[128];
        int legacy, model_idx, romset_id;
        setting_bare("machine", "ibmat", name, sizeof name);
        model_idx = model_get_model_from_internal_name(name);
        if (model_idx < 0) return NULL;
        romset_id = models[model_idx].id;
        setting_bare("videoCard", "vga", name, sizeof name);
        legacy = video_get_video_from_internal_name(name);
        return video_card_getdevice(video_old_to_new(legacy), romset_id);
}

/* One key of one device, resolved from a Chimera setting that holds a label.
 * Writes nothing - and so leaves PCem's own default in place - for "Card
 * default", for a device that has no such key, and for a label the device does
 * not offer. */
static void cfg_device_key(device_t *dev, int *header_written,
                           const char *key, const char *setting)
{
        char label[128];
        device_config_t *c;

        if (!dev || !dev->config) return;
        if (wbx_setting_str(setting, label, sizeof label) < 0) return;
        json_unescape(label);
        if (!label[0] || !strcmp(label, "Card default")) return;

        for (c = dev->config; c->type != -1; c++) {
                int i, value;

                if (strcmp(c->name, key)) continue;

                if (c->type == CONFIG_BINARY) {
                        if (!strcmp(label, "Off"))     value = 0;
                        else if (!strcmp(label, "On")) value = 1;
                        else return;
                } else if (c->type == CONFIG_SELECTION) {
                        for (i = 0; i < 16 && c->selection[i].description[0]; i++) {
                                if (!strcmp(c->selection[i].description, label))
                                        break;
                        }
                        if (i >= 16 || !c->selection[i].description[0]) {
                                /* Not a value this card has. Said out loud
                                 * rather than dropped: gates.md C - absent
                                 * must not look the same as failed. */
                                fprintf(stderr, "pcem: %s=\"%s\" is not one of the "
                                        "%s's choices for %s; leaving it at the "
                                        "card's default\n",
                                        setting, label, dev->name, key);
                                return;
                        }
                        value = c->selection[i].value;
                } else {
                        return;         /* a string or MIDI key; not offered */
                }

                if (!*header_written) {
                        cfg_add("\n[%s]\n", dev->name);
                        *header_written = 1;
                }
                cfg_add("%s = %d\n", key, value);
                return;
        }
}

/* Every [device] section, appended AFTER every global key - config_load
 * assigns an entry to the section above it, so a global key written after a
 * section header would land in that section and be invisible to
 * config_get_*(CFG_MACHINE, NULL, ...). */
static void compose_device_sections(void)
{
        static const struct { const char *key, *setting; } snd[] = {
                {"addr",        "soundCardAddress"},
                {"irq",         "soundCardIrq"},
                {"dma",         "soundCardDma"},
                {"opl_emu",     "oplEmulator"},
                {"emu_addr",    "awe32EmuAddress"},
                {"onboard_ram", "awe32OnboardRam"},
        };
        static const struct { const char *key, *setting; } vid[] = {
                {"memory",         "videoMemory"},
                {"bilinear",       "videoBilinear"},
                {"dacfilter",      "videoScreenFilter"},
                {"render_threads", "videoRenderThreads"},
                {"recompiler",     "videoRecompiler"},
        };
        /* The add-in Voodoo Graphics / Voodoo 2, which is a device of its own
         * beside the 2D card. Only written when the card is actually fitted:
         * a section for a device that is not in the machine is dead text. */
        static const struct { const char *key, *setting; } vdo[] = {
                {"type",                "voodooType"},
                {"framebuffer_memory",  "voodooFramebufferMemory"},
                {"texture_memory",      "voodooTextureMemory"},
                {"bilinear",            "voodooBilinear"},
                {"dacfilter",           "voodooScreenFilter"},
                {"render_threads",      "voodooRenderThreads"},
                {"sli",                 "voodooSli"},
                {"recompiler",          "voodooRecompiler"},
        };
        size_t i;
        int hdr;
        device_t *d;

        d = chosen_sound_device();
        for (i = 0, hdr = 0; i < sizeof snd / sizeof snd[0]; i++)
                cfg_device_key(d, &hdr, snd[i].key, snd[i].setting);

        d = chosen_video_device();
        for (i = 0, hdr = 0; i < sizeof vid / sizeof vid[0]; i++)
                cfg_device_key(d, &hdr, vid[i].key, vid[i].setting);

        if (wbx_setting_bool("voodoo", 0)) {
                for (i = 0, hdr = 0; i < sizeof vdo / sizeof vdo[0]; i++)
                        cfg_device_key(&voodoo_device, &hdr,
                                       vdo[i].key, vdo[i].setting);
        }
}

static void compose_cfg(void)
{
        char buf[256];
        char slot[256];
        const char *fn;

        g_cfgLen = 0;

        cfg_add("model = %s\n", setting_bare("machine", "ibmat", buf, sizeof buf));
        /* cpu_manufacturer and cpu are written by apply_cpu() after initpc has
         * resolved the model, because the name has to be looked up in THAT
         * machine's tables. */
        cfg_add("fpu = %s\n", drv_setting_str("fpu", "none", buf, sizeof buf));
        cfg_add("cpu_use_dynarec = %d\n", wbx_setting_bool("dynarec", 1) ? 1 : 0);
        cfg_add("cpu_waitstates = %d\n", drv_setting_int("cpuWaitStates", 0));
        cfg_add("mem_size = %d\n", drv_setting_int("memSizeKB", 4096));

        cfg_add("gfxcard = %s\n", setting_bare("videoCard", "vga", buf, sizeof buf));
        {
                static const char *speeds[] = {"default", "8-bit 8MHz", "16-bit 8MHz",
                        "16-bit 12MHz", "16-bit 16MHz", "Fast VLB/PCI"};
                char v[64];
                int i, sel = -1;
                drv_setting_str("videoSpeed", "default", v, sizeof v);
                for (i = 0; i < 6; i++) if (!strcmp(speeds[i], v)) { sel = i - 1; break; }
                cfg_add("video_speed = %d\n", sel);
        }
        cfg_add("voodoo = %d\n", wbx_setting_bool("voodoo", 0) ? 1 : 0);

        cfg_add("sndcard = %s\n", setting_bare("soundCard", "none", buf, sizeof buf));
        cfg_add("gameblaster = %d\n", wbx_setting_bool("gameBlaster", 0) ? 1 : 0);
        cfg_add("gus = %d\n", wbx_setting_bool("gus", 0) ? 1 : 0);
        cfg_add("ssi2001 = %d\n", wbx_setting_bool("ssi2001", 0) ? 1 : 0);

        cfg_add("hdd_controller = %s\n", setting_bare("hddController", "none", buf, sizeof buf));

        /* Drive C: and D:.
         *
         * The geometry has to be a REAL number here, not a zero meaning "work
         * it out": nothing in PCem's emulation core derives geometry. hdd_load
         * passes hdc[d].spt/hpc/tracks straight through and ide.c reports them
         * as the drive's identity (ide.c:169-190), so a zero is a drive of no
         * sectors that the guest cannot see at all. Upstream derives it in its
         * wxWidgets new-disk dialog, which is platform layer this port
         * replaces - so Auto is pcem_hdd_derive_geometry(), which reads the
         * image's own partition table and falls back to its length. */
        if ((fn = slot_file(PCEM_SLOT_HDD, slot, sizeof slot))) {
                int s = 0, h = 0, t = 0;
                int custom = !strcmp(drv_setting_str("hddGeometry", "Auto", buf, sizeof buf), "Custom");
                if (custom) {
                        s = drv_setting_int("hddSectors", 0);
                        h = drv_setting_int("hddHeads", 0);
                        t = drv_setting_int("hddCylinders", 0);
                }
                if (!custom || s <= 0 || h <= 0 || t <= 0)
                        pcem_hdd_derive_geometry(fn, &s, &h, &t);
                cfg_add("hdc_fn = %s\n", fn);
                cfg_add("hdc_sectors = %d\n", s);
                cfg_add("hdc_heads = %d\n", h);
                cfg_add("hdc_cylinders = %d\n", t);
        }
        if ((fn = slot_file(PCEM_SLOT_HDD2, slot, sizeof slot))) {
                int s = 0, h = 0, t = 0;
                int custom2 = !strcmp(drv_setting_str("hdd2Geometry", "Auto", buf, sizeof buf), "Custom");
                if (custom2) {
                        s = drv_setting_int("hdd2Sectors", 0);
                        h = drv_setting_int("hdd2Heads", 0);
                        t = drv_setting_int("hdd2Cylinders", 0);
                }
                if (!custom2 || s <= 0 || h <= 0 || t <= 0)
                        pcem_hdd_derive_geometry(fn, &s, &h, &t);
                cfg_add("hdd_fn = %s\n", fn);
                cfg_add("hdd_sectors = %d\n", s);
                cfg_add("hdd_heads = %d\n", h);
                cfg_add("hdd_cylinders = %d\n", t);
        }

        if ((fn = slot_file(PCEM_SLOT_FLOPPY_A, slot, sizeof slot))) cfg_add("disc_a = %s\n", fn);
        if ((fn = slot_file(PCEM_SLOT_FLOPPY_B, slot, sizeof slot))) cfg_add("disc_b = %s\n", fn);
        {
                static const char *fdd[] = {"None", "5.25\" 360k", "5.25\" 1.2M",
                        "5.25\" 1.2M Dual RPM", "3.5\" 720k", "3.5\" 1.44M",
                        "3.5\" 1.44M 3-Mode", "3.5\" 2.88M"};
                char v[64], sa[256], sb[256];
                const char *fa = slot_file(PCEM_SLOT_FLOPPY_A, sa, sizeof sa);
                const char *fb = slot_file(PCEM_SLOT_FLOPPY_B, sb, sizeof sb);
                int i, a, b;

                drv_setting_str("driveAType", "Auto", v, sizeof v);
                a = auto_fdd_type(fa, 5);          /* 3.5" 1.44M, the commonest */
                for (i = 0; i < 8; i++) if (!strcmp(fdd[i], v)) { a = i; break; }
                drv_setting_str("driveBType", "Auto", v, sizeof v);
                b = auto_fdd_type(fb, 2);          /* 5.25" 1.2M, the usual second */
                for (i = 0; i < 8; i++) if (!strcmp(fdd[i], v)) { b = i; break; }

                cfg_add("drive_a_type = %d\n", a);
                cfg_add("drive_b_type = %d\n", b);
        }
        cfg_add("bpb_disable = %d\n", wbx_setting_bool("bpbDisable", 0) ? 1 : 0);

        /* The CD-ROM. cdrom_drive 200 is "an image"; 0 is "no drive". Leaving
         * the channel set with no drive crashes PCem in callbackide (XP.md
         * section 6), so the channel goes with the drive. */
        {
                char want[32];
                int fitted;
                fn = slot_file(PCEM_SLOT_CDROM, slot, sizeof slot);
                drv_setting_str("cdDrive", "Auto", want, sizeof want);
                if (!strcmp(want, "None"))       fitted = 0;
                else if (!strcmp(want, "Fitted")) fitted = 1;
                else                              fitted = fn != NULL;   /* Auto */

                if (fitted) {
                        cfg_add("cdrom_drive = 200\n");
                        if (fn) cfg_add("cdrom_path = %s\n", fn);
                        cfg_add("cdrom_channel = %d\n", drv_setting_int("cdChannel", 2));
                        cfg_add("cd_speed = %d\n", drv_setting_int("cdSpeed", 24));
                        cfg_add("cd_model = %s\n", drv_setting_str("cdModel", "pcemcd", buf, sizeof buf));
                } else {
                        /* -1, not 0, and the difference is a crash.
                         *
                         * PCem's own no-drive value on unix is -1, and it is
                         * the ONLY value that reaches cdrom_null_open()
                         * (pc.c:319-321), which is what installs the null
                         * ATAPI into the global `atapi`. With 0 the else
                         * branch calls ioctl_set_drive(), which in this build
                         * is the dummy that does nothing, and `atapi` stays
                         * NULL - so the first IDE software reset that finds an
                         * empty drive calls atapi->stop() through it and the
                         * machine dies in callbackide (ide.c:824-829). Any
                         * machine with a hard disk hits that, because the
                         * other three IDE drives are IDE_NONE.
                         *
                         * This is the same crash docs/XP.md section 6 recorded
                         * from the other direction; clearing the channel was
                         * half of it, and this is the other half.
                         *
                         * The channel goes with the drive regardless: a channel
                         * set with no drive behind it attaches an ATAPI device
                         * to nothing. */
                        cfg_add("cdrom_drive = -1\n");
                        cfg_add("cdrom_channel = -1\n");
                }
        }
        cfg_add("zip_channel = -1\n");

        {
                char v[128];
                int i;
                drv_setting_str("mouseType", "Microsoft 2-button mouse (serial)", v, sizeof v);
                i = index_of_name(mouse_get_name, v, 16);
                cfg_add("mouse_type = %d\n", i < 0 ? 0 : i);
                pcem_driver_set_mouse_sensitivity(wbx_setting_double("mouseSensitivity", 0.5));
                drv_setting_str("joystickType", "Standard 2-button joystick(s)", v, sizeof v);
                i = index_of_name(joystick_get_name, v, 16);
                cfg_add("joystick_type = %d\n", i < 0 ? 0 : i);
        }
        cfg_add("lpt1_device = %s\n", drv_setting_str("lpt1Device", "none", buf, sizeof buf));

        /* The one host-clock seam PCem has (rtc.c:227-238) is enable_sync,
         * which seeds the CMOS from the host clock at boot. A movie cannot
         * have that, so it is always off and the date is a declared setting
         * the driver writes into the CMOS itself. */
        cfg_add("enable_sync = 0\n");

        /* LAST, and it has to be last: every key above belongs to the global
         * section, and config_load puts an entry in whichever section header
         * precedes it. */
        compose_device_sections();

        g_cfg[g_cfgLen] = 0;
}

/* ------------------------------------------------- the CMOS, made to fit
 *
 * An AT-class machine keeps its equipment list in CMOS, and a BIOS that finds
 * one drive when the CMOS says two stops at POST with "162-System Options Not
 * Set-(Run SETUP)" and waits for F1. A real owner ran SETUP once; a Chimera
 * user builds the machine out of settings and has nothing to run SETUP on,
 * and pressing F1 on the first frame of every movie is not a thing anybody
 * should have to record.
 *
 * So the CMOS PCem's own nvr/default seeded is edited to describe the machine
 * the settings actually built. Only the four standard MC146818 equipment
 * bytes and the checksum: 0x10 (floppy types), 0x14 (drive count, coprocessor
 * and display) and 0x2E/0x2F (the sum of 0x10..0x2D). Those offsets are the
 * AT's own contract and are the same on every AT-class BIOS; everything else
 * in the CMOS - the disk types, the chipset's own bytes, the setup screens'
 * preferences - is left exactly as PCem shipped it.
 *
 * MEASURED: an IBM AT with the default at.nvr and two 1.2M drives POSTs
 * clean and boots; the same machine with ONE drive stops at 162. That is the
 * whole difference this closes.
 */
extern uint8_t nvrram[128];
extern int nvrmask;
extern int fdd_get_type(int drive);

static void apply_cmos(const char *videoCard)
{
        /* PCem drive type -> CMOS drive type. PCem: 0 none, 1 360k, 2 1.2M,
         * 3 1.2M dual RPM, 4 720k, 5 1.44M, 6 1.44M 3-mode, 7 2.88M.
         * CMOS: 0 none, 1 360K, 2 1.2M, 3 720K, 4 1.44M, 5 2.88M. */
        static const uint8_t fdd_cmos[8] = { 0, 1, 2, 2, 3, 4, 4, 5 };
        int a, b, n, i, sum, video = 0;
        char fpu[32];

        extern int model;
        if (!(models[model].flags & MODEL_AT)) return;   /* an XT has no CMOS */
        if (nvrmask < 63) return;                        /* not an MC146818 */

        a = fdd_get_type(0); b = fdd_get_type(1);
        if (a < 0 || a > 7) a = 0;
        if (b < 0 || b > 7) b = 0;
        nvrram[0x10] = (uint8_t)((fdd_cmos[a] << 4) | fdd_cmos[b]);

        /* bits 5-4 of the equipment byte: 00 EGA/VGA or none, 01 CGA 40x25,
         * 10 CGA 80x25, 11 monochrome. */
        if (!strcmp(videoCard, "mda") || !strcmp(videoCard, "hercules")
            || !strcmp(videoCard, "incolor") || !strcmp(videoCard, "genius")
            || !strcmp(videoCard, "wy700"))
                video = 3;
        else if (!strcmp(videoCard, "cga") || !strcmp(videoCard, "compaq_cga")
                 || !strcmp(videoCard, "plantronics") || !strcmp(videoCard, "sigma400"))
                video = 2;

        n = (a ? 1 : 0) + (b ? 1 : 0);
        drv_setting_str("fpu", "none", fpu, sizeof fpu);
        nvrram[0x14] = (uint8_t)((nvrram[0x14] & 0x0c)
                                 | (n ? 0x01 : 0x00)
                                 | (strcmp(fpu, "none") ? 0x02 : 0x00)
                                 | ((uint8_t)video << 4)
                                 | (n ? (uint8_t)((n - 1) << 6) : 0));

        sum = 0;
        for (i = 0x10; i <= 0x2d; i++) sum += nvrram[i];
        nvrram[0x2e] = (uint8_t)((sum >> 8) & 0xff);
        nvrram[0x2f] = (uint8_t)(sum & 0xff);
}

/* ------------------------------------------------------------ the ABI */

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

/* The .cfg the settings composed, as PCem was handed it. A harness that can
 * read this can check that a setting REACHED the machine instead of taking
 * the settings page's word for it - which is how the hard disk's derived
 * geometry is checked in the gate. */
ECL_EXPORT const char *GetComposedConfig(void) { return g_cfg; }

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

        g_saveWindow = (uint8_t *)alloc_invisible(SAVEDATA_WINDOW);
        if (!g_saveWindow) {
                snprintf(g_loadError, sizeof g_loadError, "no room for the save-data window");
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

        /* The CPU is chosen BY NAME out of every CPU PCem has, so it has to be
         * resolved against the machine initpc just settled on. A CPU this
         * machine does not take is refused here, by name, with the list of the
         * ones it does - which is more use than a silent fallback to whatever
         * index 0 happens to be. */
        {
                char want[128];
                extern int model, cpu_manufacturer, cpu;
                drv_setting_str("cpu", "286/6", want, sizeof want);
                if (!resolve_cpu(model, want, &cpu_manufacturer, &cpu)) {
                        char choices[768];
                        cpu_choices(model, choices, sizeof choices);
                        snprintf(g_loadError, sizeof g_loadError,
                                 "this machine does not take a '%s'. It takes: %s",
                                 want, choices);
                        return 0;
                }
                cpu_set();
        }

        resetpchard();
        /* after resetpchard, because loadnvr() runs inside it */
        apply_cmos(setting_bare("videoCard", "vga", buf, sizeof buf));
        sound_init();
        fullspeed();

        num = drv_setting_int("fpsNumerator", 100);
        den = drv_setting_int("fpsDenominator", 1);
        if (num <= 0) { num = 100; den = 1; }
        if (den <= 0) den = 1;
        g_frameMs = (uint64_t)(1000.0 * den / num);
        if (!g_frameMs) g_frameMs = 1;

        return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
        pcem_driver_set_button(index, state);
}

ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
        pcem_driver_set_axis(index, value, &g_mouseDx, &g_mouseDy, &g_mouseDz,
                             g_width, g_height);
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
        /* The relative axes are spent: the next frame's SetAxis calls decide
         * again whether they or the position drive the mouse. Cleared AFTER
         * the frame rather than before it, because SetAxis runs before
         * FrameAdvance and clearing there would erase what it just said. */
        pcem_driver_clear_axis_frame();
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

/* What the drives were actually fitted as, so a harness can check that Auto
 * did what it claims rather than taking the setting's word for it. */
extern int fdd_get_type(int drive);
ECL_EXPORT int GetDriveAType(void) { return fdd_get_type(0); }
ECL_EXPORT int GetDriveBType(void) { return fdd_get_type(1); }

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

/* -------------------------------------------- save data (docs/save-data.md)
 *
 * What a PC keeps is its hard disks, so that is what leaves: one file per
 * disk, the whole thing byte for byte, under the name of the project file it
 * was seeded from. Hand it back into the same slot and the machine carries on
 * from where it was.
 *
 * It goes out through the WINDOW, not the pointer. There is no address at
 * which a 4 GiB disk exists - it is a read-only seed the box streams from
 * disc plus a scatter of 4 KiB blocks in guest memory (pcem-hdd.c) - so
 * GetSaveDataFileBuffer answers null, which is what tells the engine to use
 * ReadSaveDataFile/GetSaveDataScratch instead (session.cpp:2193). The window
 * is ECL_INVISIBLE: it is a transient view the host reads at a frame
 * boundary, not machine state, and a savestate must not carry it.
 */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return (int32_t)pcem_hdd_count(); }

ECL_EXPORT const char *GetSaveDataFileName(int32_t index)
{
        return pcem_hdd_export_name((int)index);
}

ECL_EXPORT int64_t GetSaveDataFileSize(int32_t index)
{
        return pcem_hdd_export_size((int)index);
}

ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t index)
{
        (void)index;
        return NULL;              /* use the window */
}

ECL_EXPORT const uint8_t *GetSaveDataScratch(void) { return g_saveWindow; }

ECL_EXPORT int64_t ReadSaveDataFile(int32_t index, int64_t offset, int64_t len)
{
        if (!g_saveWindow || len <= 0) return 0;
        if (len > SAVEDATA_WINDOW) len = SAVEDATA_WINDOW;
        return pcem_hdd_export_read((int)index, offset, g_saveWindow, len);
}

/* For the gate and the state measurement: how much of the disk the machine is
 * actually holding, and how many block-writes cost nothing because they
 * matched the seed. */
ECL_EXPORT int64_t GetHddBlocksHeld(int32_t index) { return pcem_hdd_blocks_held((int)index); }
ECL_EXPORT int64_t GetHddSeedMatches(int32_t index) { return pcem_hdd_writes_matching_seed((int)index); }
ECL_EXPORT int32_t GetHddBlockBytes(void) { return pcem_hdd_block_bytes(); }

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

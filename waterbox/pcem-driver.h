/* pcem-driver.h - the driver's own contract: the video capacity, the slot
 * names the engine mounts, and the input wire.
 */
#ifndef PCEM_DRIVER_H
#define PCEM_DRIVER_H

#include <stdint.h>

/* PCem draws into a 2048x2048 bitmap (video.c:1069). No PC video mode PCem
 * emulates is wider than 1600 or taller than 1200, and the frame buffer is
 * invisible memory, so the capacity costs nothing in a savestate. */
#define PCEM_VIDEO_MAX_W 1600
#define PCEM_VIDEO_MAX_H 1200

/* The name PCem is told its config lives under. It is never a real file -
 * pcem_driver_fopen answers it from memory. */
#define PCEM_CFG_NAME "machine.cfg"

/* Slot names, as the engine mounts them. */
#define PCEM_SLOT_FLOPPY_A "floppy_a"
#define PCEM_SLOT_FLOPPY_B "floppy_b"
#define PCEM_SLOT_HDD      "hdd"
#define PCEM_SLOT_HDD2     "hdd2"
#define PCEM_SLOT_CDROM    "cdrom"

/* The controller. A PC keyboard alone is over 100 buttons, so this is a wide
 * controller and rides the SetButton channel; the first 64 also arrive packed
 * in FrameAdvance and the two are unioned. The order here IS the button index
 * order in waterbox.config and the two must not drift - tools/gen-config.py
 * generates that list from this table. */
typedef struct {
        const char *name;   /* the Chimera button name */
        int scancode;       /* index into PCem's pcem_key[], an XT scancode */
} pcem_button_t;

extern const pcem_button_t pcem_buttons[];
extern const int PCEM_BTN_COUNT;

/* Axis indices, in waterbox.config order. */
enum {
        PCEM_AXIS_MOUSE_X = 0,
        PCEM_AXIS_MOUSE_Y,
        PCEM_AXIS_MOUSE_POS_X,
        PCEM_AXIS_MOUSE_POS_Y,
        PCEM_AXIS_JOY1_X,
        PCEM_AXIS_JOY1_Y,
        PCEM_AXIS_JOY2_X,
        PCEM_AXIS_JOY2_Y,
        PCEM_AXIS_COUNT
};

/* MOVEMENT TOO BIG FOR ONE PACKET IS KEPT, NOT DROPPED. A mouse reports in
 * bytes: mouse_serial.c clamps a report to -128..127 and mouse_ps2.c clamps its
 * accumulator to -256..255, and whatever is over the edge is simply gone. That
 * is right for a hand on a desk, which cannot move half a screen between two
 * polls, and wrong for a movie that asks the pointer to cross the screen: the
 * jump would arrive truncated and the pointer stop short with nothing said.
 *
 * So each poll takes at most what a report can carry and the rest waits for
 * the next one. pollmouse runs every other millisecond of emulated time
 * (pc.c), so a jump right across a 640-wide screen is delivered in about six
 * polls, well inside one frame, and arrives exactly. The relative Mouse X/Y
 * axis is declared -128..127 and so never reaches this path at all.
 *
 * It lives in the header because it is the one piece of this whose arithmetic
 * can be checked without a machine: tests/test-input.c does exactly that. */
#define PCEM_MOUSE_REPORT_MAX 127
static inline int pcem_take_report(int *pending)
{
        int take = *pending;
        if (take > PCEM_MOUSE_REPORT_MAX) take = PCEM_MOUSE_REPORT_MAX;
        else if (take < -PCEM_MOUSE_REPORT_MAX) take = -PCEM_MOUSE_REPORT_MAX;
        *pending -= take;
        return take;
}

void pcem_driver_set_button(int index, int state);
void pcem_driver_set_packed(int index, int state);
void pcem_driver_set_axis(int index, int value, int *dx, int *dy, int *dz,
                          int screenW, int screenH);
void pcem_driver_apply_input(void);

#endif

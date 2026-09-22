/* The mouse arithmetic, checked without a machine.
 *
 * PCem has no absolute pointing device, so an absolute position is turned into
 * the movement that would reach it and handed to the relative stream. Two
 * things about that can be wrong in ways nothing else here would notice:
 *
 *   * the difference could be taken in WIRE units instead of pixels, which
 *     makes every movement about a hundredfold too large while the machine
 *     still moves and every digest still looks alive;
 *   * a jump bigger than one mouse report could be truncated, so the pointer
 *     stops short of where the movie asked and nothing says so.
 *
 * Both are arithmetic, and arithmetic can be checked directly. The expected
 * values here are worked out from the declaration - 0..65535 across the
 * screen - and not read back out of the code under test.
 *
 * Build: see run-gate.sh, leg "the mouse arithmetic".
 */
#include <stdio.h>
#include <stdlib.h>

#include "../pcem-driver.h"

/* what pcem-input.c needs from PCem, which a test does not have */
unsigned char pcem_key[272];
int mouse_buttons;

static int failures;

static void eq(const char *what, long got, long want)
{
        if (got == want) return;
        printf("  FAIL %s: got %ld, wanted %ld\n", what, got, want);
        failures++;
}

/* Drain the pending movement the way pollmouse does, and report the total that
 * actually reached the device plus how many reports it took. */
static long drain(int *pending, int *reports)
{
        long total = 0;
        int n = 0, took;
        while ((took = pcem_take_report(pending)) != 0) { total += took; n++; }
        if (reports) *reports = n;
        return total;
}

int main(void)
{
        int dx = 0, dy = 0, dz = 0, reports;
        const int W = 640, H = 480;

        /* THE FIRST FRAME PLACES NOTHING. There is no previous position to
         * have moved from, and inventing one would fling the pointer across
         * the screen at the start of every movie. */
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 32768, &dx, &dy, &dz, W, H);
        eq("first frame moves nothing", dx, 0);

        /* A PIXEL IS A PIXEL. 32768 is the middle of a 640-wide screen, so
         * moving to 65535 is 319 pixels right (65535*640/65536 = 639, less the
         * 320 it was at). Differencing the wire instead would say 32767. */
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 65535, &dx, &dy, &dz, W, H);
        eq("half a screen right, in pixels", dx, 319);

        /* and back again */
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 0, &dx, &dy, &dz, W, H);
        eq("all the way left", dx, 319 - 639);

        /* A HELD POSITION IS STILL. */
        dx = 0;
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 0, &dx, &dy, &dz, W, H);
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 0, &dx, &dy, &dz, W, H);
        eq("a held position invents no movement", dx, 0);

        /* THE VERTICAL AXIS USES THE HEIGHT, not the width. Getting this wrong
         * is invisible on a square screen and wrong on every real one. */
        dy = 0;
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_Y, 32768, &dx, &dy, &dz, W, H);
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_Y, 65535, &dx, &dy, &dz, W, H);
        eq("half a screen down, against the height", dy, 479 - 240);

        /* THE SCREEN IS WHATEVER IS BEING DRAWN. The same wire value is a
         * different pixel in a different mode, which is the whole point of
         * carrying a fraction instead of a number. */
        dx = 0;
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 0, &dx, &dy, &dz, 320, 200);
        dx = 0;
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_POS_X, 65535, &dx, &dy, &dz, 320, 200);
        eq("a 320-wide screen is half the pixels", dx, 319);

        /* THE RELATIVE AXIS IS UNTOUCHED: it carries mickeys, not a position. */
        dx = 0;
        pcem_driver_set_axis(PCEM_AXIS_MOUSE_X, -42, &dx, &dy, &dz, W, H);
        eq("a relative axis is passed straight through", dx, -42);

        /* NOTHING IS LOST TO A PACKET BOUNDARY. A mouse reports in bytes, so a
         * jump across the screen cannot fit in one; it must arrive in several
         * and add up to exactly what was asked for. */
        dx = 639;
        eq("a screen-wide jump arrives in full", drain(&dx, &reports), 639);
        eq("and takes more than one report", reports >= 2, 1);
        eq("and leaves nothing pending", dx, 0);

        dx = -639;
        eq("leftwards too", drain(&dx, NULL), -639);

        dx = 7;
        eq("a small move is one report", drain(&dx, &reports), 7);
        eq("exactly one", reports, 1);

        if (failures) { printf("test-input: %d failure(s)\n", failures); return 1; }
        printf("test-input: all checks passed\n");
        return 0;
}

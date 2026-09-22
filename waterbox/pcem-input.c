/* pcem-input.c - the controller, and the single place the button order lives.
 *
 * PCem's keyboard state is pcem_key[272], indexed by XT scancode, with
 * extended keys at 0x80 + the code (keyboard.c:421-438). Every button below
 * names one of those, so the mapping is a table rather than a switch.
 *
 * Two channels reach a core: a packed 64-bit mask in FrameAdvance, and
 * SetButton for a wide controller. A PC keyboard is wide, so both are kept
 * and a frame is their union - neither channel can leave a key stuck down
 * for the other.
 */
#include <string.h>

#include "pcem-driver.h"

extern unsigned char pcem_key[272];
extern int mouse_buttons;

/* The 101/102-key PC keyboard, then the mouse buttons, then the media
 * controls. Scancode 0 means "not a key" - those are handled by index. */
const pcem_button_t pcem_buttons[] = {
        { "Escape",        0x01 },
        { "1",             0x02 }, { "2", 0x03 }, { "3", 0x04 }, { "4", 0x05 },
        { "5",             0x06 }, { "6", 0x07 }, { "7", 0x08 }, { "8", 0x09 },
        { "9",             0x0A }, { "0", 0x0B },
        { "Minus",         0x0C }, { "Equals", 0x0D }, { "Backspace", 0x0E },
        { "Tab",           0x0F },
        { "Q",             0x10 }, { "W", 0x11 }, { "E", 0x12 }, { "R", 0x13 },
        { "T",             0x14 }, { "Y", 0x15 }, { "U", 0x16 }, { "I", 0x17 },
        { "O",             0x18 }, { "P", 0x19 },
        { "Left Bracket",  0x1A }, { "Right Bracket", 0x1B }, { "Enter", 0x1C },
        { "Left Ctrl",     0x1D },
        { "A",             0x1E }, { "S", 0x1F }, { "D", 0x20 }, { "F", 0x21 },
        { "G",             0x22 }, { "H", 0x23 }, { "J", 0x24 }, { "K", 0x25 },
        { "L",             0x26 },
        { "Semicolon",     0x27 }, { "Apostrophe", 0x28 }, { "Backquote", 0x29 },
        { "Left Shift",    0x2A }, { "Backslash", 0x2B },
        { "Z",             0x2C }, { "X", 0x2D }, { "C", 0x2E }, { "V", 0x2F },
        { "B",             0x30 }, { "N", 0x31 }, { "M", 0x32 },
        { "Comma",         0x33 }, { "Period", 0x34 }, { "Slash", 0x35 },
        { "Right Shift",   0x36 }, { "Keypad Multiply", 0x37 },
        { "Left Alt",      0x38 }, { "Space", 0x39 }, { "Caps Lock", 0x3A },
        { "F1",            0x3B }, { "F2", 0x3C }, { "F3", 0x3D }, { "F4", 0x3E },
        { "F5",            0x3F }, { "F6", 0x40 }, { "F7", 0x41 }, { "F8", 0x42 },
        { "F9",            0x43 }, { "F10", 0x44 },
        { "Num Lock",      0x45 }, { "Scroll Lock", 0x46 },
        { "Keypad 7",      0x47 }, { "Keypad 8", 0x48 }, { "Keypad 9", 0x49 },
        { "Keypad Minus",  0x4A },
        { "Keypad 4",      0x4B }, { "Keypad 5", 0x4C }, { "Keypad 6", 0x4D },
        { "Keypad Plus",   0x4E },
        { "Keypad 1",      0x4F }, { "Keypad 2", 0x50 }, { "Keypad 3", 0x51 },
        { "Keypad 0",      0x52 }, { "Keypad Period", 0x53 },
        { "F11",           0x57 }, { "F12", 0x58 },
        /* the extended block: 0x80 + the XT code */
        { "Keypad Enter",  0x9C }, { "Right Ctrl", 0x9D },
        { "Keypad Divide", 0xB5 }, { "Print Screen", 0xB7 }, { "Right Alt", 0xB8 },
        { "Home",          0xC7 }, { "Up", 0xC8 }, { "Page Up", 0xC9 },
        { "Left",          0xCB }, { "Right", 0xCD },
        { "End",           0xCF }, { "Down", 0xD0 }, { "Page Down", 0xD1 },
        { "Insert",        0xD2 }, { "Delete", 0xD3 },
        { "Left Windows",  0xDB }, { "Right Windows", 0xDC }, { "Menu", 0xDD },
        /* not keys */
        { "Mouse Left",    0 },
        { "Mouse Right",   0 },
        { "Mouse Middle",  0 },
        { "Joystick 1 Button 1", 0 },
        { "Joystick 1 Button 2", 0 },
        { "Joystick 2 Button 1", 0 },
        { "Joystick 2 Button 2", 0 },
};

const int PCEM_BTN_COUNT = (int)(sizeof pcem_buttons / sizeof pcem_buttons[0]);

/* index of the first non-key button, computed once */
static int first_non_key(void)
{
        static int idx = -1;
        int i;
        if (idx >= 0) return idx;
        for (i = 0; i < PCEM_BTN_COUNT; i++)
                if (!pcem_buttons[i].scancode) { idx = i; return idx; }
        idx = PCEM_BTN_COUNT;
        return idx;
}

static unsigned char g_set[256];
static unsigned char g_packed[256];

void pcem_driver_set_button(int index, int state)
{
        if (index >= 0 && index < PCEM_BTN_COUNT && index < 256)
                g_set[index] = state ? 1 : 0;
}

void pcem_driver_set_packed(int index, int state)
{
        if (index >= 0 && index < PCEM_BTN_COUNT && index < 256)
                g_packed[index] = state ? 1 : 0;
}

void pcem_driver_apply_input(void)
{
        int i, base = first_non_key();
        int mb = 0;

        /* Build the whole key state first and then assign it. Several
         * buttons can name one scancode, and applying them one at a time
         * lets a later released button cancel an earlier held one. */
        unsigned char want[272];
        memset(want, 0, sizeof want);
        for (i = 0; i < base; i++)
                if (g_set[i] | g_packed[i]) {
                        int sc = pcem_buttons[i].scancode;
                        if (sc > 0 && sc < 272) want[sc] = 1;
                }
        memcpy(pcem_key, want, sizeof want);

        /* mouse buttons: PCem's mouse_buttons is a bitmask, left=1 right=2
         * middle=4 (mouse.c and the mouse_*.c back ends) */
        if (g_set[base + 0] | g_packed[base + 0]) mb |= 1;
        if (g_set[base + 1] | g_packed[base + 1]) mb |= 2;
        if (g_set[base + 2] | g_packed[base + 2]) mb |= 4;
        mouse_buttons = mb;
}

/* Where the position axes last pointed, in PIXELS, or -1 for "not yet". These
 * are ordinary guest memory, so a savestate carries them the way it carries
 * everything else the machine was told. */
static int g_lastPosPxX = -1, g_lastPosPxY = -1;

/* An absolute position, turned into the movement that would reach it.
 *
 * PCem has NO ABSOLUTE POINTING DEVICE to hand a position to. Its whole mouse
 * list (extern/pcem/src/mouse.c) is two serial mice, two PS/2 mice and two
 * machine-integrated ones, every one of them relative, and there is no
 * hypervisor backdoor in the tree to put an absolute one behind - no VMware
 * port, no tablet, nothing. So a position is steered towards, not placed at:
 * the guest's own driver still owns the cursor.
 *
 * That makes it OPEN LOOP, and it is worth being plain about what that costs.
 * If the guest applies pointer acceleration, clamps at a screen edge, or warps
 * the cursor itself, its idea of where the pointer is drifts from this one and
 * nothing here can measure the difference. Placing a pointer for real needs a
 * device PCem does not emulate; this gives targeting by feel, which is what a
 * DOS or Windows guest could offer a person with a real mouse anyway.
 *
 * The difference is taken in PIXELS, not in wire units. One wire unit is about
 * a hundredth of a pixel at 640 wide, so differencing the wire would hand the
 * mouse numbers about a hundredfold too large.
 */
static int position_delta(int value, int screen, int *last)
{
        int px, moved;
        if (screen <= 0) return 0;
        px = (int)(((long long)value * screen) / 65536);
        moved = *last < 0 ? 0 : px - *last;   /* the first frame places nothing */
        *last = px;
        return moved;
}

void pcem_driver_set_axis(int index, int value, int *dx, int *dy, int *dz,
                          int screenW, int screenH)
{
        (void)dz;
        /* The mouse is relative: the axis carries this frame's movement in
         * mickeys, which is what PCem asks for through mouse_get_mickeys. */
        switch (index) {
        case PCEM_AXIS_MOUSE_X: *dx += value; break;
        case PCEM_AXIS_MOUSE_Y: *dy += value; break;
        case PCEM_AXIS_MOUSE_POS_X: *dx += position_delta(value, screenW, &g_lastPosPxX); break;
        case PCEM_AXIS_MOUSE_POS_Y: *dy += position_delta(value, screenH, &g_lastPosPxY); break;
        default: break;
        }
}

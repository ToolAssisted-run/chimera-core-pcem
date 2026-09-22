#!/usr/bin/env python3
"""The axis wire, checked against the driver that reads it.

Two things can go wrong silently between waterbox.config and pcem-driver.h,
and neither shows up as a crash:

  * the ORDER can drift. SetAxis is given an index, and the driver turns that
    index into a meaning through PCEM_AXIS_*. Insert an axis in the config and
    forget the enum and every axis after it means something else - the mouse
    becomes a joystick and nothing refuses to run.
  * an absolute screen position can be declared in the wrong units. Chimera's
    rule (chimera docs/porting-a-core.md) is that a point on the guest's
    screen is 0..65535 with neutral 32768 on every core, because no declared
    number can be a screen a PC changes whenever it likes.

usage: check-axes.py <waterbox.config> <pcem-driver.h>
"""
import json
import re
import sys

ABS_MIN, ABS_MAX, ABS_NEUTRAL = 0, 65535, 32768
# a name that means "a point on the screen" rather than "how far it moved"
ABSOLUTE_WORDS = ("position", "gun", "touch", "scope", "justifier", "pointer")


def enum_order(header_text):
    body = re.search(r"enum\s*\{(.*?)\}", header_text, re.S)
    if body is None:
        raise SystemExit("no axis enum in the header")
    names = []
    for line in body.group(1).splitlines():
        m = re.match(r"\s*(PCEM_AXIS_[A-Z0-9_]+)", line)
        if m and not m.group(1).endswith("_COUNT"):
            names.append(m.group(1))
    return names


def enum_name_for(axis_name):
    """'Mouse Position X' -> PCEM_AXIS_MOUSE_POS_X, the driver's spelling.

    The driver abbreviates the way C does: POSITION is POS, JOYSTICK is JOY,
    and a number sticks to the word before it (Joystick 1 X is JOY1_X)."""
    s = axis_name.upper().replace("POSITION", "POS").replace("JOYSTICK", "JOY")
    s = re.sub(r"[^A-Z0-9]+", "_", s).strip("_")
    return "PCEM_AXIS_" + re.sub(r"_(\d)", r"\1", s)


def main():
    cfg = json.loads(open(sys.argv[1]).read())
    axes = cfg.get("input", {}).get("axes", [])
    order = enum_order(open(sys.argv[2]).read())
    problems = []

    if len(axes) != len(order):
        problems.append(f"{len(axes)} axes declared but {len(order)} in the enum")
    for i, axis in enumerate(axes):
        want = enum_name_for(axis["name"])
        got = order[i] if i < len(order) else "(missing)"
        if want != got:
            problems.append(f"axis {i} is {axis['name']!r}, which reads as "
                            f"{want}, but the enum has {got}")
        if any(w in axis["name"].lower() for w in ABSOLUTE_WORDS):
            if (axis["min"], axis["max"], axis["neutral"]) != (ABS_MIN, ABS_MAX, ABS_NEUTRAL):
                problems.append(
                    f"{axis['name']!r} names a point on the screen but is declared "
                    f"{axis['min']}..{axis['max']} neutral {axis['neutral']}; "
                    f"an absolute position is {ABS_MIN}..{ABS_MAX} neutral {ABS_NEUTRAL}")

    for p in problems:
        print(f"  {p}")
    print(f"{len(axes)} axes, {len(order)} enum entries: {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

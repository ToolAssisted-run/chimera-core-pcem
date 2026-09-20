#!/usr/bin/env python3
"""Generate the firmware[] block of waterbox.config from the enumeration in
docs/PLAN.md sections 5.1-5.4, which was derived from mem_bios.c's
switch (romset) joined to MODEL models[] rather than from PCem's README - the
README disagrees with the code in at least four places (PLAN.md section 5).

Two things this has to get right:

 * The id carries the DIRECTORY, not just the basename. Flattening to the
   basename collides: bios.bin alone is claimed by ati28800, mach64gx/,
   oti037/ and oti067/, and ami.bin, award.bin, phoenix.bin, gd5434.bin and
   1006bs0_.bio each have two claimants.
 * `name` is the flat mount name the driver asks for, which is the path with
   '/' turned into '_' (pcem_driver_fopen).

usage: gen-firmware.py <docs/PLAN.md> <out firmware.json>
"""
import json
import re
import sys
from pathlib import Path

# SHA1 and size for every ROM, keyed by firmware id, in tools/firmware-sha1.json
# beside this script. It is an INPUT to generation, not something written into
# the generated file by hand, so the declaration can always be rebuilt.
#
# This matters more than it looks. Chimera's Scan Folder is hash-first -
# FirmwareLocator.FindFor matches on SHA1 when the declaration has one and
# only falls back to the name - so a declaration without hashes resolves
# NOTHING against a folder full of correct ROMs, which is exactly what
# happened. The size is declared too: it is a cheap first filter before a
# candidate is hashed at all.
def load_hashes():
    path = Path(__file__).resolve().parent / "firmware-sha1.json"
    if not path.exists():
        print("WARNING: no firmware-sha1.json; entries will carry no hash",
              file=sys.stderr)
        return {}
    return json.loads(path.read_text())


HASHES = load_hashes()


def rows(text, header):
    """Rows of the markdown table that follows `header`."""
    i = text.index(header)
    out = []
    for line in text[i:].splitlines()[1:]:
        if line.startswith("|---") or not line.strip():
            if out:
                break
            continue
        if not line.startswith("|"):
            if out:
                break
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) >= 3 and not cells[0].startswith("---"):
            out.append(cells)
    return out


def files_of(cell):
    """A cell may hold several files separated by <br>, and may mark some
    optional. Optional ones are dropped: a firmware entry the wizard demands
    must be one the machine cannot start without."""
    parts = [p.strip() for p in cell.split("<br>")]
    out = []
    for p in parts:
        # PLAN.md cites the source file for a few rows, e.g.
        #   awe32.raw (`src/sound_emu8k.c`, `src/sound_sb.c`)
        # and that reference leaked into the firmware id, which made the entry
        # unmatchable no matter how good its hash was. A parenthetical
        # containing a backtick is a citation, never part of a filename.
        cut = p.find(" (`")
        if cut >= 0:
            p = p[:cut].strip()
        if p.startswith("(optional)") or p.lower().startswith("none"):
            continue
        p = p.strip("`")
        if p and "/" not in p and " " in p and not p.endswith((".bin", ".rom", ".raw", ".vbi", ".BIN", ".ROM")):
            continue
        out.append(p)
    return out


def entry(path, display, description, cond):
    # The ID IS THE MOUNT NAME: session.cpp:1186-1191 mounts each firmware
    # under its declared id, and the guest fopen()s exactly that. So the id is
    # the flattened path the driver asks for, not a prettier label.
    flat = path.replace("/", "_")
    e = {
        "id": flat,
        "display": display,
        "description": description,
        # id is the MOUNT name (what the guest fopen()s, flattened because the
        # sandbox VFS has no directories). name is the hint the engine's
        # name-matching fallback compares against a candidate file, so it has
        # to be the real basename - "6BX.F2a", not "ga686bx_6BX.F2a", which
        # matches nothing on anyone's disk.
        "name": path.rsplit("/", 1)[-1],
        "label": path,
    }
    h = HASHES.get(flat)
    if h:
        e["sha1"] = h["sha1"].upper()
        e["size"] = h["size"]
    e["requiredWhen"] = cond
    return e


def main():
    plan = Path(sys.argv[1]).read_text()
    out_path = Path(sys.argv[2])
    entries = {}

    def add(path, display, desc, setting, value):
        if path in entries:
            cond = entries[path]["requiredWhen"]
            # another claimant of the same file: widen the condition
            if cond.get("setting") == setting:
                cond["in"] = sorted(set(cond["in"]) | {value})
            else:
                entries[path]["requiredWhen"] = {"any": [cond, {"setting": setting, "in": [value]}]}
            return
        entries[path] = entry(path, display, desc,
                              {"setting": setting, "in": [value]})

    for cells in rows(plan, "| Machine (PCem's own name) | internal |"):
        display, internal, files = cells[0], cells[1].strip("`"), cells[2]
        for f in files_of(files):
            add(f, f"{display} ROM ({f.rsplit('/', 1)[-1]})",
                f"A ROM of the {display}, which you supply. PCem ships no ROMs.",
                "machine", internal)

    for cells in rows(plan, "| Video card | internal | ROM file(s) |"):
        display, internal, files = cells[0], cells[1].strip("`"), cells[2]
        for f in files_of(files):
            if f.startswith("(character generator"):
                continue
            add(f, f"{display} video BIOS ({f.rsplit('/', 1)[-1]})",
                f"The video BIOS of the {display}, which you supply.",
                "videoCard", internal)

    for cells in rows(plan, "| Device | internal | ROM file(s) |"):
        display, internal, files = cells[0], cells[1].strip("`"), cells[2]
        setting = "soundCard" if internal in ("sbawe32",) else "hddController"
        for f in files_of(files):
            add(f, f"{display} ROM ({f.rsplit('/', 1)[-1]})",
                f"The ROM of the {display}, which you supply.",
                setting, internal)

    out = sorted(entries.values(), key=lambda e: e["id"])
    out_path.write_text(json.dumps(out, indent=2))
    print(f"{len(out)} firmware entries -> {out_path}")
    dupes = {}
    for e in out:
        dupes.setdefault(e["name"].rsplit("_", 1)[-1], []).append(e["id"])
    collided = {k: v for k, v in dupes.items() if len(v) > 1}
    print(f"basenames with more than one claimant (why the id carries the "
          f"directory): {len(collided)}")


if __name__ == "__main__":
    main()

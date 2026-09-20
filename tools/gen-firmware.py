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

# The ten hashes the sources publish (PLAN.md 5.5). Everything else is
# declared without a hash, which the engine allows for a file no one can pin,
# and which M2 should replace with hashes from a known-good v17 set.
KNOWN_SHA1 = {
    "ga686bx/6BX.F2a": "637E1B3863694FFD15A40585FD563329BE3873D4",
    "voodoo3_3000/3k12sd.rom": "2825B702633553C7A7A3DAEA98B56F67BD016030",
    "awe32.raw": "6AC3C1317C1ACB83902397D7767763CCA4DE357A",
}


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
        "name": flat,
        "label": path,
    }
    if path in KNOWN_SHA1:
        e["sha1"] = KNOWN_SHA1[path]
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

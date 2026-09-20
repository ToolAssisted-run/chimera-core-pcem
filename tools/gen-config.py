#!/usr/bin/env python3
"""Generate waterbox.config's machine, card and firmware tables from PCem's
own source, so they cannot drift from the emulator.

Everything selectable in PCem is selectable here: 93 machines, 47 video cards,
every sound card, every hard-disk controller, and the per-machine CPU lists.
The TASVideos configurations are presets over the same surface, not a
different code path.

usage: gen-config.py <pcem/src> <out waterbox.config>
"""
import json
import re
import sys
from pathlib import Path


def read(src, name):
    return (src / name).read_text(errors="replace")


def table(text, marker, fields=3):
    """Entries of the form {"Display name", "internal", ...}."""
    i = text.index(marker)
    depth, j = 0, text.index("{", i)
    start = j
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    body = text[start:j]
    out = []
    for m in re.finditer(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"', body):
        out.append((m.group(1), m.group(2)))
    return out


def machines(src):
    """MODEL models[] in model.c: {"display", ROM_X, "internal", {cpus...},
    flags, min_ram, max_ram, granularity, init, device}."""
    text = read(src, "model.c")
    i = text.index("MODEL models[]")
    body = text[i:]
    out = []
    for m in re.finditer(
        r'\{\s*"([^"]+)"\s*,\s*(ROM_\w+)\s*,\s*"([^"]+)"\s*,\s*\{(.*?)\}\s*,\s*([^,]+),'
        r'\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,',
        body, re.S):
        display, romset, internal, cpublock, flags, mn, mx, gran = m.groups()
        cpus = re.findall(r'\{\s*"([^"]+)"\s*,\s*(\w+)\s*\}', cpublock)
        out.append({
            "display": display,
            "internal": internal,
            "romset": romset,
            "flags": flags.strip(),
            "min_ram": int(mn),
            "max_ram": int(mx),
            "ram_granularity": int(gran),
            "cpu_makers": [c[0] for c in cpus],
            "cpu_tables": [c[1] for c in cpus],
        })
    return out


def cpu_tables(src):
    """CPU cpus_X[] in cpu_tables.c -> {table name: [cpu display names]}."""
    text = read(src, "cpu_tables.c")
    out = {}
    for m in re.finditer(r'CPU\s+(cpus_\w+)\[\]\s*=\s*\{(.*?)\n\};', text, re.S):
        name, body = m.groups()
        out[name] = [x for x in re.findall(r'\{\s*"([^"]+)"', body) if x]
    return out


def named_list(src, files, marker_re):
    """Names out of a list of device structs, in the order the index list
    declares them - so the option text and PCem's index cannot drift."""
    import re as _re
    out = []
    for f in files:
        text = read(src, f)
        for m in _re.finditer(marker_re, text, _re.S):
            out.append(m.group(1))
    return out


def ordered_devices(src, listing_file, listing_marker, decl_files, decl_re):
    """The index order comes from the &foo, &bar list; the display name comes
    from each struct's first string. Joining them keeps the option list in
    PCem's own index order."""
    import re as _re
    text = read(src, listing_file)
    i = text.index(listing_marker)
    body = text[i:text.index("};", i)]
    order = _re.findall(r'&(\w+)', body)
    names = {}
    for f in decl_files:
        t = read(src, f)
        for m in _re.finditer(decl_re, t, _re.S):
            names[m.group(1)] = m.group(2)
    return [names[o] for o in order if o in names]


# The floppy drive list is a fixed set of eight in PCem's own settings dialog
# (wx-config.c:838-846); fdd.c's table carries no display names.
FDD_TYPES = ['None', '5.25" 360k', '5.25" 1.2M', '5.25" 1.2M Dual RPM',
             '3.5" 720k', '3.5" 1.44M', '3.5" 1.44M 3-Mode', '3.5" 2.88M']

# video_speed, -1 plus the five named bus speeds PCem offers.
VIDEO_SPEEDS = ["default", "8-bit 8MHz", "16-bit 8MHz", "16-bit 12MHz",
                "16-bit 16MHz", "Fast VLB/PCI"]


def ram_bounds(machine):
    """PCem states min_ram/max_ram in MB for an AT-class machine with a
    granularity under 128, and in KB otherwise (pc.c:748-749,
    wx-config.c:521-526). The .cfg always carries KB."""
    at = "MODEL_AT" in machine["flags"]
    if at and machine["ram_granularity"] < 128:
        return machine["min_ram"] * 1024, machine["max_ram"] * 1024, machine["ram_granularity"] * 1024
    return machine["min_ram"], machine["max_ram"], machine["ram_granularity"]


def main():
    src = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    ms = machines(src)
    vids = table(read(src, "video.c"), "VIDEO_CARD video_cards[]")
    snds = table(read(src, "sound.c"), "SOUND_CARD sound_cards[]")
    hdds = table(read(src, "hdd.c"), "hdd_controllers[]")
    cpus = cpu_tables(src)

    import glob as _glob, os as _os
    # a couple of mice are declared in their machine's own file
    mouse_files = [_os.path.basename(x) for x in
                   sorted(_glob.glob(str(src / "mouse_*.c")))] + \
                  ["amstrad.c", "olivetti_m24.c", "keyboard_olim24.c"]
    joy_files = [_os.path.basename(x) for x in
                 sorted(_glob.glob(str(src / "joystick_*.c")))]
    mice = ordered_devices(src, "mouse.c", "mouse_t *mouse_list[]",
                           mouse_files, r'mouse_t\s+(\w+)\s*=\s*\{\s*(?:\.name\s*=\s*)?"([^"]+)"')
    joys = ordered_devices(src, "gameport.c", "joystick_if_t *joystick_list[]",
                           joy_files, r'joystick_if_t\s+(\w+)\s*=\s*\{\s*(?:\.name\s*=\s*)?"([^"]+)"')

    # Every distinct CPU name across every table, in first-seen order. The
    # user picks a NAME; the driver finds which of the chosen machine's
    # manufacturer tables holds it, so there is no index to type and no
    # manufacturer to pick separately.
    seen, all_cpus = set(), []
    for tbl in cpus.values():
        for n in tbl:
            if n not in seen:
                seen.add(n); all_cpus.append(n)

    data = {
        "machines": ms,
        "video_cards": [{"display": d, "internal": i} for d, i in vids],
        "sound_cards": [{"display": d, "internal": i} for d, i in snds],
        "hdd_controllers": [{"display": d, "internal": i} for d, i in hdds],
        "cpu_tables": cpus,
        "ram_bounds": {m["internal"]: ram_bounds(m) for m in ms},
        "mice": mice,
        "joysticks": joys,
        "fdd_types": FDD_TYPES,
        "video_speeds": VIDEO_SPEEDS,
        "all_cpus": all_cpus,
    }
    out_path.write_text(json.dumps(data, indent=1))
    print(f"machines={len(ms)} video={len(vids)} sound={len(snds)} "
          f"hdd={len(hdds)} cpu_tables={len(cpus)} cpus={len(all_cpus)} "
          f"mice={len(mice)} joysticks={len(joys)} -> {out_path}")


if __name__ == "__main__":
    main()

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

    data = {
        "machines": ms,
        "video_cards": [{"display": d, "internal": i} for d, i in vids],
        "sound_cards": [{"display": d, "internal": i} for d, i in snds],
        "hdd_controllers": [{"display": d, "internal": i} for d, i in hdds],
        "cpu_tables": cpus,
        "ram_bounds": {m["internal"]: ram_bounds(m) for m in ms},
    }
    out_path.write_text(json.dumps(data, indent=1))
    print(f"machines={len(ms)} video={len(vids)} sound={len(snds)} "
          f"hdd={len(hdds)} cpu_tables={len(cpus)} -> {out_path}")


if __name__ == "__main__":
    main()

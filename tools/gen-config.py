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


def device_configs(src):
    """Every device_config_t table PCem has, collapsed to key -> the labels
    that key offers anywhere.

    PCem's per-device settings do not live in the .cfg's global section: they
    live in a [device name] section and are read back through
    device_get_config_int, which matches the key against THAT device's own
    table (device.c:120-133). So a Chimera setting for one of them is a
    setting whose value is a LABEL - "0x220", "NukedOPL", "4 MB" - which the
    driver resolves against whichever device is actually fitted and writes as
    that device's own number. The label is the only thing every card that has
    the key agrees on; the number behind it is not (a video card's "memory" is
    in MB on an S3 and in kB on an AVGA2), and neither is the set of legal
    values (an SB Pro v2 takes two addresses and an SB16 four).

    Collecting the union here means the declared option list comes from PCem's
    own tables rather than a list somebody typed, exactly as the machine and
    card lists do."""
    import glob as _glob, os as _os
    keys = {}
    for path in sorted(_glob.glob(str(src / "*.c"))):
        # Only the files the guest is actually BUILT from (the same drop list
        # as waterbox/build-guest.sh). Without this, ne2000's five base
        # addresses join the address list and the settings page offers five
        # choices for a card that is not in the binary - an option a user can
        # pick and the core cannot honour, which is the exact shape of thing
        # gates.md calls a check that cannot fail.
        base = _os.path.basename(path)
        if base.startswith("wx-") and base != "wx-thread.c":
            continue
        if base in ("soundopenal.c", "midi_alsa.c", "ne2000.c", "nethandler.c",
                    "cdrom-ioctl.c", "cdrom-ioctl-linux.c", "cdrom-ioctl-osx.c",
                    "hdd_file.c"):
            continue
        text = Path(path).read_text(errors="replace")
        for m in re.finditer(r'device_config_t\s+(\w+)\[\]\s*=\s*\{', text):
            i, depth = m.end() - 1, 0
            j = i
            while True:
                if text[j] == "{":
                    depth += 1
                elif text[j] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            body = text[i:j]
            # Each entry starts at its .name; everything up to the next .name
            # is that entry's own fields, including its selection[] labels.
            for e in re.finditer(r'\.name\s*=\s*"([^"]+)"(.*?)(?=\.name\s*=\s*"|\Z)',
                                 body, re.S):
                key, rest = e.group(1), e.group(2)
                ty = re.search(r'\.type\s*=\s*(\w+)', rest)
                d = keys.setdefault(key, {"types": [], "labels": [], "sources": {}})
                if ty and ty.group(1) not in d["types"]:
                    d["types"].append(ty.group(1))
                # The FIRST .description is the entry's own label; the rest
                # are its selection[] entries. An empty one is the terminator.
                descs = re.findall(r'\.description\s*=\s*"([^"]*)"', rest)
                mine = d["sources"].setdefault(base, [])
                for label in descs[1:]:
                    if not label:
                        continue
                    if not any(label.lower() == x.lower() for x in d["labels"]):
                        d["labels"].append(label)
                    if not any(label.lower() == x.lower() for x in mine):
                        mine.append(label)
    for d in keys.values():
        d["labels"] = order_labels(d["labels"])
        d["sources"] = {f: order_labels(v) for f, v in d["sources"].items()}
    return keys


def order_labels(labels):
    return [x for _, x in sorted(enumerate(labels),
                                 key=lambda p: label_order(p[1], p[0]))]


# Which PCem source files a Chimera setting's option list may draw on. A
# setting says "the sound card's address", so the addresses it offers are the
# ones SOUND cards have - the AHA-1542C also has a key called "addr" and it is
# a BIOS window, not a sound port, and offering its six values under a sound
# setting would be four kinds of wrong at once.
DEVICE_GROUPS = {
    "sound":  lambda f: f.startswith("sound_"),
    "video":  lambda f: f.startswith("vid_"),
    # The add-in Voodoo Graphics / Voodoo 2 card, which is a device of its own
    # alongside the 2D card (vid_voodoo.c's voodoo_device).
    "voodoo": lambda f: f == "vid_voodoo.c",
}


def device_config_groups(keys):
    """key -> option list, per group, ready to be an enum's options.

    A CONFIG_BINARY key has no selection[] at all, so its two states are named
    here once - "Off"/"On" - rather than in each setting."""
    out = {}
    for group, want in DEVICE_GROUPS.items():
        g = {}
        for key, d in keys.items():
            labels, here = [], False
            for f, ls in d["sources"].items():
                if not want(f):
                    continue
                here = True
                for x in ls:
                    if not any(x.lower() == y.lower() for y in labels):
                        labels.append(x)
            if not here:
                continue
            if not labels and "CONFIG_SELECTION" in d["types"]:
                # A selection with no labels is a PARSE failure wearing the
                # costume of a binary switch, and it would ship as a two-option
                # setting for a key with eight real values. Loud, not silent.
                raise SystemExit(f"device_configs: {key!r} in group {group!r} "
                                 f"is CONFIG_SELECTION somewhere but parsed no "
                                 f"labels here - the parser is wrong")
            g[key] = order_labels(labels) if labels else ["Off", "On"]
        out[group] = g
    return out


_UNITS = {"b": 1, "kb": 1024, "mb": 1024 * 1024, "gb": 1024 * 1024 * 1024}


def label_order(label, seq):
    """Sort a device-config label the way a person reads it: "None" first, then
    anything that is a number - a size, an address, an IRQ or DMA line - in
    numeric order, then everything else in PCem's own declaration order.

    The two halves matter for different reasons. Without the numeric half, "16
    MB" sorts before "2 MB" and a memory list reads as noise. Without keeping
    declaration order for the rest, the Voodoo type list comes out Obsidian,
    Voodoo 2, Voodoo Graphics - alphabetical, and in no sense the order anybody
    would look for those three cards in."""
    text = label.strip()
    if text.lower() in ("none", "disabled"):
        return (-1, 0)
    m = re.fullmatch(r'(\d+)\s*([kKmMgG]?[bB])', text)
    if m:
        return (0, int(m.group(1)) * _UNITS[m.group(2).lower()])
    m = re.fullmatch(r'0x([0-9a-fA-F]+)', text)
    if m:
        return (0, int(m.group(1), 16))
    m = re.fullmatch(r'(?:IRQ|DMA|ID)\s*(\d+)', text)
    if m:
        return (0, int(m.group(1)))
    if re.fullmatch(r'\d+', text):
        return (0, int(text))
    return (1, seq)


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

    dcs = device_configs(src)
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
        "device_configs": dcs,
        "device_config_groups": device_config_groups(dcs),
    }
    out_path.write_text(json.dumps(data, indent=1))
    print(f"machines={len(ms)} video={len(vids)} sound={len(snds)} "
          f"hdd={len(hdds)} cpu_tables={len(cpus)} cpus={len(all_cpus)} "
          f"mice={len(mice)} joysticks={len(joys)} "
          f"device_keys={len(data['device_configs'])} -> {out_path}")


if __name__ == "__main__":
    main()

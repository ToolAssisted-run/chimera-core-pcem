#!/usr/bin/env python3
"""Every declared preset, resolved and actually run: do its values reach the
machine, and does the machine come up?

Two questions, because they fail separately and the first one fails QUIETLY.

  REACHED. A preset's per-device values - the Sound Blaster's port, the OPL
  implementation, a Voodoo's render threads - are written into [device name]
  sections of PCem's .cfg, and a value the driver drops for any reason simply
  leaves PCem's own default in place. The machine still boots. The picture
  still looks right. Every digest still matches, because the default is what
  the core did before these settings existed. So a boot leg on its own is a
  leg that cannot fail for the thing most likely to break, which is gates.md B
  exactly. GetComposedConfig is what PCem was handed, so that is what is read.

  BOOTS. The machine has to reach a BIOS screen. The liveness test is not a
  stable digest - a machine stuck at frame 1 is perfectly deterministic and
  this project has been caught by that three times - so the picture has to
  CHANGE across the run as well as be drawn at the end.

Note what is deliberately NOT asserted: that a preset boots to a DOS prompt.
Two of the five stop at a POST prompt waiting for a key, because of the CMOS
gap docs/CMOS.md names ("only the IBM AT was tested"); that is a property of
those machines in this core and not of the presets, and docs/PRESETS.md says
so with the measurement that shows it.

usage: check-preset-boots.py <waterbox.config> <run-wbx> <core.wbx> <romdir>
                             <workdir> [frames]
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Only the renderer. `machine` is NOT skipped by the wizard for this package -
# ApplySelectedPreset skips `_cfg.MachineSetting`, the declared machine chooser
# that goes with a machines[] array, and this package declares neither.
SKIPPED_BY_THE_WIZARD = ("renderer",)

# PCem's video_speed is an index, -1 for "leave it to the card".
VIDEO_SPEEDS = ["default", "8-bit 8MHz", "16-bit 8MHz", "16-bit 12MHz",
                "16-bit 16MHz", "Fast VLB/PCI"]

# A setting whose value lands in a GLOBAL .cfg key, and how. Only the ones
# whose expected text can be computed here exactly; a setting resolved against
# a PCem table the driver owns (the CPU, the mouse) is left to the driver,
# because re-deriving it here would be a stand-in that can drift from the
# subject (gates.md E).
BARE = lambda v: str(v).split(" - ", 1)[0]
GLOBAL_KEYS = {
    # The board the preset builds, and the first thing to check: it decides the
    # BIOS, and therefore the firmware the project asks for and every timing in
    # the machine.
    "machine":       ("model", BARE),
    "fpu":           ("fpu", str),
    "dynarec":       ("cpu_use_dynarec", lambda v: "1" if v else "0"),
    "cpuWaitStates": ("cpu_waitstates", str),
    "memSizeKB":     ("mem_size", str),
    "videoCard":     ("gfxcard", BARE),
    "videoSpeed":    ("video_speed", lambda v: str(VIDEO_SPEEDS.index(v) - 1)),
    "voodoo":        ("voodoo", lambda v: "1" if v else "0"),
    "soundCard":     ("sndcard", BARE),
    "gameBlaster":   ("gameblaster", lambda v: "1" if v else "0"),
    "gus":           ("gus", lambda v: "1" if v else "0"),
    "ssi2001":       ("ssi2001", lambda v: "1" if v else "0"),
    "hddController": ("hdd_controller", BARE),
    "lpt1Device":    ("lpt1_device", str),
    "bpbDisable":    ("bpb_disable", lambda v: "1" if v else "0"),
}

# A setting whose value lands in a [device name] section, under PCem's own key
# name. The NUMBER is the fitted card's, so it is not predicted here - only
# that the key was written at all, which is the difference between the setting
# working and the setting being decorative.
DEVICE_KEYS = {
    "soundCardAddress": "addr",
    "soundCardIrq": "irq",
    "soundCardDma": "dma",
    "oplEmulator": "opl_emu",
    "awe32EmuAddress": "emu_addr",
    "awe32OnboardRam": "onboard_ram",
    "videoMemory": "memory",
    "videoBilinear": "bilinear",
    "videoScreenFilter": "dacfilter",
    "videoRenderThreads": "render_threads",
    "videoRecompiler": "recompiler",
    "voodooType": "type",
    "voodooFramebufferMemory": "framebuffer_memory",
    "voodooTextureMemory": "texture_memory",
    "voodooBilinear": "bilinear",
    "voodooScreenFilter": "dacfilter",
    "voodooRenderThreads": "render_threads",
    "voodooSli": "sli",
    "voodooRecompiler": "recompiler",
}

# Settings whose effect on the .cfg depends on things a run without content
# does not have (a disc in the slot, a disk image to derive geometry from).
# Named here so that "not checked" is a decision and not an oversight.
NOT_CHECKED_WITHOUT_CONTENT = {
    "cdDrive", "cdChannel", "cdSpeed", "cdModel",
    "hddGeometry", "hddSectors", "hddHeads", "hddCylinders",
    "hdd2Geometry", "hdd2Sectors", "hdd2Heads", "hdd2Cylinders",
    "driveAType", "driveBType",
    # resolved against a PCem table inside the driver, by name
    "cpu", "mouseType", "joystickType",
}


def resolve(cfg, preset):
    out = {s["name"]: s["default"] for s in cfg["settings"]
           if s.get("name") and "default" in s}
    out.update({k: v for k, v in preset["values"].items()
                if k not in SKIPPED_BY_THE_WIZARD})
    # The board comes from the preset's own values, like everything else -
    # nothing here supplies one, so a preset that does not name a board runs on
    # the declared default and check-presets.py fails it.
    return out


def needed_firmware(cfg, settings):
    def ev(c):
        if not isinstance(c, dict):
            return False
        if "all" in c:
            return all(ev(x) for x in c["all"])
        if "any" in c:
            return any(ev(x) for x in c["any"])
        if "not" in c:
            return not ev(c["not"])
        if "slot" in c:
            return False            # nothing is in a slot for this check
        if "setting" in c:
            got = settings.get(c["setting"])
            if "is" in c:
                return str(got) == str(c["is"])
            if "in" in c:
                return any(str(got) == str(v) for v in c["in"])
        return False
    return [e for e in cfg["firmware"]
            if "requiredWhen" in e and ev(e["requiredWhen"])]


def sha_index(root):
    out = {}
    for p in Path(root).rglob("*"):
        if p.is_file() and p.stat().st_size <= 8 * 1024 * 1024:
            out.setdefault(hashlib.sha1(p.read_bytes()).hexdigest().upper(),
                           []).append(p)
    return out


def lit_pixels(path):
    with open(path, "rb") as f:
        f.readline()
        f.readline()
        f.readline()
        d = f.read()
    return sum(1 for i in range(0, len(d), 3) if d[i] | d[i + 1] | d[i + 2])


def sections(cfg_text):
    """The composed .cfg split into ("" for the globals, then each [section])."""
    out, cur = {"": []}, ""
    for line in cfg_text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            cur = line[1:-1]
            out.setdefault(cur, [])
        elif line:
            out[cur].append(line)
    return out


def main():
    cfg = json.loads(Path(sys.argv[1]).read_text())
    runner, core, romdir, work = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
    frames = int(sys.argv[6]) if len(sys.argv) > 6 else 9000

    if not Path(romdir).is_dir():
        print(f"SKIP: no ROM collection at {romdir}")
        return 0
    by_sha = sha_index(romdir)

    bad, ran, skipped = [], 0, 0
    for preset in cfg.get("presets") or []:
        pid = preset["id"]
        settings = resolve(cfg, preset)
        d = Path(work) / pid
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

        missing = []
        for e in needed_firmware(cfg, settings):
            hits = by_sha.get((e.get("sha1") or "").upper())
            if hits:
                shutil.copy(hits[0], d / e["id"])
            else:
                missing.append(e["id"])
        if missing:
            print(f"  SKIP {pid}: this collection has no {', '.join(missing)}")
            skipped += 1
            continue

        (d / "settings").write_text(json.dumps(settings))
        (d / "slots").write_text("{}")

        # --- did the values REACH the machine?
        out = subprocess.run([runner, core, str(d), "2", "--dump-cfg"],
                             capture_output=True, text=True).stdout
        m = re.search(r"---- composed \.cfg ----\n(.*?)-----------------------",
                      out, re.S)
        if not m:
            bad.append(f"{pid}: the core did not compose a .cfg at all")
            continue
        secs = sections(m.group(1))
        glob = secs[""]
        device_lines = [l for name, ls in secs.items() if name for l in ls]

        for key, value in preset["values"].items():
            if key in SKIPPED_BY_THE_WIZARD or key in NOT_CHECKED_WITHOUT_CONTENT:
                continue
            if key in GLOBAL_KEYS:
                cfgkey, fmt = GLOBAL_KEYS[key]
                want = f"{cfgkey} = {fmt(value)}"
                if want not in glob:
                    got = next((l for l in glob if l.startswith(cfgkey + " = ")),
                               "<the key is not there at all>")
                    bad.append(f"{pid}: {key}={value!r} should have composed "
                               f"{want!r}; the machine got {got!r}")
            elif key in DEVICE_KEYS:
                if value == "Card default":
                    continue
                pcemkey = DEVICE_KEYS[key]
                if not any(l.startswith(pcemkey + " = ") for l in device_lines):
                    bad.append(f"{pid}: {key}={value!r} wrote no {pcemkey!r} into "
                               f"any device section - the card kept its default "
                               f"and the setting did nothing")
            else:
                bad.append(f"{pid}: nothing here knows what {key!r} should do to "
                           f"the .cfg; add it to GLOBAL_KEYS, DEVICE_KEYS or "
                           f"NOT_CHECKED_WITHOUT_CONTENT")

        # --- does it BOOT?
        shot = d / "end.ppm"
        r = subprocess.run([runner, core, str(d), str(frames),
                            "--shot", f"{frames - 1}={shot}",
                            "--digests", "--every", str(max(1, frames // 12))],
                           capture_output=True, text=True)
        digests = re.findall(r"^frame\s+\d+ digest=(\w+)", r.stdout, re.M)
        if "Load error" in r.stdout or "Load error" in r.stderr:
            bad.append(f"{pid}: the core refused to load it")
            continue
        if not shot.exists():
            bad.append(f"{pid}: {frames} frames and no picture at all")
            continue
        lit = lit_pixels(shot)
        moved = len(set(digests)) > 1
        ok = lit > 1000 and moved
        print(f"  {'ok  ' if ok else 'BAD '} {pid}: {lit} lit pixels at frame "
              f"{frames - 1}, {len(set(digests))} distinct frames of "
              f"{len(digests)}")
        if not moved:
            bad.append(f"{pid}: the picture never changed over {frames} frames - "
                       f"the machine is stuck, and a stuck machine is perfectly "
                       f"deterministic")
        elif lit <= 1000:
            bad.append(f"{pid}: only {lit} lit pixels - it never reached a BIOS "
                       f"screen")
        ran += 1

    for b in bad:
        print("  BAD:", b)
    print(f"{ran} presets run, {skipped} skipped for want of firmware: "
          f"{len(bad)} problems")
    if ran == 0:
        print("NOTHING RAN")
        return 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Assemble waterbox/waterbox.config from PCem's own tables.

The machine is general: every machine PCem has, every video and sound card,
every hard-disk controller, and every CPU of the chosen machine. The TASVideos
configurations are presets over that surface - entries in machines[] whose
settingOverrides narrow the defaults - not a different code path, and
"custom" leaves everything open.

usage: gen-waterbox-config.py <build/gen/tables.json> <build/gen/firmware.json>
                              <waterbox/waterbox.config>
"""
import json
import sys
from pathlib import Path

def buttons_and_axes(src_header):
    """The button order comes from waterbox/pcem-input.c, which is the single
    place it lives; this parses it so the two cannot drift."""
    text = Path(src_header).read_text()
    i = text.index("const pcem_button_t pcem_buttons[]")
    j = text.index("};", i)
    import re
    names = re.findall(r'\{\s*"([^"]+)"\s*,', text[i:j])
    return names


def main():
    tables = json.loads(Path(sys.argv[1]).read_text())
    firmware = json.loads(Path(sys.argv[2]).read_text())
    out_path = Path(sys.argv[3])
    buttons = buttons_and_axes(out_path.parent / "pcem-input.c")

    machines = tables["machines"]
    machine_opts = [m["internal"] for m in machines]

    # A machine's display name is more use than its internal id when there
    # are 93 of them, so the option text is "internal - Display".
    def label(internal, display):
        return f"{internal} - {display}"

    machine_opts = [label(m["internal"], m["display"]) for m in machines]

    # Every enum whose value is one of PCem's internal ids is shown as
    # "internal - Display" so a 93-entry list is readable. The driver takes
    # the text before " - " back off. The firmware conditions are rewritten
    # to the same labels below, so the two cannot disagree.
    labels = {}
    for m in machines:
        labels.setdefault("machine", {})[m["internal"]] = label(m["internal"], m["display"])
    for key, rows in (("videoCard", tables["video_cards"]),
                      ("soundCard", tables["sound_cards"]),
                      ("hddController", tables["hdd_controllers"])):
        labels[key] = {r["internal"]: label(r["internal"], r["display"]) for r in rows}
    labels["videoCard"]["builtin"] = "builtin - the machine's own on-board video"

    def relabel(cond):
        if not isinstance(cond, dict):
            return cond
        for group in ("any", "all"):
            if group in cond:
                return {group: [relabel(c) for c in cond[group]]}
        if "not" in cond:
            return {"not": relabel(cond["not"])}
        m = labels.get(cond.get("setting"))
        if m:
            out = dict(cond)
            if "in" in out:
                out["in"] = [m.get(v, v) for v in out["in"]]
            if "is" in out:
                out["is"] = m.get(out["is"], out["is"])
            return out
        return cond

    for e in firmware:
        if "requiredWhen" in e:
            e["requiredWhen"] = relabel(e["requiredWhen"])

    settings = [
        {"name": "system", "display": "System", "type": "enum",
         "options": ["x86 PC"], "default": "x86 PC",
         "description": "This core is one system: an IBM-compatible x86 PC. Which PC "
                        "it is - the motherboard and its BIOS - is the Machine setting "
                        "below, and everything else in it is chosen part by part."},
        {"name": "machine", "display": "Machine (BIOS)", "type": "enum",
         "options": machine_opts,
         "default": label("ibmat", [m["display"] for m in machines if m["internal"] == "ibmat"][0]),
         "description": "Which PC this is - the motherboard, its BIOS, its chipset "
                        "and which CPUs it will take. " + str(len(machine_opts))
                        + " machines, from a 1981 IBM PC to a Slot 1 Pentium II board. "
                          "It decides which BIOS ROM the project asks you for."},
        {"name": "cpu", "display": "CPU", "type": "enum",
         "options": tables["all_cpus"], "default": "286/6",
         "description": "The CPU, by name. This is every CPU PCem has; which of them a "
                        "machine will actually take is the machine's own business, and "
                        "one it does not is refused at load with a list of the ones it "
                        "does. The manufacturer follows from the name, so there is "
                        "nothing else to pick."},
        {"name": "fpu", "display": "FPU", "type": "enum", "options": ["none", "builtin"],
         "default": "none",
         "description": "A maths coprocessor. 'builtin' for a 486DX and later, where it "
                        "is part of the CPU; 'none' for a machine that shipped without one."},
        {"name": "dynarec", "display": "Dynamic Recompiler", "type": "bool", "default": True,
         "description": "PCem's recompiler. Much faster than the interpreter on a 486 "
                        "and later, and NOT bit-identical to it - the two take different "
                        "FPU paths - so this is part of what a movie was recorded on."},
        {"name": "cpuWaitStates", "display": "CPU Wait States", "type": "int",
         "default": 0, "min": 0, "max": 7,
         "description": "Memory wait states, for machines where PCem models them. 0 is "
                        "the machine's own default."},
        {"name": "memSizeKB", "display": "Memory (KB)", "type": "int",
         "default": 4096, "min": 16, "max": 2097152,
         "description": "RAM in kilobytes. Every machine has its own minimum, maximum "
                        "and granularity and the driver clamps to them exactly as PCem "
                        "does - an IBM AT tops out at 16 MB, a GA-686BX at 512 MB."},

        {"name": "videoCard", "display": "Video Card", "type": "enum",
         "options": [labels["videoCard"]["builtin"]]
                    + [labels["videoCard"][v["internal"]] for v in tables["video_cards"]],
         "default": labels["videoCard"]["vga"],
         "description": "The graphics card. 'builtin' uses the machine's own on-board "
                        "video where it has some. Everything PCem emulates in software, "
                        "from MDA to a Voodoo 3."},
        {"name": "videoSpeed", "display": "Video Speed", "type": "enum",
         "options": tables["video_speeds"], "default": "default",
         "description": "How fast the card's bus is. 'default' leaves it to the card."},
        {"name": "voodoo", "display": "Voodoo Graphics", "type": "bool", "default": False,
         "description": "A 3dfx Voodoo Graphics or Voodoo 2 as a SEPARATE add-in card "
                        "alongside the 2D card above, which is how they were sold."},

        {"name": "soundCard", "display": "Sound Card", "type": "enum",
         "options": [labels["soundCard"][x["internal"]] for x in tables["sound_cards"]],
         "default": labels["soundCard"]["none"],
         "description": "The sound card in the machine."},
        {"name": "gameBlaster", "display": "Game Blaster", "type": "bool", "default": False,
         "description": "A Creative Game Blaster / CMS, which sits alongside the card above."},
        {"name": "gus", "display": "Gravis Ultrasound", "type": "bool", "default": False,
         "description": "A Gravis Ultrasound, alongside the card above."},
        {"name": "ssi2001", "display": "SSI-2001", "type": "bool", "default": False,
         "description": "An Innovation SSI-2001, the PC card with a C64 SID on it."},

        {"name": "hddController", "display": "Hard Disk Controller", "type": "enum",
         "options": [labels["hddController"][h["internal"]] for h in tables["hdd_controllers"]],
         "default": labels["hddController"]["none"],
         "description": "The disk controller: MFM, ESDI, IDE, XT-IDE or SCSI. A machine "
                        "with IDE on the board does not need one here."},
        {"name": "hddGeometry", "display": "Hard Disk Geometry", "type": "enum",
         "options": ["Auto", "Custom"], "default": "Auto",
         "description": "'Auto' reads the disk's cylinders, heads and sectors out of "
                        "the image itself - the CHS fields of its own partition table "
                        "first, its length second - and is right for any image made by "
                        "PCem, by DOS, by Windows or by this core. A .vhd carries its "
                        "geometry and always uses it. 'Custom' exposes the three "
                        "numbers, for an unpartitioned image that is not a whole "
                        "number of cylinders. Getting this wrong is a disk that does "
                        "not boot rather than an error, so prefer Auto."},
        {"name": "hddSectors", "display": "Hard Disk Sectors", "type": "int",
         "default": 0, "min": 0, "max": 255,
         "exposedWhen": {"setting": "hddGeometry", "is": "Custom"},
         "description": "Sectors per track of the image in the hard disk slot."},
        {"name": "hddHeads", "display": "Hard Disk Heads", "type": "int",
         "default": 0, "min": 0, "max": 255,
         "exposedWhen": {"setting": "hddGeometry", "is": "Custom"},
         "description": "Heads of the image in the hard disk slot."},
        {"name": "hddCylinders", "display": "Hard Disk Cylinders", "type": "int",
         "default": 0, "min": 0, "max": 65535,
         "exposedWhen": {"setting": "hddGeometry", "is": "Custom"},
         "description": "Cylinders of the image in the hard disk slot."},
        {"name": "hdd2Geometry", "display": "Second Hard Disk Geometry", "type": "enum",
         "options": ["Auto", "Custom"], "default": "Auto",
         "description": "See Hard Disk Geometry."},
        {"name": "hdd2Sectors", "display": "Second Hard Disk Sectors", "type": "int",
         "default": 0, "min": 0, "max": 255,
         "exposedWhen": {"setting": "hdd2Geometry", "is": "Custom"},
         "description": "Sectors per track of the second hard disk."},
        {"name": "hdd2Heads", "display": "Second Hard Disk Heads", "type": "int",
         "default": 0, "min": 0, "max": 255,
         "exposedWhen": {"setting": "hdd2Geometry", "is": "Custom"},
         "description": "Heads of the second hard disk."},
        {"name": "hdd2Cylinders", "display": "Second Hard Disk Cylinders", "type": "int",
         "default": 0, "min": 0, "max": 65535,
         "exposedWhen": {"setting": "hdd2Geometry", "is": "Custom"},
         "description": "Cylinders of the second hard disk."},

        {"name": "driveAType", "display": "Floppy Drive A", "type": "enum",
         "options": ["Auto"] + tables["fdd_types"], "default": "Auto",
         "description": "The physical drive in bay A. 'Auto' fits the drive to the "
                        "image actually in the slot, by its size: 360k and smaller "
                        "get a 5.25\" 360k drive, 720k a 3.5\" 720k, 1.2M a 5.25\" "
                        "1.2M, 1.44M a 3.5\" 1.44M, 2.88M a 3.5\" 2.88M. With no "
                        "image, or one whose size is not a standard format (an .fdi, "
                        ".td0, .imd or .86f carries its own geometry), Auto fits a "
                        "3.5\" 1.44M, the commonest drive. Picking a drive by name "
                        "instead always wins - and note the drive really does matter: "
                        "PCem does not refuse a disk the drive cannot reach, it "
                        "clamps the head at the drive's last track and the guest gets "
                        "read errors."},
        {"name": "driveBType", "display": "Floppy Drive B", "type": "enum",
         "options": ["Auto"] + tables["fdd_types"], "default": "Auto",
         "description": "The physical drive in bay B, the same way. With nothing in "
                        "bay B, Auto fits a 5.25\" 1.2M - the second drive a period "
                        "PC usually had."},
        {"name": "bpbDisable", "display": "Disable BPB", "type": "bool", "default": False,
         "description": "Ignore the boot sector's BIOS Parameter Block when deciding a "
                        "floppy's format. Needed by a few copy-protected disks."},

        {"name": "cdDrive", "display": "CD-ROM Drive", "type": "enum",
         "options": ["Auto", "Fitted", "None"], "default": "Auto",
         "description": "Whether the machine has a CD-ROM drive at all. 'Auto' fits "
                        "one when there is a disc in the CD-ROM slot and leaves it "
                        "out when there is not, which is what almost everyone wants. "
                        "'Fitted' gives the machine an empty drive - a guest that "
                        "looks for one finds it - and 'None' never fits one."},
        {"name": "cdChannel", "display": "CD-ROM Channel", "type": "int",
         "exposedWhen": {"not": {"setting": "cdDrive", "is": "None"}},
         "default": 2, "min": 0, "max": 3,
         "description": "Which IDE channel the CD-ROM is on (2 is the secondary master, "
                        "which is where a period PC put it)."},
        {"name": "cdSpeed", "display": "CD-ROM Speed", "type": "int",
         "default": 24, "min": 1, "max": 72,
         "exposedWhen": {"not": {"setting": "cdDrive", "is": "None"}},
         "description": "The drive's speed multiplier. It changes how long a read takes, "
                        "so it is part of the machine."},
        {"name": "cdModel", "display": "CD-ROM Model", "type": "enum",
         "options": ["pcemcd", "toshiba_xm_5602b"], "default": "pcemcd",
         "exposedWhen": {"not": {"setting": "cdDrive", "is": "None"}},
         "description": "Which drive the machine reports itself as having."},

        {"name": "mouseType", "display": "Mouse", "type": "enum",
         "options": tables["mice"], "default": "Microsoft 2-button mouse (serial)",
         "description": "Which mouse is plugged in. A machine with no PS/2 port needs a "
                        "serial one; the Amstrad and Olivetti mice are built into those "
                        "machines."},
        {"name": "joystickType", "display": "Joystick", "type": "enum",
         "options": tables["joysticks"], "default": "Standard 2-button joystick(s)",
         "description": "What is in the game port."}
        ,
        {"name": "lpt1Device", "display": "Parallel Port Device", "type": "enum",
         "options": ["none", "dac", "dss"], "default": "none",
         "description": "What is plugged into LPT1: nothing, a Covox-style DAC, or a "
                        "Disney Sound Source."},

        {"name": "fpsNumerator", "display": "Frames Per Second (numerator)", "type": "int",
         "default": 100, "min": 1, "max": 1000,
         "description": "How much machine time one frame is. 100/1 means a frame is 10 ms "
                        "of the emulated PC, which is what every published PCem movie "
                        "uses. It is part of the movie: the same inputs at a different "
                        "rate are a different run."},
        {"name": "fpsDenominator", "display": "Frames Per Second (denominator)", "type": "int",
         "default": 1, "min": 1, "max": 1000, "description": "See the numerator."},
    ]

    config = {
        "coreName": "PCem",
        "author": "Sarah Walker and the PCem contributors; the TASVideos fork; "
                  "chimera port by Sergio Martin",
        "url": "https://github.com/ToolAssisted-run/chimera-core-pcem",
        "romFile": "rom",
        "deterministic": True,
        "_memoryLayoutMiB_note":
            "sbrk, sealed, invisible, plain, mmap. A PC's RAM is up to 512 MB and PCem "
            "mallocs it, which musl routes to mmap along with the memory lookup tables; "
            "the recompiler's arena is a further 120 MB (codegen_allocator.h). invisible "
            "holds the frame buffer, which no savestate needs to carry. mmap also holds "
            "the hard disks' write overlay, 4 KiB a written block: a seeded disk costs "
            "nothing until the guest writes, so the 4096 here is what BOUNDS a machine "
            "that writes a lot - a Windows XP install is about 1.1 GiB of blocks - and "
            "not what it reserves. A write the arena cannot hold is reported to the "
            "guest as a write fault and said so on stderr, never silently dropped.",
        "memoryLayoutMiB": [64, 16, 320, 64, 4096],
        "video": {
            "_comment":
                "The BUFFER CAPACITY. A PC changes video mode whenever it likes, so the "
                "live size comes from GetVideoWidth/Height every frame and this is only "
                "the largest PCem can produce.",
            "width": 1600, "height": 1200,
            "virtualWidth": 640, "virtualHeight": 480,
            "vsyncNumerator": 100, "vsyncDenominator": 1,
            "_vsync_note":
                "A frame is a fixed slice of machine time, not a scanout - the TASVideos "
                "fork's runpc(ms). 100 fps is 10 ms a frame, which is what every published "
                "PCem movie uses; the fps settings change it.",
            "getBgra": "GetVideoBgra",
        },
        "audio": {
            "_comment": "PCem mixes at 48 kHz (sound.c:259) and hands over a buffer when "
                        "its emulated timer says so; a frame's worth is whatever arrived.",
            "samplesPerFrame": 48000, "channels": 2, "rate": 48000, "get": "GetAudio",
        },
        "lag": {"inputWasRead": "InputWasRead"},
        "extensions": {
            ".img": "PC", ".ima": "PC", ".dsk": "PC", ".fdi": "PC", ".86f": "PC",
            ".td0": "PC", ".imd": "PC", ".hdd": "PC", ".vhd": "PC", ".hdi": "PC",
            ".iso": "PC", ".cue": "PC",
        },
        "input": {
            "name": "PC Keyboard, Mouse and Joysticks",
            "_comment":
                "A 101/102-key PC keyboard, the mouse buttons and two gameport sticks. "
                "Well past 64 buttons, so every button rides SetButton and the packed "
                "mask is unioned with it. The order here is generated from "
                "waterbox/pcem-input.c, which holds the scancode for each.",
            "buttons": buttons,
            "axes": [
                {"name": "Mouse X", "min": -128, "max": 127, "neutral": 0},
                {"name": "Mouse Y", "min": -128, "max": 127, "neutral": 0},
                {"name": "Joystick 1 X", "min": 0, "max": 65535, "neutral": 32767},
                {"name": "Joystick 1 Y", "min": 0, "max": 65535, "neutral": 32767},
                {"name": "Joystick 2 X", "min": 0, "max": 65535, "neutral": 32767},
                {"name": "Joystick 2 Y", "min": 0, "max": 65535, "neutral": 32767},
            ],
        },
        "systemId": "PC",
        "settings": settings,
        "firmware": firmware,
    }

    out_path.write_text(json.dumps(config, indent=2) + "\n")
    print(f"{len(machine_opts)} machines, {len(tables['video_cards'])} video cards, "
          f"{len(tables['sound_cards'])} sound cards, "
          f"{len(tables['hdd_controllers'])} hdd controllers, "
          f"{len(buttons)} buttons, {len(firmware)} firmware -> {out_path}")


if __name__ == "__main__":
    main()

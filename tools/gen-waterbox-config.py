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

# The six TASVideos configurations (PLAN.md section 6.1), as presets. Each
# narrows the defaults; none of them removes a machine from the list.
PRESETS = [
    ("custom", "Custom", "Any machine PCem supports, configured by hand.", {}),
    ("dos-late80s", "DOS, late 1980s (Compaq Deskpro 386)",
     "TASVideos' late-80s DOS configuration: a 20 MHz 386DX with 4 MB, IBM VGA "
     "and a Sound Blaster Pro. For games of roughly 1987-1991.",
     {"machine": "deskpro386", "cpu": 0, "memSizeKB": 4096, "videoCard": "vga",
      "soundCard": "sbpro2", "dynarec": False, "hddController": "ide", "mouseType": 0}),
    ("dos-early90s", "DOS, early 1990s (Packard Bell PB570)",
     "TASVideos' early-90s DOS configuration: a Pentium 133 with 8 MB, the "
     "board's built-in Cirrus Logic video and a Sound Blaster 16.",
     {"machine": "pb570", "memSizeKB": 8192, "videoCard": "builtin",
      "soundCard": "sb16", "dynarec": True, "hddController": "ide", "mouseType": 2}),
    ("dos-late90s", "DOS, late 1990s (Gigabyte GA-686BX)",
     "TASVideos' late-90s DOS configuration: a Pentium II/450 with 32 MB, an S3 "
     "Trio64 and a Voodoo Graphics, with a Sound Blaster 16.",
     {"machine": "ga686bx", "cpu": 6, "memSizeKB": 32768, "videoCard": "px_trio64",
      "voodoo": True, "soundCard": "sb16", "dynarec": True,
      "hddController": "ide", "mouseType": 2}),
    ("win95", "Windows 95 (Gigabyte GA-686BX)",
     "TASVideos' Windows 95 configuration: a Pentium II/233 with 256 MB and a "
     "Voodoo 3 3000. You supply the operating system.",
     {"machine": "ga686bx", "cpu": 0, "memSizeKB": 262144, "videoCard": "v3_3000",
      "soundCard": "sb16", "dynarec": True, "hddController": "ide", "mouseType": 2}),
    ("winxp", "Windows XP (Gigabyte GA-686BX)",
     "TASVideos' Windows XP configuration: a Pentium II/450 with 256 MB, a "
     "Voodoo 3 3000 and an AWE32. You supply the operating system.",
     {"machine": "ga686bx", "cpu": 6, "memSizeKB": 262144, "videoCard": "v3_3000",
      "soundCard": "sbawe32", "dynarec": True, "hddController": "ide", "mouseType": 2}),
]


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

    # The CPU list is per machine AND per manufacturer, so it cannot be one
    # enum. It is declared as an int index with the machine's list spelled out
    # in the description, and the driver clamps it the way PCem does.
    cpu_help_lines = []
    for m in machines:
        for mi, tbl in enumerate(m["cpu_tables"]):
            names = tables["cpu_tables"].get(tbl, [])
            if names:
                cpu_help_lines.append(
                    f"{m['internal']} / {m['cpu_makers'][mi]}: "
                    + ", ".join(f"{i}={n}" for i, n in enumerate(names)))

    settings = [
        {"name": "preset", "display": "Configuration", "type": "enum",
         "options": [p[0] for p in PRESETS], "default": "custom",
         "description": "A starting point. Each preset is one of the configurations "
                        "TASVideos publishes, and sets the machine, CPU, memory, "
                        "video and sound below; 'custom' leaves them alone. Changing "
                        "anything afterwards is fine - the preset only supplies "
                        "defaults."},
        {"name": "machine", "display": "Machine", "type": "enum",
         "options": machine_opts, "default": "ibmat",
         "description": "Which PC this is - the motherboard, its BIOS, its chipset "
                        "and which CPUs it will take. " + str(len(machine_opts))
                        + " machines, from a 1981 IBM PC to a Slot 1 Pentium II board. "
                          "It decides which BIOS ROM the project asks you for."},
        {"name": "cpuManufacturer", "display": "CPU Manufacturer", "type": "int",
         "default": 0, "min": 0, "max": 4,
         "description": "Which manufacturer's CPU list to pick from, for a machine "
                        "that takes more than one (0 is the first, usually Intel)."},
        {"name": "cpu", "display": "CPU", "type": "int", "default": 0, "min": 0, "max": 63,
         "description": "Which CPU out of the chosen machine and manufacturer's list, "
                        "counting from 0. The lists are PCem's own:\n"
                        + "\n".join(cpu_help_lines[:40])
                        + ("\n(and more; every machine's list is PCem's)" if len(cpu_help_lines) > 40 else "")},
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
         "options": ["builtin"] + [v["internal"] for v in tables["video_cards"]],
         "default": "vga",
         "description": "The graphics card. 'builtin' uses the machine's own on-board "
                        "video where it has some. Everything PCem emulates in software, "
                        "from MDA to a Voodoo 3."},
        {"name": "videoSpeed", "display": "Video Speed", "type": "int",
         "default": -1, "min": -1, "max": 4,
         "description": "The card's bus speed setting, -1 for the card's default. "
                        "Higher is a faster VLB/PCI bus."},
        {"name": "voodoo", "display": "Voodoo Graphics", "type": "bool", "default": False,
         "description": "A 3dfx Voodoo Graphics or Voodoo 2 as a SEPARATE add-in card "
                        "alongside the 2D card above, which is how they were sold."},

        {"name": "soundCard", "display": "Sound Card", "type": "enum",
         "options": [s["internal"] for s in tables["sound_cards"]], "default": "none",
         "description": "The sound card in the machine."},
        {"name": "gameBlaster", "display": "Game Blaster", "type": "bool", "default": False,
         "description": "A Creative Game Blaster / CMS, which sits alongside the card above."},
        {"name": "gus", "display": "Gravis Ultrasound", "type": "bool", "default": False,
         "description": "A Gravis Ultrasound, alongside the card above."},
        {"name": "ssi2001", "display": "SSI-2001", "type": "bool", "default": False,
         "description": "An Innovation SSI-2001, the PC card with a C64 SID on it."},

        {"name": "hddController", "display": "Hard Disk Controller", "type": "enum",
         "options": [h["internal"] for h in tables["hdd_controllers"]], "default": "none",
         "description": "The disk controller: MFM, ESDI, IDE, XT-IDE or SCSI. A machine "
                        "with IDE on the board does not need one here."},
        {"name": "hddSectors", "display": "Hard Disk Sectors", "type": "int",
         "default": 0, "min": 0, "max": 255,
         "description": "Geometry of the image in the hard disk slot. 0 derives it from "
                        "the file's size, the way PCem's own new-disk dialog does; set "
                        "all three for an image whose geometry cannot be guessed."},
        {"name": "hddHeads", "display": "Hard Disk Heads", "type": "int",
         "default": 0, "min": 0, "max": 255, "description": "See Hard Disk Sectors."},
        {"name": "hddCylinders", "display": "Hard Disk Cylinders", "type": "int",
         "default": 0, "min": 0, "max": 65535, "description": "See Hard Disk Sectors."},
        {"name": "hdd2Sectors", "display": "Second Hard Disk Sectors", "type": "int",
         "default": 0, "min": 0, "max": 255, "description": "Geometry of the second hard disk."},
        {"name": "hdd2Heads", "display": "Second Hard Disk Heads", "type": "int",
         "default": 0, "min": 0, "max": 255, "description": "Geometry of the second hard disk."},
        {"name": "hdd2Cylinders", "display": "Second Hard Disk Cylinders", "type": "int",
         "default": 0, "min": 0, "max": 65535, "description": "Geometry of the second hard disk."},

        {"name": "driveAType", "display": "Floppy Drive A", "type": "int",
         "default": 7, "min": 0, "max": 13,
         "description": "The physical drive: PCem's type numbers, where 2 is a 5.25\" "
                        "1.2M and 7 a 3.5\" 2.88M. It decides which images the machine "
                        "can read, not which image is in it."},
        {"name": "driveBType", "display": "Floppy Drive B", "type": "int",
         "default": 2, "min": 0, "max": 13, "description": "See Floppy Drive A."},
        {"name": "bpbDisable", "display": "Disable BPB", "type": "bool", "default": False,
         "description": "Ignore the boot sector's BIOS Parameter Block when deciding a "
                        "floppy's format. Needed by a few copy-protected disks."},

        {"name": "cdChannel", "display": "CD-ROM Channel", "type": "int",
         "default": 2, "min": 0, "max": 3,
         "description": "Which IDE channel the CD-ROM is on (2 is the secondary master, "
                        "which is where a period PC put it)."},
        {"name": "cdSpeed", "display": "CD-ROM Speed", "type": "int",
         "default": 24, "min": 1, "max": 72,
         "description": "The drive's speed multiplier. It changes how long a read takes, "
                        "so it is part of the machine."},
        {"name": "cdModel", "display": "CD-ROM Model", "type": "enum",
         "options": ["pcemcd", "toshiba_xm_5602b"], "default": "pcemcd",
         "description": "Which drive the machine reports itself as having."},

        {"name": "mouseType", "display": "Mouse", "type": "int",
         "default": 0, "min": 0, "max": 7,
         "description": "0 is a Microsoft serial mouse, 2 a PS/2 mouse. A machine with no "
                        "PS/2 port needs the serial one."},
        {"name": "joystickType", "display": "Joystick", "type": "int",
         "default": 0, "min": 0, "max": 4,
         "description": "0 is a standard 2-button gameport stick; the others are the CH "
                        "Flightstick Pro, Thrustmaster FCS and Sidewinder pad."},
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

    machines_block = []
    for pid, label, desc, over in PRESETS:
        entry = {"id": pid, "label": label, "when": [pid]}
        if over:
            entry["settingOverrides"] = {k: {"default": v} for k, v in over.items()}
        machines_block.append(entry)

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
            "holds the frame buffer, which no savestate needs to carry.",
        "memoryLayoutMiB": [64, 16, 320, 64, 2048],
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
        "machineSetting": "preset",
        "_machines_note":
            "The presets are TASVideos' published configurations. They are machines[] "
            "entries whose settingOverrides change DEFAULTS only - every machine, card "
            "and drive stays selectable under any of them, and 'custom' overrides "
            "nothing. This is a general PC, not an XP appliance.",
        "machines": machines_block,
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

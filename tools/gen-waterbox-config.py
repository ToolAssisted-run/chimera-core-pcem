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


def build_presets(labels):
    """The five machines TASVideos publish for PCem, in chronological order.

    Sources, fetched 2026-09-21 and quoted verbatim in docs/PRESETS.md:
      tasvideos.org/EmulatorResources/PCem/DOS/Configurations
      tasvideos.org/EmulatorResources/PCem/Windows/Configurations/95
      tasvideos.org/EmulatorResources/PCem/Windows/Configurations/XP

    Four things about how these are built, all of them decisions with reasons
    written down in docs/PRESETS.md rather than guesses:

      * A preset DOES set the machine, and it is the most important thing it
        says - "Packard Bell PB570" IS the preset. ApplySelectedPreset skips
        `_cfg.MachineSetting`, but that is the DECLARED machine chooser that
        goes with a machines[] array, and this package declares neither: with
        machineSetting null the comparison is against null and `machine` is an
        ordinary setting like any other. There is no page-one machine question
        here to be downstream of.
      * There is no TIME SYNC value. Both Windows tables say "enabled to host
        clock"; a core that reads the host clock is not deterministic, which is
        Chimera issue #120, and the driver already pins enable_sync = 0.
      * There is no hard disk GEOMETRY. The tables give the geometry of a disk
        PCem would CREATE; here the disk is a file the user supplies and
        hddGeometry "Auto" reads the geometry out of it. Hardcoding 63/16/8374
        would fight the user's own image.
      * There are no FIXED FLOPPY DRIVES. The tables put a 3.5" 2.88M in bay A
        and a 5.25" 1.2M in bay B for every one of the five, which is TASVideos
        making the drives big enough for anything they might mount. "Auto" fits
        the drive to the image actually in the slot, which is strictly better
        here and is the difference between Alley Cat's 180 KB disk working and
        not.

    Every value below is one the source table names, or a fact about the CPU
    the table names (its coprocessor, and whether PCem's own dialog would force
    the recompiler on for it - wx-config.c:791-797).

    There is deliberately no `when[]`. It gates a preset by MachineConfig, and
    with no machines[] declared there is no MachineConfig, so every when[] here
    would be inert - and an inert declaration that looks load-bearing is a trap
    for whoever reads this next. The board lives in values["machine"], where it
    does something."""
    m = labels["machine"]
    v = labels["videoCard"]
    s = labels["soundCard"]
    h = labels["hddController"]

    # What every one of the five shares: a starting point is a whole machine,
    # so the things the tables list for all of them are listed for all of them.
    def common(cd_speed, cd_channel=2):
        return {
            "hddController": h["ide"],
            "hddGeometry": "Auto",
            "driveAType": "Auto",
            "driveBType": "Auto",
            "cdDrive": "Auto",
            "cdModel": "pcemcd",
            "cdChannel": cd_channel,
            "cdSpeed": cd_speed,
            "gameBlaster": False,
            "gus": False,
            "ssi2001": False,
            "lpt1Device": "none",
            "soundCardAddress": "0x220",
            "oplEmulator": "NukedOPL",
        }

    return [
        {
            "id": "dos_late_80s",
            "label": "DOS, late 1980s - Compaq Deskpro 386",
            "description": "A 20 MHz 386DX with VGA and a Sound Blaster Pro v2, for DOS "
                           "games released up to the end of 1989.",
            "values": dict(common(24), **{
                "machine": m["deskpro386"],
                "cpu": "i386DX/20",
                "fpu": "none",
                "dynarec": False,
                "memSizeKB": 4096,
                "videoCard": v["vga"],
                "voodoo": False,
                "soundCard": s["sbprov2"],
                "soundCardIrq": "IRQ 7",
                "soundCardDma": "DMA 1",
                "mouseType": "Microsoft 2-button mouse (serial)",
            }),
        },
        {
            "id": "dos_early_90s",
            "label": "DOS, early 1990s - Packard Bell PB570",
            "description": "A Pentium 133 with the Cirrus Logic video on its "
                "motherboard and a Sound Blaster 16, for DOS games released "
                "from 1990 to 1994.",
            "values": dict(common(24), **{
                "machine": m["pb570"],
                "cpu": "Pentium 133",
                "fpu": "builtin",
                "dynarec": True,
                "memSizeKB": 8192,
                "videoCard": v["builtin"],
                "videoMemory": "2 MB",
                "voodoo": False,
                "soundCard": s["sb16"],
                "mouseType": "2-button mouse (PS/2)",
            }),
        },
        {
            "id": "dos_late_90s",
            "label": "DOS, late 1990s - Gigabyte GA-686BX",
            "description": "A Pentium II/450 with an S3 Trio64 and a separate Voodoo "
                "Graphics card beside it, for DOS games released from 1995 "
                "on.",
            "values": dict(common(72, cd_channel=3), **{
                "machine": m["ga686bx"],
                "cpu": "Pentium II/450",
                "fpu": "builtin",
                "dynarec": True,
                "memSizeKB": 32768,
                "videoCard": v["px_trio64"],
                "videoMemory": "4 MB",
                "voodoo": True,
                "voodooType": "Voodoo Graphics",
                "soundCard": s["sb16"],
                "mouseType": "2-button mouse (PS/2)",
            }),
        },
        {
            "id": "win95b_osr2",
            "label": "Windows 95b OSR 2 - Gigabyte GA-686BX",
            "description": "A Pentium II/233 with 256 MB of memory and a Voodoo 3 3000."
                " TASVideos installs Windows 95 OSR 2 on this machine.",
            "values": dict(common(72), **{
                "machine": m["ga686bx"],
                "cpu": "Pentium II/233",
                "fpu": "builtin",
                "dynarec": True,
                "memSizeKB": 262144,
                "videoCard": v["v3_3000"],
                "videoSpeed": "Fast VLB/PCI",
                "videoRenderThreads": "1",
                "voodoo": False,
                "soundCard": s["sb16"],
                "mouseType": "2-button mouse (PS/2)",
            }),
        },
        {
            "id": "winxp_sp3_home",
            "label": "Windows XP SP3 Home Edition - Gigabyte GA-686BX",
            "description": "A Pentium II/450 with 256 MB of memory, a Voodoo 3 3000 and"
                " an AWE32. TASVideos installs Windows XP on this machine.",
            "values": dict(common(72), **{
                "machine": m["ga686bx"],
                "cpu": "Pentium II/450",
                "fpu": "builtin",
                "dynarec": True,
                "memSizeKB": 262144,
                "videoCard": v["v3_3000"],
                "videoSpeed": "Fast VLB/PCI",
                "videoRenderThreads": "1",
                "voodoo": False,
                "soundCard": s["sbawe32"],
                "mouseType": "2-button mouse (PS/2)",
            }),
        },
    ]


# ---- what the controls and the system are called ----
# The frontend keeps no table of these: a core says what its own are called.
# MNEMONICS is the letter each button writes into a movie's text and heads its
# input column with, by the button's name - whole, or without its player ("P2
# Up" is found under "Up"), so one line serves every pad. AXIS_HEADERS is the
# short header of each axis's column. (An entry is read by position: a letter
# may change and no movie made before it is harmed.)
MNEMONICS = {
    "Escape": "E", "1": "1", "2": "2", "3": "3", "4": "4", "5": "5", "6": "6", "7": "7", "8": "8",
    "9": "9", "0": "0", "Minus": "M", "Equals": "E", "Backspace": "B", "Tab": "T", "Q": "Q",
    "W": "W", "E": "E", "R": "r", "T": "T", "Y": "Y", "U": "U", "I": "I", "O": "O", "P": "P",
    "Left Bracket": "B", "Right Bracket": "B", "Enter": "E", "Left Ctrl": "C", "A": "A", "S": "S",
    "D": "D", "F": "F", "G": "G", "H": "H", "J": "J", "K": "K", "L": "l", "Semicolon": "S",
    "Apostrophe": "A", "Backquote": "B", "Left Shift": "S", "Backslash": "B", "Z": "Z", "X": "X",
    "C": "C", "V": "V", "B": "B", "N": "N", "M": "M", "Comma": "C", "Period": "P", "Slash": "S",
    "Right Shift": "S", "Keypad Multiply": "M", "Left Alt": "A", "Space": "S", "Caps Lock": "L",
    "F1": "1", "F2": "2", "F3": "3", "F4": "4", "F5": "5", "F6": "6", "F7": "7", "F8": "8",
    "F9": "9", "F10": "0", "Num Lock": "L", "Scroll Lock": "L", "Keypad 7": "7", "Keypad 8": "8",
    "Keypad 9": "9", "Keypad Minus": "M", "Keypad 4": "4", "Keypad 5": "5", "Keypad 6": "6",
    "Keypad Plus": "P", "Keypad 1": "1", "Keypad 2": "2", "Keypad 3": "3", "Keypad 0": "0",
    "Keypad Period": "P", "F11": "F", "F12": "F", "Keypad Enter": "E", "Right Ctrl": "C",
    "Keypad Divide": "D", "Print Screen": "S", "Right Alt": "A", "Home": "H", "Up": "U",
    "Page Up": "U", "Left": "L", "Right": "R", "End": "E", "Down": "D", "Page Down": "D",
    "Insert": "I", "Delete": "D", "Left Windows": "W", "Right Windows": "W", "Menu": "M",
    "Mouse Left": "l", "Mouse Right": "r", "Mouse Middle": "M", "Joystick 1 Button 1": "1",
    "Joystick 1 Button 2": "2", "Joystick 2 Button 1": "1", "Joystick 2 Button 2": "2",
}
AXIS_HEADERS = {
    "Mouse X": "mX", "Mouse Y": "mY", "Mouse Position X": "MPX", "Mouse Position Y": "MPY",
    "Joystick 1 X": "J1X", "Joystick 1 Y": "J1Y", "Joystick 2 X": "J2X", "Joystick 2 Y": "J2Y",
}
SYSTEM_NAMES = {
    "PC": "IBM PC compatible",
}


def _bare(name):
    """A control's name without its player: "P2 Up" -> "Up"."""
    head, _, rest = name.partition(" ")
    return rest if rest and head[:1] == "P" and head[1:].isdigit() else name


def mnemonics_for(buttons):
    """The "mnemonics" of an input declaration: a letter for every one of its
    buttons, and for nothing else. A button nobody gave a letter stops the
    build - the engine would give it its rule's guess, and two columns of one
    pad would share a letter with nobody having decided it."""
    out = {}
    for b in buttons:
        key = b if b in MNEMONICS else _bare(b)
        if key not in MNEMONICS:
            raise SystemExit("no mnemonic for the button %r (MNEMONICS in %s)" % (b, __file__))
        out[key] = MNEMONICS[key]
    return out


def with_headers(axes):
    """The axes with their column headers; an axis nobody named stops the build."""
    missing = [a["name"] for a in axes if a["name"] not in AXIS_HEADERS]
    if missing:
        raise SystemExit("no header for the axes %s (AXIS_HEADERS in %s)" % (missing, __file__))
    return [dict(a, header=AXIS_HEADERS[a["name"]]) for a in axes]


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

    # PCem's per-DEVICE settings. These do not live in the .cfg's global
    # section: each is read back through device_get_config_int, which looks the
    # key up in the FITTED device's own table and returns that table's number
    # (device.c:120-133). So the Chimera setting carries the LABEL - "0x220",
    # "NukedOPL", "4 MB" - and the driver resolves it against whichever card is
    # actually in the machine. That is the only way one setting can serve 20
    # sound cards whose legal values disagree (an SB Pro v2 takes two
    # addresses, an SB16 four) and 48 video cards whose "memory" is in MB on an
    # S3 and in kB on an AVGA2.
    #
    # CARD DEFAULT, and why every one of these has it: PCem's default differs
    # per card - 2 MB of video memory on a Trio64, 4 on a GD5434, 16 on a
    # Banshee - so there is no single value that means "leave it alone", and a
    # setting that has to name one would be changing machines that never asked.
    # "Card default" writes NOTHING into the .cfg, which is exactly what the
    # core did before these settings existed.
    dcg = tables["device_config_groups"]

    def dev_opts(group, key):
        return ["Card default"] + dcg[group][key]

    device_settings = [
        {"name": "soundCardAddress", "display": "Sound Card Address",
         "type": "enum", "options": dev_opts("sound", "addr"),
         "default": "Card default",
         "description": "The I/O port address of the sound card. 0x220 is where PCs of "
             "the time had a Sound Blaster, and almost every DOS game "
             "assumes it. 'Card default' leaves the address the chosen card "
             "was sold with. An address the chosen card cannot have is "
             "ignored."},
        {"name": "soundCardIrq", "display": "Sound Card IRQ",
         "type": "enum", "options": dev_opts("sound", "irq"),
         "default": "Card default",
         "description": "The interrupt line (IRQ) the sound card uses. Only the older "
             "Sound Blasters let you choose it. A Sound Blaster 16 and later"
             " read it from their own configuration."},
        {"name": "soundCardDma", "display": "Sound Card DMA",
         "type": "enum", "options": dev_opts("sound", "dma"),
         "default": "Card default",
         "description": "The DMA channel the sound card uses, on the cards that let you"
             " choose it."},
        {"name": "oplEmulator", "display": "OPL Emulator",
         "type": "enum", "options": dev_opts("sound", "opl_emu"),
         "default": "Card default",
         "description": "Which emulation of the Yamaha OPL chip the card's FM sound "
             "uses. NukedOPL is the accurate one, and the TASVideos PCem "
             "configurations use it. DBOPL is faster. They do not sound the "
             "same and do not behave the same, so it is part of the machine,"
             " and a movie needs the same value."},
        {"name": "awe32EmuAddress", "display": "AWE32 EMU8000 Address",
         "type": "enum", "options": dev_opts("sound", "emu_addr"),
         "default": "Card default",
         "description": "The I/O port address of the EMU8000 wavetable chip on the "
             "Sound Blaster AWE32. Only the AWE32 has one."},
        {"name": "awe32OnboardRam", "display": "AWE32 Onboard RAM",
         "type": "enum", "options": dev_opts("sound", "onboard_ram"),
         "default": "Card default",
         "description": "How much sample memory the Sound Blaster AWE32 has. Only the "
             "AWE32 has any."},

        {"name": "videoMemory", "display": "Video Memory",
         "type": "enum", "options": dev_opts("video", "memory"),
         "default": "Card default",
         "description": "How much memory the graphics card has. It decides which video "
             "modes the card can show. Each card offers its own sizes: a "
             "Trio64 has 1, 2 or 4 MB, and an AVGA2 has 256 or 512 kB. A "
             "size the chosen card does not offer is ignored, so 'Card "
             "default' is always safe."},
        {"name": "videoBilinear", "display": "Video Bilinear Filtering",
         "type": "enum", "options": dev_opts("video", "bilinear"),
         "default": "Card default",
         "description": "Bilinear texture filtering, on the 3D cards that have it (a "
                        "Voodoo Banshee or Voodoo 3). It changes the picture, so it is "
                        "part of the machine."},
        {"name": "videoScreenFilter", "display": "Video Screen Filter",
         "type": "enum", "options": dev_opts("video", "dacfilter"),
         "default": "Card default",
         "description": "The output filter of the 3dfx cards. It blurs the picture the "
             "way the real card's output stage did."},
        {"name": "videoRenderThreads", "display": "Video Render Threads",
         "type": "enum", "options": dev_opts("video", "render_threads"),
         "default": "Card default",
         "description": "How many slices the 3dfx cards split a triangle into. In this "
             "build they are slices and not threads: the TASVideos version "
             "of PCem draws them one after another, in order. The split "
             "still changes what is drawn and how the texture cache behaves,"
             " so it is part of the machine. The TASVideos configurations "
             "use 1."},
        {"name": "videoRecompiler", "display": "Video Recompiler",
         "type": "enum", "options": dev_opts("video", "recompiler"),
         "default": "Card default",
         "description": "The 3dfx cards' own recompiler for drawing pixels."},

        {"name": "voodooType", "display": "Voodoo Type",
         "type": "enum", "options": dev_opts("voodoo", "type"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "Which 3dfx card the Voodoo Graphics setting above adds."},
        {"name": "voodooFramebufferMemory", "display": "Voodoo Framebuffer Memory",
         "type": "enum", "options": dev_opts("voodoo", "framebuffer_memory"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "The framebuffer memory of the added Voodoo card. It decides "
             "which resolutions the card can show."},
        {"name": "voodooTextureMemory", "display": "Voodoo Texture Memory",
         "type": "enum", "options": dev_opts("voodoo", "texture_memory"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "The texture memory of the added Voodoo card, for each texture "
             "unit (TMU)."},
        {"name": "voodooBilinear", "display": "Voodoo Bilinear Filtering",
         "type": "enum", "options": dev_opts("voodoo", "bilinear"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "Bilinear texture filtering on the added Voodoo card."},
        {"name": "voodooScreenFilter", "display": "Voodoo Screen Filter",
         "type": "enum", "options": dev_opts("voodoo", "dacfilter"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "The output filter of the added Voodoo card."},
        {"name": "voodooRenderThreads", "display": "Voodoo Render Threads",
         "type": "enum", "options": dev_opts("voodoo", "render_threads"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "How many slices the added Voodoo card splits a triangle into. "
             "See Video Render Threads."},
        {"name": "voodooSli", "display": "Voodoo SLI",
         "type": "enum", "options": dev_opts("voodoo", "sli"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "Two Voodoo cards working together (SLI), each drawing half of "
             "the lines. A pair of Voodoo 2 cards was sold this way."},
        {"name": "voodooRecompiler", "display": "Voodoo Recompiler",
         "type": "enum", "options": dev_opts("voodoo", "recompiler"),
         "default": "Card default",
         "exposedWhen": {"setting": "voodoo", "is": True},
         "description": "The added Voodoo card's own recompiler for drawing pixels."},
    ]

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
         "description": "This core emulates one kind of system: an IBM-compatible x86 "
             "PC. Which PC it is (the motherboard and its BIOS) is chosen "
             "with the Machine setting below. Every other part is chosen "
             "separately."},
        {"name": "machine", "display": "Machine (BIOS)", "type": "enum",
         "options": machine_opts,
         "default": label("ibmat", [m["display"] for m in machines if m["internal"] == "ibmat"][0]),
         "description": "Which PC this is: the motherboard, its BIOS, its chipset "
                        "and the processors it accepts. There are " + str(len(machine_opts))
                        + " machines, from a 1981 IBM PC to a Slot 1 Pentium II board. "
                          "It decides which BIOS ROM the project asks you for."},
        {"name": "cpu", "display": "CPU", "type": "enum",
         "options": tables["all_cpus"], "default": "286/6",
         "description": "The processor, by name. The list has every processor PCem "
             "knows. Each machine accepts only some of them. One that the "
             "machine does not accept is refused when the project loads, "
             "with a list of those it does accept. The manufacturer follows "
             "from the name, so there is nothing else to choose."},
        # PCem's own FPU names, from the FPU tables in cpu_tables.c: a CPU
        # carries the list of coprocessors it will take and fpu_get_type()
        # matches this string against their internal names, falling back to
        # the CPU's first entry. So "387" on a 386DX is a 387, "387" on a
        # Pentium is the built-in unit, and nothing here can produce a machine
        # PCem would refuse.
        {"name": "fpu", "display": "FPU", "type": "enum",
         "options": ["none", "8087", "287", "287xl", "387", "builtin"],
         "default": "none",
         "description": "A maths coprocessor (FPU). 'builtin' is for a 486DX and later,"
             " where it is part of the processor. 8087, 287, 287XL and 387 "
             "are the separate chips that an 8088, a 286 and a 386 could "
             "take. 'none' is for a machine sold without one. A processor "
             "with a built-in coprocessor always has it, whatever is chosen "
             "here. If the processor cannot take the chosen chip, that "
             "processor's own first choice is used."},
        {"name": "dynarec", "display": "Dynamic Recompiler", "type": "bool", "default": True,
         "description": "PCem's dynamic recompiler. It is much faster than the "
             "interpreter on a 486 and later. Its results are NOT exactly "
             "the same as the interpreter's, because the two calculate "
             "floating point differently. So it is part of the machine, and "
             "a movie needs the same value."},
        {"name": "cpuClockMHz", "display": "CPU Clock (MHz)", "type": "int",
         "default": 0, "min": 0, "max": 2000,
         "description": "0 means the chosen processor's own clock speed. Any other "
             "value runs that processor at this speed instead, faster or "
             "slower. The chip and its timings stay as chosen, and only its "
             "speed changes. So a Pentium II/450 at 900 is a Pentium II "
             "twice as fast, which PCem has no entry for. 2000 is the most "
             "PCem's timers can hold. A faster machine takes proportionally "
             "more work to emulate."},
        {"name": "cpuWaitStates", "display": "CPU Wait States", "type": "int",
         "default": 0, "min": 0, "max": 7,
         "description": "Memory wait states, for the machines where PCem models them. 0"
             " is the machine's own default."},
        {"name": "memSizeKB", "display": "Memory (KB)", "type": "int",
         "default": 4096, "min": 16, "max": 2097152,
         "description": "The memory (RAM) in kilobytes. Every machine has its own "
             "minimum, maximum and step size, and the value is limited to "
             "them exactly as PCem does it. For example an IBM AT takes at "
             "most 16 MB and a GA-686BX at most 512 MB."},

        {"name": "videoCard", "display": "Video Card", "type": "enum",
         "options": [labels["videoCard"]["builtin"]]
                    + [labels["videoCard"][v["internal"]] for v in tables["video_cards"]],
         "default": labels["videoCard"]["vga"],
         "description": "The graphics card. 'builtin' uses the video hardware on the "
             "machine's own motherboard, where it has any. The list has "
             "every card PCem emulates, from MDA to a Voodoo 3. All of them "
             "are emulated in software."},
        {"name": "videoSpeed", "display": "Video Speed", "type": "enum",
         "options": tables["video_speeds"], "default": "default",
         "description": "How fast the card's bus is. 'default' leaves it to the card."},
        {"name": "voodoo", "display": "Voodoo Graphics", "type": "bool", "default": False,
         "description": "A 3dfx Voodoo Graphics or Voodoo 2 as a SEPARATE card added "
             "beside the 2D card above, which is how they were sold."},

        {"name": "soundCard", "display": "Sound Card", "type": "enum",
         "options": [labels["soundCard"][x["internal"]] for x in tables["sound_cards"]],
         "default": labels["soundCard"]["none"],
         "description": "The sound card in the machine."},
        {"name": "gameBlaster", "display": "Game Blaster", "type": "bool", "default": False,
         "description": "A Creative Game Blaster (CMS), in addition to the card above."},
        {"name": "gus", "display": "Gravis Ultrasound", "type": "bool", "default": False,
         "description": "A Gravis Ultrasound, in addition to the card above."},
        {"name": "ssi2001", "display": "SSI-2001", "type": "bool", "default": False,
         "description": "An Innovation SSI-2001, the PC card that carries the Commodore"
             " 64's SID sound chip."},

        {"name": "hddController", "display": "Hard Disk Controller", "type": "enum",
         "options": [labels["hddController"][h["internal"]] for h in tables["hdd_controllers"]],
         "default": labels["hddController"]["none"],
         "description": "The disk controller: MFM, ESDI, IDE, XT-IDE or SCSI. A machine"
             " with IDE on its motherboard does not need one here."},
        {"name": "hddGeometry", "display": "Hard Disk Geometry", "type": "enum",
         "options": ["Auto", "Custom"], "default": "Auto",
         "description": "'Auto' reads the disk's cylinders, heads and sectors from the "
             "image itself. It first uses the values in the image's own "
             "partition table, and then the image's length. This is right "
             "for any image made by PCem, DOS, Windows or this core. A .vhd "
             "file contains its geometry, and that is always used. 'Custom' "
             "shows the three numbers, for an image without partitions whose"
             " size is not a whole number of cylinders. A wrong geometry "
             "gives a disk that does not start, not an error message, so "
             "prefer Auto."},
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
         "description": "The physical drive in bay A. 'Auto' chooses the drive that "
             "fits the image given for this drive, by the image's size. An "
             "image of 360k or smaller gets a 5.25\" 360k drive, 720k a 3.5\" "
             "720k drive, 1.2M a 5.25\" 1.2M drive, 1.44M a 3.5\" 1.44M drive "
             "and 2.88M a 3.5\" 2.88M drive. With no image, or with an image "
             "whose size is not a standard format (an .fdi, .td0, .imd or "
             ".86f file contains its own geometry), Auto chooses a 3.5\" "
             "1.44M drive, the most common one. A drive chosen by name is "
             "always used. The drive really matters: PCem does not refuse a "
             "disk that the drive cannot read completely. It stops the head "
             "at the drive's last track, and the machine gets read errors."},
        {"name": "driveBType", "display": "Floppy Drive B", "type": "enum",
         "options": ["Auto"] + tables["fdd_types"], "default": "Auto",
         "description": "The physical drive in bay B. It works the same way as Floppy "
             "Drive A. With nothing in bay B, Auto chooses a 5.25\" 1.2M "
             "drive, the second drive a PC of the time usually had."},
        {"name": "bpbDisable", "display": "Disable BPB", "type": "bool", "default": False,
         "description": "Ignores the BIOS Parameter Block in the boot sector when "
             "working out a floppy disk's format. A few copy-protected disks"
             " need this."},

        {"name": "cdDrive", "display": "CD-ROM Drive", "type": "enum",
         "options": ["Auto", "Fitted", "None"], "default": "Auto",
         "description": "Whether the machine has a CD-ROM drive. 'Auto' adds one when "
             "the project has a disc for the CD-ROM drive and leaves it out "
             "when it has none, which is what almost everyone wants. "
             "'Fitted' gives the machine an empty drive, so software that "
             "looks for a drive finds one. 'None' never adds one."},
        {"name": "cdChannel", "display": "CD-ROM Channel", "type": "int",
         "exposedWhen": {"not": {"setting": "cdDrive", "is": "None"}},
         "default": 2, "min": 0, "max": 3,
         "description": "Which IDE position the CD-ROM drive has: 0 is primary master, "
             "1 primary slave, 2 secondary master and 3 secondary slave. PCs"
             " of the time had it at 2. The TASVideos late-1990s "
             "configuration uses 3, so that both hard disks can have the "
             "primary channel to themselves."},
        {"name": "cdSpeed", "display": "CD-ROM Speed", "type": "int",
         "default": 24, "min": 1, "max": 72,
         "exposedWhen": {"not": {"setting": "cdDrive", "is": "None"}},
         "description": "The drive's speed multiplier. It changes how long a read takes, "
                        "so it is part of the machine."},
        {"name": "cdModel", "display": "CD-ROM Model", "type": "enum",
         "options": ["pcemcd", "toshiba_xm_5602b"], "default": "pcemcd",
         "exposedWhen": {"not": {"setting": "cdDrive", "is": "None"}},
         "description": "Which drive model the machine reports that it has."},

        {"name": "mouseType", "display": "Mouse", "type": "enum",
         "options": tables["mice"], "default": "Microsoft 2-button mouse (serial)",
         "description": "Which mouse is plugged in. A machine with no PS/2 port needs a"
             " serial mouse. The Amstrad and Olivetti mice are built into "
             "those machines."},
        {"name": "mouseSensitivity", "display": "Mouse Relative Sensitivity",
         "type": "float", "default": 0.5,
         "description": "Every relative mouse movement is multiplied by this number "
             "before the machine receives it. The unit is the mickey, the "
             "mouse's own step of movement. It applies to Mouse X/Y and to "
             "the movement that Mouse Position X/Y causes. It changes how "
             "far the mouse has to travel, not where the pointer ends: an "
             "absolute position still lands where it says, but it takes this"
             " many times as many steps to get there. It is the same "
             "setting, with the same default, as in the DOSBox-X core."},
        {"name": "joystickType", "display": "Joystick", "type": "enum",
         "options": tables["joysticks"], "default": "Standard 2-button joystick(s)",
         "description": "What is plugged into the game port."}
        ,
        {"name": "lpt1Device", "display": "Parallel Port Device", "type": "enum",
         "options": ["none", "dac", "dss"], "default": "none",
         "description": "What is plugged into the first parallel port (LPT1): nothing, "
             "a Covox-style sound adapter (DAC), or a Disney Sound Source."},

        {"name": "fpsNumerator", "display": "Frames Per Second (numerator)", "type": "int",
         "default": 100, "min": 1, "max": 1000,
         "description": "How much time of the emulated PC one frame is. 100/1 means a "
             "frame is 10 ms, which is what every published PCem movie uses."
             " It is part of the movie: the same inputs at a different rate "
             "give a different run."},
        {"name": "fpsDenominator", "display": "Frames Per Second (denominator)", "type": "int",
         "default": 1, "min": 1, "max": 1000, "description": "See Frames Per Second (numerator)."},
    ]

    # The per-device settings go beside the card they belong to rather than in a
    # block of their own, because the grid is read top to bottom and "Sound Card
    # Address" means nothing until you know which sound card.
    def insert_after(name, prefixes):
        i = next(k for k, s in enumerate(settings) if s["name"] == name)
        picked = [s for s in device_settings if s["name"].startswith(prefixes)]
        settings[i + 1:i + 1] = picked
        return picked

    placed = []
    placed += insert_after("voodoo", ("voodoo",))
    placed += insert_after("videoSpeed", ("videoMemory", "videoBilinear",
                                         "videoScreenFilter", "videoRenderThreads",
                                         "videoRecompiler"))
    placed += insert_after("ssi2001", ("soundCard", "oplEmulator", "awe32"))
    missed = [s["name"] for s in device_settings if s not in placed]
    if missed:
        raise SystemExit(f"device settings not placed in the grid: {missed}")

    presets = build_presets(labels)

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
            "mnemonics": mnemonics_for(buttons),
            "axes": with_headers([
                {"name": "Mouse X", "min": -128, "max": 127, "neutral": 0},
                {"name": "Mouse Y", "min": -128, "max": 127, "neutral": 0},
                {"name": "Mouse Position X", "min": 0, "max": 65535, "neutral": 32768},
                {"name": "Mouse Position Y", "min": 0, "max": 65535, "neutral": 32768},
                {"name": "Joystick 1 X", "min": 0, "max": 65535, "neutral": 32767},
                {"name": "Joystick 1 Y", "min": 0, "max": 65535, "neutral": 32767},
                {"name": "Joystick 2 X", "min": 0, "max": 65535, "neutral": 32767},
                {"name": "Joystick 2 Y", "min": 0, "max": 65535, "neutral": 32767},
            ]),
        },
        "systemId": "PC",
        "systemNames": SYSTEM_NAMES,
        "settings": settings,
        "presets": presets,
        "firmware": firmware,
    }

    out_path.write_text(json.dumps(config, indent=2) + "\n")
    print(f"{len(machine_opts)} machines, {len(tables['video_cards'])} video cards, "
          f"{len(tables['sound_cards'])} sound cards, "
          f"{len(tables['hdd_controllers'])} hdd controllers, "
          f"{len(buttons)} buttons, {len(settings)} settings, "
          f"{len(presets)} presets, {len(firmware)} firmware -> {out_path}")


if __name__ == "__main__":
    main()

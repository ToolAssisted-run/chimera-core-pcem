#!/usr/bin/env python3
"""Validate the presets this core declares, and resolve one into a settings map.

A preset is a bundle of setting values the wizard writes INTO the settings and
is then finished with (~/chimera/docs/project.md, "Configuration presets"). That
design is what makes a bad preset cheap to ship and expensive to notice: Apply
puts the values in the grid, a key the core misdeclared is IGNORED and only
named on the status line, and nothing else ever looks. Nobody reads a status
line. So the declaration is checked here instead.

Seven things, each of which is a way a preset can be wrong without anything
failing:

  1. every key in values[] is a declared setting - the frontend drops the rest;
  2. every value is legal for that setting's declared type, options and range;
  3. every preset SETS THE MACHINE, and to one of the 93 boards this build of
     PCem declares. The board IS the preset - "Packard Bell PB570" is the whole
     point of picking one - and a preset that leaves it alone silently builds
     its CPU, video card and sound card on top of whatever board happened to be
     selected, which is at best a machine nobody asked for and at worst a load
     refused for a CPU the board does not take;
  4. every id is unique, and every id and label is non-empty;
  5. no preset declares when[] while the package declares no machines[]. when[]
     gates a preset by MachineConfig; with no machines[] there is no
     MachineConfig, AppliesTo(null) is true and every preset is offered
     regardless. An inert declaration that reads as load-bearing is worse than
     no declaration;
  6. the board a preset builds is named in its label, because the label is all
     the selector shows and five presets that do not say which machine they are
     cannot be chosen between;
  7. the prose is ASCII, like the rest of this repo's prose.

A note on 3, because it was got WRONG here first and the way it went wrong is
worth keeping. ApplySelectedPreset does skip a key:

    if (pair.Key == RendererSetting || pair.Key == _cfg.MachineSetting) continue;

but `_cfg.MachineSetting` is the package's DECLARED machine chooser, the one
that goes with a machines[] array, and this package declares neither - so it is
null, "machine" never equals it, and `machine` is an ordinary setting the
wizard writes like any other. The first version of this file read that line
together with project.md's prose ("the machine setting and the renderer are not
a preset's to move"), believed it without checking what MachineSetting was for
THIS package, and then enforced the belief - and the Python stand-in that was
supposed to check the work was written from the same sentence, so it could not
disagree. That is gates.md mode E exactly. The frontend is the subject; a
stand-in for it has to be checked against it at least once, which
docs/PRESETS.md section 11 now records being done.

usage: check-presets.py <waterbox.config>
       check-presets.py <waterbox.config> --emit <preset id> <out settings.json>

--emit resolves a preset the way the wizard does - every declared setting at
its default, then the preset's values written over the top - and writes the
result as the "settings" file the guest reads. The gate's boot legs use it, so
what they boot is the preset and not a hand-typed copy of it that can drift.
"""
import json
import sys
from pathlib import Path

# What ApplySelectedPreset skips on Apply, without a word. RendererSetting is
# the literal "renderer"; the other is `_cfg.MachineSetting`, the package's
# declared machine chooser, which this package does not declare - so `machine`
# is NOT skipped here and is not in this tuple. See the note in the docstring.
SKIPPED_BY_THE_WIZARD = ("renderer",)

# The setting that names the board. Not special to the frontend - it is an
# ordinary enum - but special to a preset, which is worthless without it.
MACHINE_SETTING = "machine"


def declared(cfg):
    return {s["name"]: s for s in cfg.get("settings", []) if s.get("name")}


def value_problem(decl, value):
    """Why this value is not legal for this setting, or None."""
    name = decl["name"]
    kind = decl.get("type") or ("enum" if decl.get("options") else
                                "bool" if isinstance(decl.get("default"), bool) else
                                "int")
    if kind == "bool":
        if not isinstance(value, bool):
            return f"{name} is a bool and the preset gives {value!r}"
        return None
    if kind == "int":
        if isinstance(value, bool) or not isinstance(value, int):
            return f"{name} is an int and the preset gives {value!r}"
        if decl.get("min") is not None and value < decl["min"]:
            return f"{name}={value} is below its minimum {decl['min']}"
        if decl.get("max") is not None and value > decl["max"]:
            return f"{name}={value} is above its maximum {decl['max']}"
        return None
    if kind == "enum":
        opts = decl.get("options") or []
        if not isinstance(value, str):
            return f"{name} is an enum and the preset gives {value!r}, not a string"
        if value not in opts:
            near = [o for o in opts if o.lower() == value.lower()]
            hint = f" (did you mean {near[0]!r}? the match is exact)" if near else ""
            return f"{name}={value!r} is not one of its {len(opts)} options{hint}"
        return None
    return f"{name} has an unknown declared type {kind!r}"


def check(cfg):
    bad = []
    by_name = declared(cfg)
    presets = cfg.get("presets") or []
    machine = by_name.get("machine")
    machine_opts = (machine or {}).get("options") or []

    seen = set()
    for p in presets:
        pid = p.get("id") or ""
        where = f"preset {pid or '<no id>'}"
        if not pid:
            bad.append("a preset has no id, so nothing can name it in a log")
        elif pid in seen:
            bad.append(f"duplicate preset id {pid!r}")
        seen.add(pid)

        label = p.get("label") or ""
        if not label:
            bad.append(f"{where} has no label, so the selector would show its id")

        for field in ("label", "description"):
            text = p.get(field) or ""
            if any(ord(c) > 126 for c in text):
                bad.append(f"{where}: {field} is not ASCII")

        if p.get("when") is not None and not cfg.get("machines"):
            bad.append(f"{where} declares when[], which gates a preset by "
                       f"MachineConfig - but this package declares no machines[], "
                       f"so there is no MachineConfig and every preset is offered "
                       f"regardless. It would read as load-bearing and do nothing")
        for mv in p.get("when") or []:
            if machine is None:
                bad.append(f"{where} has a when[] but there is no machine setting")
            elif mv not in machine_opts:
                bad.append(f"{where} is for machine {mv!r}, which this core does "
                           f"not declare")

        values = p.get("values") or {}
        if not values:
            bad.append(f"{where} sets nothing")

        # THE BOARD. A preset that does not name one builds a CPU, a video card
        # and a sound card on top of whatever machine happened to be selected.
        board_value = values.get(MACHINE_SETTING)
        if machine is None:
            bad.append(f"{where}: there is no {MACHINE_SETTING!r} setting to set")
        elif board_value is None:
            bad.append(f"{where} does not set {MACHINE_SETTING!r}. The board IS the "
                       f"preset; without it the rest lands on whatever machine was "
                       f"already chosen")
        elif board_value not in machine_opts:
            bad.append(f"{where} builds machine {board_value!r}, which is not one of "
                       f"the {len(machine_opts)} boards this core declares")
        else:
            # "ga686bx - [Slot 1] Gigabyte GA-686BX" -> "Gigabyte GA-686BX"
            plain = board_value.split(" - ", 1)[-1].split("] ", 1)[-1]
            if plain not in (label or ""):
                bad.append(f"{where} builds {plain!r} but its label does not say so - "
                           f"and the label is all the selector shows")

        for key, value in values.items():
            if key in SKIPPED_BY_THE_WIZARD:
                bad.append(f"{where} sets {key!r}, which the wizard skips in "
                           f"silence - it would never reach the settings")
                continue
            decl = by_name.get(key)
            if decl is None:
                bad.append(f"{where} sets {key!r}, which is not a declared setting: "
                           f"Apply would drop it")
                continue
            problem = value_problem(decl, value)
            if problem:
                bad.append(f"{where}: {problem}")

    return bad, presets, by_name


def resolve(cfg, preset_id):
    """Every declared setting at its default, with the preset written over it -
    which is what the grid holds after Apply."""
    by_name = declared(cfg)
    out = {}
    for name, decl in by_name.items():
        if "default" in decl:
            out[name] = decl["default"]
    for p in cfg.get("presets") or []:
        if p.get("id") != preset_id:
            continue
        for key, value in (p.get("values") or {}).items():
            if key in SKIPPED_BY_THE_WIZARD:
                continue
            out[key] = value
        return out
    raise SystemExit(f"no preset with id {preset_id!r}")


def main():
    cfg = json.loads(Path(sys.argv[1]).read_text())

    if len(sys.argv) > 2 and sys.argv[2] == "--emit":
        Path(sys.argv[4]).write_text(json.dumps(resolve(cfg, sys.argv[3]),
                                                indent=1) + "\n")
        return 0

    bad, presets, by_name = check(cfg)
    for b in bad:
        print("  BAD:", b)
    keys = sorted({k for p in presets for k in (p.get("values") or {})})
    print(f"{len(presets)} presets over {len(by_name)} settings, "
          f"{len(keys)} distinct keys set: {len(bad)} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

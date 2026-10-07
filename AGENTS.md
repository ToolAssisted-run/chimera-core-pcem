# AGENTS.md - PCem core for Chimera

This repository builds the PCem IBM PC emulator as a sandboxed guest for
Chimera (https://github.com/ToolAssisted-run/chimera), a frontend for
tool-assisted speedruns. It produces one file, `pcem.chimeraCore`: the guest
binary plus the declarations Chimera reads. Chimera ships no cores and
downloads nothing; a user puts that file in Chimera's `Cores` folder.
`docs/BUILDING.md` has the detail behind every command here.

## Layout

- `extern/pcem/` - upstream PCem (the TASEmulators fork), a pinned submodule.
  Never commit in it.
- `patches/` - numbered patches against `extern/pcem`.
- `waterbox/apply-patches.sh` - applies the series to the submodule tree.
- `waterbox/build-guest.sh` - builds the guest, `build/wbx/pcem.wbx`.
- `waterbox/build-run-wbx.sh` - builds the harness, `build/wbx/run-wbx`.
- `waterbox/build-package.sh` - regenerates the declaration, builds the
  guest, writes the package.
- `waterbox/run-gate.sh` - the gate. It builds the package itself.
- `waterbox/pcem-driver.c`, `pcem-input.c`, `pcem-hdd.c` - the driver that
  replaces PCem's platform layer, the input wire, the writable disk overlay.
- `waterbox/waterbox.config` - GENERATED at package time (see Rules).
- `waterbox/file_slots.json`, `waterbox/default_keybinds.json` - the files the
  project wizard asks for, and the default bindings.
- `tools/gen-*.py` - the generators. `tools/check-*.py` - the checkers the
  gate and the package script call. `tools/firmware-sha1.json` - ROM hashes,
  an input to generation.
- `tools/build.sh`, `tools/run.sh`, `tools/driver.c` and friends - the native
  measurement harness of the early milestones. Not part of the core build.
- `docs/PLAN.md` - the design. Its firmware tables are read by the build.
  The other files in `docs/` take one subject each.
- `build/` - all build output (gitignored).

## Set up the build environment

Linux x86-64. CI uses `ubuntu-latest`.

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev

git submodule update --init

CHIMERA=/absolute/path/to/chimera     # a Chimera checkout, branch main, cloned --recursive
MB=$CHIMERA/extern/chimera-common-minibox

[ -f "$MB/build/meson-linux/build.ninja" ] || meson setup "$MB/build/meson-linux" "$MB"
meson compile -C "$MB/build/meson-linux"
[ -f "$MB/build/meson-cpp/build.ninja" ] || meson setup "$MB/build/meson-cpp" "$MB" -Dguest_cpp=true
meson compile -C "$MB/build/meson-cpp"
```

The gate's engine legs need `$CHIMERA/build/meson-linux/chimera-run`, and the
contract tests need Chimera's solution built with the .NET SDK 8.0: see
`docs/BUILDING.md`.

## Build

```sh
sh waterbox/build-package.sh -m "$MB" -r "$CHIMERA"
```

That one command regenerates `waterbox/waterbox.config`, applies the patches,
builds `build/wbx/pcem.wbx`, checks it and writes
`$CHIMERA/build/Cores/pcem.chimeraCore`. A failed guest build stops it. The
pieces, when you want one alone:

```sh
sh waterbox/apply-patches.sh
MINIBOX_DIR="$MB" sh waterbox/build-guest.sh
MINIBOX_DIR="$MB" sh waterbox/build-run-wbx.sh
```

## Install the core into Chimera

`build-package.sh -r "$CHIMERA"` already put the package in
`$CHIMERA/build/Cores/`, the cores folder of a source checkout. For a release
bundle, copy the `.chimeraCore` file into the `Cores` folder beside
`Chimera.exe` (or the folder set in File > Core Manager > Change folder...);
Refresh List rescans. The same file works on Linux and on Windows.

A package built by hand stamps `<commit>+local` (`-dirty+local` when the tree
has changes, and the patched submodule counts as one). It is for testing.
Published packages come only from CI.

## Test before you commit

```sh
CHIMERA_ROOT="$CHIMERA" MINIBOX_DIR="$MB" PCEM_ROMS=/path/to/PCem-ROMs ./waterbox/run-gate.sh
```

It must end with `0 failed`. Read the `skipped` count too:

- WITH a PCem ROM set at `PCEM_ROMS` (default `$HOME/PCem-ROMs`), every
  machine leg runs: the engine run, determinism, input, settings, presets,
  disks, save data. This is the gate that counts for a change to the driver,
  the patches or the settings.
- WITHOUT one, six legs run: the package builds, the harness builds,
  `core.wbx is not stale`, the declaration is legal, the presets are legal,
  and the negative control for the presets. This is all CI runs. It boots no
  machine.

Machine ROMs are not distributed. If you have none, run the ROM-less gate and
say in your report that no machine leg ran. Do not call that a green gate for
a change in emulation behaviour.

## Rules of this repository

- Never commit inside `extern/pcem`. A change to PCem is a numbered patch in
  `patches/` (paths relative to `extern/pcem`, `git apply` format), taking the
  next number. `sh waterbox/apply-patches.sh` must then print
  `already applied: all <n> patches` on your tree. The submodule shows as
  modified while patched; that is expected.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The ROM legs of the gate check it; a change that breaks one is a
  bug, not a gate problem.
- Run the gate before committing. A new leg needs a negative control that
  runs as part of the gate: show it fails when the thing it checks is broken.
  Assert that the machine is alive before comparing anything.
- Do not hand-edit `waterbox/waterbox.config`: `build-package.sh` rewrites it
  from `tools/gen-config.py`, `tools/gen-firmware.py` and
  `tools/gen-waterbox-config.py`. The firmware list comes from the tables in
  `docs/PLAN.md` (sections 5.1 to 5.4) and `tools/firmware-sha1.json`, so
  editing those changes the package.
- Do not add a slot for a PCem `.cfg`: the machine is composed from settings.
- Never commit a ROM, firmware, BIOS, disk image or game. Never add network
  access.
- PCem is GPL-2.0 with no "or later" grant. Never link anything GPL-3.0.
- `waterbox/run-gate.sh` must stay executable (git mode 100755): the workflow
  runs it directly. The other scripts are called through `sh`. Keep the
  scripts under `waterbox/` POSIX `sh`: no bash-isms.
- Documentation prose is plain ASCII.
- Commit messages: `type(scope): a sentence saying what is now true`, with
  the Chimera issue in parentheses when there is one, for example
  `fix(presets): ...` or `feat(cpu): ... (chimera#194)`. Types in use: feat,
  fix, docs, ci. The body says what was measured and gives the gate's
  `passed, failed, skipped` line.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every step, option and gate leg in detail.
- `docs/PLAN.md` - the design, the firmware enumeration, the licence.
- `docs/HDD.md`, `docs/CMOS.md`, `docs/PRESETS.md` - disks, CMOS, presets.
- `.github/workflows/chimera.yml` - the authoritative build recipe.
- In the Chimera checkout: `docs/porting-a-core.md`, `docs/gates.md` (how a
  gate goes green on a broken thing) and `docs/core-manager.md`.

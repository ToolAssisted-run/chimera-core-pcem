# Building the PCem core

This repository builds PCem's emulation core as a sandboxed guest and packs
it, with its declarations, into one file: `pcem.chimeraCore`. Chimera loads
that file. The steps below are the ones `.github/workflows/chimera.yml` runs on
a fresh clone on a public runner. Where this document and the workflow
disagree, the workflow is right.

Placeholders used below:

- `<chimera>`: the absolute path of a checkout of
  https://github.com/ToolAssisted-run/chimera
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, the miniBox submodule
  (the sandbox host and the guest toolchain)

Commands run from the root of this repository unless a step says otherwise.
The helper scripts are run through `sh`, as the gate and the package script
run them.

## Requirements

- Linux on x86-64. CI uses GitHub's `ubuntu-latest` runner. Cores are built on
  Linux only. The package that comes out runs on Linux and on Windows.
- The packages CI installs (one list, for the core and for Chimera):

  ```sh
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev
  ```

- The .NET SDK 8.0, for Chimera. The workflow uses `actions/setup-dotnet@v4`
  with `dotnet-version: '8.0'`. By hand, Chimera's README gives
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`.
- The workflow pins no compiler version: it uses the gcc that
  `build-essential` installs. `build-package.sh` records the gcc, binutils and
  musl versions and the miniBox commit in the package's `build.json`.
- The scripts of this repository download nothing. The guest toolchain is
  built from the miniBox sources (see "Build miniBox").
- No ROM is needed to build. Machine ROMs are needed to run a machine, and so
  for most of the gate (see "Run the gates").

## Get the sources

This repository, with its submodule (`extern/pcem`, the TASEmulators fork of
PCem). The workflow uses `actions/checkout@v6` with `submodules: true`, which
is:

```sh
git clone https://github.com/ToolAssisted-run/chimera-core-pcem.git
cd chimera-core-pcem
git submodule update --init
```

A Chimera checkout. The workflow checks out branch `main` with every submodule
(`submodules: recursive`):

```sh
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

Where the scripts look for Chimera and miniBox when nothing says:

- `waterbox/run-gate.sh`: `CHIMERA_ROOT`, else `$HOME/chimera`; `MINIBOX_DIR`,
  else `extern/chimera-common-minibox` under that.
- `waterbox/build-package.sh`: `../chimera` beside this repository, then
  `$HOME/chimera`; miniBox from `MINIBOX_DIR`, else inside that checkout.
- `waterbox/build-guest.sh` and `waterbox/build-run-wbx.sh`: `MINIBOX_DIR`,
  else `$HOME/chimera/extern/chimera-common-minibox`.

Set `CHIMERA_ROOT` and `MINIBOX_DIR`, as CI does, when the checkout is
anywhere else.

## Build miniBox

Two builds of miniBox: the host library, and the C++ guest toolchain
(`-Dguest_cpp=true`). These are the workflow's commands:

```sh
mb=<minibox>
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

The workflow keeps `build/meson-linux` and `build/meson-cpp` in
`actions/cache@v4`, keyed on the miniBox commit and its `meson.build`. By hand,
that is simply not deleting the two directories: the `[ -f ... ] ||` guards
skip the set-up when they exist.

## Build the core

In CI none of the commands of this section and the next is run directly: the
gate (`waterbox/run-gate.sh`) builds the package and the harness itself. They
are here for building without the gate.

### Patches

PCem is the submodule `extern/pcem`, pinned and never committed to. This
repository's changes to it are the numbered patches in `patches/`, applied to
the submodule's working tree by:

```sh
sh waterbox/apply-patches.sh
```

It takes no options. `build-guest.sh` runs it first, so a build needs no
manual step. It judges the series as a whole:

- a pristine tree gets every patch, in order (`applied: <patch>`);
- a tree that already carries the whole series is left alone
  (`already applied: all <n> patches`);
- anything in between is an error that names the files and prints the command
  that starts again from the submodule's HEAD;
- the series is tried on a scratch copy first, so a series that does not apply
  never half-patches the real tree;
- a submodule that is not checked out is an error that prints the command to
  run.

`PCEM_TREE` points the script at another checkout (how the script itself is
tested). While patched, `git status` shows `extern/pcem` as modified. That is
expected.

### The guest core

```sh
MINIBOX_DIR=<minibox> sh waterbox/build-guest.sh
```

It takes no options. It applies the patches, takes PCem's source list from
upstream's own `Makefile.am` (dropping the wx GUI, OpenAL, ALSA MIDI,
networking, host CD-ROM and the file-backed hard disk), generates PCem's
default NVRAMs into a C table (`tools/gen-nvr-defaults.py`), compiles
everything with miniBox's guest toolchain together with the driver in
`waterbox/`, and links `build/wbx/pcem.wbx`. Objects are kept in
`build/wbx/obj`.

Environment: `MINIBOX_DIR`; `WBX_DIR` (output directory, default `build/wbx`);
`PCEM_SRC` (another PCem source tree, in which case the patches are not
applied).

### The harness

```sh
MINIBOX_DIR=<minibox> sh waterbox/build-run-wbx.sh
```

This builds `build/wbx/run-wbx`, a host program that drives `pcem.wbx`
through miniBox's host library directly, with no engine and no package. The
gate's digest-stream, drive-type and save-data legs run through it. It links
`libminiboxhost.a` from `<minibox>/build`, so build miniBox first.

### No native reference in the gate

The gate does not compare against a native build: it runs the sandboxed core
through `run-wbx` and through Chimera's engine. The native measurement harness
of the early milestones (`tools/build.sh`, described in the README and in
`docs/M1A.md`) is not built by CI and is not part of the gate.

## Build the package

```sh
sh waterbox/build-package.sh -m <minibox> -r <chimera>
```

Options:

- `-m <miniBox dir>`: the miniBox checkout. Default: `MINIBOX_DIR`, then
  `<chimera root>/extern/chimera-common-minibox`.
- `-r <chimera root>`: the Chimera checkout. Default: `../chimera`, then
  `$HOME/chimera`. The script stops if it finds none.

This repository's script has no `-o` option: the package always goes into a
Chimera checkout.

What it does, in order:

1. Regenerates `waterbox/waterbox.config`, in place, from PCem's own sources
   and from the firmware enumeration in `docs/PLAN.md`
   (`tools/gen-config.py`, `tools/gen-firmware.py`,
   `tools/gen-waterbox-config.py`; intermediate files in `build/gen/`).
2. Runs `tools/check-declaration.py` and stops on a declaration the engine
   would refuse.
3. Deletes `build/wbx/pcem.wbx` and runs `build-guest.sh`. A failed guest
   build stops the package: an old binary is never packaged.
4. Runs miniBox's `source/guest/check-wbx.sh` on the new binary (the guest
   rules).
5. Stages it as `core.wbx` with `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, the licence texts (from `waterbox/package-licenses.json`)
   and `build.json` (what built the package).
6. Stamps the version into the staged `waterbox.config`.
7. Writes `<chimera>/build/Cores/pcem.chimeraCore`, twice, and stops if the
   two files differ. It prints `package sha1 <hash>`: the package's SHA-1 is
   the core's identity.
8. Removes `<chimera>/build/CoreCache/pcem-*`.

The version stamp:

- CI sets `CORE_VERSION` to the commit it built (`${{ github.sha }}`), and the
  script uses it as given.
- Without `CORE_VERSION`, the stamp is `<commit>+local` (12 hex digits), or
  `<commit>-dirty+local` when `git diff --quiet HEAD` finds changes. The
  patched `extern/pcem` counts as a change, so a package built by hand
  normally reads `-dirty+local`.
- `versionDate` is the commit's date in UTC, never the build's.

A hand-built package is for testing. Chimera's publish step refuses a version
carrying `+local` or `-dirty`.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets there as a file.

- In a Chimera SOURCE checkout the cores folder is `<chimera>/build/Cores/`.
  `build-package.sh -r <chimera>` (and so the gate) has already written
  `pcem.chimeraCore` there. Start Chimera with `build/ChimeraMono.sh` on Linux
  or `build\Chimera.exe` on Windows.
- In a release bundle, copy the `.chimeraCore` file into the `Cores` folder
  beside `Chimera.exe`, or into the folder chosen in
  File > Core Manager > Change folder...
- File > Core Manager lists what is in the folder. Refresh List rescans it.

The same package file works on Linux and on Windows: Chimera's sandbox
(miniBox) runs the guest inside it on either.

Released packages are on this repository's Releases page
(https://github.com/ToolAssisted-run/chimera-core-pcem/releases): a rolling
`dev` release on every green push to `main`, and a dated `nightly-YYYY-MM-DD`
release from the scheduled run when `main` moved since the last one.

## Run the gates

### Core gate

The workflow builds Chimera before the gate (in `<chimera>`):

```sh
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

The first three commands give `<chimera>/build/meson-linux/chimera-run`, the
engine runner the gate's engine legs use. The `dotnet build` is for the
contract tests below.

Then, from this repository:

```sh
CHIMERA_ROOT=<chimera> MINIBOX_DIR=<minibox> ./waterbox/run-gate.sh
```

Options: `-r <chimera root>` and `-m <minibox dir>`. The miniBox default is
worked out from `CHIMERA_ROOT` before the options are read, so `-r` alone does
not move it: give both, or set both variables as CI does.

The gate builds what it tests: it runs `build-package.sh` (the package lands
in `<chimera>/build/Cores/`) and `build-run-wbx.sh`. Its work directory is
`build/gate/`, emptied at the start. Each leg prints `PASS`, `FAIL` or `SKIP`;
it ends with `<n> passed, <m> failed, <k> skipped` and exits non-zero on any
failure, or with `NOTHING RAN` when nothing passed.

Every PC machine needs its BIOS, and no ROM is distributed, so the gate has
two shapes.

WITHOUT a ROM set (this is what CI runs), these legs run and nothing else:

- `package builds`
- `the harness builds`
- `core.wbx is not stale`: `build/wbx/pcem.wbx` is newer than
  `waterbox/pcem-driver.c`
- `the declaration is legal`: `tools/check-declaration.py`
- `the presets are legal`: `tools/check-presets.py`
- `a misdeclared preset is caught`: the negative control of the leg above

It then prints `SKIP every leg that runs a machine` and stops. Nothing in CI
boots a machine, compares two runs, or loads a savestate.

WITH a ROM set, every leg after that runs. The gate looks for a PCem ROM
folder at `PCEM_ROMS` (default `$HOME/PCem-ROMs`). The files it names are
`ga686bx/6BX.F2a`, `voodoo3_3000/3k12sd.rom`, `awe32.raw`, `mda.rom`,
`wy700.rom` and `8x12.bin`, plus the ROMs of the five presets. Those legs
prove:

- the package runs 1500 frames in Chimera's engine and draws a picture;
- two runs give the same per-frame digest stream, and the stream notices a
  slower CPU (negative control);
- the mouse axes: the declared wire matches the driver, the arithmetic, and a
  position reaching the machine;
- declared CPU options load, and a CPU the machine does not take is refused by
  name;
- the floppy drive Auto setting, and a named drive overriding it;
- every preset reaches the machine and reaches a BIOS screen;
- hard disks: a guest write leaves as save data, an exported disk seeds the
  next run, the overlay is sparse, the disk rewinds with a savestate, and the
  geometry settings;
- the CPU Clock setting, measured by the guest;
- a project's disk exported through the engine.

Some of those legs need more and report `SKIP` without it:

- `firmware resolves by hash`: `PCEM_ROM_COLLECTION`, a folder of ROMs to scan.
- `Alley Cat boots and animates` (and `Auto fits a 180 KB disk`):
  `PCEM_ALLEYCAT`, the game's floppy image, and `ibmpc/pc102782.bin` and
  `mda.rom` under `PCEM_ROM_COLLECTION`.
- `a 4 GiB seed costs nothing`: `PCEM_XP_IMAGE`, a hard disk image (default
  `build/xp/winxp-desktop.img`, see `docs/XP.md`).
- `an IBM AT POSTs without asking for F1`: `ibmat/62x0820.u27`,
  `ibmat/62x0821.u47` and `ibm_vga.bin` under `PCEM_ROMS`.
- `a project's disk exports through the engine`: `chimera-run` and the package.

`PCEM_ROM_COLLECTION` and `PCEM_ALLEYCAT` default to paths on the author's
machine. Set them, or expect those legs to skip.

The ROM legs also use `cc`, `cpp`, `as` and `ld` (the disk probes are
assembled by `tools/make-hdd-probe.sh`).

### Chimera's contract tests

Chimera's own tests, run against the packages in a cores folder: readable,
built for a guest ABI this frontend runs, a working factory, binding only
declared buttons, stamping a version. They need no ROM. In `<chimera>`:

```sh
CHIMERA_CORES_DIR=<chimera>/build/Cores \
dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

## Files the core needs at run time

No ROM, firmware or disk is in this repository or in the package. The user
provides:

- Disks, through the project wizard's slots (`waterbox/file_slots.json`), at
  least one of the first four:
  - floppy images for drive A: (`.img`, `.ima`, `.dsk`, `.fdi`, `.86f`,
    `.td0`, `.imd`), any number;
  - floppy images for drive B:, the same formats;
  - CD-ROM images (`.iso`, or `.cue` with its tracks beside it);
  - a hard disk image (`.img`, `.hdd`, `.vhd`, `.hdi`), which seeds a writable
    drive C:;
  - a second hard disk image, drive D:.
- Firmware: every machine needs its BIOS, and video cards, sound cards and
  disk controllers with a ROM need theirs. `waterbox/waterbox.config` declares
  each file by name, with its size and SHA-1 where known, required when the
  Machine (BIOS), Video Card, Sound Card or Hard Disk Controller setting names
  that device. Chimera matches a file by its hash first.

PCem's own default NVRAM contents (`extern/pcem/nvr/default`) are compiled
into the core. They are PCem's data, not user files.

## Troubleshooting

- `no miniBox guest sysroot at ...` (`build-guest.sh`): the `meson-cpp` build
  of miniBox is missing, or `MINIBOX_DIR` points elsewhere.
- `no libminiboxhost.a under <minibox>/build` (`build-run-wbx.sh`): the
  `meson-linux` build of miniBox is missing.
- `extern/pcem is not checked out` (`apply-patches.sh`): run the command it
  prints.
- `extern/pcem is partly patched` (`apply-patches.sh`): it names the files and
  prints the reset command. That command discards edits made in the tree: turn
  them into a patch first.
- `the series does not apply to the submodule's HEAD at <patch>`: the
  submodule was moved without rebasing the patches.
- `declaration is not legal` (`build-package.sh`): read what
  `tools/check-declaration.py` printed.
- `guest build failed`, `COMPILE FAILED`, `LINK FAILED`: the package stops
  there on purpose. A guest build that failed was once packaged as the
  previous binary.
- A change that does not seem to reach the binary: `build-guest.sh` recompiles
  a source only when it is newer than its object. A changed header or compiler
  flag recompiles nothing. Remove `build/wbx/obj` to rebuild everything.
- `SKIP every leg that runs a machine`: there is no folder at `PCEM_ROMS`.
- The scripts under `waterbox/` are POSIX `sh`. Keep them so: the guest build
  that failed silently did so on a bash-ism under dash.
- PCem is GPL-2.0 with no "or later" grant. Nothing GPL-3.0 may be linked in.

# chimera-core-pcem

An IBM-PC-compatible Chimera core built on PCem.

**Local only.** There is no remote and nothing is pushed, on Sergio's
instruction, until he says otherwise.

## Layout

| Path | What |
|---|---|
| `docs/PLAN.md` | the M0 evaluation: the five questions, the firmware enumeration, the preset design, the milestones, the risks |
| `docs/UPSTREAM.md` | PCem or 86Box, and what to take from 86Box |
| `docs/M1A.md` | the native speed measurement and the green/amber/red decision |
| `docs/M1B.md` | the sandbox JIT spike: does the recompiler work in miniBox, and what it costs |
| `docs/XP.md` | Windows XP installed and measured to the desktop, and what actually costs |
| `docs/HDD.md` | writable hard disks: the sparse overlay, the save-data export, derived geometry, and what a state costs |
| `docs/CMOS.md` | PCem's default NVRAMs shipped inside the core, and the CMOS made to fit the machine the settings built |
| `extern/pcem` | upstream `TASEmulators/pcem`, pinned (see PLAN.md, "The repository") |
| `configs/` | PCem `.cfg` files reproducing the TASVideos published configurations |
| `patches/` | full-file patches against `extern/pcem` |
| `tools/` | the measurement harness: headless native and miniBox-guest builds of the emulation core, sharing a 28-symbol driver |
| `src/` | scratch clones used to read the sources. **Not in git** |

## State

- M0, M0b: done, in `docs/`.
- **M1a: done, GREEN** (2026-09-20). Native Windows XP on the published
  TASVideos machine sustains 149-155% of real time on this desktop at its
  worst measured phase. `docs/M1A.md` has the numbers and the list of
  what is not proven.
- **M1b: done, GREEN** (2026-09-20). The whole emulation core builds for
  the miniBox guest and runs bit-identically to native; the sandbox costs
  15%; the 120 MiB RWX arena contributes 1.3 dirtied pages per epoch, not
  30,720. `docs/M1B.md`. One amber finding about epoch rate, and a latent
  upstream `%rbx` bug found and fixed (`patches/0002`).
- **Windows XP installs and runs** (2026-09-20). Idle desktop 365%, XP's
  graphical installer 88% median, cold boot floor 18.7%. Every dip is
  PCem's recompiler meeting code it has never seen - not the Voodoo 3's
  rasteriser and not host disk I/O, both of which were tested and ruled
  out. `docs/XP.md`.
- **Hard disks are writable** (2026-09-21). The slot's image seeds a sparse
  write overlay instead of being written in place; writes leave through the
  save-data channel and an exported image goes straight back into the slot.
  A 4121 MB Windows XP image costs **nothing** until the guest writes, a boot
  to the logon screen costs 7 MiB of a 339 MB state, and an install would cost
  about 1.1 GiB - said out loud in `docs/HDD.md` rather than discovered later.
  Geometry is derived from the image (Auto used to write three zeros, which is
  a drive of no sectors). Gate 26/26. An upstream NULL-`atapi` crash fixed on
  the way.
- **AT-class machines no longer stop at POST** (2026-09-21). PCem's own
  `nvr/default` ships inside the core and the CMOS is edited to describe the
  machine the settings built, so "161-System Options Not Set-(Run SETUP)" and
  its F1 are gone. `docs/CMOS.md`; an AT with an MFM disk still needs its
  drive type, and that is written down there.
- M2 (the machine, native) continues.

## Building the M1a harness

Needs gcc, g++ and python3. Does **not** need wxWidgets, SDL or OpenAL -
the platform layer is replaced.

```sh
tools/build.sh                 # -> build/m1a/pcem-bench
```

Then put a PCem ROM set at `build/m1a/run/roms`, PCem's own
`extern/pcem/nvr/default/*.nvr` at `build/m1a/run/nvr`, a `.cfg` from
`configs/` at `build/m1a/run/configs`, and:

```sh
tools/run.sh build/m1a/run/configs/winxp.cfg 400000 20000
```

`--keys "<ms>:<xt scancode>:<hold ms>,..."` drives the keyboard;
`--shot-every-ms` writes PPM screenshots that `tools/ppm2png.py` converts.

## Licence

PCem is GPL-2.0 (**not** "or later" - see PLAN.md section 8). Nothing
GPL-3.0 may ever be linked in. No ROM or firmware file is in this
repository and none ever will be.

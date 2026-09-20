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
| `extern/pcem` | upstream `TASEmulators/pcem`, pinned (see PLAN.md, "The repository") |
| `configs/` | PCem `.cfg` files reproducing the TASVideos published configurations |
| `tools/` | the M1a measurement harness: a headless build of the emulation core with a 27-symbol driver |
| `src/` | scratch clones used to read the sources. **Not in git** |

## State

- M0, M0b: done, in `docs/`.
- **M1a: done, GREEN** (2026-09-20). Native Windows XP on the published
  TASVideos machine sustains 149-155% of real time on this desktop at its
  worst measured phase. `docs/M1A.md` has the numbers and the list of
  what is not proven.
- M1b (the JIT inside miniBox) is next.

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

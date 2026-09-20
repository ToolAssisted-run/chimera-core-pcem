# M1a: is Windows XP fast enough, natively, on this machine?

**Status: GREEN.** The TASVideos Windows XP machine runs at a **median
of 146% of real time** on this desktop, natively, with the dynamic
recompiler on. Its pure-CPU floor is 149%. The only dips below real time
- as low as 75% - are in the one phase that does sustained host disk
writes, which is the phase the port replaces with a memory overlay. XP
stays the headline and the plan proceeds.

Everything below was run, not reasoned. Anything not observed is marked
**UNPROVEN**.

---

## 1. What was measured, and how

The brief asked for stock PCem built natively and the TASVideos XP
configuration reproduced. Two deviations, both forced and both narrowing
rather than widening what the number covers:

1. **No wxWidgets and no OpenAL on this machine, and no root to install
   them.** So instead of PCem's wx front end, the emulation core was
   built exactly as upstream's own `Makefile.am` lists it, and only the
   platform layer was replaced. The source list is *derived from*
   `src/Makefile.am` at build time (`tools/build.sh`) so it cannot drift
   from upstream, and the diff against upstream's list is precisely: the
   `wx-*` GUI files, `soundopenal.c`, `midi_alsa.c`, the networking files
   and the host CD-ROM ioctl - dropped; `wx-thread.c` (pure pthread on
   non-Windows) and `cdrom-ioctl-dummy.c` - kept. **No emulation source
   file was modified.**

2. **The speed figure is computed directly rather than read off PCem's
   status bar.** `speed% = emulated_ms / wall_ms * 100`, where the
   emulated milliseconds are the argument to the TASVideos fork's own
   `runpc(uint64_t ms)` (`src/pc.c:508`). This is the same quantity the
   status bar reports and it needs no window.

**The platform layer is 27 symbols.** That is the entire surface between
PCem's emulation core and its host, enumerated by linking the core with
nothing and reading the undefined references:

```
SOUNDBUFLEN  create_bitmap  destroy_bitmap  dir_exists  emulation_state
endblit  get_pcem_path  givealbuffer  givealbuffer_cd  hline  inital
initalmain  joystick_poll  joystick_state  keyboard_poll_host  midi_write
mouse_buttons  mouse_get_mickeys  mouse_poll_host  pcem_key
set_window_title  startblit  stop_emulation_now  timer_freq  timer_read
updatewindowsize  warning
```

Plus exactly one function pointer, `video_blit_memtoscreen_func`
(`src/video.c:733`). This is a significant M2 finding in its own right:
PLAN.md section 2 estimated the platform layer at 12,950 lines and
"cleanly separated"; it is in fact 27 symbols and one pointer, and the
measurement driver that satisfies all of them is 260 lines
(`tools/driver.c`).

**Host:** Intel Core Ultra 7 265KF, 20 cores, WSL2 on Windows.
Single-threaded throughout - PCem's emulation is one thread and the five
still-threaded video cards are not in this configuration.

**Build:** `-O3 -fcommon -msse2 -DRELEASE_BUILD`, x86-64 dynarec backend
(`codegen_backend_x86-64*.c`).

## 2. The machine

The TASVideos Windows XP configuration (`configs/tasvideos-winxp.cfg`),
reproduced from PLAN.md section 6.1:

| | |
|---|---|
| Machine | `ga686bx` - [Slot 1] Gigabyte GA-686BX |
| CPU | Intel Pentium II/450 (`cpu_manufacturer = 0`, `cpu = 6`), FPU builtin |
| Dynarec | **on** (`cpu_use_dynarec = 1`) |
| RAM | 262144 KB = 256 MB |
| Video | `v3_3000` - 3DFX Voodoo 3 3000 |
| Sound | `sbawe32` - Sound Blaster AWE32 |
| Disk | IDE, 63/16/8374 = 4121 MB |
| CD | PCemCD, 72x, image |
| Mouse | PS/2 2-button |

The POST screen confirms the machine is the real one: "Award Modular BIOS
v4.51PG", "Intel 440BX/ZX AGPSet BIOS for 6BX V.F2a", "PENTIUM II CPU at
450MHz", "Memory Test : 262144K OK".

**Firmware.** Sergio's `~/PCem-ROMs` set was used. Three files carry
published TASVideos MD5s and **all three match exactly**:

| File | MD5 measured | MD5 published | |
|---|---|---|---|
| `ga686bx/6BX.F2a` | `8ea65e0c1c4934e9a0105bb3fe33a9e9` | same | match |
| `voodoo3_3000/3k12sd.rom` | `ecc400ecd2fd7e5e4efd11f4bf837afd` | same | match |
| `awe32.raw` | `30b76c45ca0712418239d2b15c65881a` | same | match |
| `ibm_vga.bin` | `2057a38cb472300205132fb9c01d9d85` | same | match |
| `86c764x1.bin` | `fbc57ef320053c50d9034ef493abda4d` | same | match |

SHA-1s for the first three also match the two the wiki publishes
(`637e1b38...`, `2825b702...`, `6ac3c131...`).

**Media.** `en_windows_xp_home_with_service_pack_3_x86_cd_x14-92413.iso`
under `TAS/roms/system/wxp/` hashes `a22030df1988445436f300bc29c32dd2`,
which is byte-for-byte the ISO TASVideos publishes for the XP
configuration. The machine, the ROMs and the install media are all the
published ones.

## 3. The numbers

Speed is quoted as a percentage of a real Pentium II/450, i.e. 100% =
real time. `effective_MHz` is the Pentium II clock this host sustains.

| Workload | Speed | Effective |
|---|---|---|
| POST, memory test, IDE detect | 168 - 800% | 0.76 - 3.6 GHz |
| Award BIOS setup screens | 148 - 240% | 0.67 - 1.1 GHz |
| XP Setup loading drivers from CD | up to 3334% | mostly `HLT`; not evidence of anything |
| XP Setup "Welcome to Setup", spinning | 148.9 - 154.9% | 0.67 - 0.70 GHz |
| **XP Setup: create partition, NTFS format, copy files** | **74.9 - 180%, median 143%** | 0.34 - 0.81 GHz |

Over the whole 635 s of emulated time driven so far, in 259 one-second
wall-clock windows: **median 145.7%, minimum 74.9%, maximum 3334%.**
222 of the 259 windows are below 200%, so the median is not being held up
by idle phases.

Two rows need reading carefully.

**The spinning prompt is the honest CPU-only floor.** A prompt that polls
in a tight loop never halts, so every emulated cycle costs host work. It
is the worst case for speed% and the best case for the dynarec's code
cache, and over 70 s it did not drop below 148.9%. Arithmetic: 450 MHz at
150% is 675 million emulated cycles a second on a host around 5 GHz, i.e.
roughly **7.4 host cycles per emulated Pentium II cycle** - an ordinary
dynarec ratio, which is a sanity check that the figure is real rather
than an artefact of a halted guest.

**Sixteen windows fall below 100%, as low as 74.9%, and all sixteen are
inside the NTFS format and file-copy phase.** That phase is the only one
that does sustained host disk I/O: PCem's `hdd_file.c` writes the image
with plain `fseeko64`/`fwrite` (`hdd_file.c:131-194`), and here that is a
sparse file on ext4 inside WSL2's VHDX. ~~**It is a reasonable expectation, and UNPROVEN, that this dip does not
survive the port**~~ - **that expectation was wrong, and `XP.md` section
3b has the measurement.** The same workload with the disk image on
**tmpfs**, so there is no host disk I/O at all, dips identically: floor
17.0% against 18.1%, and the same count of sub-100% windows. The cause is
not the host's disk. It is PCem's recompiler compiling code it has never
seen, which correlates with the dips at Spearman -0.86 and is a thousand
times more active in the slow windows than the fast ones.

*(Measured on the cold-boot workload. This particular NTFS format and
file copy was not itself re-run on tmpfs, so for this phase the host-I/O
explanation is unsupported rather than strictly disproven.)*

## 4. The branch

**Green.** Native XP runs above real time with a median of about 1.45x,
and the only excursions below real time are in the one phase that does
sustained host disk writes - the phase the port removes. Windows 95
(Pentium II/233, i.e. half the emulated clock) therefore has roughly 3x
headroom and needs no separate defence.

The headroom is what M1b will spend. The sandbox will not be free: the
RWX dirty-page faults over a 120 MiB JIT arena (PLAN.md 7.5b) are the
named unknown, and 1.5x is what there is to lose before XP stops being
real time.

## 5. What is NOT proven

Stated plainly, because "fixed by construction" is not fixed:

- **XP at the desktop was not measured.** Text-mode Setup was driven
  all the way through - Welcome, EULA, create partition, NTFS quick
  format, and into the file copy - by scripted keystrokes, and that is a
  real mixed CPU, disk and CD workload on the real kernel and HAL. It is
  not the desktop. The install cannot be finished unattended without a
  Windows XP product key, which this session does not have and will not
  invent. **The desktop figure is UNPROVEN and needs a key from Sergio.**
- **The graphical phase of Setup was not reached**, so the Voodoo 3's
  software rasteriser has never been the bottleneck in any number above.
  Everything measured is text mode. **UNPROVEN.**
- **The interpreter fallback was not priced.** `cpu_use_dynarec = 0` was
  not run, because the dynarec branch is green and the fallback is only
  interesting if it were not.
- **Windows 95 and the Late 90s DOS preset were not run.** They are
  strictly lighter than what was measured (half the clock, or DOS
  instead of NT) and the green branch does not depend on them. Worth
  doing at M2 when the DOS presets exist anyway.
- **Nothing about the sandbox was measured.** M1a is a native
  measurement and says nothing about miniBox. That is M1b.
- **Determinism was not tested.** Two runs of the same configuration
  were not compared for identical output. M2's gate does that.

## 6. Incidental findings

Four things found while doing this, each of which would have cost time
later:

1. **`RELEASE_BUILD` is not cosmetic.** Without it, `pclog()`
   (`src/pc.c:90`) writes every log line unconditionally, and a
   measurement taken from a default `./configure` build would be
   measuring `fputs`. It also crashes immediately in a headless build
   because `pclogf` is `NULL` until something opens it.
2. **`paths_init()` and `sound_init()` live in the platform layer**
   (`wx-sdl2.c:534` and `:561`), not in `initpc()`/`resetpchard()`. A
   driver that does not call them gets a segfault in `romfopen()` and a
   `NULL` `outbuffer` in `sound_poll()` respectively. `pc_main()`'s order
   is: `paths_init`, `initpc`, `resetpchard`, `sound_init`, then video.
3. **`fatal()` writes to `pclogf` without checking it.** Any fatal error
   in a driver that has not set it becomes a segfault in `fputs`, which
   hides the actual message. The driver sets `pclogf = stderr`.
4. **PCem's Award BIOS defaults do not boot from CD**, and the CMOS is
   the only place that setting lives. The measurement driver grew a
   scripted keyboard (`--keys "<ms>:<xt scancode>:<hold ms>"`, writing
   `pcem_key[]`) to drive the BIOS setup, and `savenvr()` at exit to
   persist it. Both are things the Chimera core needs anyway - and it is
   a concrete argument for shipping a per-machine default `.nvr` with a
   sane boot order, which PLAN.md section 4.3 already proposes.

## 7. How to reproduce

```
tools/build.sh                       # builds build/m1a/pcem-bench
# roms/ -> a PCem ROM set, nvr/, configs/, disks/ under build/m1a/run/
tools/run.sh build/m1a/run/configs/winxp.cfg 400000 20000
```

`tools/driver.c` is the whole platform layer; `tools/ppm2png.py` turns
the `--shot-dir` screenshots into PNGs, which is how every screen quoted
above was read.

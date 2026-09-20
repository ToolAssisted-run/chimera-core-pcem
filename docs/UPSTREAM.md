# Which upstream: PCem or 86Box

## What this is

Risk 7 of `PLAN.md` said PCem upstream has been dead since 2020, that 86Box
is its living fork, and that 86Box was deliberately not evaluated. Sergio
asked for that question to be settled before M1 and before any speed spike,
on the grounds that starting on the dead one would be expensive to undo.

This is that evaluation, against the same headings `PLAN.md` used, so the
comparison is like for like. `PLAN.md` is the baseline; this document says
which of its rows change and by how much.

Read here: `86Box/86Box` at `c0d1a56` (2026-09, version 7.0), cloned to
`src/86box` - scratch, not a submodule, not a repository. Plus
`TASEmulators/86Box` (branch `v4.2.1st`, head `780d42ea`) through the GitHub
API, the 86Box documentation at `86box.readthedocs.io`, and GitHub
discussion `86Box/86Box#3117`. Same convention as before: **READ** means
quoted from or derived from one of those; **UNCHECKED** means reasoned, not
run. Nothing was built.

## The answer first

**Stay on PCem - and shrink M1 to a native measurement that can be done in
two days rather than two weeks.**

86Box is the better-maintained, broader, more actively developed emulator by
a wide margin, and on four of the nine headings it wins. But the entire
justification for this core is Windows XP (PLAN.md section 1), and on
exactly that question the evidence runs hard the other way:

- 86Box's own FAQ: *"We strongly discourage the use of 86Box to run Windows
  XP or newer Windows operating systems"*, and *"It is almost always better
  to run Windows XP in a different virtualizer or emulator."*
- 86Box's own maintainer, asked why 86Box is slower than PCem on a Pentium
  II: *"86Box prioritizes emulation accuracy, which means it can be heavier
  on the host than pcem."*
- A PCem-versus-86Box report on a Ryzen 5 5600X: PCem *"almost constantly
  100% of running speed"* at a Pentium II 350, 86Box dropping frames on both
  its recompilers.

Against that, PCem wins the two headings that decide the port's feasibility
- **the clock seam** (one, versus five, one of which is `rdtsc` in inline
assembly that miniBox cannot trap) and **threading** (five surviving device
threads, versus roughly fourteen with the only conversion work three major
versions stale).

**The same evidence also downgrades the proposal as a whole**, and that is
the more important outcome of this round. The 86Box FAQ publishes host
single-thread benchmark thresholds - *"~4000 = Pentium II 300 MHz"* - and
the TASVideos Windows XP configuration is a **Pentium II/450**. That is at
or beyond what a *native* build achieves on a fast modern desktop, and a
waterbox guest is slower still. So M1 changes from "port enough to measure
the JIT in the box" to "**measure stock PCem 17+st-1 natively on this
machine first**", which is a two-day job, and the decision it makes is
whether Windows XP is in scope at all.

**"Neither" remains live**, and section 10 says exactly what would make it
the answer.

---

## 1. Licence

| | PCem | 86Box |
|---|---|---|
| Statement | `COPYING` is the GPLv2 text; `README.md` "PCem is licensed under GPL v2.0"; **no per-file headers at all** | `COPYING` is the GPLv2 text; `README.md:81` *"86Box is released under the GNU General Public License, version 2 **or later**"*; per-file headers throughout |
| Effective terms | **GPL-2.0-only** | **GPL-2.0-or-later** |
| Optional deps | none | munt, FluidSynth, Ghostscript, Discord Game SDK - *"distributed under their respective licenses"* (`README.md:83`) |
| Vendored | DOSBox dbopl (GPL-2.0+), Nuked-OPL3 (**no header**), reSID-FP (GPL-2.0+), minivhd (MIT), slirp (BSD) | minitrace, crcspeed, cJSON, ini, libchdr + zstd, munt (vendored under `src/sound/munt`), and more |

**Verdict: 86Box, marginally.** `-or-later` is strictly more permissive than
PCem's `-only`, and 86Box's per-file headers make an audit cheaper than
PCem's header-less tree. Both can have `package-licenses.json` written
honestly.

Two traps in 86Box that PLAN.md's licence section would have to name, both
of which are avoidable:

- **Ghostscript is AGPL**, and would poison the package if linked. It is
  not: `src/printer/prt_ps.c:94-99` declares `gsapi_*` as function
  pointers and loads the library at run time, and the whole printer
  subsystem is optional. The port compiles it out.
- **Discord Game SDK is proprietary.** `option(DISCORD ... ON)` is on by
  default (`CMakeLists.txt:133`) and must be turned off, along with
  `FLUIDSYNTH`, `MUNT`, `SOUNDCANVAS`, `OPENAL`, `RTMIDI`, `VNC`, `QT` and
  `MINITRACE`. All are plain CMake options; none is entangled.

Neither project verifies ROM hashes; neither ships ROMs in the emulator
repository. 86Box maintains a **separate, actively updated ROM repository**
(`86Box/roms`, 100 MB, 738 stars, last pushed 2026-09-19, licence
`NOASSERTION`). Chimera would not ship it - firmware is always the user's -
but it makes filling in a hash table trivial in a way PCem's situation does
not, which matters for PLAN.md risk 2. That is an 86Box advantage that
survives even if PCem is chosen, since many ROM files are the same dumps.

## 2. Renderer

**Both keep it, and the advantage carries over almost unchanged.**

86Box rasterises every card in C into `monitors[i].target_buffer =
create_bitmap(2048, 2048)` of `uint32_t` (`src/video/video.c:818`,
`src/include/86box/video.h:112-117`) - the same shape as PCem's `buffer32`.
`video_blit_memtoscreen_monitor()` (`video.c:433`) hands a rectangle to a
platform-installed `blit_func`. No GPU, no llvmpipe, no SDL inside the
emulation core; SDL and Qt live in `src/unix` and `src/qt`.

One difference, and it is against 86Box: **the blit is asynchronous**.
`video_blit_memtoscreen_monitor()` fills a `blit_data_t` and wakes a
dedicated `blit_thread` per monitor (`video.c:414-451`), which is what the
TASVideos 86Box branch's commit `1f1da750` ("Make video blit single
threaded", 1 addition, 60 deletions) removes. `MONITORS_NUM` is 2, so a
port also clamps to one head.

**Verdict: tie on substance, PCem ahead as shipped**, because PCem's TAS
fork already ships without the blit thread and 86Box's equivalent lives on
a stale branch.

## 3. The frame

**86Box is slightly ahead, and did it without a fork.**

`pc_run()` (`src/86box.c:2020-2046`) is structurally identical to PCem's
`runpc()`, and upstream already parameterises the slice:

```c
cpu_exec((int32_t) cpu_s->rspeed / (force_10ms ? 100 : 1000));
```

`force_10ms` is an ordinary config value (`src/config.c:333`), so 86Box's
native cadence is **1000 slices a second of 1 ms each**, or 100 of 10 ms.
The driving loop (`src/unix/sdl_main.c:212-243`) accumulates
`drawits += (new_time - old_time)` and calls `pc_run()` once per
millisecond of host time.

The TASVideos branch went the last step and made it an argument - commit
`8cb0ebd2`, "Make rendering and events on the same thread, and variable
fps":

```c
void pc_run(uint32_t ms) {
    uint32_t cycles_to_run = ((uint64_t)ms * cpu_s->rspeed) / 1000ULL;
    startblit();
    cpu_exec(cycles_to_run);
```

which is the same change, by the same author, as PCem's `13a3d2e`. Three
files, 54 additions.

**Verdict: 86Box, marginally** - a finer default slice and the knob already
upstream. This is a small win and it does not move the decision.

## 4. Savestates

**Sergio's assumption is wrong, and I checked it carefully because the
answer changes the shape of the port: 86Box has no savestates either.**

`grep -rlnE "save_?state|load_?state|snapshot|serialize"` over `src/`
returns ten files, and every one is a false positive, verified by reading
each:

- `src/cpu/386_common.c` - `SMM_SAVE_STATE_MAP_SIZE`, `smram_save_state_p5`
  and friends: the x86 **System Management Mode** SMRAM state save area,
  which is emulated hardware, not emulator state.
- `src/video/vid_pgc.c:2753` - *"Here is how the real PGC serializes
  coords"*.
- `src/device/unittester.c:54-56` - `UT_CMD_CAPTURE_SCREEN_SNAPSHOT`, the
  unit-tester device's screen capture commands (see below).
- `src/video/vid_mga.c:542` - Matrox DMA `iload_state` variables.
- `src/cpu/x87_ops.h:188` - a comment about `FXSAVE` serialising a stale
  MM register.
- The rest are zstd, munt, and device-internal state machines.

There is no state serialiser, no chunk format, no version field, nothing.
86Box's documentation does not mention savestates, and the FAQ does not
either.

**Verdict: tie, and it is the good tie.** PLAN.md counted "nothing to
fight" as a PCem advantage; it is simply an advantage of this whole family
of emulators. Nothing changes: the waterbox snapshots the guest and neither
upstream has a competing mechanism to neuter (unlike rpcs3, PPSSPP and
DOSBox-X, all of which did).

**One genuine 86Box asset found on the way**: `src/device/unittester.c` is
an emulated I/O device for automated testing. A guest program writes the
string `86Box` to a trigger port, then issues `UT_CMD_CAPTURE_SCREEN_
SNAPSHOT`, `UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE`,
`UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE` and `UT_CMD_EXIT`. There is also
a `tests/` tree (cdrom, device, floppy, machine). PCem has neither. For a
gate built on hand-written `.com` programs - which PLAN.md's test-content
section proposes - this is exactly the right primitive, and it is worth
porting the *idea* to a PCem-based core even if 86Box is not chosen. It is
a small, self-contained ISA device.

## 5. Threading

**PCem wins this clearly, and it is one of the two headings that decide the
question.**

`grep -n "thread_create\b"` over 86Box master, excluding the thread
wrapper itself, returns 39 sites. Removing the frontend, networking,
gdbstub, VFIO, minitrace and the MIDI synthesizers (all compiled out) leaves
the emulation-relevant set:

| Where | 86Box master | PCem after the TAS fork |
|---|---|---|
| Video blit | `video.c:837` blit thread per monitor | none (converted) |
| S3 | `vid_s3.c:11684` fifo thread | none (converted, `55f0fdb`) |
| S3 ViRGE | `vid_s3_virge.c:6191` fifo thread | none (converted, `b4e28d6`) |
| Voodoo | `vid_voodoo.c:1242-1253` and `:1374-1385`: a fifo thread **plus up to four render threads**, twice (two chips) | none (converted) |
| ATI Mach64 | `vid_ati_mach64.c:2180` | still threaded |
| Matrox Mystique | `vid_mga.c:6919` + a DMA mutex | still threaded |
| PGC | `vid_pgc.c:3393` | still threaded |
| ET4000/W32p | (86Box's is not threaded) | still threaded |
| TGUI9440 | (86Box's is not threaded) | still threaded |
| CD audio | `sound.c:612`, `:1000` `sound_cd_thread` | none (converted) |
| Floppy drive sound | `sound.c:1042` `sound_fdd_thread` | not emulated at all |
| Hard disk drive sound | `sound.c:1099` `sound_hdd_thread` | not emulated at all |
| **Total to convert** | **~14** | **5** |

The TASVideos 86Box branch `v4.2.1st` is four commits by Clement Gallet,
dated 2024-09-16, totalling roughly 530 additions and 950 deletions:

| Commit | What |
|---|---|
| `8cb0ebd2` | rendering and events on one thread, `pc_run(uint32_t ms)` |
| `1f1da750` | video blit single-threaded |
| `780d42ea` | threading removed for S3 ViRGE and Voodoo (11 files) |
| `02ef34ef` | Qt build fix |

So the answer to "is anyone's single-threading work reusable" is **partly,
and it is stale**. That branch is based on 86Box **4.2.1** (August 2024).
Current master is **7.0**. Three major versions and two years of the most
actively developed PC emulator in existence sit between them, including a
rework of the S3 and ViRGE code, the media-history subsystem, the NVIDIA
RIVA cards and a great deal else. Rebasing four commits that delete 950
lines of threading across that gap is not a mechanical merge; it is redoing
the work with a reference. And even done, it leaves S3, Mach64, Mystique,
PGC and the three sound threads untouched.

Note also what miniBox does to a thread that is left in place: green threads
are cooperative and deterministic, but **futex timeouts are ignored**, so
the FIFO waits (`thread_wait_event(..., timeout)`) that both emulators use
deadlock, and the sandbox kills the guest. Leaving a threaded card in is not
an option in either emulator; it is only a question of how many have to go.

## 6. The clock seam

**PCem wins this clearly, and this is the other deciding heading.**

PCem has exactly one seam in emulation: `time_internal_sync()`
(`src/rtc.c:227`) and its TC8521 twin. 86Box has five, found by grepping
host-clock calls outside the platform directories:

| # | Where | What it does | Severity |
|---|---|---|---|
| 1 | `src/nvr.c:319` `time(&now)` + `localtime` | seeds the CMOS clock at boot | same as PCem; one setting fixes it |
| 2 | `src/utils/random.c:48-62` | **`rdtsc` in inline assembly**, used as the randomness source by `random_generate()` | **severe**; see below |
| 3 | `src/utils/random.c:88-93` `random_init()` -> `srand(RDTSC())`, called from `86box.c:1533` | seeds libc `rand()` from the host TSC, poisoning every `rand()` in the tree | severe |
| 4 | `src/sound/snd_sn76489.c:266` `srand(time(NULL))` | re-seeds `rand()` from the host clock at SN76489 reset, then uses it for the noise counters | moderate |
| 5 | `src/video/nv/nv_rivatimer.c:188-233` `clock_gettime(CLOCK_REALTIME)` | a host wall-clock timer that **fires emulated device callbacks**, driven from `pc_run()` line 2031 via `rivatimer_update_all()` | severe, but confined to the NVIDIA RIVA cards |

Plus `src/disk/minivhd/minivhd_util.c:152` `srand(time(0))` for VHD UUIDs
(creation only, and PCem's vendored minivhd has the same).

Why #2 is the sharp one: **miniBox does not trap `rdtsc`.** There is no
CR4.TSD interception anywhere in the sandbox - that was established as a
verified negative finding while writing PLAN.md - so a guest that executes
`rdtsc` reads the real host TSC and desyncs silently, with no error, no
guest death, and nothing in any log. 86Box executes it on every
`random_generate()`. The consumers that matter with networking off are the
bus mouse's IRQ readback (`src/device/mouse_bus.c:218`) and a UM866x Super
I/O register (`src/sio/sio_um866x.c:240`) - both read by guest software.

All five are patchable, and the patches are small. But "five seams, one of
which the sandbox physically cannot catch for you" against "one seam" is a
real difference in how much of the determinism story is proved by
construction versus proved by audit - and this project has paid for
construction-based reasoning twice this week.

On the good side of the ledger: 86Box's RAM is `memset(ram, 0x00, ...)`
(`src/mem/mem.c:2792`), same as PCem, so uninitialised-memory randomness is
not a problem in either.

## 7. Machines, video cards and firmware

**86Box subsumes PCem comprehensively, and that is a cost as much as a
benefit.**

| | PCem v17 | 86Box 7.0 | Ratio |
|---|---|---|---|
| Machines | 93 | **521** (`machines[]`, `src/machine/machine_table.c`, 26,142 lines) | 5.6x |
| CPU families / entries | 33 / 290 | **89 / 444** | 2.7x / 1.5x |
| Video cards | 47 | **150** (`video_cards[]`, `src/video/vid_table.c`) | 3.2x |
| Sound card entries | 20 | **104** | 5.2x |
| **Distinct ROM files** | **~200** | **1,412** | **7x** |
| Total source lines | 251,539 | **1,052,777** | 4.2x |

The 1,412 is exact and cheap to get - `grep -rho '"roms/[^"]*"' src/ | sort
-u` - and it breaks down as 1,047 machine BIOS files, 215 video, 58 SCSI,
41 sound, 27 HDD, 7 network, 5 floppy, 2 RTC, 2 printer, 2 memory card.
Machine ROMs are named in each `machine_*_init()` as
`bios_load_linear("roms/machines/<id>/<file>", ...)`, and 86Box has a
`bios_only` mode (`src/mem/rom.c:720-723`) in which `bios_load()` returns
`rom_present(fn1) && rom_present(fn2)` - so the per-machine requirement is
discoverable exactly as it was for PCem, just seven times over.

**What of PLAN.md's firmware table carries over to 86Box:** the *method*
entirely, and a large fraction of the *files*, since 86Box inherited PCem's
machines and cards. What does not carry over:

- Path layout: PCem's `ga686bx/6BX.F2a` becomes `roms/machines/ga686bx/
  6BX.F2a` - two directory levels instead of one, so the flat-VFS mapping
  in PLAN.md section 5.6 needs a slightly longer id, nothing more.
- The character-generator ROMs (PLAN.md 5.4) are 86Box's too, under
  `roms/video/`, and must still be declared - the same silent-failure trap.
- The ten published hashes (PLAN.md 5.5) still apply to the same dumps.
- **1,412 firmware entries is a different kind of object from 200.** A
  `waterbox.config` with that many conditional entries is megabytes of
  JSON that the wizard evaluates on every settings change, and 1,412
  unknown hashes to establish. The honest answer for an 86Box port would be
  to ship a **curated subset** of machines - which throws away most of the
  coverage advantage that motivated 86Box in the first place.

**Verdict: 86Box on coverage, PCem on tractability.** If the reason to build
a PC core were breadth of machines, 86Box would win outright. It is not;
the reason is Windows XP and Linux, and both emulators reach those with a
handful of machines.

## 8. The dynarec, and the near-call finding

**Same lineage; 86Box's is the better-kept copy; and yes, they fixed the
bug - correctly, and it is not a cost.**

86Box ships two recompilers:

- `src/codegen` - the PCem v13-era one, and **the default on x86-64**
  (`CMakeLists.txt:152-156`: `NEW_DYNAREC` is forced ON only on arm64 and is
  an `option(... OFF)` elsewhere; `src/CMakeLists.txt:198-215` selects the
  directory).
- `src/codegen_new` - the "PCem v15" recompiler, default on arm64, and the
  direct descendant of the `codegen_*` tree PLAN.md read in PCem 17.

The specific question. PCem's `call()`
(`src/codegen_backend_x86-64_ops.c:24`) declares `diff` as `uintptr_t` and
tests `diff >= -0x80000000 && diff < 0x7fffffff`, which can never be true,
so every call it emits is `MOV R9, imm64; CALL R9`. **86Box has the same
function in both backends and both are fixed**, independently and
correctly:

```c
/* src/codegen_new/codegen_backend_x86-64_ops.c:30-47 */
intptr_t diff;
diff = (intptr_t) (func - (uintptr_t) &block_write_data[block_pos + 5]);
if (diff >= -0x80000000LL && diff < 0x7fffffffLL) { ...CALL rel32... }
else { ...MOV R9, imm64; CALL R9... }

/* src/codegen/codegen_ops_x86-64.h:25-42 - the one actually used on x86-64 */
intptr_t diff = (intptr_t) (func - (uintptr_t) &block->data[block_pos + 5]);
if (diff >= -0x80000000LL && diff < 0x7fffffffLL) { ...CALL rel32... }
else { ...MOV RAX, imm64; CALL RAX... }
```

`intptr_t` and `LL` literals on both sides: the near path is live, and the
far path is still there. **This is a small point in 86Box's favour, not
against it.** Correct code either way, faster when the arena happens to land
within 2 GiB of the guest image, and correct when it does not. Nothing in
the sandbox breaks: miniBox's guest base is fixed and its arena placement is
deterministic, so whichever branch is taken is taken identically every run,
and the JIT contents are savestated as guest memory as before. The only
thing to be aware of is that the emitted **bytes** now depend on where the
arena landed relative to the guest image, so a change to
`memoryLayoutMiB` changes them - which is already true of a savestate, whose
ELF hash is part of the state.

Two further 86Box improvements over PCem's allocator, both small and both
real:

- **The arena comes from a platform function.** `plat_mmap(size,
  executable, &large)` (`src/unix/sdl_plat_unix.c:365-384`) does
  `mmap(0, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON|MAP_PRIVATE,
  **-1**, 0)` and optionally `madvise(MADV_HUGEPAGE)`. PCem calls `mmap`
  directly from `codegen_allocator.c` with **fd 0** rather than -1, which
  PLAN.md flagged as an unverified sandbox risk. 86Box's is both correct
  and owned by the platform layer a Chimera driver replaces - so the
  question disappears instead of needing a patch.
- **There is a guard on the arena size.** `codegen_allocator.c:94-104`
  defines `CODEGEN_ALLOCATOR_MAX_POOL_BYTES` as 128 MiB on arm64 and 2 GiB
  on x86-64 and `_Static_assert`s the pool against it with the message
  *"exceeds this architecture's direct-branch range"*. Somebody has thought
  about the +/-2 GiB question.

Arena sizes, for the state-size estimate in PLAN.md 7.5: 86Box's default
x86-64 recompiler embeds `uint8_t data[2048]` in each `codeblock_t`
(`src/codegen/codegen.h`) with `BLOCK_SIZE 0x10000`, so the executable
arena is roughly **142 MiB** (PCem: 120 MiB) plus a 1 MiB hash table.
Broadly the same order; no change to the conclusion.

## 9. Speed, and Windows XP

**This is the heading that decides the whole question, and it is not close.**

Neither emulator has a published benchmark this document could verify by
running. What the sources say, quoted:

**86Box's own FAQ** (`86box.readthedocs.io/en/latest/usage/faq.html`), under
"What is the top VM configuration my system will handle?", gives host
single-thread benchmark thresholds:

> ~4000 = Pentium II 300 MHz
> ~3400 = Pentium II 233 MHz
> ~2600 = Pentium 200 MHz
> ~1600 = Pentium 75 MHz
> ~700  = 486DX2 66 MHz

and, under "Can I use 86Box to run a Windows XP system?":

> We strongly discourage the use of 86Box to run Windows XP or newer
> Windows operating systems. [...] only some of the newest AMD (Ryzen 5000
> series or better), Intel (Core 12th generation or better), and Apple (M2
> or better) processors are capable of meeting the bare minimum Windows XP
> requirements under 86Box's full system emulation. [...] It is almost
> always better to run Windows XP in a different virtualizer or emulator.

and, on the Pentium III:

> In short, no. Newer CPUs are way too powerful and even the top-end
> systems that are currently on the market are not nearly performant enough
> to be able to emulate them at usable speeds.

**GitHub discussion `86Box/86Box#3117`, "Performance of 86Box vs PCem on
Pentium II".** The reporter, on a Ryzen 5 5600X emulating a Pentium II 350:
*"With PCem, I have almost constantly 100% of running speed, but on 86Box
(ODR and NDR) I always had drop, especially when sounds are played."* The
maintainer jriwanek: 86Box *"prioritizes emulation accuracy, which means it
can be heavier on the host than pcem."* A contributor: *"the fastest I can
emulate (tested on hundreds of configs) is a P2 266 in DOS or p2 233 in
Win9x"*, and *"there are only a couple consumer CPU's capable of running a
p2 350."*

Against that, **PCem's position on XP is TASVideos' published, verified
Windows XP configuration**: a Gigabyte GA-686BX with a **Pentium II/450**,
256 MB, a Voodoo 3 3000 and an AWE32, with an installation movie whose
output hashes are published. Nobody has done the equivalent for 86Box; the
TASVideos wiki has no 86Box page at all.

**Verdict: PCem, decisively, on the only question that justifies the core.**

**And the same evidence is bad news for the proposal generally.** The
TASVideos XP machine is a P2/450 and 86Box's own scale puts a P2/300 at
~4000 single-thread - a number a Ryzen 5 5600X does not reach. PCem is
faster, and the one direct report has it at ~100% for a P2/350 on that same
5600X, so a P2/450 on a current high-end desktop is plausible but not
demonstrated, and a waterbox guest is slower again by an unmeasured margin.
PLAN.md called the speed question "unmeasured"; it is still unmeasured, but
it is no longer open-ended - there is now published evidence that the target
machine sits at the edge of what is possible natively.

---

## 10. Recommendation, and what M1 becomes

**PCem.** Not because it is the better emulator - it is not - but because
the port's two hardest problems are the clock seam and threading, PCem is
ahead on both by a wide margin, and the one thing that justifies building
the core at all is the one thing 86Box is worst at and tells users not to
do.

### What of PLAN.md survives unchanged

Everything except the milestones. Specifically:

- Section 1 (the DOSBox-X case), section 3 (libTAS versus the waterbox),
  sections 4.1-4.7 (slots, the sparse disk overlay, NVRAM and flash as save
  data, the settings shape, input, video and audio declaration, memory
  layout), section 5 (the firmware enumeration), section 6 (the preset
  design via `machines` + `settingOverrides`), sections 7.1-7.5 (the five
  questions), and section 8 (licences).
- One correction to section 8: PCem is **GPL-2.0-only**, which PLAN.md
  already says; this evaluation confirms it against 86Box's explicit
  "or later" and strengthens the warning that nothing GPL-3.0 may ever be
  linked in.
- One correction to section 7.5b: the near-call observation stands as a
  fact about PCem, but the framing "fortunate for a guest at a distant
  fixed base" should be read as "an accident that happens to be harmless".
  86Box's fixed version is the right code, and if a PCem port ever wants
  the rel32 path it should take 86Box's three-line fix rather than leave
  the dead branch in place. That is a GPL-2.0-compatible borrow.

### What changes

**M1 shrinks and moves first.** It was "two weeks, build enough to measure
the JIT in the sandbox". It becomes:

- **M1a - the native speed measurement (2 days, before anything else).**
  Build stock PCem `17+st-1` on this machine. Reproduce the published
  TASVideos Windows XP configuration. Measure the emulator's own speed
  percentage at the desktop and under a representative workload, with the
  dynarec on, and record the host CPU. Do the same for the Windows 95
  configuration (Pentium II/233) and the Late 90s DOS one (Pentium II/450
  in DOS, which is a much lighter load than XP). **This is now the
  decision point**, and it needs no port work at all:
  - **Native XP at or near 100%** -> the original plan proceeds, M1b
    (the JIT in the sandbox) runs as written, XP stays the headline.
  - **Native XP well below 100%** -> XP leaves the scope, and the core is
    re-justified on Windows 95/98, Linux, post-Voodoo-1 3D and the non-IBM
    PCs. That is still more than DOSBox-X does, but it is a smaller prize
    and the decision to build should be taken again on those terms, not
    inherited.
  - **Native Windows 95 also below 100%** -> stop. The answer is
    "neither", and DOSBox-X keeps the DOS catalogue.
- **M1b - the JIT in the box (1 week, only if M1a is green or amber).**
  As PLAN.md wrote it: a guest build of the CPU, memory and recompiler
  under miniBox's musl toolchain; does the arena allocate, does generated
  code execute, and what do the RWX dirty-page faults cost. Plus the
  one-line `mmap` fd fix that 86Box does not need.
- **M2 through M5 unchanged**, except that M2 should additionally port
  86Box's unit-tester device idea (section 4 above) as the gate's
  screen-verification primitive, and that M2's ROM-hashing step can use the
  public `86Box/roms` repository as a cross-check for the dumps the two
  emulators share.

**The nine-week estimate does not move for PCem**, and M1a makes the first
two days cheaper and far more decisive than the fortnight it replaces.

### If it had been 86Box

For completeness, since the question was asked: an 86Box-based port would
keep PLAN.md's shape but would cost roughly **14 to 16 weeks** rather than
nine, made up of:

- **+3 weeks** on threading: rebasing four stale commits across three major
  versions, then converting S3, Mach64, Mystique, PGC and three sound
  threads that nobody has touched.
- **+1 week** on the four extra clock seams, most of it on proving the
  `rdtsc` and `random_generate()` removal is complete, because the sandbox
  will not tell you when you miss one.
- **+2 weeks** on firmware: 1,412 entries, a curation decision about which
  of 521 machines to expose, and the hash work that goes with it.
- **-1 week** saved on the frame (already parameterised), the allocator
  (already platform-owned and correctly written) and the test device.
- and a 4.2x larger tree to keep rebased against a fast-moving upstream.

The trade would be a living upstream and five times the machines, in
exchange for the two properties the sandbox cares about most and the speed
on the only workload that matters.

## 11. What would change this answer

- **86Box's XP performance improves materially**, or someone publishes a
  verified 86Box XP configuration. Both the FAQ text and discussion #3117
  are current as of this writing; re-check annually.
- **PCem's fork dies too.** `TASEmulators/pcem` is one person's, and its
  last commit is a backport *from* 86Box. If it stops, the dependency on
  a 2020 codebase becomes the dominant risk and the calculation is
  re-run - most likely in 86Box's favour, because by then the speed
  question will have been settled one way or the other by M1a.
- **Chimera gains a way to declare firmware in bulk** (a generated side
  file, or conditions over a machine table rather than 1,412 entries). That
  would remove one of 86Box's three cost blocks and is worth knowing if the
  question is ever reopened.
- **M1a comes back red.** Then neither is the answer, and the DOSBox-X core
  remains Chimera's PC story.

## 12. What in this document is NOT verified

- **Nothing was built or run.** No 86Box build, no PCem build, no
  measurement. Every speed statement is quoted from 86Box's documentation
  or from GitHub discussion #3117, both of which are third-party claims
  about third-party hardware.
- **The 86Box `v4.2.1st` branch was read through the GitHub API only** -
  commit list, stats and one patch (`8cb0ebd2`). It was not checked out and
  its three other diffs were read only as file lists and line counts.
- **Whether the `st` branch still applies to 7.0** is inferred from the
  version gap and the intervening rework, not attempted.
- **The 1,412 ROM-file count is a `grep` of string literals**, deduplicated.
  It has not been cross-checked against the `86Box/roms` repository, and it
  will include a handful of non-ROM strings the filter did not catch (the
  category breakdown shows a few translated error messages).
- **The 521-machine and 150-card counts** come from regexes over
  `machine_table.c` and `vid_table.c` and include `#ifdef`-guarded
  development-branch entries.
- **`voodoo->time` and the `plat_timer_read()` sites** in 86Box were traced
  as far as "written, never read in those files"; a complete audit that no
  `plat_timer_read()` result feeds emulation was NOT done for 86Box (the
  same caveat PLAN.md records for PCem, where four of about a dozen sites
  were read).
- **`rivatimer`'s blast radius** - I established that it is driven from
  `pc_run()` and fires device callbacks, and that its users are under
  `src/video/nv/`. I did not enumerate every user.
- **86Box's vendored component licences** were not audited file by file;
  only the README's statement, the top-level `COPYING`, the CMake options
  and the Ghostscript loading mechanism were read.
- **The three 86Box sound threads** were read far enough to establish that
  `sound_cd_thread` is CD audio and that floppy and hard-disk drive sounds
  exist; the exact emulation-visibility of the latter two was not chased.
- **No 86Box FreeDOS/Windows/Linux reference configuration exists to
  compare against TASVideos'** - that absence is itself a finding, but it
  means the like-for-like comparison of section 9 rests on one discussion
  thread and one FAQ.

## Log

- **2026-09-20** 86Box evaluated against PLAN.md's headings, on Sergio's
  instruction that the upstream question precede M1. `86Box/86Box` `c0d1a56`
  (v7.0, 1,052,777 lines) cloned to `src/86box` and read: GPL-2.0-or-later
  with every risky optional dependency behind a CMake switch and Ghostscript
  dlopen'd; the same software rasteriser into a 2048x2048 bitmap behind a
  platform `blit_func`, but through a blit thread; `pc_run()` already
  slice-parameterised by `force_10ms`; **no savestates** (every apparent hit
  is SMM SMRAM or a device state machine - Sergio's assumption checked and
  corrected); ~14 emulation threads against PCem's 5; **five** host-clock
  seams against PCem's one, including `rdtsc` in inline assembly that
  miniBox cannot trap and a wall-clock `rivatimer` that fires device
  callbacks from the run loop; 521 machines, 150 video cards, 444 CPUs and
  **1,412 ROM files** against PCem's 93/47/290/~200; and the `call()`
  near-call test **correctly fixed** in both of its recompiler backends,
  which is a small point in 86Box's favour rather than a cost.
  `TASEmulators/86Box` branch `v4.2.1st` found: four Clement Gallet commits
  of 2024-09-16 doing for 86Box what he did for PCem, stranded three major
  versions back. 86Box's FAQ and discussion #3117 read for the speed and
  Windows XP evidence, which runs decisively PCem's way and also casts doubt
  on the Pentium II/450 target for both. Recommendation: **stay on PCem**,
  and replace M1's fortnight with a two-day native speed measurement that
  decides whether Windows XP is in scope at all. Nothing built, nothing
  pushed, no repository created.

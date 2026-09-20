# PCem as a Chimera core: evaluation and plan

## What this is

An IBM-PC-compatible core for Chimera, asked for on 2026-09-20 with
upstream `pcem-emulator.co.uk` PCem v17 and the TASVideos fork
`TASEmulators/pcem` (read here at `fd4585b`, branch `master`, tags `v17`,
`17+st-1`, `17+st-2`) as the starting point.

This is M0: the questions answered from the sources before a line of core
code, the way flycast's, AppleWin's and KEmulator's plans were. Nothing is
built, no repository is created beyond this `docs/` directory, and the
clone used to read the sources lives in `src/` (kept out of git; it is not
a submodule and will not become one).

Throughout: **READ** means a file in the clone or a page on tasvideos.org
that this document quotes or derives from; **UNCHECKED** means a claim
that follows from what was read but was not run, built or measured.
Nothing here has been compiled.

**Read `UPSTREAM.md` beside this.** The question "PCem or 86Box" was
settled on the same day, after this plan was written, on Sergio's
instruction that it precede M1. The answer was PCem, and it changed two
things here: the milestones (M1 split into a two-day native speed
measurement and a one-week sandbox JIT spike) and risk 7. Everything
else in this document stands.

## The finding first (2026-09-20)

**PCem is worth adding, and it is an unusually clean fit for the
waterbox - cleaner than DOSBox-X was.** Six things that normally cost
weeks are already true:

1. **The licence is clean.** PCem is GPL-2.0 (top-level `COPYING`, the
   GNU GPL v2 text; `README.md`: "PCem is licensed under GPL v2.0"). The
   TASVideos fork adds no licence of its own and carries the same
   `COPYING`. `package-licenses.json` can be written honestly. This is
   the check that killed the KEmulator proposal and PCem passes it.
2. **There is no GPU anywhere.** Every video card is a C software
   rasteriser writing into one in-memory bitmap: `buffer32 =
   create_bitmap(2048, 2048)` (`src/video.c:1069`), 32 bits a pixel, and
   `video_blit_memtoscreen()` (`src/video.c:1111`) is a single function
   pointer the platform layer owns. That is way 1 of the three - the
   picture is the machine's by construction - including 3dfx Voodoo,
   Voodoo 3, Banshee, Matrox Mystique and S3 ViRGE, all of which PCem
   rasterises in software.
3. **A frame is already defined, by the TASVideos fork.** Its commit
   `13a3d2e` ("Emulate cycles based on elapsed time instead of fixed
   10ms") changed `runpc()` to `runpc(uint64_t ms)` with
   `cycles_to_run = (ms * cpu_get_speed()) / 1000` (`src/pc.c:508-511`).
   A frame IS a fixed slice of emulated CPU cycles. libTAS drives it at
   100 fps, i.e. 10 ms of machine time a frame. `FrameAdvance` is
   `runpc(ms)` and nothing else.
4. **There is no savestate system to fight.** `grep -i savestate` over
   `src/` returns nothing. PCem has never had savestates; libTAS
   snapshots the process instead. The waterbox snapshots the guest, so
   the hole upstream left is exactly the shape miniBox fills - and there
   is no upstream state serialiser to neuter, unlike rpcs3, PPSSPP and
   DOSBox-X.
5. **One clock seam.** Host time reaches the machine in exactly one
   place: `time_internal_sync()` (`src/rtc.c:227-238`, and its twin
   `src/rtc_tc8521.c:206`), which calls `time()` + `localtime()` to seed
   the CMOS clock at boot when the config's `enable_sync` is set
   (`src/pc.c:840`). After that the RTC ticks off the emulated timer.
   The only other host-clock reads are `timer_read()` (=
   `SDL_GetPerformanceCounter`, `src/wx-sdl2.c:314`) inside the
   status-bar "% CPU" accounting of the S3, TGUI, ET4000/W32 and Voodoo
   blitters - statistics, not emulation - and they leave with the
   platform layer.
6. **The single-threading work is half done and its author is known.**
   The TASVideos fork exists precisely because "the normal build of PCem
   will have determinism issues", and Clement Gallet (libTAS's author)
   converted the blit lock, the CD-audio path, the S3, the S3 ViRGE, the
   Voodoo and the Banshee blitter to single-threaded
   (`b4e28d6`, `55f0fdb`). `thread_create` survives in exactly five
   devices - ET4000/W32p, TGUI9440, Matrox Mystique, ATI Mach64 and the
   PGC - which is a bounded, enumerable list, not an open problem.

**What makes it worth doing at all is Windows XP and Linux, and the
honest answer to "given DOSBox-X" is: the DOS overlap is nearly total,
and everything after 1995 is where the value is.** Section 1 below
argues that with numbers.

**The one thing that can kill it is speed**, and M1 is built around
measuring it: a Pentium II/450 with 256 MB running Windows XP is the
target machine TASVideos publishes, PCem needs its dynamic recompiler to
approach that, and whether a 120 MiB RWX JIT arena inside miniBox
performs is UNCHECKED. If the interpreter is the only option and it
cannot run XP at a usable speed, the core collapses back to DOS-era
machines, which DOSBox-X already covers better and faster, and the
answer becomes "not worth it". That is the decision M1 makes.

---

## 1. Why PCem at all, given DOSBox-X

### Where the overlap is total

Chimera already ships a DOSBox-X core with ten machine-year presets from
a 1981 IBM 5150 to a 1999 IBM Thinkpad 240
(`~/chimera-cores/dosbox-x/waterbox/waterbox.config`, `machinePreset`),
every DOS video standard from MDA to VESA, 3dfx Voodoo 1
(`extern/dosbox-x/src/hardware/voodoo*.cpp`, `[voodoo] voodoo_card`),
Sound Blaster through SB16/ViBRA, MT-32/CM-32L, PC-98, and a hard-disk
story that already works. For 1980s and early-1990s DOS games the two
emulators run the same software, DOSBox-X runs it faster, and DOSBox-X's
DOS-level implementation (its own DOS kernel, `imgmount`, `mount`)
is far friendlier than PCem's "boot FreeDOS off an image you built
yourself". **Adding PCem buys nothing for the DOS catalogue.** TASVideos
themselves treat DOS as the PCem beginner path only because they have no
DOSBox-X equivalent under libTAS.

### Where PCem is genuinely different

**(a) It is a machine emulator, not a DOS emulator.** DOSBox-X provides a
synthetic BIOS and a DOS built in C++. PCem boots a real motherboard
BIOS dump through a real chipset: 93 machines, each with its own ROM set,
its own CMOS/NVRAM layout, its own POST and its own BIOS setup screens
(`src/model.c`, `src/mem_bios.c`). Software that probes the BIOS,
depends on chipset registers, or is an operating system rather than a DOS
program, sees a period-correct PC. This is why PCem's own `TESTED.md`
lists OS/2 1.0 through Warp 4, Windows NT 3.1 through XP, and Linux, and
DOSBox-X's does not.

**(b) Windows NT / 2000 / XP.** DOSBox-X's README
(`extern/dosbox-x/README.md:240-248`) puts it plainly, under "Features
that DOSBox-X is unlikely to support at this time":

> DOSBox-X emulation, in terms of running Windows in DOSBox-X, will focus
> primarily on Windows 1.0 through Windows ME (Millennium Edition), and
> then on Windows NT through Windows XP. [...]
> If you need to run Windows XP and later, please consider using QEMU,
> Bochs, VirtualBox, or VMware.

and, above it, "Emulation of PC hardware 2001 or later" is out of scope.
Be precise about the nuance: DOSBox-X does not say XP is impossible, it
says it is not a priority and points elsewhere. The Chimera DOSBox-X core
carries a `dosbox-x.osconfig.windowsXP.conf` inherited from the BizHawk
lineage (`waterbox/conf/`), but `gen-assets.py:23` deliberately EXCLUDES
every `osconfig` file from the shipped assets, so the Chimera package
does not offer it, and the core forces `core=normal` (the interpreter,
`docs/PLAN.md` "What stays disabled": "dynamic_x86 is JIT [...] normal/
full/simple cores only"). PCem's published TASVideos configuration for XP
is a Pentium II/450 with 256 MB, a Voodoo 3 3000 and an AWE32, and there
is a published libTAS installation movie that produces it reproducibly.
**This is the headline difference and it is real.**

**(c) Linux guests.** TASVideos publishes a PCem Tiny Core Linux 7.0
install guide (`/EmulatorResources/PCem/Linux`): partition with cfdisk,
install GRUB 0.97, boot from the hard-disk image. DOSBox-X cannot host a
protected-mode Unix at all. A Linux guest also opens the door to the
things TASVideos names on the PCem landing page - "various environments
like Flash or Java" - from the other side of the fence from the Ruffle
core.

**(d) 3D accelerators past the Voodoo 1.** PCem rasterises Voodoo 2 SLI,
Voodoo Banshee, Voodoo 3 2000/3000, Matrox Mystique, S3 ViRGE/ViRGE-DX
and ATI Mach64 in software (47 video cards, `src/video.c:84`). DOSBox-X
has the Voodoo 1 and nothing else. Every Glide/Direct3D/OpenGL PC game of
1997-2001 is PCem-only.

**(e) Non-IBM PCs DOSBox-X explicitly refuses.** The same README section:
"Any MS-DOS system other than IBM PC/XT/AT, AX, Tandy, PCjr, and PC-98
[...] Only the above listed systems will be considered." PCem has the
Amstrad PC1512/1640/2086/3086/5086/PPC512, Sinclair PC200, Olivetti M24,
Schneider EuroPC, Thomson TO16, Toshiba T1000/T1200/T3100e, Zenith
SupersPort, Commodore PC30-III, Bull Micral 45, Elonex, Tulip, NCR, and
the whole IBM PS/1 and PS/2 (MCA) line. European home-computer PC
software runs on the machine it was written for.

### The honest summary

The systems overlap is larger than it looks - **all of DOS, and most of
Windows 3.x** - and for that overlap DOSBox-X wins on speed and
convenience. PCem's case rests entirely on: Windows 9x done properly,
Windows NT/2000/XP, Linux, post-Voodoo-1 3D, and the non-IBM PCs. If XP
turns out not to be viable in the sandbox, four of those five go with it
(Linux on a 386 is cheap; the rest are not), and the remaining case is
thin. Hence M1.

---

## 2. What PCem is made of (evidence)

Read from the clone in `src/tas-pcem` at `fd4585b`.

- **251,500 lines of C/C++** across `src/` including `src/dosbox`,
  `src/minivhd`, `src/resid-fp` and `src/slirp`. Of that: 47,200 lines of
  dynamic recompiler (`codegen*`), 57,700 lines of video cards
  (`vid_*.c`), 13,600 lines of sound cards (`sound_*.c`), and **12,950
  lines of platform layer** (`wx-*.c`, `wx-*.cc`, `soundopenal.c`,
  `thread-pthread.c`, `win-midi.c`, `midi_alsa.c`). The platform layer is
  what a Chimera driver replaces; it is 5% of the tree and it is cleanly
  separated behind `ibm.h`'s small set of externs.

  **MEASURED at M1a, and much better than this estimate.** Those 12,950
  lines are what the platform layer *contains*; what it *exports to the
  emulation core* is **27 symbols and one function pointer**, enumerated
  by linking the core with no platform layer at all and reading the
  undefined references (`M1A.md` section 1). The pointer is
  `video_blit_memtoscreen_func` (`src/video.c:733`). A driver satisfying
  all 28 is 260 lines (`tools/driver.c`), and it already does the real
  work for video, keyboard, paths and the fatal path. **M2's three weeks
  should be re-read against that**: the platform strip is not a
  13,000-line rewrite, it is 28 definitions, and most of them are already
  written.
- **93 selectable machines** (`MODEL models[]`, `src/model.c`), 97
  romsets in the enum (`src/ibm.h:171-271`) of which four -
  `ROM_PX386`, `ROM_MISC286`, `ROM_IBMAT386`, `ROM_PCI486` - are
  vestigial and not offered. Each entry is
  `{name, romset, internal_name, cpu[5]{name, CPU*}, flags, min_ram,
  max_ram, ram_granularity, init, device}`.
- **290 CPU entries in 33 tables** (`src/cpu_tables.c`), from an 8088 at
  4.77 MHz to a Pentium III. The CPU choice is an index into a
  per-machine, per-manufacturer list, not a global enum: this shapes the
  settings design (section 4).
- **47 video cards** (`VIDEO_CARD video_cards[]`, `src/video.c:84`),
  **20 sound cards** (`src/sound.c`) plus the Game Blaster, Gravis
  Ultrasound and SSI2001 as independent toggles, **17 HDD controllers**
  (`src/hdd.c`) spanning MFM, ESDI, IDE, XTIDE and SCSI.
- **RAM sizing is per machine and in two different units.** `min_ram` /
  `max_ram` are megabytes when `(flags & MODEL_AT) && ram_granularity <
  128`, and kilobytes otherwise (`src/pc.c:748-749`,
  `src/wx-config.c:521-526`). The `.cfg` always stores `mem_size` in KB.
- **The build is autotools + SDL2 + wxWidgets + OpenAL** for the desktop
  port; the emulation core itself needs none of them.

### What the TASVideos fork changed (`git log v17..fd4585b`, 34 commits)

| Commit | What |
|---|---|
| `b4e28d6` | blit lock, CD audio, S3 ViRGE and Voodoo made single-threaded (44 files, -1316 lines) |
| `55f0fdb` | S3 made single-threaded |
| `c88ea55` | "Add back a mandatory timer" (Voodoo) |
| `c99fab9` | renderer sync for Windows |
| `13a3d2e` | `runpc(ms)`: cycles from elapsed time instead of a fixed 10 ms |
| `9b02d13`, `1f6d8c5`, `4aa08f2` | CD and floppy swap by hotkey, eject shortcuts |
| `b741ee1`, `9191d03` | `report_framerate_change(num, denom)` - the emulated card's refresh rate, printed at the frame it changes (`src/video.c:1272-1288`) |
| `fd4b822` | print the loaded ROM's path when running (for movie documentation) |
| `8a83e87` | extra Pentium and Cyrix CPU entries |
| `524c4ac` | longer string config entries (the semicolon-delimited disk lists) |
| `d54e060`, `fd4585b` | SVGA fps reporting on blit; S/VGA timing fix backported from 86Box |

Two of those - `13a3d2e` and `b741ee1`/`9191d03` - are exactly the two
hooks a Chimera driver needs (a sliced advance and a refresh-rate
callback), written by someone solving the same problem through a
different mechanism. That is worth a lot.

---

## 3. libTAS versus the waterbox: what each job becomes

Sergio's view to test: "a waterboxed environment like Chimera's will be a
much better host for PCem." The evidence says yes, decisively, and the
reason is that libTAS has to fight the process while the waterbox owns
the machine. Job by job, from the TASVideos pages and the libTAS design:

| Job | What libTAS does for PCem | What Chimera's sandbox does instead |
|---|---|---|
| **Input** | Injects SDL/X11 events into the real PCem binary; the emulator polls SDL normally. Mouse has a trap: "don't make a right-click your first mouse input" or the wx popup menu softlocks the display window | The driver sets PCem's key/mouse/joystick state directly from the frame's input word; no SDL, no window, no popup menu. The whole class of "GUI ate the input" bugs disappears |
| **Time** | LD_PRELOADs `clock_gettime`/`gettimeofday`/`SDL_GetTicks` and advances them by 1/fps a frame; fps must be set to **100/1** because "PCem only runs at 100fps" | The sandbox has exactly one clock, frozen at 1495889068, and `gettimeofday`/`time` are not implemented at all (the syscall surface refuses them). The only seam left is `time_internal_sync` and it becomes a declared `rtcBase` sync setting |
| **Frame boundary** | Whatever the process does between two swap-buffer calls at the forced 100 fps | `runpc(ms)` with `ms` chosen by the core: the slice is the frame, and it is exact rather than inferred |
| **Determinism of threads** | Cannot handle them - hence the whole fork: "This fork has some modifications to make parts of PCem single-threaded. The normal build of PCem will have determinism issues." Voodoo render threads must be set to 1 by hand in the config | miniBox green threads are cooperative and round-robin by ascending tid, so a thread that does exist is deterministic; the remaining five threaded devices still need conversion because their FIFO waits use `thread_wait_event(..., timeout)` and miniBox ignores futex timeouts (a wait that can only be woken by a timeout deadlocks, and miniBox kills the guest with "every thread of the core is waiting") |
| **Files** | The hard rule of the whole page: `.img`, `.nvr`, `.cfg` and `flash.bin` are sync-critical; "Runtime -> Prevent writing to disk" must be checked or "PCem will change your files, which will cause desyncs"; "All modifications to the hard drive need to be done within a libTAS+PCem movie!!!"; a reboot writes them anyway | The guest VFS is in memory. The disk image is a read-only hash-bound mount with a write overlay in guest memory; NVRAM and flash are guest memory that leaves through the savedata export. **A desync class that dominates TASVideos' documentation simply does not exist** |
| **Savestates** | Process-level; "Avoid loading a savestate made before a resolution change; this can cause desyncs, crashes, or just mess up the display"; "CD reading can break or desync if you load a savestate made before PCem has started reading from the CD"; incremental savestates must be off | The whole guest is the state, including the video card's registers and the CD image's read position. Both named hazards are artefacts of snapshotting a process that has host resources (an SDL window, an open file) - the box has neither |
| **Config** | A `.cfg` file on disk edited by hand, with absolute paths, dos2unix'd, under 255 characters a line | Sync settings in the project, rendered by the wizard, recorded in the movie header, composed into a `.cfg` in guest memory by the driver (the DOSBox-X recipe, item 9) |
| **Verification** | The mover must publish ROM hashes, media hashes, and often a separate installation movie to reproduce `flash.bin` and `.nvr` | Firmware is pinned by SHA-1 in the project; save data is an explicit export; the installation movie becomes an ordinary Chimera movie whose output is a savedata zip |

The one thing libTAS gives that the waterbox does not is **zero porting
work**: it runs the shipped binary. Everything else is worse. Sergio's
view holds.

---

## 4. Configuration and deployment, following the DOSBox-X core

The instruction is to follow `~/chimera-cores/dosbox-x` closely,
"especially in the way hdds are handled". Here is the PCem equivalent,
item by item against that core's plan.

### 4.1 `file_slots.json`

Almost the DOSBox-X file verbatim. PCem has two floppy drives with
independent swap lists, up to nine hard disks (`hdc`..`hdi`) and one CD.

```json
{
  "slots": [
    { "id": "floppyA", "title": "Floppy disks (drive A:)", "min": 0, "max": -1,
      "formats": ["img", "ima", "dsk", "fdi", "86f", "td0", "imd"],
      "help": "Images for drive A:. The first one is in the drive at boot; the order here is the swap order used by the Previous/Next/Swap Floppy A inputs." },
    { "id": "floppyB", "title": "Floppy disks (drive B:)", "min": 0, "max": -1, "formats": ["..."], "help": "..." },
    { "id": "cdrom",  "title": "CD-ROMs", "min": 0, "max": -1, "formats": ["iso", "cue"], "help": "..." },
    { "id": "hdd",    "title": "Hard disk (C:)", "min": 0, "max": 1,
      "formats": ["img", "hdi", "vhd"],
      "help": "A raw hard disk image or a VHD, mounted writable as drive C:. Writes persist for the session and can be exported as save data." },
    { "id": "hdd2",   "title": "Second hard disk (D:)", "min": 0, "max": 1, "formats": ["img", "hdi", "vhd"], "help": "..." }
  ],
  "atLeastOneOf": [["floppyA", "cdrom", "hdd"]]
}
```

Notes from the sources: PCem's floppy layer reads `.img`/`.ima`/`.dsk`
raw, `.fdi` through `fdi2raw.c`, and `.td0`/`.imd`/`.86f` through
`disc_*.c`; the CD layer is PCem's own (`src/dosbox/cdrom_image.cpp`,
`src/cdrom-image.cc`), so `.iso` and `.cue` are read by the emulator, not
by the frontend - exactly the DOSBox-X item 7 decision ("Chimera
deliberately has no disc layer"). `minivhd` (MIT) gives `.vhd`. There is
**no `.conf` slot**: PCem's `.cfg` is fully generated from settings, and
letting a user paste one in would reintroduce the desync class the
sandbox just removed. A `.cfg` slot can come later if a real need shows
up.

### 4.2 Hard disks: the DOSBox-X recipe, with one improvement

DOSBox-X copies the whole image into a jaffarCommon `MemoryFile` in the
guest heap before seal, so the sealed baseline absorbs it and a savestate
carries only dirtied pages (`docs/save-data.md:22-38`). For PCem the same
result is available more cheaply, because **PCem's disk layer is already
stdio**: `hdd_file.c:17-19` opens with `fopen64(fn, "rb")` or `"rb+"` and
reads and writes with `fseeko64`/`fread`/`fwrite` at
`hdd_file.c:131-194`. So:

- The image enters as a **read-only mount by host path**
  (`wbx_mount_file_path`, which does not copy - "For anything large - a
  disc image is gigabytes - this is the difference between a copy the
  machine never needed and no copy at all"), hash-bound to the savestate.
- `hdd_file.c` is patched to a **sparse write overlay**: reads fall
  through to the mounted base unless the sector has been written; writes
  land in a guest-memory sector map. This is precisely DOSBox-X's
  `imageDisk_Sparse`, which that core wrote for PC-98 `.hdi` images
  ("the overlay still works in whole-file offsets and a savestate carries
  only what was written").
- The overlay is the core's **save data** (the savedata guest ABI group:
  `GetSaveDataFileCount/Name/Size/Buffer`, ranged reads for the multi-GB
  case), exported as a full image so it can be handed straight back into
  the `hdd` slot. A 4 GiB Windows XP image therefore costs **zero** bytes
  of guest heap and zero bytes per savestate until the guest writes.
- When no image is provided, a `formattedHardDisk` enum offers
  pre-formatted empty images, the DOSBox-X way. PCem's geometry is
  explicit (`hdc_sectors`/`hdc_heads`/`hdc_cylinders` in the `.cfg`), so
  the enum values carry C/H/S as well as size, and the published
  TASVideos geometries are the obvious presets: 17/15/900 = 112 MB,
  17/15/1224 = 152 MB, 63/16/2099 = 1033 MB, 63/16/8374 = 4121 MB.
- **Geometry must be derivable from the image.** PCem's `.cfg` carries
  C/H/S separately from the filename, and a wrong geometry is a disk that
  does not boot. For a raw `.img` the driver computes it from the file
  size the way PCem's own "new hard drive" dialog does, and exposes
  `hddSectors`/`hddHeads`/`hddCylinders` int settings (default 0 = auto)
  for the cases where it cannot. A `.vhd` and an `.hdi` carry their own.

### 4.3 The two other writable files: NVRAM and flash

This is the piece DOSBox-X does not have, and it is the piece the
TASVideos pages spend the most words on.

- `.nvr` - the CMOS/RTC SRAM, one per machine
  (`src/nvr.c:282-...`, `nvrfopen("<machine>.nvr")`). It holds the BIOS
  setup: drive types, boot order, floppy sizes. TASVideos ships one with
  each DOS package and its MD5 is part of the sync contract.
- `flash.bin` - the flash BIOS of the Intel/Award boards
  (`src/intel_flash.c:358-415`, `src/sst39sf010.c:186-207`), written into
  the **ROM directory**, which is why TASVideos cannot distribute it:
  "Because the ROM gets written into it, we can't distribute it".

In Chimera both are guest memory seeded at Init and exported as save
data beside the disk overlay. `flash.bin` is initialised **from the
machine's BIOS ROM** rather than from a distributed blob, so the
"delete flash.bin before starting a TAS" ritual - which appears on four
separate TASVideos pages - becomes structurally impossible. The `.nvr`
default for machines that have one lives in the package
(`nvr/default/*.nvr` in the PCem tree, which is PCem's own data, GPLv2),
so a project starts from a clean, identical CMOS every time.

### 4.4 Settings

Shaped like DOSBox-X's, but with one structural difference: **nothing may
be "auto"**, because PCem's firmware requirements are conditioned on the
machine, the video card and the sound card, and a `requiredWhen`
condition cannot ask what "auto" resolved to. Every knob that selects
hardware holds a real value.

| Setting | Type | Notes |
|---|---|---|
| `machine` | enum, 93 options | `internal_name` from `src/model.c`. The root of everything |
| `cpuManufacturer` | enum | narrowed per machine (`model.cpu[].name`: Intel / AMD / Cyrix / IDT / VIA) |
| `cpu` | enum | **per machine and manufacturer**: 33 tables, 290 entries. Declared as same-name entries with disjoint `exposedWhen` conditions, generated |
| `cpuSpeedMultiplier`, `cpuWaitStates`, `fpu` | enum/int | `fpu = none/builtin`; wait states 0-7 |
| `dynarec` | bool, **sync** | `cpu_use_dynarec`. Interpreter and recompiler are not bit-identical to each other (different FPU code paths), so this is a sync setting like PPSSPP's JIT |
| `memSizeKB` | int | bounds and granularity from the machine (see the KB/MB rule above); the settings dialog cannot express per-machine min/max, so the driver clamps as `src/pc.c:748` does and the setting's help says so |
| `videoCard` | enum, 47 options | `internal` from `src/video.c`. `builtin` where the machine has fixed graphics (`MODEL_GFX_FIXED`) |
| `videoMemory`, `videoSpeed` | enum/int | the per-card `device_config_t` knobs (`memory`, and `video_speed` -1..4) |
| `voodoo` | bool + `voodooType` enum | the add-in Voodoo 1/2 card, independent of `videoCard` |
| `soundCard` | enum, 20 options | plus `gameblaster`, `gus`, `ssi2001` bools |
| `soundBlasterAddr`, `soundBlasterIRQ`, `soundBlasterDMA`, `oplEmu` | int/enum | the per-card config; `opl_emu = 1` is NukedOPL, which every TASVideos config uses |
| `hddController` | enum, 17 options | `src/hdd.c` |
| `driveAType`, `driveBType` | enum | `2` = 5.25" 1.2M, `7` = 3.5" 2.88M in the published configs |
| `cdSpeed`, `cdModel` | int/enum | 24x / 72x in the published configs |
| `mouseType` | enum | 0 = serial Microsoft, 2 = PS/2, ... (`src/mouse.c`) |
| `joystickType` | enum | standard / CH Flightstick Pro / Thrustmaster FCS / Sidewinder pad |
| `rtcBase` | string or int, **sync** | the CMOS date/time the machine boots with. Replaces `enable_sync`. Windows XP boot time is sensitive to it - TASVideos measured 51.56 s to desktop at epoch 1, 68.75 s at 1000000000, and outright failures ("Invalid time/date", "Trial expiry") at 400000000 and 800000000 - so it must be a declared, recorded value and the help must say which range works (they recommend 946684800..2145830400) |
| `fpsNumerator` / `fpsDenominator` | int | the frame slice. Default 100/1, the rate libTAS forces and every published PCem movie uses. `0/0` follows the card's own `report_framerate_change` |
| `hddSectors/Heads/Cylinders`, `hdd2*` | int | geometry, 0 = derive from the image |
| `formattedHardDisk` | enum | none / the five published geometries |

The declaration is **generated** (`gen-config.py`, as DOSBox-X does for
its 102-key list) directly from `model.c`, `video.c`, `sound.c`,
`hdd.c` and `cpu_tables.c`, so it cannot drift from the emulator.

### 4.5 Input

DOSBox-X's controller, nearly unchanged: a 101/102-key keyboard (PCem's
`keyboard_at.c` scancode sets), two 2-button gameport joysticks (PCem
supports 4-axis/4-button sticks, so more axes), mouse buttons and
position/speed axes, and the disk-swap buttons that replace the fork's
`Ctrl+,`/`Ctrl+.`/`Ctrl+/` hotkeys. This exceeds 64 buttons, so it rides
the same `SetButton` wide-input channel DOSBox-X needed; that work is
already done in the engine.

### 4.6 Video and audio declaration

- Video: buffer capacity 2048x2048 (the size of `buffer32`), live size
  per frame from the driver, `vsync` from `report_framerate_change`.
- Audio: **48000 Hz stereo** (`src/sound.c:259`, `src/soundopenal.c:24`),
  pulled by an emulated timer at 48 kHz and handed over in
  `SOUNDBUFLEN`-sized chunks through `givealbuffer()`
  (`src/sound.c:250`). The driver replaces `givealbuffer` with an append
  to the frame's audio vector. `"sampleRate": 48000`, `channels` 2,
  `samplesPerFrame` capacity 48000 (a 1 fps movie is the worst case, the
  Ruffle convention). CD audio comes through a separate
  `givealbuffer_cd()` and is already mixed with ATAPI volume and channel
  select before it gets there (`src/sound.c:180-221`).

### 4.7 Memory layout and domains

An initial `memoryLayoutMiB` of `[256, 16, 16, 512, 4096]` - `plain`
large because PCem `malloc`s the machine's RAM and its lookup tables, and
`mmap` large because the JIT arena is 120 MiB and musl routes large
allocations there. Memory domains for the RAM search: "System RAM"
(`ram`, `mem_size` KB), "Video RAM" (the card's `vram`), "CMOS/NVRAM"
(`nvrram`, 128 or 256 bytes), and the disk overlay.

---

## 5. Firmware: every machine, every ROM

This is the enumeration Sergio asked for as a deliverable in its own
right. Method and caveats, stated first because they matter:

- **Source of truth is the code, not the README.** The table below is
  derived from `src/mem_bios.c`'s `switch (romset)` (comments stripped,
  case fall-through followed) joined to `MODEL models[]` in
  `src/model.c`. PCem's own `README.md` has a per-machine ROM table too,
  and **it disagrees with the code in at least four places**: it lists
  `ibmat/at111585.0` + `.1` for the IBM AT where the code falls through
  to `ibmat/62x0820.u27` + `62x0821.u47`; `pc1512/40044.v2` where the
  code reads `40044.v1`; `europc/50145` + `50146` where the code reads
  only `50145`; and it omits the BASIC ROMs the IBM PC and Generic XT
  paths look for. Trust the code.
- **PCem does not verify ROMs.** `rom_init()` (`src/rom.c:62`) opens the
  file, `fread`s it and maps it. There is no hash check anywhere, and the
  upstream project ships none: "NO COPYRIGHTED ROM FILES ARE INCLUDED NOR
  WILL THEY BE."
- **Hashes therefore come from outside.** The only hashes any source read
  for this document publishes are the ten in section 5.4. Every other row
  is **UNCHECKED for hash** and filling them in - by hashing a known-good
  v17 ROM set, file by file - is an M2 deliverable.
- **ROM names are case sensitive and contain directories.** The
  sandbox's VFS is flat: "no `opendir`/`getdents`/`mkdir`/`openat`". So
  `romfopen()` (`src/rom.c:6-28`) is patched to translate PCem's
  `"ga686bx/6BX.F2a"` into the flat mount name the engine used - the
  firmware **id**, since `session.cpp:1190` mounts each firmware under
  `firmware_ids[i]`, not under its display name. Flattening to the
  basename is NOT an option: `bios.bin` alone is claimed by
  `ati28800`, `mach64gx/`, `oti037/` and `oti067/`, and `ami.bin`,
  `award.bin`, `phoenix.bin`, `gd5434.bin` and `1006bs0_.bio` each have
  two claimants. The id must carry the directory (e.g.
  `pcem.ga686bx.6BX.F2a`) and the driver holds the map.

### 5.1 Machine BIOS ROMs (93 machines)

| Machine (PCem's own name) | internal | ROM files, relative to the roms folder |
|---|---|---|
| [8088] AMI XT clone | `amixt` | amixt/ami_8088_bios_31jan89.bin |
| [8088] Atari PC3 | `ataripc3` | ataripc3/AWARD_ATARI_PC_BIOS_3.08.BIN |
| [8088] Compaq Portable Plus | `compaq_pip` | compaq_pip/Compaq Portable Plus 100666-001 Rev C.bin |
| [8088] DTK XT clone | `dtk` | dtk/dtk_erso_2.42_2764.bin |
| [8088] Generic XT clone | `genxt` | genxt/pcxt.rom<br>(optional) genxt/ibm-basic-1.10.rom or genxt/basicc11.f6/.f8/.fa/.fc |
| [8088] IBM PC | `ibmpc` | ibmpc/pc102782.bin<br>(optional) ibmpc/ibm-basic-1.10.rom or ibmpc/basicc11.f6/.f8/.fa/.fc |
| [8088] IBM PCjr | `ibmpcjr` | ibmpcjr/bios.rom |
| [8088] IBM XT | `ibmxt` | ibmxt/xt.rom<br>ibmxt/5000027.u19<br>ibmxt/1501512.u18 |
| [8088] Juko XT clone | `jukopc` | jukopc/000o001.bin |
| [8088] Leading Edge Model M | `ledge_modelm` | leadingedge_modelm/Leading Edge - Model M - BIOS ROM - Version 4.71.bin |
| [8088] NCR PC4i | `ncr_pc4i` | ncr_pc4i/NCR_PC4i_BIOSROM_1985.BIN |
| [8088] Phoenix XT clone | `pxxt` | pxxt/000p001.bin |
| [8088] Schneider EuroPC | `europc` | europc/50145 |
| [8088] Tandy 1000 | `tandy` | tandy/tandy1t1.020 |
| [8088] Tandy 1000 HX | `tandy1000hx` | tandy1000hx/v020000.u12 |
| [8088] Thomson TO16 PC | `to16_pc` | to16_pc/TO16_103.bin |
| [8088] Toshiba T1000 | `t1000` | t1000/t1000font.rom<br>t1000/t1000.rom |
| [8088] VTech Laser Turbo XT | `ltxt` | ltxt/27c64.bin<br>(optional) ltxt/ibm-basic-1.10.rom or ltxt/basicc11.f6/.f8/.fa/.fc |
| [8088] Xi8088 | `xi8088` | xi8088/bios-xi8088.bin |
| [8088] Zenith Data SupersPort | `zdsupers` | zdsupers/z184m v3.1d.10d |
| [8086] Amstrad PC1512 | `pc1512` | pc1512/40043.v1<br>pc1512/40044.v1<br>pc1512/40078.ic127 |
| [8086] Amstrad PC1640 | `pc1640` | pc1640/40044.v3<br>pc1640/40043.v3<br>pc1640/40100 |
| [8086] Amstrad PC2086 | `pc2086` | pc2086/40179.ic129<br>pc2086/40180.ic132<br>pc2086/40186.ic171 |
| [8086] Amstrad PC3086 | `pc3086` | pc3086/fc00.bin<br>pc3086/c000.bin |
| [8086] Amstrad PC5086 | `pc5086` | pc5086/sys_rom.bin |
| [8086] Amstrad PPC512/640 | `ppc512` | ppc512/40107.v2<br>ppc512/40108.v2<br>ppc512/40109.bin |
| [8086] Compaq Deskpro | `deskpro` | deskpro/Compaq - BIOS - Revision J - 106265-002.bin |
| [8086] Olivetti M24 | `olivetti_m24` | olivetti_m24/olivetti_m24_version_1.43_low.bin<br>olivetti_m24/olivetti_m24_version_1.43_high.bin |
| [8086] Sinclair PC200 | `pc200` | pc200/pc20v2.1<br>pc200/pc20v2.0<br>pc200/40109.bin |
| [8086] Tandy 1000 SL/2 | `tandy1000sl2` | tandy1000sl2/8079047.hu1<br>tandy1000sl2/8079048.hu2 |
| [8088] Toshiba T1200 | `t1200` | t1200/t1000font.rom<br>t1200/t1200_019e.ic15.bin |
| [8086] VTech Laser XT3 | `lxt3` | lxt3/27c64d.bin<br>(optional) lxt3/ibm-basic-1.10.rom or lxt3/basicc11.f6/.f8/.fa/.fc |
| [286] AMI 286 clone | `ami286` | ami286/amic206.bin |
| [286] Award 286 clone | `award286` | award286/award.bin |
| [286] Bull Micral 45 | `bull_micral_45` | bull_micral_45/even.fil<br>bull_micral_45/odd.fil |
| [286] Commodore PC 30 III | `cmdpc30` | cmdpc30/commodore pc 30 iii even.bin<br>cmdpc30/commodore pc 30 iii odd.bin |
| [286] Compaq Portable II | `compaq_pii` | compaq_pii/109740-001.rom<br>compaq_pii/109739-001.rom |
| [286] DELL System 200 | `dells200` | dells200/dell0.bin<br>dells200/dell1.bin |
| [286] Epson PC AX | `epson_pcax` | epson_pcax/EVAX<br>epson_pcax/ODAX |
| [286] Epson PC AX2e | `epson_pcax2e` | epson_pcax2e/EVAXE<br>epson_pcax2e/ODAXE |
| [286] Goldstar GDC-212M | `gdc212m` | gdc212m/gdc212m_72h.bin |
| [286] GW-286CT GEAR | `gw286ct` | gw286ct/2ctc001.bin |
| [286] Hyundai Super-286TR | `super286tr` | super286tr/award.bin |
| [286] IBM AT | `ibmat` | ibmat/62x0820.u27<br>ibmat/62x0821.u47 |
| [286] IBM PS/1 model 2011 | `ibmps1es` | ibmps1es/ibm_1057757_24-05-90.bin<br>ibmps1es/ibm_1057757_29-15-90.bin<br>ibmps1es/f80000.bin |
| [286] IBM PS/2 Model 30-286 | `ibmps2_m30_286` | ibmps2_m30_286/33f5381a.bin |
| [286] IBM PS/2 Model 50 | `ibmps2_m50` | i8550021/90x7423.zm14<br>i8550021/90x7426.zm16<br>i8550021/90x7420.zm13<br>i8550021/90x7429.zm18 |
| [286] IBM XT Model 286 | `ibmxt286` | ibmxt286/BIOS_5162_21APR86_U34_78X7460_27256.BIN<br>ibmxt286/BIOS_5162_21APR86_U35_78X7461_27256.BIN |
| [286] Samsung SPC-4200P | `spc4200p` | spc4200p/u8.01 |
| [286] Samsung SPC-4216P | `spc4216p` | spc4216p/phoenix.bin<br>spc4216p/7101.u8<br>spc4216p/ac64.u10 |
| [286] Samsung SPC-4620P | `spc4620p` | spc4620p/svb6120a_font.rom<br>spc4620p/31005h.u8<br>spc4620p/31005h.u10 |
| [286] Toshiba T3100e | `t3100e` | t3100e/t3100e_font.bin<br>t3100e/t3100e.rom |
| [286] Trigem 286M | `tg286m` | tg286m/ami.bin |
| [286] Tulip AT Compact | `tulip_tc7` | tulip_tc7/tc7be.bin<br>tulip_tc7/tc7bo.bin |
| [386SX] Acer 386SX25/N | `acer386` | acer386/acer386.bin<br>acer386/oti067.bin |
| [386SX] AMA-932J | `ama932j` | ama932j/ami.bin<br>ama932j/oti067.bin |
| [386SX] AMI 386SX clone | `ami386` | ami386/ami386.bin |
| [386SX] Amstrad MegaPC | `megapc` | megapc/41651-bios lo.u18<br>megapc/211253-bios hi.u19 |
| [386SX] Commodore SL386SX-25 | `cbm_sl386sx25` | cbm_sl386sx25/f000.rom |
| [386SX] DTK 386SX clone | `dtk386` | dtk386/3cto001.bin |
| [386SX] Epson PC AX3 | `epson_pcax3` | epson_pcax3/EVAX3<br>epson_pcax3/ODAX3 |
| [386SX] IBM PS/1 model 2121 | `ibmps1_2121` | ibmps1_2121/fc0000.bin |
| [386SX] IBM PS/2 Model 55SX | `ibmps2_m55sx` | i8555081/33f8146.zm41<br>i8555081/33f8145.zm40 |
| [386SX] KMX-C-02 | `kmxc02` | kmxc02/3ctm005.bin |
| [386SX] Packard Bell Legend 300SX | `pb_l300sx` | pb_l300sx/pb_l300sx.bin |
| [386SX] Samsung SPC-6033P | `spc6033p` | spc6033p/svb6120a_font.rom<br>spc6033p/phoenix.bin |
| [386DX] AMI 386DX clone | `ami386dx` | ami386dx/opt495sx.ami |
| [386DX] Compaq Deskpro 386 | `deskpro386` | deskpro386/109592-005.u11.bin<br>deskpro386/109591-005.u13.bin |
| [386DX] ECS 386/32 | `ecs_386_32` | ecs386_32/386_32_even.bin<br>ecs386_32/386_32_odd.bin |
| [386DX] IBM PS/2 Model 70 (type 3) | `ibmps2_m70_type3` | ibmps2_m70_type3/70-a_even.bin<br>ibmps2_m70_type3/70-a_odd.bin |
| [386DX] IBM PS/2 Model 80 | `ibmps2_m80` | i8580111/15f6637.bin<br>i8580111/15f6639.bin |
| [386DX] MR 386DX clone | `mr386dx` | mr386dx/opt495sx.mr |
| [386DX] Samsung SPC-6000A | `spc6000a` | spc6000a/3c80.u27<br>spc6000a/9f80.u26 |
| [486] AMI 486 clone | `ami486` | ami486/ami486.bin |
| [486] AMI WinBIOS 486 | `win486` | win486/ali1429g.amw |
| [486] Award SiS 496/497 | `sis496` | sis496/sis496-1.awa |
| [486] Elonex PC-425X | `elx_pc425x` | elx_pc425x/elx_pc425x.bin<br>elx_pc425x/elx_pc425x_vbios.bin<br>elx_pc425x/elx_pc425x_bios.bin |
| [486] IBM PS/1 Model 2133 (EMEA 451) | `ibmps1_2133` | ibmps1_2133/PS1_2133_52G2974_ROM.bin |
| [486] IBM PS/2 Model 70 (type 4) | `ibmps2_m70_type4` | ibmps2_m70_type4/70-b_even.bin<br>ibmps2_m70_type4/70-b_odd.bin |
| [486] Packard Bell PB410A | `pb410a` | pb410a/PB410A.080337.4ABF.U25.bin |
| [Socket 4] Intel Premiere/PCI | `revenge` | revenge/1009af2_.bio<br>revenge/1009af2_.bi1 |
| [Socket 4] Packard Bell PB520R | `pb520r` | pb520r/gd5434.bin<br>pb520r/1009bc0r.bio<br>pb520r/1009bc0r.bi1 |
| [Socket 5] Intel Advanced/EV | `endeavor` | endeavor/1006cb0_.bio<br>endeavor/1006cb0_.bi1 |
| [Socket 5] Intel Advanced/ZP | `zappa` | zappa/1006bs0_.bio<br>zappa/1006bs0_.bi1 |
| [Socket 5] Itautec Infoway Multimidia | `infowaym` | infowaym/1006bs0_.bio<br>infowaym/1006bs0_.bi1 |
| [Socket 5] Packard Bell PB570 | `pb570` | pb570/gd5430.bin<br>pb570/1007by0r.bio<br>pb570/1007by0r.bi1 |
| [Socket 7] ASUS P/I-P55TVP4 | `p55tvp4` | p55tvp4/tv5i0204.awd |
| [Socket 7] ASUS P/I-P55T2P4 | `p55t2p4` | p55t2p4/0207_j2.bin |
| [Socket 7] Epox P55-VA | `p55va` | p55va/va021297.bin |
| [Socket 7] Shuttle HOT-557 | `430vx` | 430vx/55xwuq0e.bin |
| [Super 7] FIC VA-503+ | `fic_va503p` | fic_va503p/je4333.bin |
| [Socket 8] Intel VS440FX | `vs440fx` | vs440fx/1018CS1_.BIO<br>vs440fx/1018CS1_.BI1<br>vs440fx/1018CS1_.BI2<br>vs440fx/1018CS1_.BI3<br>vs440fx/1018CS1_.RCV |
| [Slot 1] Gigabyte GA-686BX | `ga686bx` | ga686bx/6BX.F2a |

### 5.2 Video BIOS and font ROMs (47 cards)

Every card whose `*_available()` predicate calls `rom_present()` needs
its file or the card is not offered. Derived from each device's `init`
function in `src/vid_*.c`.

| Video card | internal | ROM file(s) |
|---|---|---|
| 3DFX Voodoo Banshee (reference) | `banshee` | pci_sg.rom |
| Creative Labs 3D Blaster Banshee PCI | `cl_banshee` | blasterpci.rom |
| 3DFX Voodoo 3 2000 | `v3_2000` | voodoo3_2000/2k11sd.rom |
| 3DFX Voodoo 3 3000 | `v3_3000` | voodoo3_3000/3k12sd.rom |
| Acumos AVGA2 / CL-GD5402 | `avga2` | avga2vram.vbi |
| ATI Graphics Pro Turbo (Mach64 GX) | `mach64gx` | mach64gx/bios.bin |
| ATI Video Xpression (Mach64 VT2) | `mach64vt2` | atimach64vt2pci.bin |
| ATI EGA Wonder 800+ (18800) | `egawonder800` | `ATI EGA Wonder 800+ N1.00.BIN` |
| ATI Korean VGA (28800) | `ati28800k` | atikorvga.bin<br>ati_ksc5601.rom |
| ATI VGA Charger (28800) | `ati28800` | bios.bin |
| ATI VGA Edge-16 (18800) | `ati18800` | vgaedge16.vbi |
| CGA | `cga` | (character generator: mda.rom, see 5.5) |
| Cirrus Logic CL-GD5428 | `cl_gd5428` | Machspeed_VGA_GUI_2100_VLB.vbi |
| Cirrus Logic CL-GD5429 | `cl_gd5429` | 5429.vbi |
| Cirrus Logic CL-GD5430 | `cl_gd5430` | gd5430/pci.bin |
| Cirrus Logic CL-GD5434 | `cl_gd5434` | gd5434.bin |
| Compaq CGA | `compaq_cga` | (character generator: mda.rom) |
| Diamond Stealth 32 (ET4000/w32p) | `stealth32` | et4000w32.bin |
| Diamond Stealth 3D 2000 (S3 ViRGE) | `stealth3d_2000` | s3virge.bin |
| EGA | `ega` | ibm_6277356_ega_card_u44_27128.bin |
| Hercules | `hercules` | (character generator: mda.rom) |
| Hercules InColor | `incolor` | (character generator: mda.rom) |
| IBM 1MB SVGA Adapter/A (GD5428) | `ibm1mbsvga` | SVGA141.ROM |
| Image Manager 1024 | `im1024` | im1024font.bin |
| Kasan Hangulmadang-16 (ET4000AX) | `kasan16` | et4000_kasan16.bin<br>kasan_ksc5601.rom |
| Matrox Mystique | `mystique` | MYSTIQUE.VBI |
| MDA | `mda` | mda.rom |
| MDSI Genius | `genius` | 8x12.bin |
| Number Nine 9FX (S3 Trio64) | `n9_9fx` | s3_764.bin |
| OAK OTI-037 | `oti037` | oti037/bios.bin |
| OAK OTI-067 | `oti067` | oti067/bios.bin |
| Olivetti GO481 (PVGA1A) | `olivetti_go481` | oli_go481_lo.bin<br>oli_go481_hi.bin |
| Paradise Bahamas 64 (Vision864) | `bahamas64` | bahamas64.bin |
| Phoenix S3 Trio32 | `px_trio32` | 86c732p.bin |
| Phoenix S3 Trio64 | `px_trio64` | 86c764x1.bin |
| Plantronics ColorPlus | `plantronics` | (character generator: mda.rom) |
| Professional Graphics Controller | `pgc` | none detected (UNCHECKED) |
| S3 ViRGE/DX | `virge375` | 86c375_1.bin |
| Sigma Color 400 | `sigma400` | sigma400_font.rom<br>sigma400_bios.rom |
| Trident TVGA8900D | `tvga8900d` | trident.bin |
| Trident TVGA9000B | `tvga9000b` | tvga9000b/BIOS.BIN |
| Trident TGUI9400CXi | `tgui9400cxi` | 9400CXI.vbi |
| Trident TGUI9440 | `tgui9440` | 9440.vbi |
| Trigem Korean VGA (ET4000AX) | `tgkorvga` | tgkorvga.bin<br>tg_ksc5601.rom |
| Tseng ET4000AX | `et4000ax` | et4000.bin |
| VGA | `vga` | ibm_vga.bin |
| Wyse 700 | `wy700` | wy700.rom |

On-board video for machines with `MODEL_GFX_FIXED` or
`MODEL_GFX_DISABLE_*` comes from the machine's own ROM set and is already
in the 5.1 table (`ama932j/oti067.bin`, `acer386/oti067.bin`,
`pb570/gd5430.bin`, `pb520r/gd5434.bin`, `infowaym/gd5434.bin`,
`elx_pc425x/elx_pc425x_vbios.bin`, `spc4620p/31005h.u8`+`.u10`,
`spc6033p/phoenix.bin`, `pc1640/40100`, `pc2086/40186.ic171`,
`pc3086/c000.bin`, `megapc/*`).

### 5.3 Sound, storage-controller and font ROMs

| Device | internal | ROM file(s) |
|---|---|---|
| Sound Blaster AWE32 | `sbawe32` | awe32.raw (`src/sound_emu8k.c`, `src/sound_sb.c`) |
| [MFM] DTC 5150X | `dtc5150x` | dtc_cxd21a.bin |
| [MFM] Fixed Disk Adapter (Xebec) | `mfm_xebec` | ibm_xebec_62x0822_1985.bin |
| [ESDI] IBM ESDI Fixed Disk Controller | `esdi_mca` | 90x8970.bin<br>90x8969.bin |
| [ESDI] Western Digital WD1007V-SE1 | `wd1007vse1` | 62-000279-061.bin |
| [IDE] XTIDE | `xtide` | ide_xt.bin |
| [IDE] XTIDE (AT) | `xtide_at` | ide_at.bin |
| [IDE] XTIDE (PS/1) | `xtide_ps1` | ide_at_1_1_5.bin |
| [SCSI] Adaptec AHA-1542C | `aha1542c` | adaptec_aha1542c_bios_534201-00.bin |
| [SCSI] BusLogic BT-545S | `bt545s` | BusLogic_BT-545S_U15_27128_5002026-4.50.bin |
| [SCSI] IBM SCSI Adapter with Cache | `ibmscsi_mca` | 92F2244.U68<br>92F2245.U69 |
| [SCSI] Longshine LCS-6821N | `lcs6821n` | `Longshine LCS-6821N - BIOS version 1.04.bin` |
| [SCSI] Rancho RT1000B | `rt1000b` | Rancho_RT1000_RTBios_version_8.10R.bin |
| [SCSI] Trantor T130B | `t130b` | trantor_t130b_bios_v2.14.bin |

No other sound card needs a ROM: the Adlib/OPL is Nuked-OPL3 in C, the
Gravis Ultrasound and the Ensoniq AudioPCI have no firmware in PCem, and
the SID in `resid-fp` is a model, not a dump.

### 5.4 The character-generator ROMs (easy to miss, and required)

`loadbios()` (`src/mem_bios.c:61-64`) unconditionally tries four fonts
before anything else, and `loadfont()` (`src/video.c:922-931`) returns
**silently** when a file is absent. The result of a missing `mda.rom` is
not an error - it is blank text. `fontdat` (CGA) and `fontdatm` (MDA,
Hercules, InColor, ColorPlus, Compaq CGA) both come from it.

| File | Feeds | Needed when |
|---|---|---|
| `mda.rom` | `fontdat` / `fontdatm` | any CGA/MDA/Hercules-class card, and every machine with built-in CGA-class graphics |
| `wy700.rom` | `fontdatw` | video card `wy700` |
| `8x12.bin` | MDSI Genius font | video card `genius` |
| `im1024font.bin` | Image Manager 1024 font | video card `im1024` |

### 5.5 The ten hashes the sources publish

Everything else in 5.1-5.4 is UNCHECKED for hash. These are quoted from
the TASVideos pages read on 2026-09-20 and from the READMEs inside the
three downloaded DOS packages.

| File | MD5 | SHA-1 | Where it came from |
|---|---|---|---|
| ga686bx/6BX.F2a | 8ea65e0c1c4934e9a0105bb3fe33a9e9 | 637e1b3863694ffd15a40585fd563329be3873d4 | `/EmulatorResources/PCem/Windows/Configurations` |
| voodoo3_3000/3k12sd.rom | ecc400ecd2fd7e5e4efd11f4bf837afd | 2825b702633553c7a7a3daea98b56f67bd016030 | same |
| awe32.raw | 30b76c45ca0712418239d2b15c65881a | 6ac3c1317c1acb83902397d7767763cca4de357a | same |
| ibm_vga.bin | 2057a38cb472300205132fb9c01d9d85 | - | `/EmulatorResources/PCem/DOS/Configurations`, Late 80s |
| deskpro386/109592-005.u11.bin | 70e208d5992be21b26fe796d76964c1d | - | same |
| deskpro386/109591-005.u13.bin | d35bab5b74fc21fef900b14b0807e126 | - | same |
| pb570/1007by0r.bi1 | 26d8f651e468874e852bdd1ffb6e6804 | - | same, Early 90s |
| pb570/1007by0r.bio | 15d245587d08979f16383190c55fb153 | - | same |
| pb570/gd5430.bin | d4ba691ce5e04d950ca7624050e6b409 | - | same |
| 86c764x1.bin | fbc57ef320053c50d9034ef493abda4d | - | same, Late 90s |

Non-firmware hashes the same sources publish, worth recording because the
preset packages cite them: `deskpro386.nvr` md5
367072234f7095aebb4e663fb1dc8f98 (PCem's own `nvr/default/`),
`pb570.nvr` md5 9228bf13f3fe46351b1885e073558b67, the Compaq diagnostic
disk md5 09652bd116a0af7ed006996b001ab9c9, FreeDOS 1.2 `FLOPPY.img` md5
36689ac4152d8efb13ab230e9aea46ec, `FD12LGCY.iso` md5
a55750577b4c3b88dba489df4f929f88, and the TASVideos DOS disk images
(late80s.img 257208622a9d51f5504eacdabd161955, early90s.img
5512d2895d082655d75e78e82ce48280, late90s.img
4970100ff619201db3de803eba9cd3d9).

### 5.6 How this becomes Chimera firmware

The DOSBox-X pattern exactly, one entry per file, `requiredWhen` keyed
on the settings that select the hardware:

```json
{ "id": "pcem.ga686bx.6BX.F2a", "display": "Gigabyte GA-686BX BIOS",
  "description": "The flash BIOS of a Gigabyte GA-686BX you own.",
  "name": "6BX.F2a", "label": "GA-686BX F2a",
  "sha1": "637E1B3863694FFD15A40585FD563329BE3873D4",
  "requiredWhen": { "setting": "machine", "is": "ga686bx" } }

{ "id": "pcem.mda.rom", "display": "IBM character generator ROM",
  "name": "mda.rom", "label": "MDA/CGA font",
  "requiredWhen": { "any": [
      { "setting": "videoCard", "in": ["mda","cga","hercules","incolor","plantronics","compaq_cga"] },
      { "setting": "machine",   "in": ["ibmpcjr","tandy","tandy1000hx","tandy1000sl2","pc1512","olivetti_m24","t1000","t1200","t3100e"] } ] } }
```

Roughly **200 firmware entries**, generated from the same tables as the
settings so they cannot drift. Entries whose hash is not yet known are
declared without `sha1` - the engine allows that ("An entry may omit the
hash only for a file no one can pin") and records the chosen file's
actual hash into the project - but the intent is to fill every one in at
M2 rather than lean on that.

Two consequences worth stating plainly:

- **The wizard's firmware page will ask for one to five files, never
  two hundred**, because the conditions narrow to the chosen machine,
  video card and sound card. A user who picks the "Late 90s DOS" preset
  is asked for `ga686bx/6BX.F2a` and `86c764x1.bin`. That is the whole
  point of doing it this way.
- **The Firmware folder answers forever.** Anyone who already keeps a
  PCem ROM set drops the folder in once and every future PCem project
  finds its files by hash.

---

## 6. Predefined configurations, and a better way to offer them

### 6.1 What TASVideos publishes (read, and reproduced here)

Six configurations, four for DOS and two for Windows. The DOS three that
ship as downloadable packages were unpacked and their `.cfg` files read
directly; the Windows two and the Early 80s one are transcribed from the
wiki's step lists.

| Preset | Machine | CPU | RAM | Video | Sound | Disk geometry | Mouse | OS |
|---|---|---|---|---|---|---|---|---|
| Early '80s | (a UserFile, not a package; not read) | - | - | - | - | - | - | floppy-only, no hard drive |
| Late '80s | `deskpro386` (Compaq Deskpro 386) | Intel i386DX/20, no FPU, **dynarec off** | 4 MB | IBM VGA | Sound Blaster Pro v2, 0x220/IRQ7/DMA1, NukedOPL | 17/15/900 = 112 MB | Microsoft serial 2-button | FreeDOS 1.2 |
| Early '90s | `pb570` (Packard Bell PB570) | Intel Pentium 133, builtin FPU, **dynarec on** | 8 MB | built-in CL-GD5430, 2 MB | Sound Blaster 16, 0x220, NukedOPL | 17/15/1224 = 152 MB | PS/2 2-button | FreeDOS 1.2 |
| Late '90s | `ga686bx` (Gigabyte GA-686BX) | Intel Pentium II/450, **dynarec on** | 32 MB | Phoenix S3 Trio64, 4 MB, **+ Voodoo Graphics** | Sound Blaster 16, 0x220, NukedOPL | 63/16/2099 = 1033 MB | PS/2 2-button | FreeDOS 1.2 |
| Windows 95 | `ga686bx` | Intel Pentium II/233 | 256 MB | 3DFX Voodoo 3 3000, render threads 1, Fast VLB/PCI | Sound Blaster 16, NukedOPL | 63/16/8374 = 4121 MB | PS/2 2-button | Windows 95B OSR 2.1 |
| Windows XP | `ga686bx` | Intel Pentium II/450 | 256 MB | 3DFX Voodoo 3 3000, render threads 1, Fast VLB/PCI | Sound Blaster AWE32, NukedOPL | 63/16/8374 = 4121 MB | PS/2 2-button | Windows XP SP3 Home |

Common to all six: floppy A = 3.5" 2.88M (type 7), floppy B = 5.25" 1.2M
(type 2), `hdd_controller = ide`, `cd_model = pcemcd`, CD speed 24x
(DOS 80s/90s) or 72x (late 90s and Windows), LPT device none,
`enable_sync = 1`, `cdrom_drive = 200`.

The DOS presets ship as a 3.5-3.7 MB 7z containing a FreeDOS-preinstalled
`.img` (112-1033 MB), the `.cfg`, a clean `.nvr`, and a README listing
the ROM MD5s and a seven-step setup ritual. The Windows ones cannot ship
an image at all (Microsoft), so they ship an **installation movie**
instead: a libTAS `.ltm` that boots a blank 4 GiB image from the retail
ISO and installs the OS unattended, producing `flash.bin`, the `.nvr` and
the `.img` with published hashes.

**What is good about this.** The eras are chosen by game release date
(the DOS page literally says "for games released from 1990-01-01 to
1994-12-31"), which is the right question to ask someone who does not
know what a Packard Bell PB570 is. The hardware inside each is a
coherent, period-correct machine. The ROM list per preset is short and
explicit. The Windows path solves the "we cannot distribute an OS"
problem honestly.

**What is bad about it, and what Chimera must not copy.** It is a file
ritual, not a configuration. Seven manual steps, absolute paths, "ONLY
the part before the first dot", `dos2unix`, "delete flash.bin", "if there
is already a file named this, delete it and replace it", and a standing
warning that touching any of those files outside a movie desyncs
everything. Departing from a preset means hand-editing a `.cfg` and, if
the change is structural, recording a whole new installation movie with
"meticulous documentation". There is no middle ground between "use our
exact bytes" and "you are on your own".

### 6.2 What Chimera should do instead

The constraint Sergio set is: preserve the freedom to pick any of the 93
machines, while making the common cases easy. Three mechanisms were
considered.

**Option A - the DOSBox-X "auto" sentinel. REJECTED.** DOSBox-X has one
preset setting and every other knob defaults to `auto`, meaning "keep the
preset's value". It works there because DOSBox-X's firmware needs (MT-32,
PC-98 ROMs, IBM BASIC, a VGA BIOS) are few and independent of the
preset. It **cannot** work for PCem, because a firmware `requiredWhen`
condition has to ask "which machine?" and "which video card?" and
`auto` is not an answer. A preset whose knobs read `auto` cannot tell the
wizard which of 200 ROM files to ask for. This is the decisive argument
and it is worth stating up front in the eventual README.

**Option B - a new `presets` declaration in waterbox.config.** A named
list, each preset a map of setting name to value, which the wizard
applies as the defaults of step two and which the user then edits freely.
Clean, explicit, and orthogonal to machines. Cost: a chimera engine and
frontend change (parse, apply on entry to the settings page, re-apply on
preset change, leave everything editable), plus a decision about what the
project records when the user departs.

**Option C - `machines` + `settingOverrides`. RECOMMENDED, and it needs
no chimera change at all.** Chimera already has exactly this mechanism
and it was built for a different reason. Reading
`source/gui/Chimera.Emulation.Common/Waterbox/WaterboxConfig.cs:174-209`:
a machine's `settingOverrides` produces a narrowed declaration where

```csharp
Options = over.Options ?? decl.Options,
Default = over.Default ?? decl.Default,
```

**An override that supplies only `default` changes the default and
leaves the full option list intact.** That is a preset: it fills in the
machine, the CPU, the RAM, the video card, the sound card, the drive
types and the disk geometry, and the user can still open any of those
dropdowns and pick anything PCem supports. And because the overridden
values are REAL values (not `auto`), the firmware decision tree resolves
against them and asks for exactly the right ROMs.

So the package declares `machineSetting: "preset"` and a `machines` array
whose entries are the eras, not the motherboards:

```json
"machines": [
  { "id": "dos_late80s", "label": "DOS, late 1980s (Compaq Deskpro 386)",
    "when": ["dos_late80s"], "systemId": "DOS",
    "settingOverrides": {
      "machine":    { "default": "deskpro386" },
      "cpu":        { "default": "i386DX/20" },
      "memSizeKB":  { "default": 4096 },
      "videoCard":  { "default": "vga" },
      "soundCard":  { "default": "sbprov2" },
      "dynarec":    { "default": false },
      "driveAType": { "default": "288" }, "driveBType": { "default": "12" },
      "hddSectors": { "default": 17 }, "hddHeads": { "default": 15 }, "hddCylinders": { "default": 900 },
      "cdSpeed":    { "default": 24 } } },
  { "id": "dos_early90s", "label": "DOS, 1990-1994 (Packard Bell PB570)", ... },
  { "id": "dos_late90s",  "label": "DOS, 1995 and later (Gigabyte GA-686BX + Voodoo)", ... },
  { "id": "win95",        "label": "Windows 95 (GA-686BX, Pentium II/233, Voodoo 3)", "systemId": "PC", ... },
  { "id": "winxp",        "label": "Windows XP (GA-686BX, Pentium II/450, Voodoo 3, AWE32)", "systemId": "PC", ... },
  { "id": "linux",        "label": "Linux (GA-686BX, Pentium II, S3 Trio64)", "systemId": "PC", ... },
  { "id": "custom",       "label": "Custom machine", "when": ["custom"], "systemId": "PC",
    "settingOverrides": {} }
]
```

Four things fall out of this that are better than TASVideos' packages:

1. **The preset is a first-class recorded fact.** It is the machine the
   project pins and the movie cites, beside every exposed setting at its
   effective value. "Late 90s DOS with 64 MB instead of 32" is a legal,
   fully documented, reproducible configuration - something the TASVideos
   scheme can only express as "a modified .cfg, explain what and why".
2. **The DOS/PC platform split comes free and matches TASVideos.** Their
   landing page: "If you're running DOS, put Platform: DOS at the very
   start of the .ltm movie annotations [...] We merged Linux and Windows
   into PC, but DOS remained separate." A machine entry carries its own
   `systemId`, so the DOS presets declare `DOS` and the rest `PC`, and
   the movie header says so without anyone typing an annotation.
3. **The firmware page becomes two lines instead of a README.** Picking
   "Windows XP" asks for `6BX.F2a`, `3k12sd.rom` and `awe32.raw` - the
   three files the TASVideos Windows page asks for - and the Firmware
   folder probably already has them.
4. **"Custom" is not a second-class citizen.** It is one more entry whose
   overrides are empty, so the user gets all 93 machines and PCem's own
   defaults, and the firmware tree follows wherever they go.

The trade-offs, stated honestly:

- **A preset is not a lock.** Once the user changes `machine`, the
  recorded preset name ("Late 90s DOS") no longer describes the machine.
  Nothing is wrong - every effective setting is recorded and the label is
  decoration - but the label can mislead a reader of the movie header.
  Mitigation: the frontend already shows the settings; the README should
  say the preset names the starting point, not the result. A stricter
  alternative would re-label a departed preset as "custom", which needs
  frontend work and is not worth it at M1.
- **`machines` was designed for genuinely different machines** (a Mega
  Drive and a Master System), and using it for six flavours of the same
  IBM PC is a stretch of the concept. It is a stretch the code supports
  exactly, and the doc's own framing - "Because the machine IS a setting"
  - survives it. If it ever grates, Option B is the refactor, and nothing
  in the core changes.
- **UNCHECKED:** whether the wizard re-applies a machine's overridden
  defaults when the user switches presets mid-wizard, or only on first
  entry. `WaterboxConfig.cs` caches narrowed declarations per machine id,
  which is consistent with re-applying, but the frontend path was not
  read. This is the one thing M4 must verify before the preset design is
  declared done.

### 6.3 What ships beside the presets

- **The DOS disk images.** TASVideos' three FreeDOS images are
  redistributable (FreeDOS is GPL), 112/152/1033 MB, and their hashes are
  published. Either ship them zstd-compressed as package assets the way
  DOSBox-X ships its formatted FAT16 disks, or - better - ship the
  **empty formatted geometries** and a first-boot FreeDOS install as a
  documented Chimera movie. The first is easier and is what M4 should do.
- **The Windows and Linux installations stay the user's.** They provide
  the retail ISO; the core provides an empty 4121 MB disk; the
  installation is an ordinary Chimera movie whose product is an exported
  save-data image that goes back into the `hdd` slot. This is strictly
  better than the libTAS arrangement, because the exported image is
  hash-pinned by the project rather than by a wiki page.

---

## 7. The five questions

### 7.1 How does it draw a frame with no GPU?

Way 1, entirely. Every one of the 47 cards rasterises in C into
`buffer32`, a 2048x2048 32-bit `BITMAP` in ordinary heap memory
(`src/video.c:1069`, `src/video.h:1-7`). When a card finishes a screen it
calls `video_blit_memtoscreen(x, y, y1, y2, w, h)`
(`src/video.c:1111-1117`), which indirects through
`video_blit_memtoscreen_func` - a function pointer the platform layer
installs. The driver installs its own, copies the rectangle into the
core's BGRA buffer and records `w`/`h` for `GetVideoWidth`/`Height`.
`makecol32(r,g,b) = b | (g<<8) | (r<<16)` (`src/video.h:19-20`) is
already the BGRA byte order Chimera wants.

The 3D cards are the same story: the Voodoo, Banshee, Voodoo 3, Mystique
and ViRGE all have full C rasterisers (`vid_voodoo_render.c`,
`vid_mga.c`, `vid_s3_virge.c`) with an x86/x86-64 code generator as an
*optimisation* (`vid_voodoo_codegen_x86-64.h`), not a requirement. There
is no llvmpipe question, no GPU bridge, no `drawEveryFrame` semantics -
this core never touches the host GPU. Compared with flycast (which had
to port `refsw`) and rpcs3 (which needed the whole HW bridge), this is
the cheapest renderer situation of any core in the house.

Frame sizes change: DOS text is 720x400, VGA 640x480, XP 800x600 or
1024x768. Capacity 2048x2048 is declared; the live size comes per frame.

### 7.2 What makes it deterministic, and where is the clock seam?

**The seam is `time_internal_sync()`**, `src/rtc.c:227-238`:

```c
void time_internal_sync(uint8_t *nvrram) {
        time_t cur_time;
        time(&cur_time);
        cur_time_tm = localtime(&cur_time);
        time_internal_set(cur_time_tm);
        time_set_nvrram(nvrram, cur_time_tm);
}
```

and its TC8521 twin at `src/rtc_tc8521.c:206`. Called when the config's
`enable_sync` is on (`src/pc.c:840`, default 1; every published TASVideos
config has it on). After the seed, `internal_clock` is advanced by the
RTC's own emulated timer, so the machine's clock is emulated-time from
then on. Replace the two calls with an `rtcBase` sync setting and the
seam is closed.

What the sandbox does anyway: `clock_gettime` returns a constant
(1495889068 = 2017-05-27 12:44:28 UTC) for every clock id, and
`gettimeofday` and `time` are **not implemented at all** - a guest that
calls one is killed with "the core asked for system call N which the
sandbox does not provide". Since musl's `time()` is built on
`clock_gettime`, PCem inside the box would silently boot every machine on
2017-05-27 without the patch. That is deterministic but wrong (and, per
TASVideos' own measurements, a date outside 2000-2037 changes Windows XP's
boot time and can trip the Voodoo BIOS into "Invalid time/date"), so
`rtcBase` is not optional polish.

Other determinism facts, all read:

- `rand()` appears in three places that matter: the SN76489's noise
  counters at reset (`src/sound_sn76489.c:187-189`) and the dynarec's
  block eviction (`src/codegen_allocator.c:57`,
  `src/codegen_block.c:484`). In the guest that is musl's `rand()` from
  the default seed with nobody calling `srand()`, and its state is guest
  memory, so it is deterministic and savestated. No work needed - but
  note that native and guest builds use different libcs, so the
  native==sandbox gate leg has to account for it (link the same `rand`
  in run-native, as other cores do).
- `timer_read()` (`SDL_GetPerformanceCounter`) is used only to compute
  the status bar's "% CPU" for the S3, TGUI, ET4000/W32 and Voodoo
  blitters (e.g. `src/vid_s3.c:2783-2794`). It leaves with the platform
  layer. **UNCHECKED**: a full audit that no `timer_read` result feeds
  an emulated value; four call sites were read and none did.
- The x87 is modelled as `double ST[8]` (`src/x86.h:100`), not 80-bit
  long double. Deterministic within one build with `-ffp-contract=off`
  and no fast-math, which is the standing rule here anyway.
- **The interpreter and the recompiler are not guaranteed to agree**
  (separate FPU/MMX implementations in `x87_ops*.h` versus
  `codegen_ops_fpu_*.c`). `dynarec` must therefore be a sync setting, and
  the gate must run both.

### 7.3 What IS a frame for a PC emulator?

It is hard, and the honest answer is that a PC has no frame. The video
card refreshes on its own schedule (70 Hz for VGA text, 60 for 640x480,
whatever XP's driver programs), the CPU runs continuously, and DOS games
tear across the vertical retrace with no notion of a frame boundary at
all. Both DOSBox-X and Ruffle hit this and both answered "the core
defines the cadence".

PCem's answer already exists and is better than either, because the
TASVideos fork made it explicit: **a frame is a fixed quantity of
emulated CPU cycles**, `cycles_to_run = ms * cpu_get_speed() / 1000`
(`src/pc.c:511`), and libTAS runs it at 100 fps so `ms` is 10. Within
that slice the PIT, the RTC, the video card's retrace timer, the sound
poll at 48 kHz and every device timer fire off the emulated cycle counter
(`src/timer.c`), the card blits whenever it blits, and at the end of the
slice the driver reads whatever picture is in `buffer32` and whatever
samples accumulated.

Consequences to state plainly:

- **The frame is a slice of machine time, not a picture.** A 100 fps
  slice on a 70 Hz text mode means some frames repeat the previous
  picture and others straddle a retrace. That is the machine's behaviour
  and it is what every existing PCem movie already records.
- **`report_framerate_change(num, denom)`** (`src/video.c:1272`, added by
  the fork) gives the card's real refresh so the frontend can show it and
  an encode can use it - the DOSBox-X `_refreshRateNumerator` pattern.
- **`fpsNumerator`/`fpsDenominator` default to 100/1**, for direct
  comparability with every published PCem movie, with `0/0` meaning
  "follow the card". Changing it is a sync setting.
- Audio per frame is `48000 * denom / num` sample pairs (480 at 100 fps),
  with the odd sample carried the way Ruffle's M3 learned.

### 7.4 Threading

A waterbox guest is cooperative (miniBox green threads, round-robin by
ascending tid, switched at `sched_yield`/`nanosleep`/futex), which makes
threads deterministic but not free: **futex timeouts are ignored**, so a
wait that only a timeout can end is a deadlock, and miniBox kills the
guest when every thread is blocked.

PCem's thread inventory after the TASVideos fork, from
`grep thread_create src/*.c`:

| Where | Status |
|---|---|
| CPU / main loop | single-threaded; `runpc()` is a call |
| blit lock (`startblit`/`endblit`) | an SDL mutex in the platform layer (`src/wx-sdl2.c:156-164`); leaves with it |
| CD audio | single-threaded (`b4e28d6`) |
| S3, S3 ViRGE | single-threaded (`b4e28d6`, `55f0fdb`) |
| Voodoo 1/2, Banshee, Voodoo 3 | single-threaded (`b4e28d6`); `render_threads` is still a device config and **must be forced to 1** (TASVideos instruct this by hand for both Windows configs) |
| Sound | no thread; `sound_poll()` is an emulated timer at 48 kHz (`src/sound.c:166`) that calls `givealbuffer` inline |
| ET4000/W32p (`vid_et4000w32.c:1208`) | **still a FIFO thread** |
| TGUI9440 (`vid_tgui9440.c:763`) | **still a FIFO thread** |
| Matrox Mystique (`vid_mga.c:5331`) | **still a FIFO thread** + a DMA mutex |
| ATI Mach64 (`vid_ati_mach64.c:3422`) | **still a FIFO thread** |
| PGC (`vid_pgc.c:2403`) | **still a drawing thread** |

So the port's threading work is exactly five devices, each following the
recipe the fork already applied four times: drain the FIFO inline on the
register write instead of handing it to a thread. Until they are
converted, those five cards are simply not offered (their entries drop
out of the `videoCard` enum). Note that this is the same list TASVideos
implicitly avoids by recommending "S3 ViRGE/DX" as the default GPU.

No libco coroutine is needed - unlike DOSBox-X, whose `Normal_Loop` has
no return - because `runpc(ms)` returns on its own.

### 7.5 Savestates, and how big is a Windows XP state?

PCem has no savestate code, so the whole state is guest memory and
miniBox takes it. Allocation sizes read from `src/mem.c:1421-1448`,
`src/codegen_allocator.h:17-23`, `src/codegen_x86-64.h:1` and
`src/video.c:1069`, for the published Windows XP machine (`ga686bx`,
256 MB, Voodoo 3 3000):

| Region | Size | Note |
|---|---|---|
| `ram` | 256 MiB | `malloc(mem_size * 1024)` |
| `byte_dirty_mask` | 32 MiB | one bit per byte of RAM |
| `byte_code_present_mask` | 32 MiB | one bit per byte of RAM |
| `pages` | ~12.5 MiB | `((mem_size + 384) KiB) >> 12` entries of ~80 bytes |
| `readlookup2` + `writelookup2` | 16 MiB | 1M `uintptr_t` each |
| `page_lookup` | 8 MiB | 1M pointers |
| JIT arena | **120 MiB** | `MEM_BLOCK_NR 131072 * MEM_BLOCK_SIZE 0x3c0`, RWX |
| `codeblock[]` | ~1.4 MiB | `BLOCK_SIZE 0x4000` entries |
| `buffer32` | 16 MiB | 2048x2048x4 |
| Voodoo 3 3000 VRAM | 16 MiB | plus texture/aux buffers (UNCHECKED exact) |
| ROMs, NVRAM, misc | ~1 MiB | |
| **Total live guest memory** | **~510 MiB** | |
| Hard disk | **0** | the sparse overlay (4.2); a full-copy design would add 4121 MiB |

So **a full XP state is around half a gigabyte** - between a PS2 and a
PS3 in this project's terms, and well inside what the state manager
already handles (a PS3 state is 4.31 GiB and gets its own file, issue
#84; a 1 GB machine's anchor is 150-700 ms). A DOS machine with 4-32 MB
is 200-260 MiB, dominated by the JIT arena and the lookup tables.

Two levers if that proves heavy:

- **The JIT arena can be invisible.** It is 120 MiB of pure cache: every
  block is regenerable from guest RAM. xemu does exactly this for its
  256 MB TCG buffer (`alloc_invisible` + a `StateLoaded` flush) and it
  removed 215 MB from every state. The cost is a recompile storm after
  every rewind; the benefit is a quarter of the state gone. **Decide by
  measurement at M5, not now.** The lookup tables and dirty masks are in
  the same category (derivable) but are cheap to get wrong, so leave
  them stateful.
- **The dirty masks shrink with RAM.** 64 MB of RAM (plenty for Windows
  95 and for Linux) costs 16 MiB of masks instead of 64.

### 7.5b The dynamic recompiler in the sandbox

Worth its own answer, because it is the one thing that can kill the core.

**It should work.** miniBox grants RWX (`host.c:76-82`,
`MB_PROT_RWX`), and four cores already generate code inside the box:
rpcs3 (asmjit + LLVM), xemu (QEMU TCG, 256 MB), PPSSPP (x86-64 JIT plus
softgpu codegen) and flycast (`rec_x64`). Generated code lives at fixed
deterministic addresses because the guest ELF is `ET_EXEC` at
`0x36f00000000` with no relocation, so the JIT is savestate-safe by
construction.

Three specifics read from PCem's backend:

1. ~~**The allocation call needs a one-line patch.**~~ **MEASURED AT M1b:
   it does not. Risk deleted.** `src/codegen_allocator.c:34` passes fd `0`
   rather than `-1`, and miniBox's `mmap` never reads the fd at all -
   `dispatch_inner`'s `NR_mmap` case (`host.c:337-357`) uses `a1..a4` and
   discards the rest. PCem's call is accepted exactly as emulibc's `-1` is.
   Passing NULL as the address was already correct and matters more than it
   looked: a nonzero hint is `MAP_FIXED` to the sandbox **whether or not
   `MAP_FIXED` is set**, because the flag is never read - the decision is
   made purely on `addr != 0` (`memblock.c:827-875`). That is the trap
   PPSSPP had to patch.
2. **Distance is not a problem.** `call()`
   (`src/codegen_backend_x86-64_ops.c:24-43`) picks between a `CALL rel32`
   and a `MOV R9, imm64; CALL R9`. Its near-call test is
   `if (diff >= -0x80000000 && diff < 0x7fffffff)` with `diff` declared
   `uintptr_t`, so `-0x80000000` promotes to 2147483648 and the condition
   is **never true**: every call PCem's x86-64 backend emits is already
   the absolute 64-bit form. Fortunate for a guest at a distant fixed
   base. (A signedness bug upstream, and one this port must not "fix".)
3. ~~**Self-modifying pages cost a fault each.**~~ **MEASURED AT M1b:
   1.3 pages an epoch, not 30,000.** The mechanism is real - miniBox maps a
   clean RWX page RX and faults on the first write per epoch
   (`memblock.c:97`) - but two things stop it biting. PCem touches only
   **10.4%** of the 120 MiB it allocates (12.5 MiB, after five minutes of
   Windows XP Setup), and an untouched page never faults; and a page written
   in three consecutive epochs is promoted to **hot** and stops being held
   read-only (`memblock.c:89-91`, `HOT_AFTER 3`), which is exactly a
   recompiler's access pattern. Putting the arena in invisible memory, the
   xemu remedy, measured about 10% SLOWER for 13.8 MiB saved on a 287 MiB
   state, so it is available but not the default (see `M1B.md` section 3).

**The interpreter-only fallback.** `exec386()` instead of
`exec386_dynarec()` is a config flag away (`src/pc.c:514-519`), and the
Late 80s TASVideos preset runs that way (`cpu_use_dynarec = 0` for a
386DX/20). The cost is **UNMEASURED** - PCem's own documentation gives no
ratio, and nothing was built for this document. What can be said: the
project's guidance for a Pentium II-class machine is the recompiler, and
a 450 MHz Pentium II interpreted instruction by instruction on a modern
host is, by any reasonable estimate, far below real time. If the JIT
cannot run in the box, Windows XP is off the table and so is most of the
core's justification.

---

## 8. Licence

| Component | Licence | Evidence | In a package? |
|---|---|---|---|
| PCem (upstream) | **GPL-2.0, no "or later" statement** | top-level `COPYING` is the GPLv2 text; `README.md`: "PCem is licensed under GPL v2.0"; the `.c` files carry no per-file headers at all | yes; the core is GPL-2.0 |
| TASEmulators/pcem | same | the fork adds no LICENSE; `COPYING` unchanged; `README.md` unchanged on this point | yes |
| DOSBox pieces (`src/dosbox/dbopl.*`, `cdrom_image.cpp`, `cdrom.h`) | GPL-2.0-or-later | file headers: "Copyright (C) 2002-2015 The DOSBox Team [...] either version 2 of the License, or (at your option) any later version" | yes |
| Nuked OPL3 (`src/dosbox/nukedopl.*`) | **no licence header in PCem's copy** | the file states authorship and version 1.7.4 only; upstream `nukeykt/Nuked-OPL3` is LGPL-2.1 | yes if upstream LGPL-2.1 is confirmed - **VERIFY** |
| reSID-FP (`src/resid-fp/`) | GPL-2.0-or-later | per-file headers, "Copyright (C) 2004 Dag Lem" | yes |
| minivhd (`src/minivhd/`) | MIT | `src/minivhd/LICENSE`, "Copyright (c) 2019-2020 Sherman Perry" | yes |
| slirp (`src/slirp/`) | BSD + "Copyright (c) 1995 Danny Gasparovski" | headers | not compiled (networking off) |
| ne2000 (`src/ne2000.c`) | LGPL (Bochs) | header: "GNU Lesser General Public License" | not compiled |
| PCem default NVRAMs (`nvr/default/*.nvr`) | part of the PCem tree, GPL-2.0 | | yes |
| TASVideos FreeDOS disk images | FreeDOS is GPL-2.0 | | yes, if shipped (4.6 / 6.3) |
| Machine, video and sound ROMs | **copyrighted third-party dumps** | PCem: "NO COPYRIGHTED ROM FILES ARE INCLUDED NOR WILL THEY BE. PLEASE DO NOT ASK FOR THEM." | **never** - firmware only |

**`package-licenses.json` can be written honestly**, with
`"effectiveTerms": "GPL-2.0"` (note: **not** `-or-later`, unlike the
DOSBox-X package - PCem makes no "or later" grant, so the combined work
is GPL-2.0-only and nothing GPL-3.0 may ever be linked in).

Two items to settle before M1 is declared done:
1. Confirm Nuked-OPL3's upstream licence and that LGPL-2.1 code may be
   combined into a GPL-2.0-only binary (it may - LGPL-2.1 section 3
   permits conversion to GPLv2 - but say so in the file).
2. Confirm `resid-fp`'s `COPYING` and whether the SID is even needed
   (it backs the SSI2001, one obscure card; dropping it removes a
   component and 10,000 lines).

---

## 9. Recommendation

**Build it, gated by M1 on speed.** The order of work:

1. **The licence is clear and the renderer is free.** Neither is true of
   most candidates.
2. **The TASVideos fork is the upstream to pin**, not stock PCem: it has
   the sliced `runpc(ms)`, the refresh-rate callback, and four of the
   nine threaded devices already converted. Pin `TASEmulators/pcem` at
   `fd4585b` as `extern/pcem`, carry the port as `patches/` full-file
   copies (the chimera-core-ppsspp recipe DOSBox-X uses).
3. **Follow the DOSBox-X core in shape**: generated `waterbox.config`,
   composed `.cfg` in guest memory, wide input via `SetButton`, sparse
   disk overlay, savedata export, `run-native` / `run-wbx` / `run-gate.sh`
   / `tests/run-frontend.sh`.
4. **Do the presets with `machines` + `settingOverrides`**, not with
   `auto` sentinels, because the firmware tree depends on real values.
5. **Say no to**: networking (slirp, ne2000), host CD-ROM ioctl,
   MIDI passthrough, the wx GUI and its shader pipeline, the five
   still-threaded video cards (until converted), and PCem's `.cfg` as a
   user-supplied file.

### Milestones

- **M0 DONE (2026-09-20): this document.** Sources read; no repository,
  no submodule, no build. The clone in `src/` is scratch.

- **The repository (2026-09-20).** `~/chimera-cores/pcem` is now a real
  local git repository. It is **local only**: no GitHub remote, no push,
  on Sergio's instruction, until he says otherwise. `src/` is in
  `.gitignore` and stays scratch.

  **The upstream is pinned as a submodule at `extern/pcem`:**

  | | |
  |---|---|
  | Remote | `https://github.com/TASEmulators/pcem.git` |
  | Commit | `fd4585bb1eb2c411819391c7241c1ed5b621dfc7` |
  | Subject | "Backport S/VGA timing fix from 86Box" |
  | Branch / tag | `master`, one commit past `17+st-2` |

  **Why this and not `pcem-emulator.co.uk` v17**, restating section 9
  item 2 with the commit names: the fork carries `13a3d2e`, which turned
  `runpc()` into `runpc(uint64_t ms)` and made a frame a fixed slice of
  emulated CPU cycles - the Chimera `FrameAdvance` contract, already
  written; `b741ee1`/`9191d03`, which added
  `report_framerate_change(num, denom)` - the refresh-rate callback a
  Chimera video declaration needs; and `b4e28d6`/`55f0fdb`, Clement
  Gallet's single-threading of the blit lock, CD audio, S3, S3 ViRGE,
  Voodoo and Banshee, which is the hardest single problem in the port
  and is half-solved here by someone who solved it for the same reason.
  Stock v17 has none of the three. `fd4585b` rather than the `17+st-2`
  tag because the two commits past it (`d54e060`, `fd4585b`) are an SVGA
  fps-reporting fix and an S/VGA timing fix backported from 86Box -
  both wanted, neither risky.

- **M0b DONE (2026-09-20): `UPSTREAM.md`.** PCem or 86Box, settled before
  M1 on Sergio's instruction. Answer: **stay on PCem**, because the two
  properties the sandbox cares about most - one clock seam instead of five,
  and five surviving device threads instead of about fourteen - are PCem's,
  and because 86Box's own FAQ discourages the one workload that justifies
  the core. That document also splits M1 in two, below.

- **M1a - the native speed measurement (2 days, and the decision point).**
  Evidence found while evaluating 86Box (`UPSTREAM.md` section 9) moved
  this to the front and made it much cheaper: 86Box publishes host
  single-thread thresholds ("~4000 = Pentium II 300 MHz") and the
  TASVideos Windows XP machine is a Pentium II/450, so the target may be
  at the edge of what a NATIVE build manages on a fast desktop - which no
  amount of sandbox work would fix. So: build stock PCem `17+st-1`
  natively on this machine, reproduce the published TASVideos Windows XP
  configuration, and measure the emulator's own speed percentage at the
  desktop and under a representative workload, dynarec on, recording the
  host CPU. Repeat for Windows 95 (Pentium II/233) and for Late 90s DOS
  (Pentium II/450 in DOS, a far lighter load). Also price the fallback
  with `cpu_use_dynarec = 0`. No port work at all.
  - **Native XP at or near 100%** -> M1b, and XP stays the headline.
  - **Native XP well below 100%** -> XP leaves the scope; re-justify the
    core on Windows 95/98, Linux, post-Voodoo-1 3D and the non-IBM PCs,
    and take the build decision again on those terms rather than
    inheriting it.
  - **Native Windows 95 also below 100%** -> stop. "Neither" is the
    answer and DOSBox-X keeps the DOS catalogue.

  **M1a DONE (2026-09-20): GREEN. See `M1A.md`.** The TASVideos XP
  machine - GA-686BX, Pentium II/450, 256 MB, Voodoo 3 3000, AWE32,
  dynarec on, with the ROMs and the install ISO whose published MD5s all
  match - sustains **148.9% to 154.9%** of real time on this desktop
  through XP Setup's spinning prompt, which is the worst case measured;
  3100% while the guest is halted on the CD. XP stays the headline. Not
  proven: XP at the desktop (the install needs a product key this session
  does not have), the interpreter fallback, and anything about the
  sandbox.

- **M1b - the JIT in the box (1 week, hard stop; only if M1a is green or
  amber).** A guest build of just the CPU, memory and recompiler under
  miniBox's musl toolchain: does the arena allocate, does generated code
  execute, and what do the RWX dirty-page faults cost. Includes the
  one-line `mmap` fd fix (7.5b). **Green** -> M2. **Red** (the JIT cannot
  run in the box and the interpreter is far too slow) -> stop.

  **M1b DONE (2026-09-20): GREEN. See `M1B.md`.** The WHOLE emulation
  core built for the guest, not the planned subset; it passes
  `check-wbx.sh`; and it produces a **bit-identical** machine to the
  native build on three workloads including 300 s of real Windows XP
  Setup. The sandbox costs **15%** (floor 129.6% against native's
  152.5%). The named risk did not materialise: the arena contributes
  **1.3 dirtied pages per epoch**, not 30,720, because PCem touches only
  10.4% of the 120 MiB it asks for and miniBox's hot-page rule exempts
  what it does touch. Two corrections to this document follow in 7.5b.
  One amber finding, not PCem's: at one epoch per emulated frame the
  CPU-bound floor is 44.4%, so generic dirty-tracking on a 256 MB machine
  is where M1a's headroom goes.

- **M2 - the machine, native (2 weeks; was 3 before M1a measured the
  platform surface at 28 symbols, see section 2).** Strip the platform
  layer:
  `wx-*`, `soundopenal.c`, `thread-pthread.c`, `plat-*` replaced by
  `pcem-driver.cpp`. `video_blit_memtoscreen_func` to BGRA;
  `givealbuffer`/`givealbuffer_cd` to a per-frame vector; keyboard,
  mouse, joystick from the input block; `romfopen()` mapped onto flat
  mounted firmware ids; `time_internal_sync` replaced by `rtcBase`;
  `hdd_file.c` turned into a sparse overlay; NVRAM and flash into guest
  memory. Convert or drop the five threaded cards. `run-native` drives
  `runpc(10)` in a loop. **Gate**: the Late 80s and Late 90s TASVideos
  DOS configurations boot FreeDOS to a prompt with identical digests over
  two runs; a stalled host changes nothing; the same build reading the
  wall clock fails the leg; a different key schedule changes the machine.
  Also at M2: hash a known-good v17 ROM set and fill in section 5.

  **Three legs M1b earned the hard way, and they are requirements, not
  suggestions** (`M1B.md` section 6b; `~/chimera/docs/gates.md`):

  1. **Liveness before any measurement.** A dead guest returns 0 from
     every call, so it "completes" the workload instantly - M1b's
     negative control printed `speed=2339127.5%` from a machine that
     never executed an instruction. **Every leg must assert
     `wbx_get_death` is clear AND that the frame counter moved, before
     it looks at any other number.** This is gates.md mode B and it is
     now a known instance in this project, so a gate that omits it is a
     regression, not an oversight.
  2. **A per-frame digest stream, not an end-state digest.** M1b's
     end-of-run framebuffer digest did not notice the CPU being changed
     from a Pentium II/450 to a /233 - the last screen is the same text
     either way. Only the frame count moved. An end-state digest is
     evidence, not proof.
  3. **Every leg proven to bite**, by breaking the thing and watching it
     go red, and said so in the commit - as `patches/0002` was.

- **M3 - the guest (2 weeks).** musl/GCC guest build, `-mcmodel=large`,
  no TLS, `check-wbx.sh` clean, `run-wbx.c`; native == sandbox on every
  M2 leg; savestate round-trip around every frame lossless; a new host
  finishing a run from a state. Memory domains. Decide the JIT arena's
  visibility by measuring both.

- **M4 - the package (2 weeks).** Generated `waterbox.config` (93
  machines, 290 CPUs, 47 video cards, 20 sound cards, 17 HDD
  controllers), the ~200 firmware entries, `file_slots.json`, the six
  presets as `machines` + `settingOverrides`, `default_keybinds.json`,
  `package-licenses.json`, savedata export of disk + NVRAM + flash, the
  formatted-disk assets. **Gate**: the frontend legs - package boots, the
  wizard asks for exactly the right ROMs for each preset, settings reach
  the machine, save data round-trips - plus the verification that
  switching preset mid-wizard re-applies its defaults (6.2, the one
  unverified piece of the design).

- **M5 - Windows and Linux (open-ended).** Reproduce the TASVideos
  Windows 95 and XP installations as Chimera movies whose product is an
  exported disk image; Tiny Core Linux the same. Each failure here is a
  finding, not a broken gate. Then the compatibility tail: the five
  converted video cards, the Voodoo 2 SLI, the MCA machines, PCem's
  `TESTED.md` list as a checklist.

### Costs

About **eight weeks to a shipping DOS/Windows-95 core** (M1-M4) if M1 is
green, plus open-ended M5. (Nine before M1a; M2 lost a week when the
platform surface turned out to be 28 symbols rather than 13,000 lines.) That is comparable to DOSBox-X's own port and
cheaper than flycast's, because there is no renderer to write, no
savestate system to neuter, no coroutine slicing to invent, and the
hardest single problem - single-threading - is half-solved upstream by
someone who solved it for the same reason.

---

## 10. Risks, ranked

1. **Speed (M1).** The whole case rests on machines DOSBox-X cannot run,
   and those machines are Pentium II-class. If the dynarec does not run
   in the sandbox, or the RWX dirty-fault cost eats the win, the core
   loses its reason to exist. Mitigated by making it the first
   milestone with a hard stop.
2. **The ROM hash table (section 5).** 200 firmware entries and ten
   published hashes. Every other hash has to be established from a
   known-good set, and a wrong one means a user with a perfectly good
   dump cannot create a project. Mitigated by allowing hash-less entries
   at first and filling them in at M2, and by the fact that PCem itself
   never checks.
3. **The five threaded video cards.** Bounded and enumerable, but the
   Mystique's FIFO thread also carries a DMA mutex and is the largest of
   the five. Worst case they stay unoffered; nothing in the TASVideos
   corpus uses them.
4. **The `.cfg` surface is enormous.** 93 machines x per-machine CPU
   lists x 47 video cards x 20 sound cards x per-device config blocks is
   a generated declaration of a few thousand lines, and every combination
   is a machine somebody can pick. Most combinations have never been
   tested by anybody. The README must say so, and the presets exist
   precisely so that nobody has to.
5. **Interpreter/dynarec divergence.** They are separate implementations
   of the same instructions; a movie recorded under one will not
   necessarily play under the other. Handled by making `dynarec` a sync
   setting, but it doubles the gate.
6. **Disk-geometry mistakes.** PCem stores C/H/S in the config, not in
   the image, and a wrong geometry is a disk that does not boot with no
   useful error. The derived-from-size path has to be right, and the
   exported save-data image has to carry its geometry (a header, or the
   project's settings).
7. **Upstream is dead.** PCem v17 is from 2020 and the project has not
   released since; the TASVideos fork is the living branch and it is one
   person's. 86Box is the active continuation (v7.0) and the fork already
   backports from it (`fd4585b`). **EVALUATED 2026-09-20, see
   `UPSTREAM.md`**: 86Box was measured against every heading of this plan
   and PCem was kept, on the clock seam, on threading and on speed. The
   risk stands as a long-term maintenance question - re-run the comparison
   if `TASEmulators/pcem` stops, since by then M1a will have settled the
   speed question that decided it.
8. **The target machine may be out of reach natively** (new, and now
   ranked with the rest). 86Box's published host thresholds put a Pentium
   II/300 at ~4000 single-thread, and TASVideos' Windows XP configuration
   is a Pentium II/450. PCem is the faster of the two emulators, but "a
   Ryzen 5 5600X holds ~100% at a Pentium II/350 in PCem" is the best
   evidence there is and it is one person's report. M1a exists to replace
   it with a measurement.

---

## 11. What would change this plan

- **M1a red** -> the answer is "not worth it". Say so plainly, keep this
  document, and revisit if someone ports a faster x86 recompiler.
- **M1a amber** -> Windows XP leaves the scope and section 1's case has to
  be re-argued on Windows 95/98, Linux, post-Voodoo-1 3D and the non-IBM
  PCs alone. Still more than DOSBox-X does; a smaller prize.
- **86Box instead of PCem. EVALUATED - see `UPSTREAM.md`.** The answer was
  PCem, on the clock seam (one versus five, one of 86Box's being `rdtsc`
  in inline assembly the sandbox cannot trap), on threading (five
  surviving device threads versus about fourteen, with the only
  conversion work three major versions stale), and on speed and Windows
  XP, where 86Box's own FAQ says to use something else. 86Box wins on
  licence (`-or-later`), machine coverage (521 versus 93), the frame
  (already slice-parameterised upstream), the recompiler's allocator and
  call emission, and having a test device and a test suite. It remains the
  fallback if `TASEmulators/pcem` dies, at roughly +5 to +7 weeks.
- **DOSBox-X grows real XP support.** Unlikely - its own README points
  elsewhere - but it would remove the largest part of the case.
- **Chimera gains a real `presets` declaration** (Option B in 6.2) ->
  move the presets to it; nothing in the core changes.

---

## 12. What in this document is NOT verified

Stated separately because "fixed by construction" reasoning has cost this
project real bugs this week.

- **Nothing was built, run or measured.** Every performance statement -
  the interpreter's cost, the JIT's cost in the box, XP's boot time under
  Chimera, the ~510 MiB state size - is arithmetic over sizes read from
  source, not a measurement.
- **The ROM hashes.** Ten are quoted from TASVideos. The other ~200 files
  in section 5 have no hash from any source consulted, and no ROM set was
  obtained or hashed.
- **`windowsxp.cfg` and `windows95b.cfg` were not read.** Their download
  links (TASVideos UserFiles 638283187222055723 and the 95 equivalent)
  return "DoesNotExist". The Windows rows in 6.1 are transcribed from the
  wiki's prose step lists, which are detailed but are not the file.
- **The Early '80s DOS preset was not read** - it is a UserFile, not a
  package.
- **The three DOS `.cfg` files WERE read** (extracted from the published
  7z archives), as were their READMEs and the DOS/Configurations wiki
  page.
- **Whether miniBox rejects `mmap(..., MAP_ANON, fd=0, 0)`** - PCem's
  allocator passes fd 0, not -1.
- **Whether the wizard re-applies a machine's `settingOverrides`
  defaults when the machine setting changes mid-wizard.** The
  declaration-narrowing code was read; the frontend path was not. The
  whole preset design in 6.2 rests on this.
- **Whether a firmware id may contain a `/`.** The design assumes not
  (flat VFS) and flattens with a separator plus a driver-side map; the
  engine's mount path was read (`session.cpp:1190`) but the name's legal
  character set was not.
- **The PGC's ROM needs** - no `rom_init` was found in `vid_pgc.c`; not
  chased further.
- **That no `timer_read()` result feeds emulation.** Four of roughly a
  dozen call sites were read; all four were status-bar accounting.
- **Voodoo 3 texture/auxiliary buffer sizes** in the state estimate.
- **Nuked-OPL3's licence**, which PCem's vendored copy does not state.
- **86Box** was not evaluated when this document was first written. It was
  evaluated on the same day, in `UPSTREAM.md`, which has its own
  "not verified" section - read that one too before acting on the upstream
  choice. In particular: nothing was built for it either, and its speed
  conclusions are quoted third-party claims about third-party hardware.

---

## Log

- **2026-09-20** M0. `TASEmulators/pcem` `fd4585b` cloned and read
  (251,500 lines; 93 machines and their ROM sets extracted from
  `model.c` + `mem_bios.c`; 47 video cards, 20 sound cards, 17 HDD
  controllers and their ROMs extracted from `video.c`, `sound.c`,
  `hdd.c` and the per-device `init` functions; 290 CPUs in 33 tables;
  no savestates; one clock seam; five surviving device threads; the
  x86-64 backend's dead near-call path). The full TASVideos
  `/EmulatorResources/PCem` tree read - landing page, General, DOS,
  DOS/Configurations, Windows, Windows/Configurations, /95, /XP, Linux -
  and the three published DOS packages downloaded and unpacked for their
  `.cfg` files, READMEs and ROM hashes. DOSBox-X's scope statement read
  from its own README; the Chimera DOSBox-X core's `waterbox.config`,
  `file_slots.json`, `package-licenses.json` and plan read as the
  template. miniBox's RWX, green-thread, savestate, clock and VFS
  behaviour established from the sandbox sources. Recommendation: build
  it, with M1 a two-week hard stop on whether the dynamic recompiler
  runs fast enough in the sandbox to host Windows XP - the machine that
  is the entire case for the core. Nothing built, nothing pushed, no
  repository created beyond this directory.
- **2026-09-20** M0b. 86Box evaluated against every heading above, on
  Sergio's instruction that the upstream question precede M1; the full
  comparison is `UPSTREAM.md`. Outcome: stay on PCem. Two corrections to
  this document fall out of it - risk 7 is now resolved rather than open,
  and M1 is split into a two-day NATIVE speed measurement (M1a, which is
  now the decision point, because 86Box's published host thresholds put
  the Pentium II/450 target at the edge of what a native build manages)
  and a one-week sandbox JIT spike (M1b). Two smaller ones: PCem's
  GPL-2.0-ONLY is confirmed against 86Box's explicit "or later"; and the
  near-call observation in 7.5b should be read as a harmless accident
  rather than good fortune, since 86Box fixed the same code correctly in
  both its recompiler backends and that three-line fix is a
  GPL-2.0-compatible borrow if the rel32 path is ever wanted. One asset
  worth stealing regardless of upstream: 86Box's `unittester` ISA device
  (a guest program triggers screen capture and verification over I/O
  ports), which is the right primitive for this core's gate. Nothing
  built, nothing pushed, no repository created.

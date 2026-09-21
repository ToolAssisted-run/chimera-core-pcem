# Hard disks: writable, sparse, and what a state costs

**Status: built and gated, 2026-09-21.** PCem's hard disks are writable, the
writes survive a round trip out of the machine and back in, and a seeded disk
costs nothing until the guest writes to it. `run-gate.sh` is 26 legs, of which
nine are this work and four have been broken on purpose and watched go red.

This is PLAN.md 4.2 built, with the measurement PLAN.md could not make.

## 1. What it is

Upstream `src/hdd_file.c` is stdio straight onto the user's file:
`fopen64(fn, "rb+")`, `fseeko64`, `fwrite`. That is what every TASVideos PCem
page warns about - *"PCem will change your files, which will cause desyncs"*,
*"Runtime -> Prevent writing to disk must be checked"* - and it is not
available here anyway, because a project's files are read-only, hash-bound
mounts.

So, following the DOSBox-X core (`waterbox/sparse-disk.h` there), the slot's
image **seeds** a writable disk rather than being written in place. The
replacement is `waterbox/pcem-hdd.c`: the same six `hdd_*` functions PCem
calls, over a sparse write overlay. `hdd_file.c` is dropped from the source
list in `build-guest.sh` rather than patched, because the whole file is
replaced and this keeps the `extern/pcem` diff to the two genuine upstream bugs
in `patches/`.

- **The seed is a read-only mount, opened once and read through.** A 4121 MB
  Windows XP image is never copied anywhere - not into the guest heap, not
  into the sealed baseline. `wbx_mount_file_path` streams it from disc.
- **A write lands in a 4 KiB block held in guest memory**, and later reads of
  that block come from it. Untouched regions cost nothing.
- **The whole disk - seed underneath, written blocks on top - is what the
  save-data channel hands back out**, under the name of the project file it was
  seeded from, so an exported image drops straight back into the same slot.

Three decisions are this port's own rather than DOSBox-X's.

**The block is 4096 bytes, one guest page.** DOSBox-X uses 64 KiB. A block is
machine state and lands in every savestate, and miniBox's dirty tracking is
page-granular (`memblock.c`), so a 64 KiB block makes a single 512-byte sector
write cost sixteen dirty pages instead of one. Blocks come out of page-aligned
1 MiB slabs for the same reason: a plain `malloc(4096)` has a header in front
of it and straddles two pages, which would double the cost of every block for
nothing.

**A block that matches the seed is not held.** This is the RPCS3 disc-mirror
idea - bytes that are already in the read-only source do not need to be copied
into the machine. An installer that rewrites a file with the same contents, or
a format that writes zeros over a region that is already zero, costs nothing.

The comparison is made **only when the block is not yet held**, and that is
deliberate. miniBox marks a page dirty when it is written and never un-marks it
on content (`set_dirty`, `memblock.c:237`; only stack pages are compared back
against the baseline), so releasing a block that has come back to the seed
would recover guest memory but **not one byte of savestate**. The whole saving
is in the allocation that never happens, so that is where the comparison goes -
and the hot path, a block written over and over, does no base I/O at all.

**The overlay outlives `hdd_close()`.** PCem opens its disks from each
controller's init and `resetpchard()` runs those inits again, so a guest that
presses Ctrl-Alt-Del re-opens every disk. A real PC's disk survives a reboot,
so the overlay is keyed by file name and kept for the life of the machine;
`hdd_close()` detaches the controller from it and nothing more.

## 2. Out through the save-data channel

`docs/save-data.md`'s sixth guest ABI group, with the streaming pair rather
than the pointer:

| Export | What |
|---|---|
| `GetSaveDataFileCount` | one per disk the machine opened |
| `GetSaveDataFileName` | the project file's own name, so export and import are the same name |
| `GetSaveDataFileSize` | the disk as the MACHINE sees it: `spt * hpc * tracks * 512`, not the file's length |
| `GetSaveDataFileBuffer` | **null**, which is what tells the engine to use the window |
| `GetSaveDataScratch` + `ReadSaveDataFile` | a 256 KiB window, reassembled from seed and overlay in order |

There is no address at which a 4 GiB disk exists - it is a file the box streams
from disc plus a scatter of 4 KiB blocks - so there is no pointer to return.
The engine already has the windowed path (`session.cpp:2193`); DOSBox-X is the
other tenant. The window is `ECL_INVISIBLE`: it is a transient view the host
reads at a frame boundary, not machine state.

There is deliberately **no `savedata` slot**. The exported image is a hard disk
image, and it goes back into the `hdd` slot, which is what `file_slots.json`
has said since the slot was written.

## 3. Geometry, which had to be built too

PCem keeps C/H/S in its `.cfg` and **not** in the image, and nothing in the
emulation core derives it: `hdd_load()` hands `hdc[d].spt/hpc/tracks` straight
to `ide.c`, which reports them as the drive's identity (`ide.c:169-190`). A
zero there is a drive of no sectors. Upstream derives geometry in its
wxWidgets new-disk dialog, which is platform layer this port replaces - so
until now the Hard Disk Geometry setting's "Auto" wrote three zeros and the
hard disk slot could not work at all, whatever else was true.

`pcem_hdd_derive_geometry()` is what Auto means now, and it reads the image
rather than guessing from its length, because a wrong geometry is a disk that
does not boot with no useful error (PLAN.md risk 6):

1. a `.vhd` carries its geometry in its own footer; use it;
2. otherwise, score candidate (sectors, heads) pairs against the CHS fields of
   the image's own MBR partition table - a partition whose start or end CHS
   maps back to its LBA under a candidate is evidence for it, and a candidate
   that divides the image into whole cylinders is stronger evidence still;
3. otherwise, the first pair that divides the image exactly;
4. otherwise 63/16.

Fields saturated at 1023/254/63 - what an MBR writes for a partition past CHS
reach - are **skipped**, not range-checked. Getting that wrong is how the first
version read the 63/16/8374 Windows XP image as 63/255/525: at 16 heads the
saturated head value of 254 failed the range check and threw out the right
answer.

Measured: the 4121 MB TASVideos Windows XP image derives as **63/16/8374**,
which is the geometry TASVideos publish for it, and a blank 10,321,920-byte
disk derives as 63/16/20, which is how it was made. "Custom" still overrides,
and the gate has a negative control for that.

## 4. The state cost, measured

This is the part PLAN.md said to think hardest about, and the numbers are from
this machine on 2026-09-21, sandbox build, `run-wbx --state-every`.

### A seeded disk costs nothing

The published TASVideos Windows XP machine (GA-686BX, Pentium II/450, 256 MB,
Voodoo 3 3000, AWE32) with the 4,321,787,904-byte installed XP image in the
`hdd` slot:

| | |
|---|---|
| disk in the slot | **4121 MB** |
| blocks held at the first frame | **0** |
| whole-machine state at the first frame | **3.5 MB** |

The disk contributes exactly nothing until the guest writes. A full-copy
design - DOSBox-X's, which copies the image into the guest heap before seal -
would have put 4121 MB into the sealed baseline instead.

### A boot costs about 7 MB

The same machine, booted from that image to the Windows XP welcome screen
(24,000 frames = 240 s of emulated time; the screenshot is the XP logon
screen, so this is a real boot of a real installed OS off the seeded image):

| frame | state | blocks held | disk held |
|---|---|---|---|
| 0 | 3.5 MB | 0 | 0 |
| 3,000 | 297.5 MB | 1 | 4 KiB |
| 6,000 | 332.5 MB | 761 | 3.0 MiB |
| 9,000 | 335.2 MB | 1,320 | 5.2 MiB |
| 12,000 | 336.5 MB | 1,350 | 5.3 MiB |
| 18,000 | 338.6 MB | 1,720 | 6.7 MiB |
| 21,000 | 338.7 MB | 1,728 | 6.8 MiB |
| end (24,000) | - | 1,824 | 7.1 MiB |

So **a Windows XP state is about 339 MB and the disk is 2.1% of it.** The
other 331 MB is the machine: 256 MB of RAM, the dirty masks, the lookup
tables and the recompiler's arena, which is what PLAN.md 7.5 predicted (it
said ~510 MB for a machine at this size; 338 MB is what the epoch actually
dirties).

### An install costs about 1.1 GiB, and that is the honest answer

An install is the case that hurts, and it could not be re-run today - the XP
install is 33 minutes of emulated time and the image already exists. So it was
measured on the artefact instead: **the finished XP image has 283,444 non-zero
4 KiB blocks out of 1,055,124 = 1107.2 MiB (26.9%)**. An install run inside
the core onto a blank disk writes every one of those blocks, so:

> **Installing Windows XP inside this core adds roughly 1.1 GiB to every
> savestate from then on.** A state goes from about 338 MB to about 1.4 GB.

That is a real cost and it should be said out loud rather than discovered:
the installation-movie workflow (PLAN.md 6.3) produces the heaviest states this
core can make, and the answer for anyone who does not need to re-record the
install itself is to export the disk at the end and start a new project from
the exported image - where it costs nothing again, because it is back to being
a seed.

Two things keep that from being worse than it looks:

- **1.1 GiB is the ceiling, not the running total.** A block is held once
  however many times it is written; the install's 1.5 GB of write traffic
  collapses onto the blocks it ends up occupying.
- **The blocks that match the seed are never allocated, and that is not a
  theoretical saving.** Over the XP boot above, **1,975 block-writes matched
  the seed exactly and cost nothing**, against 1,824 blocks actually held.
  More than half the block-writes an XP boot makes are Windows writing back
  what was already there, and on this workload the optimisation more than
  halves what the disk contributes to a state. On a blank disk the seed is
  zeros, so every zero-write - and a format is mostly zero-writes over ground
  that is already zero - is in the same category.

### Where the ceiling is

`memoryLayoutMiB`'s mmap arena is raised from 2048 to **4096 MiB** by this
work, which is where the overlay lives. After the machine's own ~600 MB that
leaves room for about 3.4 GiB of written blocks - comfortably more than an
install, and short of rewriting every block of a 4121 MB disk. A write the
arena cannot hold is reported to the guest as a **write fault** and said so on
stderr; it is never silently dropped.

One trap found while raising it, worth not rediscovering: `4096u << 20` is a
32-bit shift and comes out **zero**, so the arena silently became empty and
every machine died on the first `malloc`. `(uintptr_t)4096 << 20`.

## 5. The gate

Eight legs, in `waterbox/run-gate.sh`. The point of every one of them is that
**the bytes have to survive** - a leg that proves a write did not crash proves
nothing, and a digest stream proves nothing either: with the overlay's writes
deliberately thrown away, the per-frame picture digest of the whole run is
*bit-identical* to the working build.

The probes are two 512-byte boot sectors (`tools/hdd-probe.S`), because the
round trip needs a guest that writes and a guest that reads and neither needs
an operating system or an image anybody has to supply:

| Leg | What it asserts |
|---|---|
| the disk probes assemble | both boot sectors are exactly 512 bytes |
| a guest write leaves as save data | run 1 writes a magic string to LBA 1 of a blank disk; the EXPORT contains it |
| an exported disk seeds the next run | run 2, seeded with run 1's export, reads LBA 1 and copies it to LBA 2 only on a match - so magic at LBA 2 cannot exist unless the machine read the seed. The differential is the same boot sector against a fresh blank disk, where LBA 2 must stay zero |
| the overlay is sparse | one sector written holds one 4 KiB block, not the disk |
| the disk rewinds with the machine | the disk when the state was taken, at the end of the run, and the instant the state goes back: the second must DIFFER (or the leg proves nothing) and the third must MATCH |
| Auto derives the disk geometry | the composed `.cfg` says 63/16/20 for a disk that is 63/16/20 |
| Custom geometry overrides Auto | negative control |
| a project's disk exports through the engine | a real `.chimeraProject` with the image in the `hdd` slot, run through `chimera-run --export-savedata`: the route a user's disk actually takes, which no other leg touches because a run without a project has no slots mount |
| a 4 GiB seed costs nothing | the XP image: state at the first frame under 32 MB, geometry 63/16/8374 (skipped when the image is absent) |

### Proven to bite

Four breaks, each built and run:

| Break | What went red |
|---|---|
| `disk_write` returns immediately | *a guest write leaves as save data* (no magic at LBA 1) and *the overlay is sparse* (0 blocks held). **The picture digest stream did not move at all** |
| `base_read` always zero-fills | *an exported disk seeds the next run*: the seeded run finds nothing at LBA 1, so nothing reaches LBA 2 |
| overlay slabs allocated with `alloc_invisible` | *the disk rewinds with the machine*: `disk_after_load` came back equal to `disk_at_end` instead of `disk_at_save` |
| (found, not staged) `4096u << 20` for the arena | every machine died on the first `malloc` |

The third break is why the writer probe does not simply write once and stop.
With a single write, the state leg passed even with the overlay in invisible
memory - because the *block index* is ordinary guest memory and rolled back on
its own, so reads fell through to the seed and came out right by accident. The
writer now writes a counter into the same sector over and over, so the state is
taken between two writes to the same block and the block's contents have to
roll back for the leg to pass.

## 6. A crash fixed on the way

`cdrom_drive = 0` leaves PCem's global `atapi` **NULL**, and the first IDE
software reset that finds an empty drive calls `atapi->stop()` through it
(`ide.c:824-829`). Every machine with a hard disk hits that, because the other
three IDE drives are `IDE_NONE`. PCem's own no-drive value on unix is `-1`,
which is the only value that reaches `cdrom_null_open()` (`pc.c:319-321`) and
installs the null ATAPI; the driver now writes that.

This is the same crash `XP.md` section 6 recorded from the other direction -
"cdrom_drive = 0 with cdrom_channel still set segfaults PCem in callbackide".
Clearing the channel was half of it and this is the other half. It is an
upstream bug, fixed in the driver's composed config rather than in
`extern/pcem`.

## 7. What is NOT proven

- **No operating system was installed inside the core.** The 1.1 GiB install
  figure is counted off the finished image, not measured by running an install
  in the sandbox. It is an upper bound on the blocks held at the end and a
  lower bound on the traffic; a real install would also touch blocks it later
  frees, which this does not count.
- **No `.vhd` and no `.hdi` was tested.** The VHD path is written (minivhd for
  the seed's reads and its geometry) and compiles; no VHD exists on this
  machine to run it against. The `.hdi` header skip DOSBox-X does for PC-98 is
  not implemented here at all.
- **The second hard disk (`hdd2`) was never run.** The code is symmetric with
  the first and the config keys are written; nothing exercised two disks at
  once.
- **Nothing was tested past 4 GiB of disk.** `hdd_file_t.sectors` is an `int`
  and PCem's own read/write take an `int` sector offset, so the ceiling is
  PCem's, at 2^31 sectors; the overlay's own arithmetic is 64-bit.
- **No SCSI, MFM or ESDI controller was run.** Every leg uses IDE. The overlay
  sits under `hdd_load`/`hdd_read_sectors`/`hdd_write_sectors`, which all five
  controller families call, so they should all work - that is an inference
  from where the seam is, not a measurement.
- **The write-fault path was never taken.** Nothing has filled the arena.
- **One run each** for the state sizes.

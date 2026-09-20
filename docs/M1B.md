# M1b: does PCem's recompiler work inside miniBox, and what does it cost?

**Status: GREEN on the question M1b was asked.** PCem's dynamic
recompiler allocates its arena, generates code and executes it inside
the sandbox, producing a **bit-identical machine** to the native build.
The sandbox itself costs about **15%**. The feared cost - one RWX
dirty-page fault per arena page per epoch, up to 30,720 of them - **does
not happen**, and the measurements say why.

There is one amber finding, and it is not about PCem: at one miniBox
epoch per emulated frame, the CPU-bound floor falls below real time.
Section 5.

Everything below was run. Anything not observed is marked **UNPROVEN**.

---

## 1. What was built

The **whole** PCem emulation core - all ~300 C and C++ files upstream's
`Makefile.am` lists, minus the platform layer - compiled for miniBox's
musl guest toolchain with `-mcmodel=large -fno-pic -fno-pie`, linked
against the C++ guest recipe, and it passes `check-wbx.sh` clean (no TLS
symbols, no `%fs` accesses). `tools/build-guest.sh` derives its source
list from the same `Makefile.am` the native build does, so the two
cannot drift apart or from upstream.

That is a larger result than the plan asked for. PLAN.md's M1b was "a
guest build of just the CPU, memory and recompiler"; there was no need
to carve a subset out, because the whole thing built.

The guest is driven by `tools/guest-spike.c` - the same 28 platform
definitions as the native driver - and `tools/m1b-host.c` runs it
through the miniBox host and does the timing, because the guest's clock
is frozen.

**Two patches were needed.** Both are in `patches/`.

## 2. The bug that had to be fixed first: PCem clobbers %rbx

`patches/0002-dynarec-restore-rbx-not-rdx.patch`.

PCem's x86-64 dynarec prologue pushes eight registers
(`codegen_backend_x86-64.c:350-357`):

```
RBX RBP RSI RDI R12 R13 R14 R15
```

and its epilogue pops:

```
R15 R14 R13 R12 RDI RSI RBP RDX      <- RDX, not RBX
```

`REG_RDX` is 2 and `REG_RBX` is 3 (`codegen_backend_x86-64_defs.h:23-24`),
so these are different registers and this is not an alias. **Every
generated block therefore returns with %rbx - a callee-saved register
under both the SysV and Win64 ABIs - clobbered, and drops the saved %rbx
into %rdx.**

**86Box, which forked this same backend, fixed it**
(`src/codegen_new/codegen_backend_x86-64.c`: `host_x86_POP(block,
REG_RBX)`). PCem still has the typo. This is the third item on
UPSTREAM.md's "what to steal from 86Box" list arriving early and
unplanned.

Why it has survived upstream: whether it bites depends entirely on
whether the compiler keeps a live value in %rbx across the
`call *0x20(%r12)` in `exec386_dynarec`. A native `-O3` build does not.
A guest build with `-mcmodel=large -fno-pic` does, and the machine dies
on the very first generated block:

```
miniBox: the core crashed: it wrote to address 0xf0 (at 0x36f000cc1cf)
  36f000cc1ca: call *0x20(%r12)        <- the generated block
  36f000cc1cf: movl $0x0,(%rbx)        <- %rbx came back as 0xf0
```

**This is a latent upstream bug, not a sandbox artefact.** It would bite
any build whose register allocation differs. It is worth reporting
upstream.

## 3. The arena: it allocates, and it is not the problem

`patches/0001-jit-arena-placement.patch`.

PCem asks for 131072 blocks of 0x3c0 bytes = **125,829,120 bytes, exactly
120 MiB, 30,720 pages** (`codegen_allocator.h:17-23`). Inside the box:

```
Allocated 125829120 bytes on invisible heap, usage 125829120/335544320
m1b: JIT arena at 0x36f073ae000, 125829120 bytes (30720 pages)
```

Three findings, in order of how much they change the plan.

**(a) PLAN.md 7.5b's "one-line `mmap` fd fix" is not needed. Delete the
risk.** PCem passes `fd = 0` where POSIX convention is `-1`
(`codegen_allocator.c:34`). miniBox's `mmap` **never reads the fd at
all**: `dispatch_inner`'s `NR_mmap` case (`host.c:337-357`) uses
`a1..a4` and discards the rest. PCem's call is accepted exactly as
emulibc's `-1` is. PCem also passes `addr = 0`, which is the correct
thing to do - a non-zero address means `MAP_FIXED` to the sandbox
regardless of the flag, the trap PPSSPP had to patch around.

**(b) The fault storm does not happen, and the reason is measurable.**
With a miniBox epoch opened every emulated frame over 300 s of Windows XP
Setup:

| arena in | pages dirtied per epoch | JIT blocks used | state size |
|---|---|---|---|
| ordinary mmap memory | **361.2** | 13683 of 131072 (10.4%, 12.5 MiB) | 287.2 MiB |
| invisible memory | **359.9** | 13683 of 131072 (10.4%, 12.5 MiB) | 273.4 MiB |

The arena's entire contribution is **1.3 pages per epoch**, against a
predicted worst case of 30,720. Two reasons, and both are readable in
miniBox:

- PCem touches **10.4% of the arena it asked for** - 12.5 MiB, about
  3,200 pages, after five minutes of a real OS installer. The other
  89.6% is allocated and never written, and an untouched page costs
  nothing in faults or in state bytes.
- Of those ~3,200 pages, essentially none fault per epoch, because
  miniBox promotes a page written in three consecutive epochs to **hot**
  and stops holding it read-only (`memblock.c:89-91`, `HOT_AFTER 3` at
  `memblock.c:287`). A recompiler rewriting the same working set is
  precisely the access pattern that mechanism exists for.

  *(The hot-page attribution is inferred from the mechanism plus the
  measured 1.3-vs-3,200 gap; it was not confirmed by reading miniBox's
  own hot counter. **Partly UNPROVEN.**)*

**(c) The invisible arena - xemu's pattern - measured WORSE, so it is
not the default.** xemu puts its 256 MB TCG buffer in invisible memory
(chimera-core-xemu patch 0017) because 215 MB of a 316 MB state was that
buffer. PCem is not in that position. Three repeats of each at 60 s:

| | run 1 | run 2 | run 3 |
|---|---|---|---|
| invisible arena | 219.3% | 234.1% | 232.5% |
| ordinary mmap arena | 245.5% | 260.6% | 246.2% |

Repeatably about **10% slower** for **13.8 MiB saved on a 287 MiB
state**, and only ~5 KiB per greenzone delta (1.3 pages). So the default
is the ordinary mmap arena; `-DCHIMERA_JIT_ARENA_INVISIBLE` selects the
other. The invisible path also carries xemu's obligation - generated code
is in no savestate, so the driver must discard every block on state load
- and **that flush is not implemented**, which is a second reason not to
make it the default yet.

**Why invisible is slower is not explained.** It is repeatable across
six runs and two workloads. Page-size or placement differences between
the invisible heap and the mmap arena are the obvious suspects and were
not tested. **UNPROVEN.**

## 4. It runs the same machine

Not "it ran": the **same** machine. Native and guest were run over the
same configuration and compared by an FNV-1a digest of the visible
framebuffer, computed identically on both sides
(`driver.c: screen_digest`, `guest-spike.c: GetScreenDigest`):

| workload | native | guest |
|---|---|---|
| BIOS POST, 5 s | 447 blits, 640x480, `d4e29a360e349da3` | 447 blits, 640x480, **`d4e29a360e349da3`** |
| POST to DISK BOOT FAILURE, 60 s | 3546 blits, 720x400, `c0f526fed0c3fc5b` | 3546 blits, 720x400, **`c0f526fed0c3fc5b`** |
| XP Setup from the CD, 300 s | 20450 blits, 720x396, `7f307559900711db` | 20450 blits, 720x396, **`7f307559900711db`** |

Identical in all three, and identical again with the invisible arena and
with every epoch rate. The Windows XP Setup workload is the real one:
the same GA-686BX machine, the same Pentium II/450, the same
TASVideos-hashed ISO, booted from a CD mounted into the box by host path.

One trap on the way, recorded because it looked like a divergence and
was not: the guest's `cdrom_path` kept a leading `/`, which the flat VFS
refuses, so the guest had no CD and sat at DISK BOOT FAILURE while
native ran Setup - same blit count, different digest. **A digest that
differs is not automatically a determinism bug; check the machine is the
same machine first.**

## 5. The numbers, and the one amber finding

Windows XP Setup, 300 s of emulated time, GA-686BX / Pentium II/450 /
dynarec on. "Floor" is the worst one-second wall-clock window; that is
the figure M1a established as the honest one, because the fast windows
are only fast because the guest is halted.

| | overall | floor | median | windows below 100% |
|---|---|---|---|---|
| **native** (M1a's build) | 1167% | **152.5%** | 165.2% | 0 / 50 |
| guest, **no epochs** | 1082% | **129.6%** | 147.4% | 0 / 42 |
| guest, epoch every 100 ms | 1193% | **105.7%** | 147.5% | 0 / 67 |
| guest, epoch every 33 ms | 200% | **81.3%** | 96.7% | 88 / 147 |
| guest, epoch every 10 ms (one per frame) | 198% | **44.4%** | 249.0% | 46 / 151 |

**The sandbox itself is cheap.** With no epochs the floor is 129.6%
against native's 152.5% - **85% of native, a 1.18x slowdown** - and
nothing drops below real time. For a core that JITs, that is a good
number.

**The cost is epoch dirty-tracking, and it is not PCem's.** It scales
with how often an epoch is opened, not with anything the recompiler
does - the arena contributes 1.3 of ~360 pages. Every core that stores
greenzone frames pays this; PCem is just a big machine (256 MB of RAM,
16 MB of video memory) so it has a lot of ordinary pages to track.

**At one epoch per frame the floor is 44.4% and 46 of 151 windows are
below real time. That is the amber finding.** Chimera opens an epoch per
*stored* greenzone frame (`state_history.cpp:1213`), gated by budget and
the anchor rule, so one per emulated frame is the worst case rather than
the normal case - but it is a case that happens, and it is where the
headroom M1a measured goes.

Two honesty notes on that table:
- The medians for the 33 ms and 10 ms rows are not consistent with each
  other (96.7% vs 249.0%) despite nearly equal overall figures. Each row
  is one run; the two were not repeated enough to explain the shape.
  **The floors and the overall figures are the robust numbers; treat the
  medians of those two rows as unexplained.**
- 100 ms and 1000 ms epochs were also run at 120 s and behaved like the
  100 ms row here.

## 6. The verdict

**Green on M1b as asked.** The JIT is viable in the box: it allocates, it
executes, it is bit-identical to native, and it costs 15%. The specific
thing PLAN.md said could kill the core - the RWX dirty-page faults over a
120 MiB arena - was measured and is 1.3 pages an epoch, not 30,720. Risk 1
in PLAN.md section 10 can be closed on the JIT, and 7.5b's `mmap` fd
concern can be deleted outright.

The epoch-rate finding is real but is a different risk with a different
owner: it is generic miniBox dirty-tracking on a machine with 256 MB of
RAM, and the levers for it (how often the greenzone stores a frame, and
whether PCem's RAM can be told apart from its scratch) belong to M3 and
the engine, not to the recompiler.

## 6a. Savestates round-trip, including into a brand new host

Added after the first M1b report, which flagged this as the most
load-bearing of the remaining unknowns: states had been saved and sized
but never loaded back. **Both round-trip tests pass, bit-identically.**

All three runs are the same 60 s of Windows XP Setup, guest build,
default (mmap) arena - so the recompiler's generated code is **inside**
the state, which is the case most in need of the test:

| | blits | size | digest |
|---|---|---|---|
| baseline, no state work | 3630 | 720x400 | `55b90b678f1559cb` |
| `--roundtrip`: save + load around **every** chunk (60 cycles) | 3630 | 720x400 | **`55b90b678f1559cb`** |
| `--resume-at 30000`: save, **destroy the host**, build a new one, load, finish | 3630 | 720x400 | **`55b90b678f1559cb`** |

The third is the reopened-project case and the stronger of the two: the
second host is a fresh `wbx_create_host` at a different place in the
host's address space, brought up through `Init` and `wbx_seal` before
the load replaces everything that produced. A host address kept in guest
state would show here. None does.

One thing the API required that is worth writing down: **a state can only
be loaded into a host that has been sealed.** Loading into a freshly
created host fails with `Not sealed!`; the new host has to be run through
`Init` then `wbx_seal` first, even though the load then overwrites all of
it.

**Proven to bite.** `--resume-no-load` builds the new host and
deliberately does not load the state, so it carries on from reset:

```
blits=1527  digest=125afdabe7aebf83      (against 3630 / 55b90b678f1559cb)
```

Both the frame count and the digest move, so the passing result is not
consistent with a load that silently did nothing.

Still unproven here: round-trip across a **resolution change** (libTAS
names that as a PCem hazard), and round-trip on a machine with a writable
hard disk attached - this configuration boots from CD only.

## 6b. Two gate lessons, both found by trying to break this one

`gates.md` asks that every leg be proven to bite. Doing that here found
two ways this harness could have gone green on a broken thing, and both
are fixed.

**1. A dead guest reports an enormous speed.** Reverting
`patches/0002` and running the negative control:

```
miniBox: the core crashed: it wrote to address 0xf0
RESULT emulated_ms=60000 wall_s=0.003 speed=2339127.5% blits=0 0x0 digest=0000000000000000
```

A dead guest returns 0 from every call, so it "completes" the whole
workload instantly. **A harness that asserted only "speed is above X"
would have passed a machine that never executed an instruction** - and
speed is exactly what this harness exists to measure. `m1b-host.c` now
calls `wbx_get_death` and checks the blit count before printing any
number, and refuses with exit 3. Re-running the negative control against
the fixed harness prints `FAIL: the guest is dead` and no RESULT line.

**2. The screen digest alone is too weak to prove "the same machine".**
Running the guest with the CPU changed from Pentium II/450 to /233:

```
cpu = 6 : blits=3630 digest=55b90b678f1559cb
cpu = 0 : blits=3591 digest=55b90b678f1559cb   <- same digest
```

The final picture is the same "DISK BOOT FAILURE" text either way, so
the digest is blind to a CPU running at half the clock. Only the blit
count moved. **The digest is reported with the blit count and the
resolution, and all three must match** - that triple did catch it. Even
so, an end-state digest is the wrong shape: **M2's gate needs a per-frame
digest stream, not a digest of the last frame**, and this is the concrete
reason.

## 7. What is NOT proven

- **XP at the desktop, in the box or natively.** Everything here is
  Setup. **UNPROVEN.**
- **Long runs.** 10.4% of the arena after five minutes is not the same as
  10.4% after an hour. PCem recycles blocks, so it may stay bounded, but
  that was not run. If the arena fills, both the fault count and the
  state size change. **UNPROVEN.**
- ~~**Savestate round-trip.**~~ **DONE, and it passes - see section 6a.**
  Save+load around every chunk, and a brand new host finishing the run
  from a state, both bit-identical. What remains untested is a round-trip
  across a resolution change, and one with a writable hard disk attached.
- **That the end-state digest would catch a subtle divergence.** It
  demonstrably does not catch a halved CPU clock (section 6b). It is
  evidence that the two builds agree, not proof.
- **Why the invisible arena is slower.** Repeatable, unexplained.
- **The hot-page attribution** in section 3(b): inferred from the
  mechanism and the measured gap, not confirmed against miniBox's own
  counters.
- **Anything about Windows.** Linux host only.
- **Threads.** This configuration uses none of PCem's five still-threaded
  video cards. They remain PLAN.md's open item.

## 8. How to reproduce

```
tools/build-guest.sh                     # -> build/m1b/pcem-spike.wbx
gcc -O2 -I<minibox>/source/host -o build/m1b/m1b-host tools/m1b-host.c \
    <minibox>/build/source/host/libminiboxhost.so -Wl,-rpath,<...>
build/m1b/m1b-host build/m1b/pcem-spike.wbx build/m1b/work 300000 \
    --epoch-ms 10 --iso /path/to/winxp.iso
```

`build/m1b/work` holds the ROM set with PCem's directory separators
flattened to `_` (the flat VFS has no directories; `rom.c` and `nvr.c`
are compiled `-Dfopen=spike_fopen` to do the translation), `machine.cfg`,
and the CD-boot CMOS as `nvr_machine.ga686bx.nvr`.

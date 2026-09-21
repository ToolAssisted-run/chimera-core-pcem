# The CMOS: PCem's defaults, and the F1 that is gone

**Status: built and gated, 2026-09-21.** Every AT-class machine used to stop
at POST on its first frame with

```
161-System Options Not Set-(Run SETUP)
(RESUME = "F1" KEY)
```

and wait for a keypress. It does not any more. This is PLAN.md 4.3's first
half; the flash BIOS and the save-data export of both are still open.

## Why it happened

Two separate things, and fixing only the first turns 161 into 162.

**There was no CMOS at all.** `nvrfopen()` (`nvr.c:33-57`) asks for
`<config>.<machine>.nvr` and falls back to `<nvr path>/default/<machine>.nvr`.
PCem ships a clean default for every machine that needs one, in
`nvr/default/`, but the sandbox has no such folder and a project cannot supply
one - the CMOS is not firmware and it is not a user file. So every machine
booted with an empty CMOS, which is error 161: bad checksum.

**And PCem's default does not describe the user's machine.** `at.nvr` says two
1.2M drives, no hard disk, 640 KB base, EGA/VGA:

```
0x10 = 0x22   floppy A and B are both type 2 (1.2M)
0x12 = 0x00   no fixed disks
0x14 = 0x41   a floppy is installed; two drives; EGA/VGA display
0x2E = 0x00E5 the sum of 0x10..0x2D
```

A user who picks one drive gets a BIOS that counts one and a CMOS that says
two, which is error 162: configuration mismatch. A real owner ran SETUP once
and never thought about it again. A Chimera user builds the machine out of
settings and has nothing to run SETUP on, and pressing F1 on the first frame
of every movie is not a thing anybody should have to record.

## What was built

**PCem's defaults travel inside the core.** `tools/gen-nvr-defaults.py` turns
`extern/pcem/nvr/default/*` into a C table the guest carries - 44 files,
21 KB, PCem's own data under the same GPL-2.0 as the rest of it (PLAN.md
section 8). `pcem_driver_fopen` serves them: the fallback is the only path
with a `default/` component in it, so the interception is exact.

**And the CMOS is edited to describe the machine the settings built.**
`apply_cmos()` in `pcem-driver.c`, after `resetpchard()` because `loadnvr()`
runs inside it. Only the standard MC146818 equipment bytes:

| Byte | What |
|---|---|
| 0x10 | floppy drive types, from `fdd_get_type(0)` and `(1)` - which is what "Auto" fitted, so the CMOS follows Auto |
| 0x14 | drive count (bits 7-6), coprocessor (bit 1), display class (bits 5-4), floppy present (bit 0) |
| 0x2E / 0x2F | the checksum, the sum of 0x10..0x2D |

Everything else - the fixed-disk types, the chipset's own bytes, the setup
screens' preferences - is left exactly as PCem shipped it. The guard is
`models[model].flags & MODEL_AT` and `nvrmask >= 63`, so an XT (which has no
CMOS) and anything with a non-MC146818 NVRAM are not touched.

## Measured

| | |
|---|---|
| IBM AT, one 1.2M drive, no disk, before | **161**, then **162** once the default loaded |
| the same machine, after | POSTs clean and boots |
| lit pixels, booted (the marker probe fills the screen) | **81,479** |
| lit pixels, sitting at a POST stop | **1,932** |
| Windows XP on the GA-686BX, 24,000 frames, before and after | **bit-identical** frame-digest streams |

That last row is the one that matters for not breaking anything: the machine
every other measurement in this repo was taken on is unchanged.

## The gate leg, and that it bites

`an IBM AT POSTs without asking for F1`. The assertion is deliberately **not**
the text on the screen: a third boot-sector probe (`tools/hdd-probe.S`
`MODE=3`) fills the screen with `#`, which no POST stop can do, and the leg
counts lit pixels against a threshold of 20,000 - nowhere near either side.

Proven to bite: with `apply_cmos()` returning immediately, the same run comes
back with **1,937** lit pixels, because the machine is sitting at 162.

## What is NOT fixed

- **An AT-class machine with an MFM or ESDI hard disk still cannot see it.**
  CMOS 0x12 holds the fixed-disk type, an index into the BIOS's own geometry
  table, and `apply_cmos` deliberately does not write it: the derived geometry
  of an arbitrary image is not one of the IBM AT's fourteen types, and writing
  a type whose geometry disagrees with the image is worse than writing none.
  Measured: an IBM AT with the writer probe and a 63/16/20 disk now POSTs
  clean, boots the floppy, and the INT 13h write returns failure. **Machines
  with IDE and a BIOS that autodetects - everything from the 386 on - are
  unaffected, and that is where every hard-disk leg in the gate runs.** The
  fix is a `hddCmosType` setting, or mapping the derived geometry onto the
  nearest standard type; neither is built.
- **The CMOS does not leave the machine.** It is guest memory, so it is in
  every savestate and it is a memory domain ("CMOS/NVRAM") - what a guest
  changes in SETUP persists for the session and rewinds correctly - but it is
  not in the save-data export, so BIOS setup made in one project does not
  travel to another. PLAN.md 4.3 wants it there beside the disk.
- **`flash.bin` is not done at all.** The Intel/Award boards' flash BIOS
  (`intel_flash.c`, `sst39sf010.c`) still has nowhere to live.
- **A hard reset reloads the default.** `loadnvr()` runs inside
  `resetpchard()`, and with no writable `.nvr` on the flat VFS there is
  nothing for it to read but the shipped default, so a hard reset discards
  what the guest put in SETUP. A guest-initiated Ctrl-Alt-Del does not go
  through that path, so this is only the host-side hard reset.
- **Only the IBM AT was tested.** The other 50-odd AT-class machines were not.
  The three bytes are the AT's own contract and are the same on every AT-class
  BIOS, which is why it is only those three - but that is an argument, not a
  measurement.

# Windows XP on the desktop: the measurement, and what actually costs

**Windows XP Home SP3 installs and runs.** The machine reaches the
desktop, unattended, from the TASVideos-published ISO on the
TASVideos-published machine.

**The headline is not the one M1a expected, and not the one I expected
after the first contaminated run.** XP is fast when it is running and
slow when it is *discovering code*:

| | median | floor | below real time |
|---|---|---|---|
| desktop, idle | **365.6%** | 267.2% | 0 / 30 |
| desktop + Task Manager | 319.8% | 48.8% | 1 / 80 |
| text Setup: partition, format, file copy | 159.2% | 60.2% | 9 / 493 |
| XP cold boot to desktop | 179.9% | **18.7%** | 25 / 58 |
| **GUI Setup under load (640x480)** | **88.4%** | **20.5%** | **782 / 1215 (64%)** |

All figures are percentages of a real Pentium II/450, one-second
wall-clock windows, native build, nothing else running on the machine.

**The cause of every dip is the same thing, and it is not what was
predicted.** It is PCem's recompiler meeting code it has never seen:

| | median speed | median NEW blocks per emulated second |
|---|---|---|
| windows below real time | **33.7%** | **189,772** |
| windows above 200% | **356.5%** | **164** |

A thousand-fold difference in the rate of new code generated, Spearman
rank correlation **-0.86** between new-block rate and speed, and **zero
code-cache flushes across the entire boot**. Section 3.

---

## 1. Getting there

The install is unattended, driven by a `winnt.sif` answer file on a
1.44 MB FAT12 floppy that `tools/make-unattend-floppy.py` builds. The
product key is read from a file at run time and appears in no script, no
log, no commit and no screenshot; the image is written under `build/`,
which is gitignored, and the repository was checked for the key and for
every one of its five groups before committing.

Two things about this machine that cost a run each and are worth not
rediscovering:

- **The CMOS boot order has to be `CDROM,A,C`.** With the default
  `A,CDROM,C` and a floppy in the drive, the machine never boots the CD.
  A floppy with a boot signature but no bootstrap stops at "Non-System
  disk"; one *without* the `0x55AA` signature does not get skipped, it
  hangs POST; and an `INT 18h` bootstrap, which is the documented "not
  bootable, try the next device" escape, gets "PRESS A KEY TO REBOOT"
  from this Award BIOS. Setting the boot order in CMOS and saving the
  `.nvr` is the only thing that works.
- **`winnt.sif` is an INF file.** `%windir%` is a parse error, because an
  INF treats `%NAME%` as a string-table token. So is an INF-escaped inner
  quote (`""HKCU\Control Panel\Desktop""`), and so is an unquoted value
  carrying plain inner quotes. Setup rejects the whole file before it
  starts with "Line NN of the INF file \winnt.sif is invalid".

**A process failure worth recording:** I added a `[GuiRunOnce]` block and
launched a 33-minute run without validating it. Setup rejected the file
30 seconds in and the machine sat at "Press any key to exit" for the
remaining 32 minutes. A 15-second boot would have caught it. Every long
run is now preceded by a 60 ms-emulated validation boot.

## 2. The numbers, phase by phase

Phase boundaries are taken from the screen mode recorded in the
screenshots, not guessed: 656x200 and 720x400 are POST, 720x396 is
text-mode Setup, 640x480 is GUI Setup and the desktop.

The two that matter:

**GUI Setup under load** - 640x480, XP's graphical installer doing device
installation - is the heaviest sustained graphical workload available
without installing third-party software. **Median 88.4%, floor 20.5%,
p10 51.3%, and 782 of 1215 one-second windows below real time.** It is
genuinely below real time for most of its duration.

**The settled desktop is not.** Idle it sits at 365.6%; with Task Manager
open, 319.8%. Once XP is up and its code is compiled, this machine has
3x of headroom.

## 3. What actually costs: cold code, not pixels and not disk

Three candidate explanations were tested. Two are wrong, and the first
of those was mine.

**(a) It is not the Voodoo 3's software rasteriser.** I said in my
previous report that the rasteriser looked like the thing that decides
XP viability. It is not. The same cold boot, same disk, with the Voodoo 3
3000 replaced by a plain 2D S3 Trio64:

| | floor | p10 | below real time |
|---|---|---|---|
| Voodoo 3 3000 | 16.7% | 24.2% | 30 / 62 |
| S3 Trio64 | 15.3% | 24.1% | 36 / 75 |

The floors and the p10s are the same to within noise. Changing the video
card does not move the dips. *(Caveat: the S3 run boots a copy of the
same image, so XP re-detects the adapter; the comparison is sound on the
floor but not exact overall.)*

**(b) It is not host disk I/O.** M1a localised its sub-100% windows to
the NTFS format and file copy and expected them to disappear once the
port replaces `hdd_file.c`'s `fseeko64`/`fwrite` with a memory overlay.
It marked that UNPROVEN. It is now measured, and it is wrong: the same
1.1 GB disk image copied onto **tmpfs**, so there is no host disk I/O at
all, boots identically.

| | floor | p10 | below real time |
|---|---|---|---|
| image on ext4 inside WSL2's VHDX | 18.1% | 26.8% | 27 / 61 |
| same image on tmpfs | 17.0% | 23.0% | 27 / 61 |

Identical. **M1A.md's expectation is corrected, not confirmed.**
*(Tested on the boot workload. The format-and-copy phase specifically
was not re-run on tmpfs, so for that phase the claim is merely
unsupported rather than disproven.)*

**(c) It is the recompiler, meeting code it has never seen.** PCem
latches its recompiler counters once per emulated second
(`pc.c:530-560`), and the driver now prints them per window. Over a
140 s cold boot, 57 windows:

| | median speed | median **new** blocks / emulated s | median evicted |
|---|---|---|---|
| windows below real time (n=24) | 33.7% | **189,772** | 245 |
| windows above 200% (n=27) | 356.5% | **164** | 0 |

Spearman rank correlation between new-block rate and speed: **-0.86**.
Total code-cache flushes across the whole boot: **zero**.

So this is not invalidation and not thrashing. It is simply volume:
booting NT pages in and relocates hundreds of DLLs and runs each
initialisation path **once**, and a recompiler pays its full two-pass
compilation cost for code that then never runs again. The moment the
working set is compiled - the settled desktop - the same machine runs at
356%.

The transition is visible in a single second of the log: at t=9 s the
machine is at 876% with 216 new blocks a second; at t=10 s it is at
58.9% with 63,561.

## 4. What this means for the core

- **XP is viable, and the boot is the worst case rather than the steady
  state.** A TAS does not spend its time booting. The settled desktop has
  3x of headroom and a game's hot loop is compiled once, so the steady
  state should look much more like 320-365% than like 20%. **That is an
  inference from the mechanism, not a measurement - see section 5.**
- **The remedy, if one is wanted, is a cheaper first tier**, not a faster
  rasteriser and not a faster disk. PCem already ships an interpreter
  (`exec386`, selected by `cpu_use_dynarec = 0`); interpreting until a
  block is hot and only then compiling is the standard answer to exactly
  this profile. That is an M5 optimisation, not an M2 blocker.
- **M1a's headroom is not spent where M1a thought.** The sandbox costs
  15% (M1B.md), the epoch dirty-tracking costs more, and cold-code
  compilation costs more than either. All three stack on the same
  workload and none of them was the 120 MiB JIT arena everyone was
  worried about.

## 5. What is NOT proven

- **No game, and no 3D, was ever run.** The heaviest graphical workload
  measured is XP's own installer. Two attempts to start XP's built-in
  OpenGL screensaver through the Run dialog failed - a New Connection
  Wizard took focus both times and swallowed the keystrokes - and I did
  not install third-party software to manufacture a load. **The figure an
  XP TAS would actually live at is UNMEASURED**, and section 4's first
  bullet is an inference.
- **The desktop was measured native, not in the sandbox.** M1B.md's
  sandbox costs were measured on Setup, not on the desktop, and the two
  have not been combined.
- **One run each.** The phase distributions come from single runs. The
  cold-boot figures were repeated four times across the video-card and
  tmpfs comparisons and were stable; the GUI Setup distribution was not
  repeated.
- **The format-and-copy phase was not re-tested on tmpfs** (section 3b).
- **`enable_sync = 1`** throughout, so the CMOS clock was seeded from the
  host. That is the seam `rtcBase` replaces and it was not varied; the
  TASVideos boot-time table shows XP's boot time is sensitive to it.

## 6. A PCem bug found on the way

`cdrom_drive = 0` with `cdrom_channel` still set to a valid channel
**segfaults PCem** in `callbackide` a few seconds into the run - the
ATAPI device is attached to the channel with nothing behind it. This is a
configuration a settings UI could easily produce ("no CD-ROM" while the
channel keeps its default), so the driver must either clear the channel
or refuse the combination. Backtrace:

```
#0 callbackide ()
#1 timer_process ()
#2 exec386_dynarec ()
#3 runpc ()
```

## 7. How to reproduce

```
python3 tools/make-unattend-floppy.py build/xp/unattend.img   # reads ~/winxp.key
# CMOS boot order must be CDROM,A,C; see section 1
tools/build.sh
build/m1a/pcem-bench --config <xpinstall.cfg> --bench-ms 3000000 \
    --shot-every-ms 30000 --shot-dir <dir>
```

Validate any change to the answer file with `--bench-ms 60000` and look
at the screenshot before starting a long run.

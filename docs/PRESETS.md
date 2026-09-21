# Configuration presets: five machines somebody has already got working

**Status: built and gated, 2026-09-21.** `waterbox.config` declares five
presets, the wizard offers them above the settings grid with an Apply button,
and Apply writes their values into the settings and is then finished with them.
Nineteen new settings were added so that every row of the TASVideos tables is a
thing the user can see and change; `run-gate.sh` grows three legs, one of which
is a negative control that runs every time.

This is PLAN.md section 6 built - but built on the frontend's Option B, not the
Option C that section recommended. See section 8.

## 1. The contract, and a mistake worth keeping

The declaration is `~/chimera/docs/project.md`, "Configuration presets":

```json
"presets": [
  { "id", "label", "description", "when": [machine ids], "values": { name: value } }
]
```

Apply WRITES the values into the settings. Nothing about the preset survives
the click: the project pins the resolved values and the movie cites them,
exactly as if each had been typed by hand, and every one of them is sitting in
the grid where it can be read and changed afterwards. A preset is a starting
point somebody else has already got working, never a narrowing.

**Every preset sets the machine, and that is the most important thing it says.**
"Packard Bell PB570" IS the preset; a bundle of a CPU, a video card and a sound
card without the board they plug into is not a machine anybody asked for, and
on the wrong board the core refuses to load at all - *"this machine does not
take an 'i386DX/20'. It takes: ..."*.

### The mistake, because it is the instructive part

The first version of this work left `machine` OUT of all five presets and made
`check-presets.py` FAIL a preset that set it. The reasoning was this line of
`NewProjectWizard.ApplySelectedPreset`:

```csharp
if (pair.Key == RendererSetting || pair.Key == _cfg.MachineSetting) continue;
```

read together with project.md's prose - *"the machine setting and the renderer
are not a preset's to move"*. Both are real. What was never checked is what
`_cfg.MachineSetting` IS for this package.

It is the **declared** machine chooser, the one that goes with a `machines[]`
array, and this package declares neither: `machineSetting` is absent from
`waterbox.config` and `machines[]` is empty. So it is null, `"machine"` never
equals null, and `machine` is an ordinary setting the wizard writes like any
other. There is no page-one machine question here to be downstream of.

Measured, in the running frontend (section 10):

```
PROBE: MachineSetting=<null> Machines=0 Presets=5
PROBE: dos_late_80s applied, machine now deskpro386 - [386DX] Compaq Deskpro 386
```

**How it survived a gate is the part to remember.** The check was a Python
stand-in for `ApplySelectedPreset`, and the stand-in was written from the same
sentence the belief came from. It skipped `machine` because the belief said to,
so it agreed with the belief, and five presets, two gate legs, a boot proof and
a document all agreed with each other while being wrong. That is `gates.md`
mode E - *a synthetic stand-in that does not behave like the real subject* -
and it is worse than the examples there, because those stand-ins at least came
from a different source than the claim.

The rule it forces: **when a stand-in and the real subject can differ, the
stand-in's job is to be checked against the real one at least once.** Section 10
is that check, now done, including watching it fail.

**`when[]` is dropped.** `PresetsFor` gates a preset by MachineConfig, and with
no `machines[]` there is no MachineConfig, so `AppliesTo(null)` is true and
every preset is offered regardless - a `when[]` here would be inert. The first
version declared it anyway, "semantically right and forward-compatible", which
is exactly the trap: an inert declaration that reads as load-bearing is worse
than none, and this one was load-bearing in the wrong direction (it was where
the resolver got the board from). The board lives in `values["machine"]`, where
it does something, and `check-presets.py` now FAILS a preset that declares
`when[]` while the package declares no `machines[]`.

## 2. The five, and where each value comes from

Chronological order, because the selector does not sort.

| id | label | `values["machine"]` |
|---|---|---|
| `dos_late_80s` | DOS, late 1980s - Compaq Deskpro 386 | `deskpro386 - [386DX] Compaq Deskpro 386` |
| `dos_early_90s` | DOS, early 1990s - Packard Bell PB570 | `pb570 - [Socket 5] Packard Bell PB570` |
| `dos_late_90s` | DOS, late 1990s - Gigabyte GA-686BX | `ga686bx - [Slot 1] Gigabyte GA-686BX` |
| `win95b_osr2` | Windows 95b OSR 2 - Gigabyte GA-686BX | `ga686bx - [Slot 1] Gigabyte GA-686BX` |
| `winxp_sp3_home` | Windows XP SP3 Home Edition - Gigabyte GA-686BX | `ga686bx - [Slot 1] Gigabyte GA-686BX` |

The label carries the board because the label is all the selector shows, and
`check-presets.py` fails a preset whose label does not name the board it
builds. The descriptions say what the machine is and what it is for; none of
them tells the user to go and pick a board, because none of them has to.

Sources, fetched 2026-09-21: `tasvideos.org/EmulatorResources/PCem/DOS` and
`/DOS/Configurations`, `/Windows/Configurations/95` and `/XP`. The era wording
in the labels is theirs: the DOS page defines late '80s as games released up to
1989-12-31, early '90s as 1990-01-01 to 1994-12-31, and late '90s as after
1995-01-01.

Every value in a preset is one the source table names, or a fact about the CPU
the table names. Two values are the second kind and are worth saying out loud:

- **`fpu`.** The tables do not mention a coprocessor. A 386DX has none on the
  die, so the Deskpro preset says `none`; a Pentium and a Pentium II have one
  and PCem's `fpus_builtin` table offers nothing else, so the other four say
  `builtin`.
- **`dynarec`.** PCem's own settings dialog forces the recompiler on and greys
  the box for a CPU flagged `CPU_REQUIRES_DYNAREC`, and off for one without
  `CPU_SUPPORTS_DYNAREC` (`wx-config.c:791-797`). i386DX/20 has neither flag and
  Pentium/Pentium II have both, so the Deskpro preset says `false` and the other
  four say `true`. This matches the `.cfg` inside TASVideos' own Late '80s
  package, which PLAN.md section 6.1 recorded as `cpu_use_dynarec = 0`.

One value is stated where the source is silent, deliberately:
`soundCardAddress = 0x220` in all five. The two DOS tables name it; the two
Windows tables do not. 0x220 is both PCem's default for every Sound Blaster and
the port every DOS and Windows driver of the period assumes, so stating it
changes no machine and makes the grid say what the machine is rather than leave
a person to know what a default is.

## 3. Row by row: every line of the tables, and what became of it

| TASVideos row | Chimera | |
|---|---|---|
| Machine | `machine` | existing, and **every preset sets it** - see section 1 |
| CPU | `cpu` | existing |
| Memory | `memSizeKB` | existing (PCem's `mem_size` is always KB) |
| Video: which card | `videoCard` | existing |
| Video: "2 MB" / "4 MB" | **`videoMemory`** | NEW |
| Video: "+ Voodoo Graphics" | `voodoo` | existing |
| Video: which add-in Voodoo | **`voodooType`** | NEW |
| Video: "Voodoo Graphics option disabled" | `voodoo` = false | existing |
| Video: "render threads 1" | **`videoRenderThreads`** | NEW (and `voodooRenderThreads` for the add-in card) |
| Video: "speed Fast VLB/PCI" | `videoSpeed` | existing |
| Sound: which card | `soundCard` | existing |
| Sound: "Address 0x220" | **`soundCardAddress`** | NEW |
| Sound: "IRQ 7" | **`soundCardIrq`** | NEW |
| Sound: "DMA 1" | **`soundCardDma`** | NEW |
| Sound: "NukedOPL" | **`oplEmulator`** | NEW |
| Sound: "all other options unchecked" | `gameBlaster`, `gus`, `ssi2001` | existing, and all three are stated rather than left to a default |
| "LPT device none" | `lpt1Device` | existing |
| Mouse | `mouseType` | existing |
| HDD: "IDE Standard" | `hddController` | existing |
| HDD: "Type 09" / "Type 46" | - | **deliberately not a setting** (section 4) |
| HDD: size, and the custom 63/16/8374 | `hddGeometry` = Auto | existing; the preset deliberately pins nothing (section 4) |
| FDD1 "3.5\" 2.88M", FDD2 "5.25\" 1.2M" | `driveAType`, `driveBType` = Auto | existing; deliberately Auto (section 4) |
| CD: "PCemCD" | `cdModel` | existing |
| CD: "24x" / "72x" | `cdSpeed` | existing |
| CD: "on Secondary Slave" | `cdChannel` = 3 | existing; its description now names all four positions |
| CD: `cdrom_drive = 200` set by hand | - | not a setting: the driver writes 200 whenever a drive is fitted, and -1 (never 0) when one is not |
| "Time sync: enabled to host clock" | - | **deliberately not a setting** (section 4) |

Eleven further settings were added in the same pass, for keys on the same
devices that the tables do not name. The source note for this work says it
plainly - *"all other options unchecked" means the preset should state those
settings explicitly rather than rely on a default* - and a setting that exists
only when a table happens to mention it is not "fully settable":

`awe32EmuAddress`, `awe32OnboardRam`, `videoBilinear`, `videoScreenFilter`,
`videoRecompiler`, `voodooFramebufferMemory`, `voodooTextureMemory`,
`voodooBilinear`, `voodooScreenFilter`, `voodooSli`, `voodooRecompiler`.

And the `fpu` setting grew from two options to six: PCem's CPU tables carry a
list of coprocessors per CPU (`fpus_8088`, `fpus_80286`, `fpus_80386`,
`fpus_builtin`), and until now a 386DX could not be given the 387 it was sold
with. `fpu_get_type` matches the string against that CPU's own list and falls
back to its first entry, so no choice here can build a machine PCem would
refuse.

### How a per-device setting works, because it is not the obvious way

None of the nineteen is a global `.cfg` key. Each card's own options live in a
`[device name]` section and are read back through `device_get_config_int`, which
looks the key up in THAT device's table and returns the matching number
(`device.c:120-133`). Three facts follow:

- the NUMBER is the device's. A video card's `memory` is in MB on an S3 Trio64
  and in kB on an AVGA2;
- so is the set of legal values. An SB Pro v2 takes two addresses where an SB16
  takes four;
- and the same key NAME means different things on different devices. The
  AHA-1542C has an `addr` too, and it is a BIOS window.

So the Chimera setting carries the LABEL PCem shows a user - `"0x220"`,
`"NukedOPL"`, `"4 MB"` - and `cfg_device_key()` resolves it against the table of
the card that is actually fitted. One setting serves 20 sound cards and 48 video
cards with no per-card table in the driver to go stale, and the option lists are
generated from PCem's own `device_config_t` tables by `tools/gen-config.py`
(`device_config_groups`), restricted to the files the guest is actually built
from and to the device family the setting is about.

Every one of them has a **"Card default"** option and it is the default. PCem's
default differs per card - 2 MB of video memory on a Trio64, 4 on a GD5434, 16
on a Banshee - so there is no single value that means "leave it alone". "Card
default" writes nothing, which is exactly what this core did before these
settings existed, and it is why the whole change moves no existing machine.

A label the fitted card does not offer also writes nothing, and says so on
stderr naming the setting, the label, the card and the key. PCem does not
range-check what it reads (`device_get_config_int` returns the file's value
unvalidated), so silently writing `0x260` into a Sound Blaster Pro v2 would give
it a port no such card ever had. This is gates.md C: absent must not look the
same as failed.

## 4. Four things the tables say that are deliberately not here

**Time sync to the host clock.** Both Windows tables say "enabled to host
clock". A core that reads the host clock is not deterministic, which is the bug
fixed in Chimera issue #120 (`sys_time_get_current_time`); Chimera's clock is
the movie's. The driver has always written `enable_sync = 0` and there is
deliberately no setting for it. A deliberate divergence from the published
table, recorded so that a movie that will not sync against a libTAS one has a
named reason.

**The hard disk geometry.** The DOS tables give an HDD *type* and *size* and the
Windows tables a custom 63/16/8374 = 4121 MB. Those describe a disk PCem would
CREATE from its new-disk dialog. Here the disk is a file the user puts in the
`hdd` slot, and `hddGeometry` "Auto" derives the geometry from the image itself -
its own MBR's CHS fields first, its length second (docs/HDD.md section 3). A
preset that pinned 63/16/8374 would fight the user's actual file, and getting it
wrong is a disk that does not boot rather than an error. So every preset sets
`hddGeometry` to Auto and nothing else, and "Custom" with the three numbers is
right there for an image Auto cannot read.

This interaction is load-bearing and was nearly wrong once already: until
2026-09-21 "Auto" wrote three zeros, and a zero in `hdc[d].spt/hpc/tracks` is a
drive of no sectors that the guest cannot see at all.

**The HDD type number.** "Type 09", "Type 46" are indexes into the BIOS's own
geometry table, written to CMOS byte 0x12. `apply_cmos` deliberately does not
write that byte (docs/CMOS.md): the derived geometry of an arbitrary image is
not one of the standard types, and writing a type whose geometry disagrees with
the image is worse than writing none. Every machine in these five presets has
IDE and a BIOS that autodetects, so none of them needs it. A `hddCmosType`
setting is the fix for the MFM and ESDI machines that do; it is named in CMOS.md
and not built.

**The fixed floppy drives.** All five tables put a 3.5" 2.88M in bay A and a
5.25" 1.2M in bay B. That is TASVideos making the drives big enough for anything
they might mount, and here it would be actively wrong: "Auto" fits the drive to
the image in the slot, and the late-1980s package exists to run games on 180 KB
and 360 KB disks. The gate already proves the case - Alley Cat's 180 KB image
needs a 5.25" 360k drive, which is a drive nobody would have guessed. So all
five presets say Auto, and a named drive still overrides it.

## 5. The Early '80s package

The DOS page's prose names FOUR packages and the Configurations page has tables
for only THREE. The missing one is "Early '80s", for 1980s games that boot or
run straight off a floppy with no hard drive.

**It is not shipped, and nothing here was invented to stand in for it.** The
source does not specify the machine: PLAN.md section 6.1 records it as "a
UserFile, not a package; not read", and the prose gives an era and a use but no
board, no CPU, no video card and no sound card. Deriving one would mean choosing
an 8088 or an 8086, a machine out of the fourteen PCem has at that end, CGA or
MDA, and a Game Blaster or nothing - four inventions presented under TASVideos'
name.

What exists instead is better than a guess: the gate boots Alley Cat (1984) on
an `ibmpc` IBM PC 5150 with CGA off a 180 KB single-sided disk, and those
settings are in `run-gate.sh` where anyone can read them. If a fifth preset is
wanted, that measured machine is the honest basis for it - declared as this
port's own, not as TASVideos'.

## 6. Firmware, per preset, against the real folder

Measured 2026-09-21, and **re-measured after the board moved into
`values["machine"]`**, because the machine decides the BIOS and therefore the
requirement. Each preset resolved, every firmware entry's `requiredWhen`
evaluated against the resolved settings, and the requirement's SHA1 matched
against every file under
`C:\Users\sergiom\Documents\TAS\firmware\PCem-ROMs` - which is how the wizard's
Scan Folder does it, hash-first.

| preset | required | in the folder |
|---|---|---|
| `dos_late_80s` | `deskpro386/109591-005.u13.bin`, `deskpro386/109592-005.u11.bin`, `ibm_vga.bin` | all 3 |
| `dos_early_90s` | `pb570/1007by0r.bi1`, `pb570/1007by0r.bio`, `pb570/gd5430.bin` | all 3 |
| `dos_late_90s` | `ga686bx/6BX.F2a`, `86c764x1.bin` | both |
| `win95b_osr2` | `ga686bx/6BX.F2a`, `voodoo3_3000/3k12sd.rom` | both |
| `winxp_sp3_home` | `ga686bx/6BX.F2a`, `voodoo3_3000/3k12sd.rom`, `awe32.raw` | all 3 |

**Every preset is satisfiable**, before and after the change - the boards were
the same either way, so the table did not move, but it was run again rather
than assumed. The two Windows presets ask for exactly the three files the
TASVideos Windows page names, and nothing else.

Two things this also settled:

- **The PB570's on-board video is covered by the machine, not by the video
  card.** `videoCard = builtin` produces no requirement of its own; the board's
  `gd5430.bin` is one of the three ROMs the `pb570` machine asks for. So the
  early-'90s preset's "Built-in video" does resolve.
- **The add-in Voodoo Graphics needs no ROM at all.** `voodoo_device` has no
  `available()` and never calls `rom_init`; the only `texram.dmp` in
  `vid_voodoo.c` is a debug dump it WRITES. So the late-'90s preset's
  `voodoo = true` adds no firmware requirement, and correctly does not.

## 7. Boot proof: all five, on this machine

Each preset resolved, its firmware mounted by hash, 9000 frames (90 s of
emulated time), screenshot at the last frame. Nothing else in the slots except
where noted. **Re-run after the board moved into `values["machine"]`**: the
per-frame digest streams came back bit-identical to the first run, which is the
evidence that the two resolvers picked the same board rather than an assurance
that they had to.

The composed `.cfg` says the board arrived: `model = deskpro386`,
`model = pb570`, `model = ga686bx` for the other three.

| preset | what happened | lit pixels |
|---|---|---|
| `dos_late_80s` | POSTs, counts 04096 KB, stops at Compaq's **162-System Options Not Set** waiting for F1 | 2,862 |
| `dos_early_90s` | AMIBIOS POSTs: 0008192 KB, Processor Speed 133 MHz, keyboard and mouse detected, Floppy A and B installed - then **NVRAM Checksum Error, NVRAM Cleared**, ESC to boot | 6,859 |
| `dos_late_90s` | POSTs clean to the Award summary and DISK BOOT FAILURE | 21,991 |
| `win95b_osr2` | POSTs clean to the Award summary and DISK BOOT FAILURE | 22,601 |
| `winxp_sp3_home` | POSTs clean to the Award summary and DISK BOOT FAILURE | 22,617 |

All five reach a BIOS screen. What the three Award summaries actually say is the
proof that the values arrived, and it is worth quoting:

- `win95b_osr2`: PENTIUM II, **233MHz**, Co-Processor Installed, Extended Memory
  **261120K** (256 MB); PCI listing `121A 0005` Display Controller - the Voodoo
  3 3000.
- `winxp_sp3_home`: the same at **450MHz**.
- `dos_late_90s`: PENTIUM II 450MHz, Extended Memory **31744K** (32 MB); PCI
  listing `5333 8811` Display Controller (the S3 Trio64) **and** `121A 0001`
  Multimedia Device (the add-in Voodoo Graphics) - both cards, which is what
  "Trio64 + Voodoo Graphics" means; and **`Sec. Slave Disk : CDROM, Mode 4`**,
  which is `cdChannel = 3` reaching the machine and the BIOS finding the drive
  where the table says to put it.

### The two POST stops are not the presets' doing

Measured, not argued. Rebuilt with `apply_cmos()` returning immediately and both
runs repeated: the resulting screenshots are **byte-identical** to the working
build's. So the CMOS the driver writes changes nothing on these two boards, and
the stop is theirs.

It also still holds with the board coming from the preset: the re-run's digest
streams and lit-pixel counts are identical to the first run's, so nothing about
moving `machine` into `values` landed a different board or a different POST.

This is docs/CMOS.md's own named gap coming due - *"Only the IBM AT was tested.
The other 50-odd AT-class machines were not [...] that is an argument, not a
measurement."* Now it is a measurement, and the argument was wrong for two of
them:

- the **Compaq Deskpro 386** wants its SETUP run from Compaq's diagnostic
  diskette, which is what its 162 message says; the three MC146818 equipment
  bytes are not what it is checking.
- the **Packard Bell PB570** has a 128-byte NVRAM (`nvrmask = 127`) and its
  AMIBIOS keeps its own checksum over a range the driver's 0x10..0x2D sum does
  not cover, so it reports the checksum bad and clears it.

**Both presets work after one keypress.** F1 on the Deskpro, ESC on the PB570,
with Alley Cat's 180 KB disk in bay A: both reach the game's *"Do you want to
use a joystick (Y/N)?"* prompt at frame 30,000. So these are machines that need
one frame of input recorded at the start of a movie, not machines that do not
boot. Fixing them properly is CMOS work, not preset work, and is left where
CMOS.md already has it.

## 8. PLAN.md section 6 recommended Option C; Option B is what shipped

PLAN.md 6.2 weighed three designs and recommended **Option C**, `machines` +
`settingOverrides`, on the grounds that it needed no Chimera change. The
frontend went the other way and built **Option B**, the `presets` declaration
(chimera 8125d43), for reasons project.md sets out: Apply writes values the user
then owns, and nothing records a preset NAME whose meaning a later core build
could change under a movie. That is the better design and this core now uses it.

Two of Option C's four advantages do not come with Option B, and should not be
quietly forgotten:

- **The DOS/PC platform split does not come free.** A `machines[]` entry carries
  its own `systemId`; a preset does not. So a DOS project's movie header says
  `PC`, where TASVideos' convention is `Platform: DOS`. Nothing is wrong, and
  nothing about it is fixed by presets.
- **The preset is not a recorded fact.** That is the point of Option B, not a
  defect - the resolved values are recorded instead, and they are the truth.
  But a movie header will not say "Windows XP preset", and a reader has to read
  the settings.

The third and fourth survive intact: the firmware page still becomes two lines
because the preset writes REAL values that the decision tree resolves against
(section 6 measured it), and "custom" is still not a second-class citizen -
it is simply not applying a preset.

## 9. The gate

Three legs in `waterbox/run-gate.sh`, taking it from 26 to 29.

| Leg | What it asserts |
|---|---|
| the presets are legal | **every preset SETS the machine**, to one of the 93 boards this build declares, and names it in its label; every other `values` key is a declared setting; every value legal for its declared type, options and range; no preset touches the renderer, which the wizard skips in silence; no preset declares an inert `when[]`; ids unique; the prose is ASCII |
| a misdeclared preset is caught | **negative control, run every time.** A copy of the declaration with one preset given a setting that does not exist and another given a string where the core declared an int |
| every preset reaches the machine and boots | for each of the five: resolve it, mount its firmware by hash, and read `GetComposedConfig` - the `.cfg` PCem was actually handed - to check the values arrived; then run 9000 frames and require a picture at the end AND a picture that CHANGED during the run |

The third leg exists because of gates.md B, and the reason is specific to this
change. A per-device value the driver drops leaves PCem's own default in place,
so the machine still boots, the picture still looks right, and **every digest
still matches** - the default is exactly what this core did before these
settings existed. A boot leg on its own therefore cannot fail for the thing most
likely to break. So the assertion is the composed `.cfg`, and the liveness
assertion is a picture that MOVED rather than a stable digest, because a machine
stuck at frame 1 is perfectly deterministic and this project has been caught by
that three times.

### Proven to bite

Two breaks, each built and run:

| Break | What went red |
|---|---|
| `compose_device_sections()` commented out | *every preset reaches the machine and boots*: **18 problems across all five presets**, naming every device key that had gone missing - `soundCardAddress`, `oplEmulator`, `soundCardIrq`, `soundCardDma`, `videoMemory`, `voodooType`, `videoRenderThreads`. The boot half went on passing for four of the five |
| a preset with an undeclared key and a string in an int | *a misdeclared preset is caught*, which is the permanent control above |
| a preset with an undeclared key and one setting `renderer` | the FRONTEND probe of section 10, in the real wizard: both reported, `thereIsNoSuchSetting` and `renderer`, which is also an independent confirmation of which keys Apply really drops |

One thing found while doing it, worth not rediscovering: at **2000** frames the
Compaq Deskpro 386 is still counting memory and has 361 lit pixels, which trips
the boot threshold. 9000 is not an arbitrary number.

## 10. Driven through the REAL wizard, once

The one thing the gate cannot do, because a frontend leg must not depend on a
core package being installed. Done by hand on 2026-09-21 with a throwaway
`[TestClass]` in `Chimera.Tests.Client.GUI`, which loaded THIS core's actual
`waterbox.config` and `file_slots.json`, built a real `NewProjectWizard` under
Xvfb, and for each preset called `SelectPreset(id)`, `ApplyPreset()` and
`SettingValue(name)` for every key the preset sets. Deleted afterwards and the
solution rebuilt without it.

What it measured:

```
PROBE: MachineSetting=<null> Machines=0 Presets=5
PROBE: offered=True names=[DOS, late 1980s - Compaq Deskpro 386 |
       DOS, early 1990s - Packard Bell PB570 | DOS, late 1990s - Gigabyte GA-686BX |
       Windows 95b OSR 2 - Gigabyte GA-686BX | Windows XP SP3 Home Edition - Gigabyte GA-686BX]
PROBE: dos_late_80s applied, machine now deskpro386 - [386DX] Compaq Deskpro 386
PROBE: dos_early_90s applied, machine now pb570 - [Socket 5] Packard Bell PB570
PROBE: dos_late_90s applied, machine now ga686bx - [Slot 1] Gigabyte GA-686BX
PROBE: win95b_osr2 applied, machine now ga686bx - [Slot 1] Gigabyte GA-686BX
PROBE: winxp_sp3_home applied, machine now ga686bx - [Slot 1] Gigabyte GA-686BX
```

- `MachineSetting` is null and `Machines` is empty, in the running frontend, for
  this package. That is the fact section 1 failed to check.
- All five are offered, in declaration order, under the labels declared.
- Every value of every preset - 29 distinct keys, `machine` among them - came
  back from `SettingValue` equal to what the preset declares. Zero mismatches.

**And it was watched failing**, because a probe that has only ever passed proves
nothing. With `thereIsNoSuchSetting: 1` added to one preset and
`renderer: "opengl"` to another, it reported exactly two problems and named
both. That break is also the independent confirmation that `renderer` IS dropped
and `machine` is NOT: the two keys went into the same run and only one of them
came back missing.

What the probe still does not cover: it drives the settings page only. It says
nothing about the firmware page, the files page, or what a saved project
records - and it ran on Mono under Xvfb, where the people who use Chimera run
.NET Framework WinForms (gates.md E again, from the other direction).

## 11. What is NOT established

- **The gate still drives a Python stand-in, not the wizard.** Section 10
  checked the stand-in against the real subject once, by hand, and that is
  what settled the machine question - but the leg that runs on every gate is
  still the stand-in, and it can drift from the frontend again. What would
  stop that permanently is a frontend test over an installed core package,
  which is a thing the frontend gate deliberately does not have. So the
  standing risk is named rather than closed: **if `ApplySelectedPreset`
  changes, nothing here notices.**
- **The probe covered the settings page only**, on Mono under Xvfb. It says
  nothing about the firmware page, the files page, what a saved project
  records, or .NET Framework WinForms.
- **No preset was booted into an operating system.** Three of the five reach
  DISK BOOT FAILURE with nothing in the slots, which is the right answer for an
  empty machine. Whether the Windows 95 preset installs Windows 95 is untested,
  and the answer to that question is an installation movie.
- **Sound was never listened to.** `oplEmulator = NukedOPL` is proved to reach
  the machine as `opl_emu = 1` in the right section. Whether NukedOPL and DBOPL
  produce different samples here was not measured, and neither was whether the
  address, IRQ and DMA a preset sets are the ones a DOS game's driver finds.
- **`voodooSli`, `voodooFramebufferMemory`, `voodooTextureMemory`,
  `videoBilinear`, `videoScreenFilter`, `videoRecompiler`, `awe32EmuAddress`
  and `awe32OnboardRam` were never set to anything but "Card default" in any
  run.** They go down the same `cfg_device_key` path as the six that were, and
  that path is gated - but the specific keys are an inference from the seam,
  not a measurement.
- **A "Card default" that is not the card's default would be invisible.** The
  check asserts that a non-default setting wrote SOMETHING; it does not predict
  the number, because predicting it would mean re-deriving PCem's device tables
  in Python and that stand-in could drift from the driver it is checking. What
  catches a wrong number today is reading the composed `.cfg`, which the gate
  prints.
- **`dynarec` is not clamped to what the CPU supports.** PCem's own dialog
  forces it on for a `CPU_REQUIRES_DYNAREC` CPU and greys the box; this port
  writes the setting through unmodified (`pc.c:712` reads it raw). The presets
  set the right value, but a user can still turn it off under a Pentium II and
  build a machine PCem's own GUI would not let them. Named here; not changed,
  because "fully settable" cuts both ways and the fix belongs with whoever
  decides which way.
- **Most of PCem's per-device keys are still not settings.** Nineteen of the 37
  `device_config_t` keys in the build are now reachable. The rest are: the SCSI
  controllers' `addr`, `irq`, `dma`, `host_id`, `bios_addr`; the CGA/MDA/
  Hercules/PCjr/Tandy family's `display_type`, `composite_type`, `snow_enabled`,
  `contrast`, `video_emulation`, `codepage`, `display_language`, `monitor_type`,
  `enable_nmi`, `dithering`; the Aztech's `codec`, `mixaddr` and
  `wss_interrupt_after_config`; the Adlib Gold's `surround`; the Amstrad's
  `language`; the Xi8088's `bios_128kb` and `turbo_setting`; the UPC's
  `serial_irq` and `parallel_irq`; and `midi`, which has nowhere to go in the
  sandbox. None is in any TASVideos table. The mechanism to add them is now
  three lines each - a `dev_opts()` entry in the generator and a row in the
  driver's table - so this is a list of work, not a wall.

#!/usr/bin/env python3
"""Build a 1.44 MB FAT12 floppy image holding a Windows Setup answer file.

Windows XP Setup, booted from CD, looks for winnt.sif on the floppy in A:
and runs unattended from it. That is far more reliable than scripting two
hundred keystrokes through Setup's GUI blind, and it is the only way to
supply a product key without the key ever appearing in a script, a log or a
screenshot: this reads it from a file at run time and writes it straight into
the image.

The product key is a credential.
  - It is read from the file named by --key (default ~/winxp.key).
  - It is never printed, and the image this writes must never be committed.
    Write it under build/, which is gitignored.

usage: make-unattend-floppy.py <out.img> [--key ~/winxp.key]
"""
import os
import struct
import sys

SECTOR = 512
SECTORS = 2880              # 1.44 MB
RESERVED = 1
FATS = 2
FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR      # 14
DATA_START = RESERVED + FATS * FAT_SECTORS + ROOT_SECTORS   # 33

WINNT_SIF = """[Data]
AutoPartition=1
MsDosInitiated="0"
UnattendedInstall="Yes"

[Unattended]
UnattendMode=FullUnattended
OemSkipEula=Yes
FileSystem=NTFS
WaitForReboot="No"
TargetPath=\\WINDOWS
Repartition=Yes

[GuiUnattended]
AdminPassword=*
AutoLogon=Yes
AutoLogonCount=1
OEMSkipRegional=1
OemSkipWelcome=1
TimeZone=85

[UserData]
ProductKey={key}
FullName="TAS"
OrgName="TAS"
ComputerName=PCEM

[Display]
BitsPerPel=16
XResolution=640
YResolution=480
VRefresh=60

[Identification]
JoinWorkgroup=WORKGROUP

[Networking]
InstallDefaultComponents=Yes
"""


def fat12_set(fat, cluster, value):
    off = cluster + cluster // 2
    if cluster & 1:
        fat[off] = (fat[off] & 0x0F) | ((value << 4) & 0xF0)
        fat[off + 1] = (value >> 4) & 0xFF
    else:
        fat[off] = value & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)


def build(out_path, key):
    img = bytearray(b"\x00" * (SECTORS * SECTOR))

    # ---- boot sector: a valid BPB, and a bootstrap that hands the boot
    # straight back to the BIOS.
    #
    # The machine's CMOS boot order is A, CDROM, C, and Setup wants the answer
    # file on A:, so the floppy is in the way of the CD. Two things that do
    # NOT work, both tried: a signature with no bootstrap stops at "Non-System
    # disk", and omitting the 0x55AA signature entirely does not make this
    # Award BIOS skip the drive - it hangs in POST before the boot summary.
    # What does work is the documented escape: INT 18h, which on a PC BIOS
    # means "this device is not bootable, carry on down the boot sequence".
    # So the bootstrap is two bytes, and the CD boots next.
    bs = bytearray(b"\x00" * SECTOR)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", bs, 11,
                     SECTOR,        # bytes per sector
                     1,             # sectors per cluster
                     RESERVED,      # reserved sectors
                     FATS,          # number of FATs
                     ROOT_ENTRIES,  # root entries
                     SECTORS,       # total sectors
                     0xF0,          # media descriptor
                     FAT_SECTORS,   # sectors per FAT
                     18,            # sectors per track
                     2,             # heads
                     0,             # hidden sectors
                     0)             # large total sectors
    bs[38] = 0x29
    bs[39:43] = b"\x12\x34\x56\x78"
    bs[43:54] = b"UNATTEND   "
    bs[54:62] = b"FAT12   "
    bs[62:64] = b"\xCD\x18"          # int 18h - not bootable, next device
    bs[64:66] = b"\xEB\xFE"          # and if that ever returns, stop here
    bs[510:512] = b"\x55\xAA"        # a signature, so the BIOS reads it at all
    img[0:SECTOR] = bs

    # ---- the file
    data = WINNT_SIF.format(key=key).replace("\n", "\r\n").encode("ascii")
    nclusters = (len(data) + SECTOR - 1) // SECTOR

    fat = bytearray(b"\x00" * (FAT_SECTORS * SECTOR))
    fat[0], fat[1], fat[2] = 0xF0, 0xFF, 0xFF
    for i in range(nclusters):
        cluster = 2 + i
        fat12_set(fat, cluster, 0xFFF if i == nclusters - 1 else cluster + 1)
    for n in range(FATS):
        off = (RESERVED + n * FAT_SECTORS) * SECTOR
        img[off:off + len(fat)] = fat

    entry = bytearray(b"\x20" * 32)
    entry[0:11] = b"WINNT   SIF"
    entry[11] = 0x20                                  # archive
    struct.pack_into("<HHI", entry, 22, 0, 0, 0)      # time, date
    struct.pack_into("<H", entry, 26, 2)              # first cluster
    struct.pack_into("<I", entry, 28, len(data))      # size
    root_off = (RESERVED + FATS * FAT_SECTORS) * SECTOR
    img[root_off:root_off + 32] = entry

    data_off = DATA_START * SECTOR
    img[data_off:data_off + len(data)] = data

    with open(out_path, "wb") as f:
        f.write(img)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    out = args[0]
    key_path = os.path.expanduser("~/winxp.key")
    if "--key" in args:
        key_path = os.path.expanduser(args[args.index("--key") + 1])
    with open(key_path) as f:
        key = f.read().strip()
    if len(key) != 29 or key.count("-") != 4:
        print("key file does not look like XXXXX-XXXXX-XXXXX-XXXXX-XXXXX",
              file=sys.stderr)
        return 1
    build(out, key)
    # Deliberately says nothing about the key itself.
    print("wrote %s (1.44 MB FAT12, winnt.sif, product key from %s)"
          % (out, key_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Prove that Auto fits the drive to the image, by running the core with a
floppy of each standard size and reading back what PCem was actually told.

The driver composes PCem's .cfg in memory, so the check is: does the machine
report the drive the image calls for? GetDriveAType/GetDriveBType export
exactly what went into the config.

usage: check-auto.py <run-wbx> <core.wbx> <workdir>
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

# size -> the drive Auto must fit (PCem's own type numbers)
CASES = [
    (163840,  1, '160k -> 5.25" 360k'),
    (368640,  1, '360k -> 5.25" 360k'),
    (737280,  4, '720k -> 3.5" 720k'),
    (1228800, 2, '1.2M -> 5.25" 1.2M'),
    (1474560, 5, '1.44M -> 3.5" 1.44M'),
    (2949120, 7, '2.88M -> 3.5" 2.88M'),
    (0,       5, 'no disk -> 3.5" 1.44M'),
    (999999,  5, 'odd size -> 3.5" 1.44M'),
]


def main():
    runner, core, base = sys.argv[1], sys.argv[2], sys.argv[3]
    bad = 0
    for size, want, label in CASES:
        work = tempfile.mkdtemp(prefix="pcem-auto-")
        try:
            for f in os.listdir(base):
                p = os.path.join(base, f)
                if os.path.isfile(p):
                    shutil.copy(p, work)
            s = json.load(open(os.path.join(work, "settings")))
            s["driveAType"] = "Auto"
            json.dump(s, open(os.path.join(work, "settings"), "w"))
            names = []
            if size:
                open(os.path.join(work, "disk.img"), "wb").write(b"\0" * size)
                names = ["disk.img"]
            json.dump({"floppy_a": names}, open(os.path.join(work, "slots"), "w"))

            out = subprocess.run([runner, core, work, "2", "--drive-types"],
                                 capture_output=True, text=True).stdout
            got = None
            for line in out.splitlines():
                if line.startswith("DRIVES"):
                    got = int(line.split("a=")[1].split()[0])
            if got != want:
                print(f"  BAD  {label}: fitted type {got}, wanted {want}")
                bad += 1
            else:
                print(f"  ok   {label}")
        finally:
            shutil.rmtree(work, ignore_errors=True)
    print(f"{len(CASES) - bad} of {len(CASES)} Auto cases correct")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

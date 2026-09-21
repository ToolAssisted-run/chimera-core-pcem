#!/usr/bin/env python3
"""Writes a .chimeraProject for the PCem package, so a gate can drive the core
through the ENGINE rather than through run-wbx.

run-gate.sh's engine leg hands chimera-run a bare rom and a settings blob,
which never exercises a slot: the driver resolves its floppies and hard disks
out of the project's "slots" mount, and that mount only exists for a project.
So the hard disk's whole route - project file -> slot -> mounted by path ->
seed for the overlay -> export through the save-data channel - is invisible to
every other leg. This writes the project that makes it visible.

usage: make-project.py <package> <out.chimeraProject> <frames>
                       [--setting k=v]... [--file slot=path]...
                       [--firmware id=path]...
"""

import hashlib
import json
import os
import sys
import zipfile


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    package, out, frames = args[0], args[1], int(args[2])
    over, files_in, firmware_in = {}, [], []
    i = 3
    while i < len(args):
        if args[i] == "--setting":
            k, _, v = args[i + 1].partition("=")
            over[k] = v
            i += 2
        elif args[i] == "--file":
            slot, _, path = args[i + 1].partition("=")
            files_in.append((slot, path))
            i += 2
        elif args[i] == "--firmware":
            fid, _, path = args[i + 1].partition("=")
            firmware_in.append((fid, path))
            i += 2
        else:
            print(f"unknown argument {args[i]!r}", file=sys.stderr)
            return 2

    blob = open(package, "rb").read()
    cfg = json.loads(zipfile.ZipFile(package).read("waterbox.config"))

    inputs = cfg.get("input") or {}
    buttons = inputs.get("buttons") or []
    axes = inputs.get("axes") or []

    # The mnemonic the engine parses: groups by player, axes first as a value
    # padded to five and closed with a comma, then one character per button.
    # Mirrored from EntryLayout::generate - see the ares core's copy, which
    # this is adapted from.
    def player_of(name):
        if len(name) > 2 and name[0] in "Pp" and name[1].isdigit():
            return int(name[1])
        return 0

    groups = max([player_of(a["name"]) for a in axes]
                 + [player_of(b) for b in buttons] + [0]) + 1
    row, key = "", ""
    for g in range(groups):
        row += "|"
        key += "#"
        for a in axes:
            if player_of(a["name"]) != g:
                continue
            row += "%5d," % a.get("neutral", 0)
            key += a["name"] + "|"
        for b in buttons:
            if player_of(b) != g:
                continue
            row += "."
            key += b + "|"
    row += "|"
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join([row] * frames) + "\n[/Input]\n"

    def sha1(path):
        h = hashlib.sha1()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest().upper()

    files = [{"name": os.path.basename(p), "sha1": sha1(p), "slot": s}
             for s, p in files_in]

    # Every declared setting gets a value, because that is what the frontend
    # does; a setting the project leaves out is a setting the core never sees.
    settings = {}
    for decl in cfg.get("settings", []):
        name = decl["name"]
        if decl.get("default") is not None:
            settings[name] = decl["default"]
    settings.update(over)

    project = {
        "id": "pcemhddgate00000"[:16],
        "title": "PCem hard disk, through Chimera",
        "description": "written by waterbox/tests/make-project.py",
        "core": {
            "name": cfg.get("coreName", "PCem"),
            "version": cfg["version"],
            "sha1": hashlib.sha1(blob).hexdigest().upper(),
        },
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": [{"id": fid, "sha1": sha1(p)} for fid, p in firmware_in],
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": "PC",
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": "0",
            "VsyncNumerator": str(cfg.get("video", {}).get("vsyncNumerator", 100)),
            "VsyncDenominator": str(cfg.get("video", {}).get("vsyncDenominator", 1)),
        },
    }
    with open(out, "w") as f:
        json.dump(project, f, indent="\t")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate what this core DECLARES against the rules the engine enforces.

Written because a slot id of "floppyA" shipped, and nothing caught it: not
build-package.sh, not the gate, not loading the package. Only a user creating
a project ever saw it, as a complaint about THEIR file. A declaration this
core emits should never be one the engine can refuse.

usage: check-declaration.py <waterbox.config> <file_slots.json>
"""
import json
import re
import sys
from pathlib import Path

# validSlot(), source/engine/source/manifest_util.cpp:15 - lowercase letters,
# digits, underscore and hyphen, and nothing else.
SLOT_ID = re.compile(r"^[a-z0-9_-]+$")

# A firmware id is mounted as a file name in the guest's flat VFS, so it must
# not carry a path separator or be a directory traversal.
BAD_IN_NAME = ("/", "\\", "\0")


def main():
    cfg = json.loads(Path(sys.argv[1]).read_text())
    slots = json.loads(Path(sys.argv[2]).read_text())
    bad = []

    ids = [s["id"] for s in slots["slots"]]
    for sid in ids:
        if not SLOT_ID.match(sid):
            bad.append(f"slot id {sid!r} is malformed: the engine allows only "
                       f"[a-z0-9_-] (manifest_util.cpp:15)")
    if len(set(ids)) != len(ids):
        bad.append("duplicate slot ids")

    for group in slots.get("atLeastOneOf", []):
        for sid in group:
            if sid not in ids:
                bad.append(f"atLeastOneOf names {sid!r}, which is not a slot")

    fw_ids = [e["id"] for e in cfg.get("firmware", [])]
    for fid in fw_ids:
        if any(c in fid for c in BAD_IN_NAME) or fid in (".", ".."):
            bad.append(f"firmware id {fid!r} is not usable as a file name")
    if len(set(fw_ids)) != len(fw_ids):
        bad.append("duplicate firmware ids")

    # Every condition must name a setting that exists, or it silently
    # evaluates false and the firmware is never asked for.
    names = {s["name"] for s in cfg.get("settings", [])}
    def walk(c, where):
        if not isinstance(c, dict):
            return
        for g in ("any", "all"):
            if g in c:
                for sub in c[g]:
                    walk(sub, where)
                return
        if "not" in c:
            return walk(c["not"], where)
        if "setting" in c:
            if c["setting"] not in names:
                bad.append(f"{where} is conditioned on setting "
                           f"{c['setting']!r}, which does not exist")
            else:
                decl = next(s for s in cfg["settings"] if s["name"] == c["setting"])
                opts = decl.get("options")
                if opts:
                    for v in ([c["is"]] if "is" in c else c.get("in", [])):
                        if v not in opts:
                            bad.append(f"{where} wants {c['setting']}={v!r}, "
                                       f"which is not one of its options")
    for e in cfg.get("firmware", []):
        if "requiredWhen" in e:
            walk(e["requiredWhen"], f"firmware {e['id']}")
    for s in cfg.get("settings", []):
        if "exposedWhen" in s:
            walk(s["exposedWhen"], f"setting {s['name']}")

    # A default must be one of the options.
    for s in cfg.get("settings", []):
        if s.get("options") and "default" in s and s["default"] not in s["options"]:
            bad.append(f"setting {s['name']} defaults to {s['default']!r}, "
                       f"which is not one of its options")

    for b in bad:
        print("  BAD:", b)
    print(f"{len(ids)} slots, {len(fw_ids)} firmware entries, "
          f"{len(names)} settings: {len(bad)} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

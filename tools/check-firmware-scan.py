#!/usr/bin/env python3
"""Simulate Chimera's Scan Folder against a real ROM collection.

Sergio's goal, verbatim: "so that a single 'include sub-folders' search
matches all firmwares." Chimera's FirmwareLocator is hash-first - it matches
on SHA1 when the declaration carries one and only falls back to the name - so
this hashes every file in the folder and asks, for each declared entry,
whether some file in there matches it.

usage: check-firmware-scan.py <waterbox.config> <rom folder>
Exits non-zero if any entry with a declared hash fails to resolve.
"""
import hashlib
import json
import sys
from pathlib import Path


def main():
    cfg = json.loads(Path(sys.argv[1]).read_text())
    root = Path(sys.argv[2])
    if not root.is_dir():
        print(f"SKIP: no ROM collection at {root}")
        return 0

    by_sha, by_name, files = {}, {}, 0
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        files += 1
        h = hashlib.sha1(p.read_bytes()).hexdigest().upper()
        by_sha.setdefault(h, []).append(p)
        by_name.setdefault(p.name.lower(), []).append(p)

    hashed = [e for e in cfg["firmware"] if e.get("sha1")]
    unhashed = [e for e in cfg["firmware"] if not e.get("sha1")]

    missing, wrong_size, should_have = [], [], []
    for e in hashed:
        hits = by_sha.get(e["sha1"].upper())
        if not hits:
            missing.append(e)
            continue
        if "size" in e and all(p.stat().st_size != e["size"] for p in hits):
            wrong_size.append(e)

    print(f"scanned {files} files under {root}")
    print(f"declared {len(cfg['firmware'])} firmware entries: "
          f"{len(hashed)} with a hash, {len(unhashed)} without")
    print(f"resolved by hash: {len(hashed) - len(missing)} of {len(hashed)}")
    for e in missing:
        print(f"  UNRESOLVED {e['id']}  (sha1 {e['sha1'][:12]}...)")
    for e in wrong_size:
        print(f"  SIZE MISMATCH {e['id']}")
    for e in unhashed:
        here = e["name"].lower() in by_name
        print(f"  no hash declared: {e['id']} - "
              f"{'present by name' if here else 'NOT IN THIS COLLECTION'}")
        if here:
            should_have.append(e)

    # An entry with no hash whose file IS in this collection is a declaration
    # gap, not an acceptable unknown: it could have been hashed and was not.
    # Without this, a declaration with EVERY hash stripped reports "resolved
    # 0 of 0" and passes - which is precisely the shape of failure this whole
    # leg exists to catch.
    for e in should_have:
        print(f"  MISSING HASH (the file is right here): {e['id']}")

    return 1 if (missing or wrong_size or should_have) else 0


if __name__ == "__main__":
    sys.exit(main())

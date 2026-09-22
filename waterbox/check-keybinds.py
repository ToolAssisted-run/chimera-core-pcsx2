#!/usr/bin/env python3
"""Every declared control has a key on it out of the box.

chimera#132: the Arcade Panel declared all 47 of a JVS loom's lines and left
21 of them - every one that is not on a standard panel - with no default
binding at all. A twin-stick or drum game therefore looked like a game whose
controls did not work, and the only way to find out otherwise was to guess
which lines it read and bind them by hand.

Nothing could catch that. The controls were declared, so the frontend drew
them; the core read them, so a test that pressed them passed. What was missing
was a default, which is not a behaviour any digest can see.

Duplicates are NOT checked, and deliberately so: a port holds a gun or a pad,
never both, and the PlayStation 2 set binds Z to P1 Cross and to P1 Gun A on
purpose. A rule against that would be wrong 23 times over.

usage: check-keybinds.py waterbox/default_keybinds.json
"""
import json
import sys


def main(path):
    with open(path) as f:
        binds = json.load(f)

    missing = []
    total = 0
    for group, controls in binds.get("AllTrollers", {}).items():
        for name, spec in controls.items():
            total += 1
            if not (spec or "").strip():
                missing.append(f"{group}: {name}")

    if missing:
        print(f"check-keybinds: {len(missing)} of {total} declared controls have no default key:")
        for m in missing:
            print(f"  {m}")
        return 1
    print(f"check-keybinds: all {total} declared controls have a default key")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "waterbox/default_keybinds.json"))

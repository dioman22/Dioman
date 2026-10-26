#!/usr/bin/env python3
"""Create the FL Studio-style Plugin database fixture tree.

FL's registry is a folder of .fst files; subfolders are the categories the FL browser
shows. Empty .fst files are enough for the reader (we never parse the binary format).
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "tests", "fixtures", "fl", "Plugin database")

FILES = [
    ("Effects", "EQ", "Pro-Q 4.fst"),
    ("Effects", "Dynamics", "Pro-C 2.fst"),
    ("Effects", "Reverb", "ValhallaVintageVerb.fst"),
    ("Generators", "Synths", "Serum.fst"),
    ("Generators", "Synths", "Diva.fst"),
    ("Generators", "Drums", "MT-PowerDrumKit.fst"),
]


def main() -> int:
    for category, sub, name in FILES:
        folder = os.path.join(ROOT, category, sub)
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            with open(path, "wb") as handle:
                handle.write(b"sanitised fixture: FL .fst placeholder\n")
    print(ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main())

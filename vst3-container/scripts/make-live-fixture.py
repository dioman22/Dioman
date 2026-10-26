#!/usr/bin/env python3
"""Rebuild tests/fixtures/live/Live-plugins-1.db.

The committed database is a *sanitised* Ableton Live plugin database: same table and
columns as Live 12.x, fake plugin ids, a disabled row, a VST2 row, a row with a NULL
vendor and a unicode display name. It exists so the reader's output is asserted
byte-for-byte and so the read-only test has a real SQLite file to checksum.
"""

import os
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "fixtures", "live", "Live-plugins-1.db")


def main() -> int:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    if os.path.exists(OUT):
        os.remove(OUT)
    con = sqlite3.connect(OUT)
    cur = con.cursor()
    cur.execute(
        "CREATE TABLE modules ("
        "module_id INTEGER PRIMARY KEY, path TEXT, last_scan INTEGER)"
    )
    cur.execute(
        "CREATE TABLE plugins ("
        "plugin_id INTEGER PRIMARY KEY, module_id INTEGER, dev_identifier TEXT, "
        "name TEXT NOT NULL, vendor TEXT, version TEXT, sdk_version TEXT, "
        "flags INTEGER, scanstate INTEGER, enabled INTEGER)"
    )
    rows = [
        (1, 1, "vst3:FabFilter:Pro-Q 4", "Pro-Q 4", "FabFilter", "4.01", "VST 3.7.5", 1, 3, 1),
        (2, 2, "vst3:u-he:Diva", "Diva", "u-he", "1.4.5", "VST 3.7.0", 1, 3, 1),
        (3, 3, "vst3:Xfer:Serum", "Serum", "Xfer Records", "1.368", "VST 3.6.14", 1, 3, 0),
        (4, 4, "vst2:Old:LegacyComp", "LegacyComp", "Old Audio", "2.0", "", 0, 3, 1),
        (5, 5, "vst3:Free:Unicode", "Kick 808 \u2013 \u00dcn\u00efcode", None, "1.0", "VST 3.7.5", 1, 3, 1),
        (6, 6, None, "Mystery Plugin", "Mystery", "0.1", "", 0, 3, 1),
    ]
    cur.executemany(
        "INSERT INTO plugins (plugin_id, module_id, dev_identifier, name, vendor, version, "
        "sdk_version, flags, scanstate, enabled) VALUES (?,?,?,?,?,?,?,?,?,?)",
        rows,
    )
    cur.executemany(
        "INSERT INTO modules (module_id, path, last_scan) VALUES (?,?,?)",
        [(i, f"/Library/Audio/Plug-Ins/VST3/{name}.vst3", 1712345678)
         for i, name in [(1, "FabFilter Pro-Q 4"), (2, "Diva"), (3, "Serum"),
                         (4, "LegacyComp"), (5, "Kick 808"), (6, "Mystery Plugin")]],
    )
    con.commit()
    con.close()
    print(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())

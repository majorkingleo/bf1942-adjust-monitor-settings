"""Look for case-insensitive sibling collisions in real .rfa directory tables.

If two entries in the same directory differ only by case, their relative order in the
table reveals how the original breaks that tie. Windows cannot create such a pair on disk,
so real archives are the only place to observe it.

Usage: python probe_case_ties.py <archive.rfa> [...]
"""

import struct
import sys


def names(path):
    with open(path, "rb") as f:
        data = f.read()

    toc_offset = struct.unpack_from("<I", data, 0)[0]
    count = struct.unpack_from("<I", data, toc_offset)[0]

    out = []
    pos = toc_offset + 4
    for _ in range(count):
        name_len = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        out.append(data[pos:pos + name_len].decode("latin-1"))
        pos += name_len + 24
    return out


for path in sys.argv[1:]:
    table = names(path)
    by_dir = {}
    for index, name in enumerate(table):
        slash = name.rfind("/")
        directory = name[:slash] if slash >= 0 else ""
        leaf = name[slash + 1:] if slash >= 0 else name
        by_dir.setdefault(directory, []).append((leaf, index, name))

    collisions = 0
    for directory, members in sorted(by_dir.items()):
        lowered = {}
        for leaf, index, name in members:
            lowered.setdefault(leaf.lower(), []).append((leaf, index, name))

        for key, group in lowered.items():
            if len(group) > 1:
                collisions += 1
                print(f"{path}")
                print(f"  dir '{directory}' has {len(group)} entries equal case-insensitively:")
                for leaf, index, name in group:
                    print(f"      index {index:5}  '{name}'")
                # what the table order says about the tie-break
                leaves = [leaf for leaf, _, _ in group]
                print(f"      table order: {leaves}")
                print(f"      byte-wise would be: {sorted(leaves)}")

    print(f"{path}: {len(table)} entries, {len(by_dir)} directories, "
          f"{collisions} case-insensitive sibling collision(s)")

"""Write one entry's stored payload bytes out of an .rfa, for byte comparison.

The shipping archives keep their data blocks out of table order and the payload of a chunked
entry is preceded by a chunk descriptor table, so the payload cannot simply be cut at
dataOffset. This computes the right window:

    payload = [dataOffset + header_size, dataOffset + storedSize)

Usage: python dump_payload.py <archive.rfa> <entry name> <outfile>
"""

import struct
import sys


def main():
    path, name, outfile = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(path, "rb") as handle:
        data = handle.read()

    toc_offset = struct.unpack_from("<I", data, 0)[0]
    count = struct.unpack_from("<I", data, toc_offset)[0]

    pos = toc_offset + 4

    for _ in range(count):
        name_len = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        entry_name = data[pos : pos + name_len].decode("latin-1")
        pos += name_len
        stored, uncompressed, data_offset, reserved1, reserved2, flags = struct.unpack_from(
            "<IIIIII", data, pos
        )
        pos += 24

        if entry_name != name:
            continue

        chunk_count = struct.unpack_from("<I", data, data_offset)[0]
        header = 16 + 12 * (chunk_count - 1)
        payload_size = stored - header

        payload = data[data_offset + header : data_offset + header + payload_size]

        with open(outfile, "wb") as out:
            out.write(payload)

        print(
            f"{entry_name}\n"
            f"  chunkCount={chunk_count} uncompressed={uncompressed} "
            f"stored={stored} header={header}\n"
            f"  payload {payload_size} B -> {outfile}"
        )

        if len(payload) > 0:
            print(f"  first bytes: {payload[:24].hex(' ')}")

        return

    raise SystemExit(f"entry not found: {name}")


if __name__ == "__main__":
    main()

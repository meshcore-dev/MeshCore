#!/usr/bin/env python3
"""Canonical signature of an ESP32 partition table (from a partitions.bin).

Used to decide OTA partition compatibility: the observer firmware computes the
same signature at runtime from its *flashed* partition table (via esp_partition),
and `ota update` refuses only when the target build's signature differs from the
running device's — i.e. a real partition-table change, not a blanket flag.

The signature MUST be computed identically here and in firmware
(src/helpers/ESP32Board.cpp). Definition:

  for each partition-table entry: (type, subtype, offset, size)
  sort by offset ascending
  format each as "%x:%x:%x:%x" (lowercase hex, no 0x, no padding)
  join with ","

partitions.bin layout: 32-byte records, each starting with magic 0xAA 0x50; the
trailing MD5 record (magic 0xEB 0xEB) and 0xFF padding are ignored.

Usage:  partition_signature.py <partitions.bin>   # prints the signature
"""
import struct
import sys


def signature(bin_path: str) -> str:
    data = open(bin_path, "rb").read()
    entries = []
    for i in range(0, len(data) - 31, 32):
        rec = data[i:i + 32]
        if rec[0:2] != b"\xaa\x50":  # not a partition entry (MD5 record / padding)
            continue
        ptype, subtype = rec[2], rec[3]
        offset, size = struct.unpack("<II", rec[4:12])
        entries.append((ptype, subtype, offset, size))
    entries.sort(key=lambda e: e[2])
    return ",".join("%x:%x:%x:%x" % e for e in entries)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: partition_signature.py <partitions.bin>", file=sys.stderr)
        sys.exit(2)
    print(signature(sys.argv[1]))

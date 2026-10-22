#!/usr/bin/env python3
"""Packs loose z/x/y.png tiles into per-column pak files: maps/{z}/{x}.pak

Format 'TPK1' (all uint32 little-endian):
  magic 'TPK1' | y_min | y_max | offsets[n+1]   (n = y_max - y_min + 1)
Tile i's PNG bytes live at [offsets[i], offsets[i+1]); equal offsets = no tile.
Offsets are absolute file positions. 4.5M tiny files become ~30k paks, so a
FAT SD card copies at raw bandwidth instead of file-creation rate.
"""
import os
import struct
import sys
from concurrent.futures import ThreadPoolExecutor

SRC = sys.argv[1] if len(sys.argv) > 1 else "tiles_stage/maps"
DST = sys.argv[2] if len(sys.argv) > 2 else "tiles_stage/packs"

def pack_column(z: str, x: str) -> int:
    xdir = os.path.join(SRC, z, x)
    ys = sorted(int(n[:-4]) for n in os.listdir(xdir) if n.endswith(".png"))
    if not ys:
        return 0
    y0, y1 = ys[0], ys[-1]
    n = y1 - y0 + 1
    out = os.path.join(DST, z, f"{x}.pak")
    tmp = out + ".tmp"
    os.makedirs(os.path.dirname(out), exist_ok=True)
    header_size = 12 + 4 * (n + 1)
    offsets = [header_size]
    blobs = []
    have = set(ys)
    for y in range(y0, y1 + 1):
        if y in have:
            with open(os.path.join(xdir, f"{y}.png"), "rb") as f:
                b = f.read()
            blobs.append(b)
            offsets.append(offsets[-1] + len(b))
        else:
            blobs.append(b"")
            offsets.append(offsets[-1])
    with open(tmp, "wb") as f:
        f.write(b"TPK1" + struct.pack("<II", y0, y1))
        f.write(struct.pack(f"<{n + 1}I", *offsets))
        for b in blobs:
            if b:
                f.write(b)
    os.replace(tmp, out)
    return len(ys)

def main() -> None:
    jobs = []
    for z in sorted(os.listdir(SRC), key=lambda s: int(s) if s.isdigit() else 99):
        zdir = os.path.join(SRC, z)
        if not os.path.isdir(zdir):
            continue
        for x in os.listdir(zdir):
            if os.path.isdir(os.path.join(zdir, x)):
                # skip columns already packed with a plausible size
                out = os.path.join(DST, z, f"{x}.pak")
                if not os.path.exists(out):
                    jobs.append((z, x))
    print(f"PLAN: {len(jobs)} columns to pack", flush=True)
    done = tiles = 0
    with ThreadPoolExecutor(max_workers=8) as pool:
        for cnt in pool.map(lambda j: pack_column(*j), jobs):
            done += 1
            tiles += cnt
            if done % 2000 == 0:
                print(f"MILESTONE: {done}/{len(jobs)} columns, {tiles} tiles packed", flush=True)
    print(f"DONE: {done} columns, {tiles} tiles packed", flush=True)

if __name__ == "__main__":
    main()

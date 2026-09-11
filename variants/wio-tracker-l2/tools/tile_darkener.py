#!/usr/bin/env python3
"""Builds a dark tile set from the packed day tiles.

Luminance inversion with preserved hue: invert Y in YCbCr, gently compress
toward dark so labels come out light on a near-black ground and water/parks
keep their (darkened) colors. Reads maps/{z}/{x}.pak, writes the same format
to packs_dark/, palette-quantized so the dark set stays close to the day
set's size. Resumable: existing outputs are skipped.
"""
import io
import os
import struct
import sys
from multiprocessing import Pool

from PIL import Image

SRC = "tiles_stage/packs"
DST = "tiles_stage/packs_dark"

def darken_png(data: bytes) -> bytes:
    img = Image.open(io.BytesIO(data)).convert("RGB").convert("YCbCr")
    y, cb, cr = img.split()
    y = y.point(lambda v: 16 + (255 - v) * 220 // 255)   # invert, lift blacks
    out = Image.merge("YCbCr", (y, cb, cr)).convert("RGB").quantize(colors=192)
    buf = io.BytesIO()
    out.save(buf, format="PNG", optimize=False)
    return buf.getvalue()

def process_pak(job) -> int:
    z, name = job
    src = os.path.join(SRC, z, name)
    dst = os.path.join(DST, z, name)
    if os.path.exists(dst):
        return 0
    with open(src, "rb") as f:
        raw = f.read()
    if raw[:4] != b"TPK1":
        return 0
    y0, y1 = struct.unpack_from("<II", raw, 4)
    n = y1 - y0 + 1
    offs = struct.unpack_from(f"<{n + 1}I", raw, 12)
    blobs = []
    for i in range(n):
        b = raw[offs[i]:offs[i + 1]]
        blobs.append(darken_png(b) if b else b"")
    header_size = 12 + 4 * (n + 1)
    new_offs = [header_size]
    for b in blobs:
        new_offs.append(new_offs[-1] + len(b))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".tmp"
    with open(tmp, "wb") as f:
        f.write(b"TPK1" + struct.pack("<II", y0, y1))
        f.write(struct.pack(f"<{n + 1}I", *new_offs))
        for b in blobs:
            if b:
                f.write(b)
    os.replace(tmp, dst)
    return sum(1 for b in blobs if b)

def main() -> None:
    jobs = []
    for z in sorted(os.listdir(SRC), key=lambda s: int(s) if s.isdigit() else 99):
        zdir = os.path.join(SRC, z)
        if not os.path.isdir(zdir):
            continue
        for name in os.listdir(zdir):
            if name.endswith(".pak"):
                jobs.append((z, name))
    print(f"PLAN: {len(jobs)} paks to darken", flush=True)
    done = tiles = 0
    with Pool(processes=8) as pool:
        for cnt in pool.imap_unordered(process_pak, jobs, chunksize=4):
            done += 1
            tiles += cnt
            if done % 250 == 0:
                print(f"MILESTONE: {done}/{len(jobs)} paks, {tiles} tiles darkened", flush=True)
    print(f"DONE: {done} paks, {tiles} tiles darkened", flush=True)

if __name__ == "__main__":
    main()

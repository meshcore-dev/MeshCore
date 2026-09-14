#!/usr/bin/env python3
"""Offline map tile downloader for the Wio Tracker L2 Pro SD card.

Downloads OSM raster tiles into <dest>/maps/{z}/{x}/{y}.png - the layout
Meshtastic MUI reads and the planned MeshCore map screen will share.

Polite by design: single-threaded, ~2 req/s, resumable (skips existing
files), retries with backoff. Re-run any time to fill gaps or add areas.
"""

import math
import os
import sys
import time
import threading
import urllib.request
from concurrent.futures import ThreadPoolExecutor

DEST = sys.argv[1] if len(sys.argv) > 1 else "/Volumes/L2MAPS"
# defaults are polite for public OSM; for a local render server use e.g.
#   TILE_URL="http://localhost:8080/tile/{z}/{x}/{y}.png" TILE_WORKERS=8 TILE_DELAY=0
TILE_URL = os.environ.get("TILE_URL", "https://tile.openstreetmap.org/{z}/{x}/{y}.png")
USER_AGENT = "L2Pro-offline-map-prep/1.0 (personal one-time use, throttled)"
WORKERS = int(os.environ.get("TILE_WORKERS", "2"))
DELAY_SECS = float(os.environ.get("TILE_DELAY", "0.2"))
MILESTONE_EVERY = 2500

# (name, lon_min, lat_min, lon_max, lat_max, z_min, z_max)
AREAS = [
    ("fargo_moorhead",  -97.00, 46.70,  -96.60, 47.00, 10, 17),
    ("bismarck_mandan", -100.95, 46.70, -100.60, 46.90, 10, 17),
    ("i94_corridor",    -100.95, 46.55,  -96.60, 47.05,  8, 12),
    ("nd_region",       -104.10, 45.80,  -96.00, 49.10,  5,  9),
    # --- expansion pack ---
    ("grand_forks",     -97.15, 47.85,  -96.95, 48.00, 10, 17),
    ("i29_corridor",    -97.30, 45.93,  -96.60, 49.00,  8, 12),
    ("minot",          -101.40, 48.18, -101.20, 48.30, 10, 15),
    ("jamestown",       -98.78, 46.85,  -98.62, 46.95, 10, 15),
    ("valley_city",     -98.05, 46.88,  -97.95, 46.96, 10, 15),
    ("devils_lake",     -98.92, 48.08,  -98.80, 48.16, 10, 15),
    ("wahpeton",        -96.65, 46.23,  -96.55, 46.31, 10, 15),
    ("dickinson",      -102.85, 46.83, -102.72, 46.92, 10, 15),
    ("williston",      -103.70, 48.11, -103.55, 48.21, 10, 15),
    ("mn_lakes",        -96.00, 46.20,  -94.00, 47.10,  9, 13),
    ("msp_metro",       -93.55, 44.70,  -92.90, 45.25, 10, 16),
    ("nd_blanket",     -104.10, 45.80,  -95.20, 49.10, 10, 13),
    # widened Fargo-Moorhead metro at EVERY zoom the map UI offers
    ("fargo_metro_wide", -97.10, 46.65,  -96.50, 47.05,  5, 17),
    # statewide street level: both states finish z5-15 before any z16 starts,
    # so a full card degrades gracefully (z16 is ~75% of the tile count)
    ("nd_full",         -104.10, 45.80,  -96.50, 49.10,  5, 15),
    ("mn_full",          -97.30, 43.45,  -89.45, 49.40,  5, 15),
    ("nd_full_z16",     -104.10, 45.80,  -96.50, 49.10, 16, 16),
    ("mn_full_z16",      -97.30, 43.45,  -89.45, 49.40, 16, 16),
]

MIN_FREE_BYTES = 2 * 1024 ** 3   # stop writing when the card has < 2GB left


def tile_range(lon_min, lat_min, lon_max, lat_max, z):
    def to_tile(lon, lat):
        n = 2 ** z
        x = int((lon + 180.0) / 360.0 * n)
        lat_r = math.radians(lat)
        y = int((1.0 - math.asinh(math.tan(lat_r)) / math.pi) / 2.0 * n)
        return max(0, min(n - 1, x)), max(0, min(n - 1, y))

    x0, y1 = to_tile(lon_min, lat_min)   # note: y grows southward
    x1, y0 = to_tile(lon_max, lat_max)
    return range(x0, x1 + 1), range(y0, y1 + 1)


def iter_tiles():
    seen = set()
    for name, lon_min, lat_min, lon_max, lat_max, z_min, z_max in AREAS:
        for z in range(z_min, z_max + 1):
            xs, ys = tile_range(lon_min, lat_min, lon_max, lat_max, z)
            for x in xs:
                for y in ys:
                    key = (z, x, y)
                    if key not in seen:
                        seen.add(key)
                        yield key


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read()


counts_lock = threading.Lock()
done = skipped = failed = processed = 0
total = 0


def bump(kind):
    global done, skipped, failed, processed
    with counts_lock:
        if kind == "done": done += 1
        elif kind == "skip": skipped += 1
        else: failed += 1
        processed += 1
        if processed % MILESTONE_EVERY == 0:
            pct = processed * 100 // total
            print(f"MILESTONE: {processed}/{total} ({pct}%) done={done} skipped={skipped} failed={failed}", flush=True)


card_full = False


def card_has_room():
    global card_full
    if card_full:
        return False
    st = os.statvfs(DEST)
    if st.f_bavail * st.f_frsize < MIN_FREE_BYTES:
        card_full = True
        print("CARD FULL: below free-space floor, skipping remaining downloads", flush=True)
        return False
    return True


def handle(tile):
    z, x, y = tile
    path = os.path.join(DEST, "maps", str(z), str(x), f"{y}.png")
    if os.path.exists(path) and os.path.getsize(path) > 0:
        bump("skip")
        return
    if not card_has_room():
        bump("fail")
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for attempt in range(4):
        try:
            data = fetch(TILE_URL.format(z=z, x=x, y=y))
            with open(path, "wb") as f:
                f.write(data)
            bump("done")
            time.sleep(DELAY_SECS)
            return
        except Exception as e:
            wait = 2 ** attempt * 5
            print(f"RETRY z{z}/{x}/{y} attempt {attempt + 1}: {e} (wait {wait}s)", flush=True)
            time.sleep(wait)
    print(f"ERROR: gave up on z{z}/{x}/{y}", flush=True)
    bump("fail")


def main():
    global total
    tiles = list(iter_tiles())
    total = len(tiles)
    print(f"PLAN: {total} unique tiles -> {DEST}/maps ({WORKERS} workers)", flush=True)

    with ThreadPoolExecutor(max_workers=WORKERS) as pool:
        list(pool.map(handle, tiles))

    print(f"DONE: total={total} downloaded={done} skipped={skipped} failed={failed}", flush=True)


if __name__ == "__main__":
    main()

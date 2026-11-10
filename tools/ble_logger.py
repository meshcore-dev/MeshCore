"""BLE stability logger for a MeshCore-style Nordic UART peripheral.

Connects, sends APP_START once, then GET_BATT every INTERVAL seconds and logs
every connect / disconnect / missing reply to a CSV. Reconnects automatically.

Usage: python tools/ble_logger.py [--name MeshCore] [--minutes 30] [--interval 30]
Only one central can be connected: disconnect the phone app first.
"""
import argparse
import asyncio
import csv
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX ="6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write (central -> board)
NUS_RX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify (board -> central)

APP_START = bytes([0x01, 0x03]) + b"      " + b"blelogger"
GET_BATT = bytes([0x14])


async def find(name, timeout=15):
    def match(d, adv):
        return (d.name or adv.local_name or "").startswith(name) or NUS in [
            u.lower() for u in adv.service_uuids
        ]

    return await BleakScanner.find_device_by_filter(match, timeout=timeout)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="MeshCore")
    ap.add_argument("--minutes", type=float, default=30)
    ap.add_argument("--interval", type=float, default=30)
    ap.add_argument("--out", default=f"ble_log_{datetime.now():%Y%m%d_%H%M%S}.csv")
    args = ap.parse_args()

    end = time.monotonic() + args.minutes * 60
    stats = {"connects": 0, "disconnects": 0, "no_reply": 0, "replies": 0}

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time", "event", "detail"])

        def log(event, detail=""):
            w.writerow([datetime.now().isoformat(timespec="seconds"), event, detail])
            f.flush()
            print(f"{datetime.now():%H:%M:%S} {event} {detail}")

        while time.monotonic() < end:
            dev = await find(args.name)
            if dev is None:
                log("not_found", "board not advertising (phone connected?)")
                continue

            lost = asyncio.Event()
            replies = asyncio.Queue()

            def on_disc(_client):
                lost.set()

            try:
                async with BleakClient(dev, disconnected_callback=on_disc) as client:
                    stats["connects"] += 1
                    log("connected", dev.address)
                    await client.start_notify(NUS_RX, lambda _c, data: replies.put_nowait(bytes(data)))
                    await client.write_gatt_char(NUS_TX, APP_START, response=False)

                    while time.monotonic() < end and not lost.is_set():
                        while not replies.empty():
                            replies.get_nowait()
                        await client.write_gatt_char(NUS_TX, GET_BATT, response=False)
                        try:
                            data = await asyncio.wait_for(replies.get(), timeout=5)
                            stats["replies"] += 1
                            log("reply", f"code=0x{data[0]:02x} len={len(data)}")
                        except asyncio.TimeoutError:
                            stats["no_reply"] += 1
                            log("no_reply")
                        try:
                            await asyncio.wait_for(lost.wait(), timeout=args.interval)
                        except asyncio.TimeoutError:
                            pass
                    if lost.is_set():
                        stats["disconnects"] += 1
                        log("disconnected")
            except Exception as e:  # connect failures, pairing errors, drops mid-write
                stats["disconnects"] += 1
                log("error", f"{type(e).__name__}: {e}")
                await asyncio.sleep(3)

        log("summary", str(stats))


asyncio.run(main())

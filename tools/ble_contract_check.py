"""Check the LifeLine BLE contract against a flashed wristband.

Sends every phone -> wristband command once and checks the reply layout
(refs/adrs/2026-09-28-wristband-ble-contract-and-simulator.md in the LifeLine repo).

Usage: python tools/ble_contract_check.py
"""
import asyncio
import struct
import sys

from bleak import BleakClient, BleakScanner

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: (d.name or adv.local_name or "").startswith("LifeLine-")
        or NUS in [u.lower() for u in adv.service_uuids],
        timeout=15,
    )
    if dev is None:
        sys.exit("wristband not advertising")

    q = asyncio.Queue()
    failures = 0

    async with BleakClient(dev) as c:
        await c.start_notify(NUS_RX, lambda _c, d: q.put_nowait(bytes(d)))

        async def ask(name, frame, check):
            nonlocal failures
            await c.write_gatt_char(NUS_TX, frame, response=False)
            try:
                r = await asyncio.wait_for(q.get(), 5)
            except asyncio.TimeoutError:
                failures += 1
                print(f"FAIL {name}: no reply")
                return
            ok, detail = check(r)
            failures += 0 if ok else 1
            print(f"{'ok  ' if ok else 'FAIL'} {name}: {detail}")

        def self_info(r):
            if r[0] != 0x05 or len(r) < 58:
                return False, f"code={r[0]:#x} len={len(r)}"
            freq, bw = struct.unpack_from("<II", r, 48)
            name = r[58:].decode(errors="replace")
            return name.startswith("LifeLine-"), (
                f"id={r[4:10].hex().upper()} name={name} freq={freq} bw={bw} sf={r[56]} cr={r[57]}"
            )

        def battery(r):
            if r[0] != 0x0C or len(r) != 11:
                return False, f"code={r[0]:#x} len={len(r)}"
            mv = struct.unpack_from("<H", r, 1)[0]
            return True, f"{mv} mV"

        def is_ok(r):
            return r == b"\x00", r.hex()

        def is_err(code):
            return lambda r: (r == bytes([0x01, code]), r.hex())

        await ask("APP_START", bytes([0x01, 0, 0, 0, 0, 0, 0, 0]) + b"check", self_info)
        await ask("GET_BATT", bytes([0x14]), battery)
        await ask("SET_LOCATION", bytes([0xF0]) + struct.pack("<ii", 39_920_000, 32_850_000), is_ok)
        await ask("SET_LOCATION out of range", bytes([0xF0]) + struct.pack("<ii", 95_000_000, 0), is_err(6))
        await ask("SET_LOCATION short", bytes([0xF0, 1, 2]), is_err(6))
        await ask("SOS_ACK", bytes([0xF1]) + struct.pack("<H", 1), is_ok)
        await ask("SOS_RESULT", bytes([0xF2]) + struct.pack("<HBB", 1, 2, 3), is_ok)
        await ask("SOS_CANCEL", bytes([0xF3]) + struct.pack("<H", 1), is_ok)
        await ask("unknown command", bytes([0x42]), is_err(1))
        # the phone location should now come back in SELF_INFO
        await ask(
            "APP_START after SET_LOCATION",
            bytes([0x01, 0, 0, 0, 0, 0, 0, 0]),
            lambda r: (
                struct.unpack_from("<ii", r, 36) == (39_920_000, 32_850_000),
                f"lat/lon={struct.unpack_from('<ii', r, 36)}",
            ),
        )

    print("PASS" if failures == 0 else f"{failures} FAILED")
    sys.exit(1 if failures else 0)


asyncio.run(main())

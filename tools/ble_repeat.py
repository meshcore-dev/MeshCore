"""Turn companion client-repeat on/off over BLE (CMD_SET_RADIO_PARAMS).

  python tools/ble_repeat.py on        # freq -> 869495 kHz, repeat on; saves old params
  python tools/ble_repeat.py off       # restores the saved params, repeat off

The board must already be paired (tools/ble_pair.py). Radio params persist on
the board (savePrefs), so always run `off` afterwards.
"""
import asyncio
import json
import struct
import sys
from pathlib import Path

from bleak import BleakClient, BleakScanner

NUS_TX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
CMD_APP_START = bytes([0x01, 0x03]) + b"      " + b"repeat"
CMD_SET_RADIO_PARAMS = 11
REPEAT_FREQ_KHZ = 869495
SAVED = Path(__file__).with_name("radio_params_saved.json")


async def main(mode):
    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: (d.name or adv.local_name or "").startswith("MeshCore")
        or "6e400001-b5a3-f393-e0a9-e50e24dcca9e" in [u.lower() for u in adv.service_uuids],
        timeout=15,
    )
    if dev is None:
        sys.exit("board not advertising")

    q = asyncio.Queue()
    async with BleakClient(dev) as c:
        await c.start_notify(NUS_RX, lambda _c, d: q.put_nowait(bytes(d)))
        await c.write_gatt_char(NUS_TX, CMD_APP_START, response=False)
        info = await asyncio.wait_for(q.get(), 5)
        assert info[0] == 5, f"unexpected reply {info[0]:#x}"
        freq, bw = struct.unpack_from("<II", info, 48)
        sf, cr = info[56], info[57]
        print(f"current: freq={freq} kHz bw={bw} Hz sf={sf} cr={cr}")

        if mode == "on":
            if not SAVED.exists():  # never overwrite the original values
                SAVED.write_text(json.dumps(dict(freq=freq, bw=bw, sf=sf, cr=cr)))
            frame = bytes([CMD_SET_RADIO_PARAMS]) + struct.pack("<IIBBB", REPEAT_FREQ_KHZ, bw, sf, cr, 1)
        else:
            p = json.loads(SAVED.read_text())
            frame = bytes([CMD_SET_RADIO_PARAMS]) + struct.pack("<IIBBB", p["freq"], p["bw"], p["sf"], p["cr"], 0)

        await c.write_gatt_char(NUS_TX, frame, response=False)
        while True:
            r = await asyncio.wait_for(q.get(), 5)
            if r[0] in (0, 1):
                print("OK" if r[0] == 0 else f"ERROR code={r[1] if len(r) > 1 else '?'}")
                break


asyncio.run(main(sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] in ("on", "off") else sys.exit(__doc__)))

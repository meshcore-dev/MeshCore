"""List nearby BLE devices that advertise the Nordic UART Service (MeshCore
companion). First step of the BLE stability measurement."""
import asyncio

from bleak import BleakScanner

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    print("scanning 10 s ...")
    found = await BleakScanner.discover(timeout=10, return_adv=True)
    for address, (device, adv) in found.items():
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        mark = "  <-- NUS" if NUS in uuids else ""
        print(f"{address}  rssi={adv.rssi:>4}  name={device.name!r}{mark}")


asyncio.run(main())

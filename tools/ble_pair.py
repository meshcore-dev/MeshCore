"""Pair this PC with a MeshCore companion board using its fixed PIN.

bleak only supports Just Works pairing; the board needs a passkey
(BLE_PIN_CODE, default 123456), so this drives the WinRT pairing API directly.

Usage: python tools/ble_pair.py [--name MeshCore] [--pin 123456] [--unpair]
"""
import argparse
import asyncio

from bleak import BleakScanner
from winrt.windows.devices.bluetooth import BluetoothLEDevice
from winrt.windows.devices.enumeration import (
    DevicePairingKinds,
    DevicePairingProtectionLevel,
)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="MeshCore")
    ap.add_argument("--pin", default="123456")
    ap.add_argument("--unpair", action="store_true")
    args = ap.parse_args()

    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: (d.name or adv.local_name or "").startswith(args.name)
        or "6e400001-b5a3-f393-e0a9-e50e24dcca9e" in [u.lower() for u in adv.service_uuids],
        timeout=15,
    )
    if dev is None:
        print("board not advertising (is the phone app disconnected?)")
        return
    print("found", dev.name, dev.address)

    le = await BluetoothLEDevice.from_bluetooth_address_async(
        int(dev.address.replace(":", ""), 16)
    )
    info = le.device_information

    if args.unpair:
        res = await info.pairing.unpair_async()
        print("unpair:", res.status)
        return

    if info.pairing.is_paired:
        print("already paired")
        return

    custom = info.pairing.custom

    def on_request(sender, ev):
        print("pairing requested, kind:", ev.pairing_kind)
        if ev.pairing_kind == DevicePairingKinds.PROVIDE_PIN:
            ev.accept(args.pin)
        else:
            ev.accept()

    custom.add_pairing_requested(on_request)
    kinds = (
        DevicePairingKinds.PROVIDE_PIN
        | DevicePairingKinds.CONFIRM_ONLY
        | DevicePairingKinds.DISPLAY_PIN
        | DevicePairingKinds.CONFIRM_PIN_MATCH
    )
    res = await custom.pair_async(
        kinds, DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION
    )
    print("pair result:", res.status)


asyncio.run(main())

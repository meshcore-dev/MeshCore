#!/usr/bin/env python3

import argparse
import asyncio
import sys
import traceback
from typing import List, Optional

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
DFU_SERVICE_UUID = "00001530-1212-EFDE-1523-785FEABCD123"
CMD_START_DFU = bytes.fromhex("3f646675")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send the MeshCore companion Start DFU command over BLE."
    )
    parser.add_argument(
        "--name",
        help="Substring match against the BLE device name.",
    )
    parser.add_argument(
        "--address",
        help="Exact BLE address/identifier to connect to.",
    )
    parser.add_argument(
        "--non-interactive",
        action="store_true",
        help="Connect to the first matching device instead of prompting.",
    )
    parser.add_argument(
        "--scan-seconds",
        type=float,
        default=8.0,
        help="How long to scan before giving up (default: 8).",
    )
    parser.add_argument(
        "--wait-seconds",
        type=float,
        default=3.0,
        help="How long to wait for notifications after sending the command (default: 3).",
    )
    parser.add_argument(
        "--connect-timeout",
        type=float,
        default=15.0,
        help="BLE connection timeout in seconds (default: 15).",
    )
    parser.add_argument(
        "--post-scan-seconds",
        type=float,
        default=8.0,
        help="How long to scan for Nordic DFU devices after sending the command (default: 8).",
    )
    return parser.parse_args()


def name_matches(device_name: Optional[str], wanted: Optional[str]) -> bool:
    if not wanted:
        return True
    if not device_name:
        return False
    return wanted.lower() in device_name.lower()


async def find_device(args: argparse.Namespace):
    if args.address:
        return await BleakScanner.find_device_by_address(args.address, timeout=args.scan_seconds)

    devices = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    matches = []
    for device, adv_data in devices.values():
        uuids = [uuid.lower() for uuid in (adv_data.service_uuids or [])]
        if SERVICE_UUID.lower() in uuids and name_matches(device.name, args.name):
            matches.append(device)

    matches.sort(key=lambda device: ((device.name or "").lower(), device.address))
    return matches


def choose_device(devices: List) -> Optional[object]:
    if not devices:
        return None

    print("Matching MeshCore companion devices:")
    for idx, device in enumerate(devices, start=1):
        print(f"  {idx}. {device.name or '(unknown)'} [{device.address}]")

    while True:
        choice = input("Select device number (or press Enter to cancel): ").strip()
        if not choice:
            return None
        if choice.isdigit():
            selected = int(choice)
            if 1 <= selected <= len(devices):
                return devices[selected - 1]
        print("Invalid selection.")


def on_notify(_, data: bytearray) -> None:
    print(f"TX: {data.hex()}")


async def scan_for_dfu_devices(scan_seconds: float) -> List:
    devices = await BleakScanner.discover(timeout=scan_seconds, return_adv=True)
    matches = []
    for device, adv_data in devices.values():
        uuids = [uuid.lower() for uuid in (adv_data.service_uuids or [])]
        if DFU_SERVICE_UUID.lower() in uuids:
            matches.append(device)

    matches.sort(key=lambda device: ((device.name or "").lower(), device.address))
    return matches


async def main() -> int:
    args = parse_args()

    if args.address and args.name:
        print("Use either --address or --name, not both.", file=sys.stderr)
        return 2

    print("Scanning for MeshCore companion device...")
    found = await find_device(args)
    if args.address:
        device = found
    elif args.non_interactive:
        device = found[0] if found else None
    else:
        device = choose_device(found)

    if not device:
        print("No matching MeshCore companion device found.", file=sys.stderr)
        return 1

    print(f"Connecting to {device.name or '(unknown)'} [{device.address}]")

    try:
        async with BleakClient(device.address, timeout=args.connect_timeout) as client:
            await client.start_notify(TX_UUID, on_notify)
            print("Sending CMD_START_DFU (63, 'dfu')...")
            await client.write_gatt_char(RX_UUID, CMD_START_DFU, response=True)
            await asyncio.sleep(args.wait_seconds)
    except Exception as exc:
        print(
            f"BLE operation failed: {type(exc).__name__}: {exc!r}",
            file=sys.stderr,
        )
        traceback.print_exc()
        return 1

    print("Scanning for Nordic DFU devices...")
    dfu_devices = await scan_for_dfu_devices(args.post_scan_seconds)
    if dfu_devices:
        print("Nordic DFU service detected on:")
        for idx, dfu_device in enumerate(dfu_devices, start=1):
            print(f"  {idx}. {dfu_device.name or '(unknown)'} [{dfu_device.address}]")
    else:
        print("No Nordic DFU service devices found.")

    print("Done. If successful, the device should now leave normal companion mode.")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))

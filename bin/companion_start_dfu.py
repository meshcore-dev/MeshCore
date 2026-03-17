#!/usr/bin/env python3

import argparse
import asyncio
import sys
from typing import List, Optional

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
CMD_START_DFU = bytes.fromhex("35646675")


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
        async with BleakClient(device) as client:
            await client.start_notify(TX_UUID, on_notify)
            print("Sending CMD_START_DFU (53, 'dfu')...")
            await client.write_gatt_char(RX_UUID, CMD_START_DFU, response=True)
            await asyncio.sleep(args.wait_seconds)
    except Exception as exc:
        print(f"BLE operation failed: {exc}", file=sys.stderr)
        return 1

    print("Done. If successful, the device should now leave normal companion mode.")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))

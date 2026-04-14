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
CMD_DEVICE_QUERY = bytes.fromhex("1603")
CMD_START_DFU = bytes.fromhex("3f646675")

RESP_CODE_DEVICE_INFO = 0x0D
FAMILY_NRF52 = "nrf52"
FAMILY_ESP32 = "esp32"
FAMILY_UNKNOWN = "unknown"


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


class NotificationCollector:
    def __init__(self) -> None:
        self.queue: asyncio.Queue[bytes] = asyncio.Queue()

    def on_notify(self, _, data: bytearray) -> None:
        payload = bytes(data)
        print(f"TX: {payload.hex()}")
        self.queue.put_nowait(payload)

    async def wait_for(self, first_byte: int, timeout: float) -> Optional[bytes]:
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                return None
            try:
                payload = await asyncio.wait_for(self.queue.get(), timeout=remaining)
            except asyncio.TimeoutError:
                return None
            if payload and payload[0] == first_byte:
                return payload


def _decode_c_string(raw: bytes) -> str:
    return raw.decode("utf-8", errors="ignore").rstrip("\x00").strip()


def parse_device_info(payload: bytes) -> Optional[dict]:
    if len(payload) < 80 or payload[0] != RESP_CODE_DEVICE_INFO:
        return None

    return {
        "fw_ver": payload[1],
        "fw_build": _decode_c_string(payload[8:20]),
        "model": _decode_c_string(payload[20:60]),
        "ver": _decode_c_string(payload[60:80]),
    }


def classify_family(model: str) -> str:
    model_lower = model.lower()

    esp32_keywords = (
        "esp32",
        "heltec v2",
        "heltec v3",
        "heltec v4",
        "tracker v2",
        "station g2",
        "t-beam",
        "tbeam",
        "t-deck",
        "tdeck",
        "xiao c3",
        "xiao c6",
        "xiao s3",
        "meshadventurer",
        "unit c6l",
        "nibble",
        "thinknode m2",
        "thinknode m5",
        "thinknode m6",
        "ebyte",
        "eora-s3",
        "rak3112",
    )
    if any(keyword in model_lower for keyword in esp32_keywords):
        return FAMILY_ESP32

    nrf52_keywords = (
        "nrf",
        "rak 4631",
        "rak4631",
        "rak 3401",
        "rak3401",
        "xiao nrf52",
        "t114",
        "t190",
        "t1000",
        "wio tracker",
        "sensecap",
        "mesh pocket",
        "meshtiny",
        "wismesh",
        "ikoka",
        "thinknode m1",
        "thinknode m3",
    )
    if any(keyword in model_lower for keyword in nrf52_keywords):
        return FAMILY_NRF52

    return FAMILY_UNKNOWN


async def scan_for_dfu_devices(scan_seconds: float) -> List:
    devices = await BleakScanner.discover(timeout=scan_seconds, return_adv=True)
    matches = []
    for device, adv_data in devices.values():
        uuids = [uuid.lower() for uuid in (adv_data.service_uuids or [])]
        if DFU_SERVICE_UUID.lower() in uuids:
            matches.append(device)

    matches.sort(key=lambda device: ((device.name or "").lower(), device.address))
    return matches


async def query_device_info(client: BleakClient, collector: NotificationCollector) -> Optional[dict]:
    await client.write_gatt_char(RX_UUID, CMD_DEVICE_QUERY, response=True)
    payload = await collector.wait_for(RESP_CODE_DEVICE_INFO, timeout=3.0)
    if not payload:
        return None
    return parse_device_info(payload)


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
            collector = NotificationCollector()
            await client.start_notify(TX_UUID, collector.on_notify)
            device_info = await query_device_info(client, collector)
            family = FAMILY_UNKNOWN
            if device_info:
                model = device_info["model"] or "(unknown model)"
                version = device_info["ver"] or "(unknown version)"
                family = classify_family(device_info["model"])
                print(f"Detected model: {model}")
                print(f"Detected firmware: {version}")
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

    if family == FAMILY_NRF52:
        print("Detected nRF52-family device. Scanning for Nordic DFU devices...")
        dfu_devices = await scan_for_dfu_devices(args.post_scan_seconds)
        if dfu_devices:
            print("Nordic DFU service detected on:")
            for idx, dfu_device in enumerate(dfu_devices, start=1):
                print(f"  {idx}. {dfu_device.name or '(unknown)'} [{dfu_device.address}]")
        else:
            print("No Nordic DFU service devices found.")
    elif family == FAMILY_ESP32:
        print("Detected ESP32-family device.")
        print("Expected next step: the device should reboot into Wi-Fi OTA mode and advertise 'MeshCore-OTA'.")
        print("Connect to that Wi-Fi network, then open: http://192.168.4.1/update")
    else:
        print("Device family could not be classified from device info.")
        print("If it is an nRF52 device, look for a Nordic DFU BLE target next.")
        print("If it is an ESP32 device, look for a 'MeshCore-OTA' Wi-Fi access point and browse to http://192.168.4.1/update")

    print("Done. If successful, the device should now leave normal companion mode.")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))

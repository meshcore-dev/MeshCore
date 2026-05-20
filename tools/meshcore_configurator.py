#!/usr/bin/env python3
"""
USB serial configurator for MeshCore repeater and room-server firmware.

This avoids the browser Web Serial configurator and talks directly to the
firmware CLI at 115200 baud.

Examples:
  python3 tools/meshcore_configurator.py
  python3 tools/meshcore_configurator.py --port /dev/ttyUSB0 --command "get radio"
  python3 tools/meshcore_configurator.py --port /dev/ttyUSB0 --us-preset --reboot
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

try:
    import esptool
except ImportError:
    esptool = None


DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 1.2
DEFAULT_FLASH_BAUD = 460800
DEFAULT_FLASH_ADDRESS = "0x0"
US_CANADA_RECOMMENDED = "910.525,62.5,7,5"


@dataclass(frozen=True)
class Setting:
    key: str
    label: str
    kind: str = "text"
    choices: tuple[str, ...] = ()
    help: str = ""
    reboot: bool = False

    @property
    def get_command(self) -> str:
        return f"get {self.key}"

    def set_command(self, value: str) -> str:
        return f"set {self.key} {value}"


SETTINGS: tuple[Setting, ...] = (
    Setting("radio", "Radio params: freq,bw,sf,cr", help="Example: 910.525,62.5,7,5", reboot=True),
    Setting("freq", "Frequency MHz", help="Example: 910.525", reboot=True),
    Setting("tx", "LoRa chip TX power dBm", kind="int", help="Hammer default build flag is 22 dBm"),
    Setting("name", "Node name"),
    Setting("lat", "Latitude", kind="float"),
    Setting("lon", "Longitude", kind="float"),
    Setting("guest.password", "Room guest password"),
    Setting("owner.info", "Owner info", help="Use | where you want a newline"),
    Setting("repeat", "Repeat packets", kind="choice", choices=("on", "off")),
    Setting("af", "Airtime factor", kind="float", help="Higher means longer silent period after TX"),
    Setting("txdelay", "Flood retransmit delay factor", kind="float"),
    Setting("direct.txdelay", "Direct retransmit delay factor", kind="float"),
    Setting("rxdelay", "Receive processing delay", kind="float"),
    Setting("flood.max", "Max flood hops", kind="int", help="0-64"),
    Setting("path.hash.mode", "Path hash mode", kind="choice", choices=("0", "1", "2")),
    Setting("loop.detect", "Loop detection", kind="choice", choices=("off", "minimal", "moderate", "strict")),
    Setting("multi.acks", "Multi-acks", kind="choice", choices=("0", "1")),
    Setting("flood.advert.interval", "Flood advert interval hours", kind="int", help="0 disables, otherwise 3-168"),
    Setting("advert.interval", "Zero-hop advert interval minutes", kind="int", help="0 disables, otherwise 60-240"),
    Setting("int.thresh", "Interference threshold", kind="int"),
    Setting("agc.reset.interval", "AGC reset interval seconds", kind="int", help="Rounded down to multiple of 4"),
    Setting("allow.read.only", "Room allow read-only", kind="choice", choices=("on", "off")),
    Setting("radio.rxgain", "SX126x boosted RX gain", kind="choice", choices=("on", "off")),
    Setting("adc.multiplier", "Battery ADC multiplier", kind="float", help="May be unsupported on Hammer"),
    Setting("bridge.enabled", "Bridge enabled", kind="choice", choices=("on", "off")),
    Setting("bridge.delay", "Bridge delay ms", kind="int"),
    Setting("bridge.source", "Bridge source", kind="choice", choices=("rx", "tx")),
    Setting("bridge.channel", "ESPNow bridge channel", kind="int", help="1-14"),
    Setting("bridge.secret", "ESPNow bridge secret"),
)

INFO_COMMANDS: tuple[tuple[str, str], ...] = (
    ("ver", "Firmware version"),
    ("board", "Board name"),
    ("get role", "Firmware role"),
    ("get public.key", "Public key"),
    ("get bridge.type", "Bridge type"),
    ("powersaving", "Power saving"),
    ("clock", "Clock"),
    ("stats-core", "Core stats"),
    ("stats-radio", "Radio stats"),
    ("stats-packets", "Packet stats"),
    ("neighbors", "Neighbors"),
    ("get acl", "ACL"),
)


class MeshCoreCLI:
    def __init__(self, port: str, baud: int = DEFAULT_BAUD, timeout: float = DEFAULT_TIMEOUT):
        if serial is None:
            raise RuntimeError("pyserial is not installed. Install it with: python3 -m pip install pyserial")
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = serial.Serial(port, baudrate=baud, timeout=timeout, write_timeout=timeout)
        time.sleep(0.2)
        self.drain()

    def close(self) -> None:
        self.ser.close()

    def drain(self) -> str:
        time.sleep(0.05)
        data = self.ser.read(self.ser.in_waiting or 1)
        chunks = [data] if data else []
        while self.ser.in_waiting:
            chunks.append(self.ser.read(self.ser.in_waiting))
            time.sleep(0.02)
        return b"".join(chunks).decode(errors="replace")

    def command(self, command: str, wait: float = 0.25) -> str:
        self.drain()
        self.ser.write(command.encode("utf-8") + b"\r")
        self.ser.flush()
        time.sleep(wait)
        chunks = []
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            waiting = self.ser.in_waiting
            if waiting:
                chunks.append(self.ser.read(waiting))
                deadline = time.monotonic() + 0.15
            else:
                time.sleep(0.03)
        text = b"".join(chunks).decode(errors="replace")
        return clean_reply(command, text)

    def get(self, key: str) -> str:
        return self.command(f"get {key}")

    def set(self, key: str, value: str) -> str:
        return self.command(f"set {key} {value}")

    def password(self, value: str) -> str:
        return self.command(f"password {value}")

    def reboot(self) -> str:
        return self.command("reboot", wait=0.1)

    def apply_us_canada_recommended(self) -> str:
        return self.set("radio", US_CANADA_RECOMMENDED)


def clean_reply(command: str, text: str) -> str:
    lines = []
    for raw in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        line = raw.strip()
        if not line:
            continue
        if line == command or line.endswith(command):
            continue
        if line.startswith("-> "):
            line = line[3:].strip()
        if line.startswith("  -> "):
            line = line[5:].strip()
        lines.append(line)
    return "\n".join(lines).strip()


def require_pyserial() -> None:
    if serial is None:
        print("Missing dependency: pyserial")
        print("Install it with:")
        print("  python3 -m pip install pyserial")
        print()
        print("On Ubuntu you may also need serial permissions:")
        print("  sudo usermod -aG dialout $USER")
        print("Then log out and back in.")
        raise SystemExit(2)


def require_esptool() -> None:
    if esptool is None:
        print("Missing dependency: esptool")
        print("Install it with:")
        print("  python3 -m pip install esptool")
        raise SystemExit(2)


def available_ports() -> list[str]:
    require_pyserial()
    return [p.device for p in list_ports.comports()]


def choose_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port

    ports = available_ports()
    if not ports:
        print("No serial ports found.")
        print("Plug in the Hammer and check: ls -l /dev/ttyUSB* /dev/ttyACM*")
        print("If the port exists but this fails, add yourself to dialout:")
        print("  sudo usermod -aG dialout $USER")
        raise SystemExit(1)
    if len(ports) == 1:
        return ports[0]

    print("Serial ports:")
    for index, port in enumerate(ports, 1):
        print(f"  {index}. {port}")
    choice = input("Select port: ").strip()
    if choice.isdigit() and 1 <= int(choice) <= len(ports):
        return ports[int(choice) - 1]
    return choice


def flash_esp32_firmware(port: str, firmware: str, baud: int = DEFAULT_FLASH_BAUD,
                         address: str = DEFAULT_FLASH_ADDRESS) -> None:
    require_esptool()
    firmware_path = Path(firmware).expanduser()
    if not firmware_path.exists():
        raise FileNotFoundError(firmware)

    args = [
        "--chip", "esp32",
        "--port", port,
        "--baud", str(baud),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        address,
        str(firmware_path),
    ]

    print("Flashing ESP32 firmware...")
    print(f"Port: {port}")
    print(f"File: {firmware_path}")
    print(f"Address: {address}")
    print()
    esptool.main(args)


def print_table(rows: Iterable[tuple[str, str]]) -> None:
    rows = list(rows)
    width = max((len(left) for left, _ in rows), default=0)
    for left, right in rows:
        print(f"{left:<{width}}  {right}")


def show_overview(dev: MeshCoreCLI) -> None:
    rows = []
    for command, label in INFO_COMMANDS[:7]:
        rows.append((label + ":", dev.command(command) or "(no reply)"))
    print_table(rows)
    print()
    rows = []
    for setting in SETTINGS:
        reply = dev.command(setting.get_command)
        if reply.startswith("> "):
            reply = reply[2:]
        rows.append((setting.label + ":", reply or "(unsupported/no reply)"))
    print_table(rows)


def prompt_value(setting: Setting) -> str | None:
    if setting.choices:
        print(f"Choices: {', '.join(setting.choices)}")
    if setting.help:
        print(setting.help)
    value = input(f"New value for {setting.label}: ").strip()
    if not value:
        return None
    return value


def configure_setting(dev: MeshCoreCLI) -> None:
    for index, setting in enumerate(SETTINGS, 1):
        print(f"{index:2}. {setting.label} [{setting.key}]")
    choice = input("Setting number or key: ").strip()
    setting = None
    if choice.isdigit() and 1 <= int(choice) <= len(SETTINGS):
        setting = SETTINGS[int(choice) - 1]
    else:
        setting = next((item for item in SETTINGS if item.key == choice), None)
    if setting is None:
        print("Unknown setting.")
        return

    current = dev.command(setting.get_command)
    print(f"Current: {current or '(no reply)'}")
    value = prompt_value(setting)
    if value is None:
        return
    if setting.choices and value not in setting.choices:
        print("Not one of the listed choices.")
        return
    reply = dev.command(setting.set_command(value))
    print(reply or "(no reply)")
    if setting.reboot:
        print("This setting needs a reboot before the radio uses it.")


def raw_command(dev: MeshCoreCLI) -> None:
    print("Raw mode. Empty line exits.")
    while True:
        command = input("meshcore> ").strip()
        if not command:
            return
        print(dev.command(command) or "(no reply)")


def region_menu(dev: MeshCoreCLI) -> None:
    while True:
        print()
        print("Region menu")
        print("  1. List regions")
        print("  2. Show region")
        print("  3. Set home region")
        print("  4. Allow flood for region")
        print("  5. Deny flood for region")
        print("  6. Create region")
        print("  7. Remove region")
        print("  8. Save region changes")
        print("  9. Back")
        choice = input("> ").strip()
        if choice == "1":
            print(dev.command("region") or dev.command("region list allowed") or "(no reply)")
        elif choice == "2":
            name = input("Region name or *: ").strip()
            if name:
                print(dev.command(f"region get {name}") or "(no reply)")
        elif choice == "3":
            name = input("Home region name: ").strip()
            print(dev.command(f"region home {name}") if name else dev.command("region home"))
        elif choice == "4":
            name = input("Region name or *: ").strip()
            if name:
                print(dev.command(f"region allowf {name}") or "(no reply)")
        elif choice == "5":
            name = input("Region name or *: ").strip()
            if name:
                print(dev.command(f"region denyf {name}") or "(no reply)")
        elif choice == "6":
            name = input("New region name: ").strip()
            parent = input("Parent region, blank for default: ").strip()
            if name:
                command = f"region put {name} {parent}".strip()
                print(dev.command(command) or "(no reply)")
        elif choice == "7":
            name = input("Region name to remove: ").strip()
            if name:
                print(dev.command(f"region remove {name}") or "(no reply)")
        elif choice == "8":
            print(dev.command("region save") or "(no reply)")
        elif choice == "9":
            return


def gps_menu(dev: MeshCoreCLI) -> None:
    while True:
        print()
        print("GPS menu")
        print("  1. GPS status")
        print("  2. GPS on")
        print("  3. GPS off")
        print("  4. Set node lat/lon from current GPS fix")
        print("  5. Sync clock from GPS")
        print("  6. Show GPS advert policy")
        print("  7. Advert saved lat/lon")
        print("  8. Advert live GPS location")
        print("  9. Hide location in adverts")
        print("  10. Send advert")
        print("  11. Back")
        choice = input("> ").strip()
        if choice == "1":
            print(dev.command("gps") or "(no reply)")
        elif choice == "2":
            print(dev.command("gps on") or "(no reply)")
        elif choice == "3":
            print(dev.command("gps off") or "(no reply)")
        elif choice == "4":
            print(dev.command("gps setloc") or "(no reply)")
        elif choice == "5":
            print(dev.command("gps sync") or "(no reply)")
        elif choice == "6":
            print(dev.command("gps advert") or "(no reply)")
        elif choice == "7":
            print(dev.command("gps advert prefs") or "(no reply)")
        elif choice == "8":
            print(dev.command("gps advert share") or "(no reply)")
        elif choice == "9":
            print(dev.command("gps advert none") or "(no reply)")
        elif choice == "10":
            print(dev.command("advert") or "(no reply)")
        elif choice == "11":
            return


def firmware_update_menu(dev: MeshCoreCLI) -> None:
    print()
    print("Firmware update")
    print("Use a merged ESP32 image when flashing at 0x0.")
    print("For Hammer builds, PlatformIO creates:")
    print("  .pio/build/hammer_sx1262_repeater/firmware-merged.bin")
    print()
    firmware = input("Firmware .bin path: ").strip()
    if not firmware:
        return
    firmware = os.path.expanduser(firmware)
    address = input(f"Flash address [{DEFAULT_FLASH_ADDRESS}]: ").strip() or DEFAULT_FLASH_ADDRESS
    baud_text = input(f"Flash baud [{DEFAULT_FLASH_BAUD}]: ").strip()
    flash_baud = int(baud_text) if baud_text else DEFAULT_FLASH_BAUD

    confirm = input(f"Flash {firmware} to {dev.port} at {address}? Type YES: ").strip()
    if confirm != "YES":
        print("Canceled.")
        return

    dev.close()
    try:
        flash_esp32_firmware(dev.port, firmware, flash_baud, address)
    finally:
        try:
            dev.ser.open()
            time.sleep(0.5)
            dev.drain()
        except Exception:
            pass


def interactive(dev: MeshCoreCLI) -> None:
    actions: tuple[tuple[str, str, Callable[[MeshCoreCLI], None]], ...] = (
        ("1", "Show all known settings", show_overview),
        ("2", "Change a setting", configure_setting),
        ("3", "Apply US/Canada recommended radio preset", lambda d: print(d.apply_us_canada_recommended())),
        ("4", "Change admin password", lambda d: print(d.password(input("New admin password: ").strip()))),
        ("5", "Send advert", lambda d: print(d.command("advert"))),
        ("6", "Send zero-hop advert", lambda d: print(d.command("advert.zerohop"))),
        ("7", "Power saving on", lambda d: print(d.command("powersaving on"))),
        ("8", "Power saving off", lambda d: print(d.command("powersaving off"))),
        ("9", "GPS", gps_menu),
        ("10", "Region management", region_menu),
        ("11", "Raw command mode", raw_command),
        ("12", "Firmware update", firmware_update_menu),
        ("13", "Reboot", lambda d: print(d.reboot() or "Reboot command sent.")),
    )
    while True:
        print()
        print(f"MeshCore configurator connected to {dev.port}")
        for key, label, _ in actions:
            print(f"  {key}. {label}")
        print("  q. Quit")
        choice = input("> ").strip().lower()
        if choice in ("q", "quit", "exit"):
            return
        action = next((item for item in actions if item[0] == choice), None)
        if action is None:
            print("Unknown choice.")
            continue
        action[2](dev)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Configure MeshCore firmware over USB serial.")
    parser.add_argument("--port", help="Serial port, for example /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--command", action="append", help="Run one raw command and print its reply. Can be used more than once.")
    parser.add_argument("--set", nargs=2, metavar=("KEY", "VALUE"), action="append", help="Set a MeshCore preference.")
    parser.add_argument("--get", metavar="KEY", action="append", help="Get a MeshCore preference.")
    parser.add_argument("--us-preset", action="store_true", help=f"Apply radio preset {US_CANADA_RECOMMENDED}.")
    parser.add_argument("--reboot", action="store_true", help="Reboot after other commands.")
    parser.add_argument("--flash", metavar="BIN", help="Flash a merged ESP32 firmware image, usually firmware-merged.bin.")
    parser.add_argument("--flash-address", default=DEFAULT_FLASH_ADDRESS, help="Flash address for --flash. Use 0x0 for merged images.")
    parser.add_argument("--flash-baud", type=int, default=DEFAULT_FLASH_BAUD, help="Baud rate for --flash.")
    parser.add_argument("--list-ports", action="store_true", help="List serial ports and exit.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    require_pyserial()

    if args.list_ports:
        ports = available_ports()
        if ports:
            print("\n".join(ports))
            return 0
        print("No serial ports found.")
        return 1

    port = choose_port(args.port)
    if args.flash:
        flash_esp32_firmware(port, args.flash, args.flash_baud, args.flash_address)
        return 0

    dev = MeshCoreCLI(port, args.baud, args.timeout)
    try:
        did_one_shot = False
        for key in args.get or ():
            did_one_shot = True
            print(dev.get(key))
        for key, value in args.set or ():
            did_one_shot = True
            print(dev.set(key, value))
        if args.us_preset:
            did_one_shot = True
            print(dev.apply_us_canada_recommended())
        for command in args.command or ():
            did_one_shot = True
            print(dev.command(command))
        if args.reboot:
            did_one_shot = True
            print(dev.reboot() or "Reboot command sent.")
        if not did_one_shot:
            interactive(dev)
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

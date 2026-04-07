#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parent
OUT_DIR = ROOT / "out"
UF2CONV = ROOT / "bin" / "uf2conv" / "uf2conv.py"
PLATFORM_PATTERN = re.compile(r"(ESP32_PLATFORM|NRF52_PLATFORM|STM32_PLATFORM|RP2040_PLATFORM)")
DEBUG_DISABLE_FLAGS = (
    "-UMESH_DEBUG -UBLE_DEBUG_LOGGING -UWIFI_DEBUG_LOGGING -UBRIDGE_DEBUG "
    "-UGPS_NMEA_DEBUG -UCORE_DEBUG_LEVEL -UESPNOW_DEBUG_LOGGING "
    "-UDEBUG_RP2040_WIRE -UDEBUG_RP2040_SPI -UDEBUG_RP2040_CORE -UDEBUG_RP2040_PORT "
    "-URADIOLIB_DEBUG_SPI -UCFG_DEBUG -URADIOLIB_DEBUG_BASIC -URADIOLIB_DEBUG_PROTOCOL"
)
HELP_TEXT = """Usage:
python build.py <command> [target]

Commands:
  help|usage|-h|--help: Shows this message.
  list|-l: List firmwares available to build.
  build-firmware <target> [target...]: Build the firmware for the given build target(s).
  build-firmwares: Build all companion, repeater, and room server firmwares.
  build-matching-firmwares <build-match-spec>: Build firmwares whose target name contains the given string.
  build-companion-firmwares: Build all companion firmwares.
  build-repeater-firmwares: Build all repeater firmwares.
  build-room-server-firmwares: Build all chat room server firmwares.

Environment Variables:
  FIRMWARE_VERSION: Required. Used to generate the firmware version string.
  DISABLE_DEBUG=1: Disables all debug logging flags.
  PLATFORMIO_BUILD_FLAGS: Appended to the generated version/build flags.

Examples:
  PowerShell: $env:FIRMWARE_VERSION="v1.0.0"
  cmd.exe: set FIRMWARE_VERSION=v1.0.0
  bash/zsh: export FIRMWARE_VERSION=v1.0.0
  python build.py build-firmware RAK_4631_repeater
  python build.py build-matching-firmwares RAK_4631
  python build.py build-companion-firmwares
"""
VERSION_HINT = """FIRMWARE_VERSION must be set in environment.
Examples:
  PowerShell: $env:FIRMWARE_VERSION="v1.0.0"
  cmd.exe: set FIRMWARE_VERSION=v1.0.0
  bash/zsh: export FIRMWARE_VERSION=v1.0.0"""
GROUP_SUFFIXES = {
    "build-companion-firmwares": ("_companion_radio_usb", "_companion_radio_ble"),
    "build-repeater-firmwares": ("_repeater",),
    "build-room-server-firmwares": ("_room_server",),
}
ARTIFACTS = {
    "ESP32_PLATFORM": (("firmware.bin", ".bin"), ("firmware-merged.bin", "-merged.bin")),
    "STM32_PLATFORM": (("firmware.bin", ".bin"), ("firmware.hex", ".hex")),
    "RP2040_PLATFORM": (("firmware.bin", ".bin"), ("firmware.uf2", ".uf2")),
    "NRF52_PLATFORM": (("firmware.uf2", ".uf2"), ("firmware.zip", ".zip")),
}


@dataclass(frozen=True)
class BuildInfo:
    version: str
    commit: str
    build_date: str
    env: dict[str, str]

    @property
    def version_string(self) -> str:
        return f"{self.version}-{self.commit}"


def run(args: list[str], *, env: dict[str, str] | None = None, capture: bool = False) -> str:
    result = subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        text=True,
        check=True,
        capture_output=capture,
    )
    return result.stdout if capture else ""


def require_version() -> str:
    version = os.environ.get("FIRMWARE_VERSION")
    if not version:
        print(VERSION_HINT, file=sys.stderr)
        raise SystemExit(1)
    version = version.strip()
    if len(version) >= 2 and version[0] == version[-1] and version[0] in {'"', "'"}:
        version = version[1:-1]
    return version


def make_build_info() -> BuildInfo:
    version = require_version()
    commit = run(["git", "rev-parse", "--short", "HEAD"], capture=True).strip()
    build_date = datetime.now().strftime("%d-%b-%Y")
    flags = [
        os.environ.get("PLATFORMIO_BUILD_FLAGS", "").strip(),
        f'-DFIRMWARE_BUILD_DATE=\\"{build_date}\\"',
        f'-DFIRMWARE_VERSION=\\"{version}-{commit}\\"',
    ]
    if os.environ.get("DISABLE_DEBUG") == "1":
        flags.append(DEBUG_DISABLE_FLAGS)
    env = os.environ.copy()
    env["PLATFORMIO_BUILD_FLAGS"] = " ".join(flag for flag in flags if flag)
    return BuildInfo(version=version, commit=commit, build_date=build_date, env=env)


def load_config() -> tuple[list[str], dict[str, str | None]]:
    config = json.loads(run(["pio", "project", "config", "--json-output"], capture=True))
    envs: list[str] = []
    platforms: dict[str, str | None] = {}
    for section, options in config:
        if not isinstance(section, str) or not section.startswith("env:"):
            continue
        env_name = section.removeprefix("env:")
        envs.append(env_name)
        platforms[env_name] = next(
            (
                match.group(1)
                for key, value in options
                if key == "build_flags" and isinstance(value, list)
                for flag in value
                if isinstance(flag, str) and (match := PLATFORM_PATTERN.search(flag))
            ),
            None,
        )
    return envs, platforms


def select_envs(envs: list[str], *, contains: str | None = None, suffixes: tuple[str, ...] = ()) -> list[str]:
    selected = envs
    if contains is not None:
        needle = contains.lower()
        selected = [env for env in selected if needle in env.lower()]
    if suffixes:
        lowered = tuple(suffix.lower() for suffix in suffixes)
        selected = [env for env in selected if env.lower().endswith(lowered)]
    return selected


def reset_out_dir() -> None:
    shutil.rmtree(OUT_DIR, ignore_errors=True)
    OUT_DIR.mkdir(parents=True, exist_ok=True)


def copy_artifacts(build_dir: Path, firmware_name: str, platform: str | None) -> None:
    for source_name, output_suffix in ARTIFACTS.get(platform, ()):
        source = build_dir / source_name
        if source.exists():
            shutil.copy2(source, OUT_DIR / f"{firmware_name}{output_suffix}")


def maybe_create_nrf52_uf2(build_dir: Path, env: dict[str, str], platform: str | None) -> None:
    if platform != "NRF52_PLATFORM":
        return
    run(
        [
            sys.executable,
            str(UF2CONV),
            str(build_dir / "firmware.hex"),
            "-c",
            "-o",
            str(build_dir / "firmware.uf2"),
            "-f",
            "0xADA52840",
        ],
        env=env,
    )


def maybe_merge_esp32(env_name: str, env: dict[str, str], platform: str | None) -> None:
    if platform == "ESP32_PLATFORM":
        run(["pio", "run", "-t", "mergebin", "-e", env_name], env=env)


def build_target(env_name: str, platform: str | None, build: BuildInfo) -> None:
    build_dir = ROOT / ".pio" / "build" / env_name
    firmware_name = f"{env_name}-{build.version_string}"
    run(["pio", "run", "-e", env_name], env=build.env)
    maybe_merge_esp32(env_name, build.env, platform)
    maybe_create_nrf52_uf2(build_dir, build.env, platform)
    copy_artifacts(build_dir, firmware_name, platform)


def print_usage(exit_code: int, *, stream: object = sys.stdout) -> int:
    print(HELP_TEXT, file=stream)
    return exit_code


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        return print_usage(0)

    command, args = argv[1], argv[2:]
    if command in {"help", "usage", "-h", "--help"}:
        return print_usage(1)

    envs, platforms = load_config()

    if command in {"list", "-l"}:
        print("\n".join(envs))
        return 0

    if command == "build-firmware":
        if not args:
            print("usage: python build.py build-firmware <target> [target...]", file=sys.stderr)
            return 1
        targets = args
    elif command == "build-matching-firmwares":
        if len(args) != 1:
            print("usage: python build.py build-matching-firmwares <build-match-spec>", file=sys.stderr)
            return 1
        targets = select_envs(envs, contains=args[0])
    elif command == "build-firmwares":
        targets = []
        for suffixes in GROUP_SUFFIXES.values():
            targets.extend(select_envs(envs, suffixes=suffixes))
    elif command in GROUP_SUFFIXES:
        targets = select_envs(envs, suffixes=GROUP_SUFFIXES[command])
    else:
        return print_usage(1, stream=sys.stderr)

    reset_out_dir()
    build = make_build_info()
    for env_name in targets:
        build_target(env_name, platforms.get(env_name), build)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

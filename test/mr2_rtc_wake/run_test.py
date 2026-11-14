#!/usr/bin/env python3
"""Compile the complete production RTC wake helper with a host Wire model."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++"))
    args = parser.parse_args()
    if not args.cxx:
        parser.error("g++ not found; supply --cxx /path/to/g++")

    with tempfile.TemporaryDirectory(prefix="mr2-rtc-wake-") as temporary:
        directory = Path(temporary)
        helper = directory / "helpers"
        helper.mkdir()
        source = ROOT / "variants/inhero_mr2"
        # Copy both files verbatim. Only hardware/framework dependencies are
        # replaced; no production function extraction or rewriting is needed.
        for name in ("Rv3028Wake.cpp", "Rv3028Wake.h"):
            shutil.copyfile(source / "helpers" / name, helper / name)
        board_header = (source / "InheroMr2Board.h").read_text(encoding="utf-8")
        constants = re.findall(
            r"^#define\s+(?:RTC_I2C_ADDR|RV3028_REG_\w+)\s+[^\n]+",
            board_header, re.MULTILINE)
        if len(constants) != 6:
            raise ValueError("Unexpected RTC register declarations")
        (directory / "InheroMr2Board.h").write_text(
            "\n".join(constants) + "\n", encoding="utf-8")
        executable = directory / "rtc_wake_test.exe"
        subprocess.run([
            args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(HERE / "stubs"), "-I", str(directory),
            "-x", "c++", str(HERE / "fixture.inc"),
            str(helper / "Rv3028Wake.cpp"), "-o", str(executable),
        ], check=True)
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    raise SystemExit(main())

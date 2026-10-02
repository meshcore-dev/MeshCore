#!/usr/bin/env python3
"""Exercise the production MR2 UVLO control flow with host hardware fakes."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for token in re.finditer(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
        text[opening:], re.DOTALL,
    ):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:opening + token.end()]
    raise ValueError(f"Unbalanced function: {signature}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++"))
    args = parser.parse_args()
    if not args.cxx:
        parser.error("g++ not found; supply --cxx /path/to/g++")
    board = (ROOT / "variants/inhero_mr2/InheroMr2Board.cpp").read_text(encoding="utf-8")
    config = (ROOT / "variants/inhero_mr2/BoardConfigContainer.cpp").read_text(encoding="utf-8")
    production = "\n\n".join([
        *(function(board, signature) for signature in (
            "static bool restoreConfiguredChargeEnable()",
            "void InheroMr2Board::begin()",
            "void InheroMr2Board::loop()",
            "void InheroMr2Board::initiateShutdown(uint8_t reason)",
            "bool InheroMr2Board::configureRTCWake(uint32_t minutes)",
        )),
        function(config, "void BoardConfigContainer::tickPeriodic()"),
    ])
    with tempfile.TemporaryDirectory(prefix="mr2-uvlo-flow-") as temporary:
        directory = Path(temporary)
        (directory / "production.inc").write_text(production, encoding="utf-8")
        executable = directory / "flow.exe"
        subprocess.run([
            args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-x", "c++", str(HERE / "fixture.inc"), "-I", str(directory),
            "-o", str(executable),
        ], check=True)
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    raise SystemExit(main())

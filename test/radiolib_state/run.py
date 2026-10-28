"""Compile the real RadioLib wrapper against a deterministic radio backend."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required")
with tempfile.TemporaryDirectory(prefix="radiolib-state-") as output:
    executable = Path(output)/"tx-guards"
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
               "-Itest/radiolib_state/mocks", "-Isrc",
               "test/radiolib_state/test_tx_guards.cpp",
               "src/helpers/radiolib/RadioLibWrappers.cpp", "-o", str(executable)]
    if args.sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)], check=True)

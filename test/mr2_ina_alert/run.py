"""Host regression for the production INA228 alert configuration function.

Run with Python 3; requires g++ on PATH or CXX pointing to a compiler.
Only register I/O is mocked. The final comparison check models datasheet
Table 7-16, not a physical ADC or a board timing measurement.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "variants/inhero_mr2/lib/Ina228Driver.cpp"
HEADER = DRIVER.with_suffix(".h")
source = DRIVER.read_text(encoding="utf-8")
start = source.index("void Ina228Driver::enableAlert(")
brace = source.index("{", start)
depth = 0
for end in range(brace, len(source)):
    depth += (source[end] == "{") - (source[end] == "}")
    if depth == 0:
        method = source[start:end + 1]
        break
else:
    raise ValueError("Unbalanced enableAlert function")
defines = "\n".join(re.findall(r"^#define INA228_(?:DIAG_ALRT_|REG_).*$", HEADER.read_text(encoding="utf-8"), re.M))
fixture = Path(__file__).with_name("fixture.inc").read_text(encoding="utf-8")
code = fixture.replace("// PRODUCTION_DEFINES", defines).replace("// PRODUCTION_METHOD", method)
compiler = os.environ.get("CXX") or shutil.which("g++")
if not compiler and Path("C:/Tools/mingw64/bin/g++.exe").is_file():
    compiler = "C:/Tools/mingw64/bin/g++.exe"
if not compiler:
    raise SystemExit("Set CXX to a C++17 compiler or add g++ to PATH")
with tempfile.TemporaryDirectory(prefix="mr2-ina-alert-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / ("test.exe" if os.name == "nt" else "test")
    cpp.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

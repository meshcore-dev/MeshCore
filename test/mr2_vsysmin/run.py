#!/usr/bin/env python3
"""Compile production MR2 charger configuration against a host BQ state model.

No PlatformIO/Arduino dependencies. Generated files live in a temporary directory;
the .inc fixture is deliberately excluded from PlatformIO's normal C++ test scan.
"""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def match_one(pattern, text):
    matches = re.findall(pattern, text, re.DOTALL)
    if len(matches) != 1:
        raise ValueError(f"Expected one production declaration: {pattern}")
    return matches[0]


def function_body(text, signature):
    """Extract a complete function, ignoring braces in comments and literals."""
    start = text.index(signature)
    opening = text.index("{", start)
    # Tokenization makes the bracket counter insensitive to comment/string edits.
    tokens = re.finditer(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
        text[opening:], re.DOTALL,
    )
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:opening + token.end()]
    raise ValueError(f"Unbalanced production function: {signature}")


def extract_production():
    header = (ROOT / "variants/inhero_mr2/BoardConfigContainer.h").read_text(encoding="utf-8")
    source = (ROOT / "variants/inhero_mr2/BoardConfigContainer.cpp").read_text(encoding="utf-8")
    declarations = [
        match_one(r"enum BatteryType\s*:\s*uint8_t\s*\{[^}]*\};", header),
        match_one(r"// Battery type properties\s*(typedef struct\s*\{.*?\}\s*BatteryProperties;)", header),
        match_one(r"static inline constexpr BatteryProperties battery_properties\[\]\s*=\s*\{.*?\n\s*\};", header),
        match_one(r"static constexpr float BQ_MIN_SYSTEM_V\s*=\s*[^;]+;", header),
        match_one(r"static constexpr float IINDPM_MAX_A\s*=\s*[^;]+;", header),
    ]
    functions = [function_body(source, signature) for signature in (
        "bool BoardConfigContainer::configureBaseBQ()",
        "bool BoardConfigContainer::configureChemistry(BatteryType type)",
        "const BoardConfigContainer::BatteryProperties* BoardConfigContainer::getBatteryProperties(BatteryType type)",
    )]
    return "\n".join(declarations), "\n\n".join(functions)


def compile_and_run(compiler, directory, declarations, functions, name):
    (directory / "production_declarations.inc").write_text(declarations, encoding="utf-8")
    (directory / "production_functions.inc").write_text(functions, encoding="utf-8")
    executable = directory / (name + ".exe")
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-x", "c++", str(HERE / "fixture.inc"), "-I", str(directory), "-o", str(executable),
    ], check=True)
    return subprocess.run([str(executable)], capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++"))
    parser.add_argument("--check-mutations", action="store_true",
                        help="Also prove tests reject three original/regression failure modes")
    args = parser.parse_args()
    if not args.cxx:
        parser.error("g++ not found; supply --cxx /path/to/g++")
    declarations, functions = extract_production()
    with tempfile.TemporaryDirectory(prefix="mr2-vsysmin-") as temporary:
        directory = Path(temporary)
        result = compile_and_run(args.cxx, directory, declarations, functions, "regression")
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode:
            return result.returncode
        if args.check_mutations:
            mutations = (
                ("old-2.75V", declarations.replace("BQ_MIN_SYSTEM_V = 2.50f", "BQ_MIN_SYSTEM_V = 2.75f"), functions),
                ("missing-post-CELL-restore", declarations,
                 functions.replace("bq.setMinSystemV(BQ_MIN_SYSTEM_V);\n  bq.setChargeLimitA",
                                   "bq.setChargeLimitA")),
                ("missing-final-MPPT-enable", declarations,
                 functions.replace("bq.setMPPTenable(getMPPTEnabled());", "")),
            )
            for name, mutant_declarations, mutant_functions in mutations:
                if (mutant_declarations, mutant_functions) == (declarations, functions):
                    raise ValueError(f"Mutation {name} no longer changes production code")
                mutant = compile_and_run(args.cxx, directory, mutant_declarations, mutant_functions, name)
                if mutant.returncode == 0:
                    raise RuntimeError(f"Regression tests failed to detect {name}")
                print(f"PASS mutation rejected: {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Require ArduinoJson 7.4.3 in checked-in PlatformIO configurations.

The checker scans the root ``platformio.ini`` plus every
``variants/**/platformio.ini``. It checks every explicit ArduinoJson
declaration and, without a fragile fixed count, requires one in the native
test environment and each MQTT bridge environment.

Run ``python3 scripts/check_arduinojson_pin.py`` for the repository and add
``--self-test`` to exercise accepted, unpinned, wrong, and missing pins in a
temporary fixture.
"""

import argparse
from pathlib import Path
import re
import sys
from tempfile import TemporaryDirectory


VERSION = "7.4.3"
PACKAGE = "bblanchon/ArduinoJson"
PACKAGE_PATTERN = re.compile(r"\bbblanchon\s*/\s*ArduinoJson\b", re.IGNORECASE)
PIN_PATTERN = re.compile(r"@\s*7\.4\.3\Z")
SECTION_PATTERN = re.compile(r"^\s*\[([^]]+)\]\s*$")


def active(line):
    """Remove PlatformIO whole-line and inline comments."""

    if line.lstrip().startswith((";", "#")):
        return ""
    comments = [index for index in (line.find(";"), line.find("#")) if index >= 0]
    return line[: min(comments)] if comments else line


def configuration_paths(root):
    root_config = root / "platformio.ini"
    if not root_config.is_file():
        raise FileNotFoundError(f"root PlatformIO configuration is missing: {root_config}")
    variants = root / "variants"
    return [root_config] + sorted(variants.rglob("platformio.ini") if variants.is_dir() else [])


def sections(lines):
    """Yield ``(name, header_line, lines)`` while preserving source locations."""

    name, header_line, content = "<global>", 1, []
    for number, line in enumerate(lines, 1):
        match = SECTION_PATTERN.fullmatch(active(line).strip())
        if match:
            yield name, header_line, content
            name, header_line, content = match.group(1), number, []
        else:
            content.append((number, line))
    yield name, header_line, content


def expects_pin(config, root, section, lines):
    if config == root / "platformio.ini":
        return section == "env:native"
    if not section.startswith("env:"):
        return False
    text = "\n".join(active(line) for _, line in lines)
    return "WITH_MQTT_BRIDGE" in text or "elims/PsychicMqttClient" in text


def check(root):
    """Return (configs, declarations, required environments, error messages)."""

    root = root.resolve()
    configs, declarations, required, errors = configuration_paths(root), 0, 0, []
    for config in configs:
        relative = config.relative_to(root).as_posix()
        for section, header_line, lines in sections(config.read_text(encoding="utf-8").splitlines()):
            found = False
            for number, raw_line in lines:
                declaration = active(raw_line)
                match = PACKAGE_PATTERN.search(declaration)
                if not match:
                    continue
                found, declarations = True, declarations + 1
                version = declaration[match.end() :].strip()
                if not version:
                    errors.append(f"{relative}:{number}: {PACKAGE} is unpinned; use '@ {VERSION}'")
                elif not PIN_PATTERN.fullmatch(version):
                    errors.append(
                        f"{relative}:{number}: {PACKAGE} must be pinned exactly to {VERSION}; "
                        f"found '{version}'"
                    )
            if expects_pin(config, root, section, lines):
                required += 1
                if not found:
                    errors.append(
                        f"{relative}:{header_line}: missing required {PACKAGE} @ {VERSION} "
                        f"in [{section}]"
                    )
    return configs, declarations, required, errors


def self_test():
    """Exercise discovery and the valid, unpinned, wrong, and missing cases."""

    with TemporaryDirectory() as directory:
        root = Path(directory)
        root_config = root / "platformio.ini"
        variant = root / "variants" / "nested" / "mqtt" / "platformio.ini"
        variant.parent.mkdir(parents=True)
        root_config.write_text("[env:native]\nbblanchon/ArduinoJson\n", encoding="utf-8")
        variant.write_text(
            "[env:wrong]\n-D WITH_MQTT_BRIDGE=1\nbblanchon/ArduinoJson @ ^7.4.3\n"
            "[env:missing]\nelims/PsychicMqttClient@^0.2.4\n",
            encoding="utf-8",
        )
        configs, _, required, errors = check(root)
        joined = "\n".join(errors)
        if len(configs) != 2 or required != 3 or len(errors) != 3 or not all(
            phrase in joined for phrase in ("unpinned", "must be pinned exactly", "missing required")
        ):
            print("ArduinoJson pin checker self-test failed: invalid cases were not reported.", file=sys.stderr)
            return 1

        root_config.write_text("[env:native]\nbblanchon/ArduinoJson @ 7.4.3\n", encoding="utf-8")
        variant.write_text(
            "[env:one]\n-D WITH_MQTT_BRIDGE=1\nbblanchon/ArduinoJson @ 7.4.3\n"
            "[env:two]\nelims/PsychicMqttClient@^0.2.4\nbblanchon/ArduinoJson@7.4.3\n",
            encoding="utf-8",
        )
        if check(root)[3]:
            print("ArduinoJson pin checker self-test failed: valid pins were rejected.", file=sys.stderr)
            return 1

    print("ArduinoJson pin checker self-test passed.")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()

    try:
        configs, declarations, required, errors = check(args.project_root)
    except FileNotFoundError as error:
        print(f"ArduinoJson pin check could not run: {error}", file=sys.stderr)
        return 2
    if errors:
        print("ArduinoJson pin check failed:", *[f"  {error}" for error in errors], sep="\n", file=sys.stderr)
        return 1

    print(
        f"ArduinoJson pin check passed: {declarations} declaration(s) across {len(configs)} "
        f"root/variant configuration file(s); {required} required environment section(s) covered."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

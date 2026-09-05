#!/usr/bin/env python3
"""Compare MQTT built-in preset *names* between two MQTTPresets.h files.

Only the first string field of each ``MQTT_PRESETS`` entry is compared (set
equality, case-sensitive). URL, auth, CA, keepalive, and credentials are
ignored so channel branches may diverge on config details without failing CI.

Usage::

    python3 scripts/check_mqtt_preset_parity.py FILE_A FILE_B \\
        --label-a observer-firmware --label-b observer-firmware-dev

    python3 scripts/check_mqtt_preset_parity.py --self-test
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


COUNT_PATTERN = re.compile(
    r"static\s+const\s+int\s+MQTT_PRESET_COUNT\s*=\s*(\d+)\s*;"
)
# Start of the preset table; allow optional whitespace/newlines.
TABLE_START_PATTERN = re.compile(
    r"static\s+const\s+MQTTPresetDef\s+MQTT_PRESETS\s*\[\s*[^\]]*\]\s*=\s*\{",
    re.MULTILINE,
)
# First quoted string after an opening brace at entry start (after optional WS).
ENTRY_NAME_PATTERN = re.compile(r'\{\s*"([^"]+)"\s*,')


def _strip_line_comment(line: str) -> str:
    """Remove ``//`` comments; do not treat ``://`` in URLs as comments."""

    in_string = False
    i = 0
    while i < len(line):
        ch = line[i]
        if ch == '"' and (i == 0 or line[i - 1] != "\\"):
            in_string = not in_string
        elif not in_string and ch == "/" and i + 1 < len(line) and line[i + 1] == "/":
            # Prefer not to cut URL schemes: require that the previous char is not ':'.
            if i == 0 or line[i - 1] != ":":
                return line[:i]
        i += 1
    return line


def extract_preset_block(text: str) -> str:
    """Return the interior of the ``MQTT_PRESETS[...] = { ... };`` initializer."""

    match = TABLE_START_PATTERN.search(text)
    if not match:
        raise ValueError("MQTT_PRESETS table not found")

    depth = 1
    i = match.end()
    in_string = False
    while i < len(text):
        ch = text[i]
        if ch == '"' and (i == 0 or text[i - 1] != "\\"):
            in_string = not in_string
        elif not in_string:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[match.end() : i]
        i += 1
    raise ValueError("MQTT_PRESETS table is not closed")


def parse_presets(text: str, *, source: str = "<input>") -> tuple[list[str], int | None]:
    """Return (names in file order, MQTT_PRESET_COUNT or None if absent)."""

    count_match = COUNT_PATTERN.search(text)
    declared_count = int(count_match.group(1)) if count_match else None

    block = extract_preset_block(text)
    names: list[str] = []
    for raw_line in block.splitlines():
        line = _strip_line_comment(raw_line).strip()
        if not line:
            continue
        entry = ENTRY_NAME_PATTERN.match(line)
        if entry:
            names.append(entry.group(1))

    if not names:
        raise ValueError(f"{source}: no preset entries found in MQTT_PRESETS")

    seen: set[str] = set()
    dupes: list[str] = []
    for name in names:
        if name in seen:
            dupes.append(name)
        seen.add(name)
    if dupes:
        raise ValueError(f"{source}: duplicate preset name(s): {', '.join(sorted(set(dupes)))}")

    if declared_count is not None and declared_count != len(names):
        raise ValueError(
            f"{source}: MQTT_PRESET_COUNT is {declared_count} but found {len(names)} preset entries"
        )

    return names, declared_count


def format_missing(label: str, names: set[str]) -> str:
    if not names:
        return f"Missing from {label}: (none)"
    return f"Missing from {label}: {', '.join(sorted(names))}"


def compare(
    names_a: set[str],
    names_b: set[str],
    *,
    label_a: str,
    label_b: str,
) -> list[str]:
    """Return human-readable error lines if sets differ; empty list if equal."""

    only_a = names_a - names_b
    only_b = names_b - names_a
    if not only_a and not only_b:
        return []
    return [
        format_missing(label_b, only_a),  # in A but not B → missing from B
        format_missing(label_a, only_b),  # in B but not A → missing from A
    ]


def load_names(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    names, _ = parse_presets(text, source=str(path))
    return set(names)


def self_test() -> int:
    """Exercise equal-with-different-URLs, missing-name, and duplicate cases."""

    header = """
static const int MQTT_PRESET_COUNT = {count};
static const MQTTPresetDef MQTT_PRESETS[MQTT_PRESET_COUNT] = {{
{body}
}};
"""

    with TemporaryDirectory() as directory:
        root = Path(directory)
        equal_a = root / "a.h"
        equal_b = root / "b.h"
        missing = root / "missing.h"
        dupes = root / "dupes.h"

        equal_a.write_text(
            header.format(
                count=2,
                body='    { "alpha", "wss://a.example/mqtt", nullptr },\n'
                '    { "beta",  "wss://b.example/mqtt", nullptr },',
            ),
            encoding="utf-8",
        )
        # Same names, different URLs/config — must pass.
        equal_b.write_text(
            header.format(
                count=2,
                body='    { "beta",  "mqtt://other:1883", nullptr },\n'
                '    { "alpha", "wss://changed.example/mqtt", nullptr },',
            ),
            encoding="utf-8",
        )
        if compare(load_names(equal_a), load_names(equal_b), label_a="a", label_b="b"):
            print("self-test failed: equal name sets with different URLs were rejected.", file=sys.stderr)
            return 1

        missing.write_text(
            header.format(
                count=1,
                body='    { "alpha", "wss://a.example/mqtt", nullptr },',
            ),
            encoding="utf-8",
        )
        errs = compare(load_names(equal_a), load_names(missing), label_a="a", label_b="missing")
        if len(errs) != 2 or "beta" not in errs[0] or "(none)" not in errs[1]:
            print(f"self-test failed: missing-name case unexpected: {errs!r}", file=sys.stderr)
            return 1

        try:
            parse_presets(
                header.format(
                    count=2,
                    body='    { "alpha", "wss://a.example/mqtt", nullptr },\n'
                    '    { "alpha", "wss://b.example/mqtt", nullptr },',
                ),
                source="dupes",
            )
            print("self-test failed: duplicate names were accepted.", file=sys.stderr)
            return 1
        except ValueError as error:
            if "duplicate" not in str(error):
                print(f"self-test failed: unexpected duplicate error: {error}", file=sys.stderr)
                return 1

        try:
            parse_presets(
                header.format(
                    count=99,
                    body='    { "alpha", "wss://a.example/mqtt", nullptr },',
                ),
                source="count-mismatch",
            )
            print("self-test failed: count mismatch was accepted.", file=sys.stderr)
            return 1
        except ValueError as error:
            if "MQTT_PRESET_COUNT" not in str(error):
                print(f"self-test failed: unexpected count error: {error}", file=sys.stderr)
                return 1

        # URL with :// must not be treated as a line comment.
        url_comment = root / "url.h"
        url_comment.write_text(
            header.format(
                count=1,
                body='    { "alpha", "wss://mqtt.example:443/mqtt", nullptr }, // note',
            ),
            encoding="utf-8",
        )
        if load_names(url_comment) != {"alpha"}:
            print("self-test failed: URL scheme was treated as a comment.", file=sys.stderr)
            return 1

        # Silence unused path in fixture layout.
        dupes.write_text("// unused\n", encoding="utf-8")

    print("MQTT preset parity checker self-test passed.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file_a", type=Path, nargs="?", help="First MQTTPresets.h (e.g. production)")
    parser.add_argument("file_b", type=Path, nargs="?", help="Second MQTTPresets.h (e.g. dev)")
    parser.add_argument("--label-a", default="file_a", help="Label for file_a in reports")
    parser.add_argument("--label-b", default="file_b", help="Label for file_b in reports")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    if args.file_a is None or args.file_b is None:
        parser.error("FILE_A and FILE_B are required unless --self-test is set")

    try:
        names_a = load_names(args.file_a)
        names_b = load_names(args.file_b)
    except (OSError, ValueError) as error:
        print(f"MQTT preset parity check could not run: {error}", file=sys.stderr)
        return 2

    errors = compare(names_a, names_b, label_a=args.label_a, label_b=args.label_b)
    if errors:
        print("MQTT preset name parity check failed:", *errors, sep="\n  ", file=sys.stderr)
        return 1

    print(
        f"MQTT preset name parity check passed: {len(names_a)} preset name(s) "
        f"match between {args.label_a} and {args.label_b}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

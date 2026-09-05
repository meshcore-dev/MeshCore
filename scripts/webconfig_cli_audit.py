#!/usr/bin/env python3
"""Check the portal terminal's command table against the mock backend.

Autocomplete in webui/index.html carries its own list of commands. Nothing ties
that list to what a node actually answers, so it can quietly drift into offering
commands that do not exist — or, more often here, the mock can lag the table and
make a perfectly real command look broken.

This drives every command the table offers through /api/cli and reports the ones
that come back an error, so the two stay honest about each other.

    python3 scripts/webconfig_mock_server.py --port 8137 &
    python3 scripts/webconfig_cli_audit.py

Exits non-zero if anything fails that is not in EXPECTED_FAILURES. Stdlib only.
"""

import json
import os
import re
import secrets
import sys
import time
import urllib.error
import urllib.request

BASE = os.environ.get("WEBCONFIG_MOCK", "http://localhost:8137")
HERE = os.path.dirname(os.path.abspath(__file__))
INDEX_HTML = os.path.join(HERE, "..", "webui", "index.html")

# Errors that are the correct answer, not a gap.
EXPECTED_FAILURES = {
    # Runtime-gated on the real device by Board::canControlLoRaFemLna(); the
    # command exists in every build and the board answers for itself. The mock
    # board is a Heltec V3, which has no front-end module.
    "get radio.fem.rxgain": "unsupported",
    "set radio.fem.rxgain on": "unsupported",
    # Guarded by the firmware the same way when no alert PSK is configured.
    "alert test": "not configured",
}

# Commands that change the node out from under the audit.
SKIP = {"reboot", "clkreboot", "poweroff", "shutdown", "erase", "start ota",
        "stop webconfig", "ota update", "start webconfig", "start webconfig ap"}


def table():
    """The commands autocomplete offers, read straight out of the page."""
    html = open(INDEX_HTML, encoding="utf-8").read()

    def section(start, end):
        return html[html.index(start):html.index(end)]

    verbs = re.findall(r'\["([^"]+)","', section("var CLI_VERBS=", "var CLI_KEYS="))
    keys = re.findall(r'\["([^"]+)","(?:[^"\\]|\\.)*",(\d)',
                      section("var CLI_KEYS=", "var CLI_SLOT="))
    fields = re.findall(r'\["(\w+)","', section("var CLI_SLOT=", "var CLI_TYPES="))

    gets = ["get " + k for k, mode in keys if mode != "2"]
    gets += ["get mqtt%d.%s" % (n, f) for n in (1, 3) for f in fields]
    # Verbs taking an argument need a value the node will accept; those are
    # covered by the round-trip probes below rather than guessed at here.
    plain = [v for v in verbs if not v.endswith(" ") and v not in SKIP]
    return gets + plain


# Where top-level commands are implemented. MyMesh handles a few before
# delegating to CommonCLI, which is exactly how discover.* stayed missing from
# the table for so long: grepping CommonCLI alone does not see them.
COMMAND_SOURCES = [
    "src/helpers/CommonCLI.cpp",
    "src/helpers/CommonCLI_Observer.cpp",
    "examples/simple_repeater/MyMesh.cpp",
]

# Firmware commands the table deliberately does not offer. Everything below
# except tls.bundletest is also rejected by /api/cli (wcCliUnavailable), so the
# portal never pretends to run something it cannot.
NOT_OFFERED = {
    "tls.bundletest",  # TLS debugging, not an operator command
    "start ota",       # binds port 80, which the portal is already using
    "clock sync",      # takes its time from the caller; a web request has none
    "log",             # streams to Serial and stalls the radio ("log start" is offered)
    "get acl",         # streams to Serial, returns nothing
}


def firmware_commands():
    """Top-level command literals the firmware dispatches on."""
    found = set()
    for rel in COMMAND_SOURCES:
        path = os.path.join(HERE, "..", rel)
        try:
            src = open(path, encoding="utf-8").read()
        except OSError:
            continue
        for lit in re.findall(r'(?:mem|str)n?cmp\(\s*command\s*,\s*"([^"]+)"', src):
            found.add(lit.strip())
    return found - NOT_OFFERED
ROUND_TRIPS = [
    ("set radio.watchdog 30", "get radio.watchdog", "30"),
    ("set dutycycle 25", "get dutycycle", "25.0"),
    ("set alert.mqtt on", "get alert.mqtt", "on"),
    ("set bridge.source tx", "get bridge.source", "tx"),
    ("set mqtt.neighbors on", "get mqtt.neighbors", "on"),
    ("set path.hash.mode 2", "get path.hash.mode", "2"),
    ("set mqtt.iata den", "get mqtt.iata", "DEN"),
    # Secret reads are masked back down for an HTTP caller, in CommonCLI's own
    # words for a non-serial one (wcIsSecretReadCommand).
    ("set guest.password hunter2", "get guest.password", "******** (serial only)"),
    ("set wifi.pwd hunter2", "get wifi.pwd", "******** (serial only)"),
]


class Client:
    def __init__(self, base):
        self.base = base
        r = self._open("/api/login", b'{"password":"password"}')
        self.cookie = r.headers["Set-Cookie"].split(";")[0]
        # The node caps a sequence at MAX_BATCH and reports it; chunk to match
        # rather than hardcoding a number that drifts when the slot is resized.
        self.max_cmds = json.load(self._open("/api/status")).get("max_cmds", 24)

    def _open(self, path, data=None):
        headers = {"Content-Type": "application/json"}
        if getattr(self, "cookie", None):
            headers["Cookie"] = self.cookie
        return urllib.request.urlopen(urllib.request.Request(
            self.base + path, data=data, headers=headers,
            method="POST" if data is not None else "GET"))

    def run(self, cmds):
        """[(command, result)]. The node never echoes the command back — it may
        carry a secret — so results pair with what was sent, by index."""
        out = []
        for i in range(0, len(cmds), self.max_cmds):
            chunk = cmds[i:i + self.max_cmds]
            results = self._sequence(chunk)
            if len(results) != len(chunk):
                sys.exit("node returned %d results for %d commands" % (len(results), len(chunk)))
            out += list(zip(chunk, results))
        return out

    def _sequence(self, cmds):
        reqid = secrets.token_hex(8)
        body = json.dumps({"reqid": reqid, "cmds": cmds}).encode()
        for _ in range(200):                 # the executor frees itself in time
            try:
                self._open("/api/cli", body)
                break
            except urllib.error.HTTPError as e:
                if e.code != 409:
                    raise
                time.sleep(0.5)
        # Results stream and page, so keep reading from a cursor until the node
        # says done — "done" arrives only once every result has been handed over.
        out = []
        while True:
            r = json.load(self._open("/api/cli/result?reqid=%s&from=%d" % (reqid, len(out))))
            out += r.get("results", [])
            if r["state"] == "done":
                return out
            time.sleep(0.05)


def main():
    try:
        cli = Client(BASE)
    except OSError as e:
        sys.exit("cannot reach the mock at %s (%s)\n"
                 "start it with: python3 scripts/webconfig_mock_server.py --port 8137" % (BASE, e))

    failures = []

    cmds = table()
    unexpected = []
    for cmd, res in cli.run(cmds):
        if res["ok"]:
            continue
        want = EXPECTED_FAILURES.get(cmd)
        if want and want in res["reply"]:
            continue
        unexpected.append((cmd, res["reply"]))
    print("commands offered by autocomplete : %d" % len(cmds))
    print("answered                         : %d" % (len(cmds) - len(unexpected)))
    print("sequence cap reported by the node: %d" % cli.max_cmds)
    for cmd, reply in unexpected:
        print("   FAIL  %-30s %s" % (cmd, reply))
    failures += unexpected

    # The reverse direction: a command the firmware implements but the table
    # never offers is invisible to the check above, because the check only ever
    # drives what the table already knows about.
    offered = " ".join(cmds) + " " + " ".join(
        re.findall(r'\["([^"]+)","', open(INDEX_HTML, encoding="utf-8").read()))
    missing = sorted(c for c in firmware_commands() if c not in offered)
    print("\nfirmware commands not in the table: %d" % len(missing))
    for c in missing:
        print("   MISSING  %s" % c)
    failures += [(c, "not offered by autocomplete") for c in missing]

    results = cli.run([c for probe in ROUND_TRIPS for c in probe[:2]])
    print("\nround-trips                      : %d" % len(ROUND_TRIPS))
    for i, (setc, getc, want) in enumerate(ROUND_TRIPS):
        setr, getr = results[i * 2][1], results[i * 2 + 1][1]
        # `get` answers "> value"; compare the value, as the terminal displays it
        got = re.sub(r"^>\s?", "", getr["reply"])
        if setr["ok"] and got == want:
            continue
        print("   FAIL  %-30s got %r, wanted %r (set: %s)"
              % (getc, got, want, setr["reply"]))
        failures.append((getc, got))

    # Commands the portal refuses must be refused clearly, not run and fudged.
    print("\nrefused with a reason              : ", end="")
    refused = []
    for cmd in sorted(NOT_OFFERED - {"tls.bundletest"}):
        try:
            cli._sequence([cmd])
            refused.append((cmd, "was accepted, expected a 400"))
        except urllib.error.HTTPError as e:
            body = json.load(e) if e.code == 400 else {}
            if e.code != 400 or not body.get("error"):
                refused.append((cmd, "HTTP %d, expected 400 with a reason" % e.code))
    print("%d/%d" % (len(NOT_OFFERED) - 1 - len(refused), len(NOT_OFFERED) - 1))
    for cmd, why in refused:
        print("   FAIL  %-30s %s" % (cmd, why))
    failures += refused

    print("\n%s" % ("FAILED: %d" % len(failures) if failures else "all clear"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

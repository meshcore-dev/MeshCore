#!/usr/bin/env python3
"""Local mock of the WebConfig portal backend, for iterating on webui/index.html
in a real browser with no firmware, no flashing, and no paid emulator account.

It serves the real webui/index.html and implements the same /api/* contract as
src/helpers/esp32/WebConfigServer.cpp — including the 202+reqid handshake, the
pending -> done result polling, aggregate-success reboot gating, secret masking
(********), and the IATA / owner-key / length validation the firmware enforces.
So the browser drives the actual portal JS (wizard, save/poll/reqid, effective
value handling, reboot overlay, stats, scan) against realistic responses.

/api/cli is the CLI terminal's backend and has no firmware counterpart yet: it
is the proposed contract (202 + reqid, streamed per-command results) executed
against a CommonCLI-shaped interpreter, so the terminal UI can be designed
against realistic single- and multi-line replies before any of it goes on-device.

It does NOT run the C++ handlers (that's what test/ gtest covers) or the
AsyncTCP transport — it's a frontend + contract harness.

Usage:
    python3 scripts/webconfig_mock_server.py               # LAN mode (login: password)
    python3 scripts/webconfig_mock_server.py --setup       # first-boot setup wizard
    python3 scripts/webconfig_mock_server.py --port 9000 --active-slots 2
Then open http://localhost:8080/ (or the chosen port). Editing index.html and
refreshing shows changes immediately — the page is re-read per request.

Stdlib only; no pip install.
"""

import argparse
import copy
import json
import os
import re
import secrets
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

HERE = os.path.dirname(os.path.abspath(__file__))
INDEX_HTML = os.path.join(HERE, "..", "webui", "index.html")

sys.path.insert(0, HERE)
# The build-time comment stripper, shared so --minify serves byte-for-byte what
# the generator embeds rather than a second implementation that could drift.
from webconfig_minify import strip_source  # noqa: E402

MINIFY = False
# Overridable with --fw-version to exercise the console's channel labelling:
#   v1.16.0.5-observer-a1b2c3d            release
#   v1.16.0.5-observer-beta-dev-a1b2c3d   dev
#   v1.16.0                               local build, no OTA
FW_VERSION = "v1.16.0.5-observer-a1b2c3d"

SENTINEL = "********"
ADMIN_PASSWORD = "password"          # matches the default ADMIN_PASSWORD build flag
BATCH_PENDING_SECS = 0.8             # how long POST->done takes, to exercise polling
SCAN_SECS = 0.8

# Destination buffer sizes (chars, minus the NUL) — mirrors the MQTTPrefs fields
# the firmware validates in CommonCLI_Observer.cpp.
LEN_LIMITS = {
    "name": 31, "wifi.ssid": 31, "wifi.pwd": 63, "mqtt.origin": 31,
    "mqtt.email": 63, "mqtt.ntp": 63, "timezone": 31, "snmp.community": 23,
}
# "filter" is absent on purpose: it is a bitmask, not a text buffer, so it has
# no destination-buffer limit. It is still bounded by the shared CLI command
# budget below, like every other key.
SLOT_LEN_LIMITS = {"server": 63, "username": 31, "password": 63,
                   "token": 47, "topic": 95, "audience": 63}

# BatchEntry::cmd[160] in WebConfigServer.cpp holds "set <key> <value>" plus a
# NUL. Over-long values are rejected there rather than truncated, because a
# clipped value can still be valid and would persist as a different setting.
BATCH_CMD_SIZE = 160

# Preset names + what the UI must collect (mirrors handlePresets()).
PRESETS = (
    [(n, "none") for n in (
        "analyzer-us", "analyzer-eu", "nz-analyzer", "meshmapper", "waev",
        "meshomatic", "cascadiamesh", "tennmesh", "nashmesh", "ctmesh", "chimesh",
        "meshat.se", "eastidahomesh", "coloradomesh", "dutchmeshcore-1",
        "dutchmeshcore-2", "meshcore-ca-1", "meshcore-ca-2", "meshcore-fi",
        "bostonmesh", "rflab", "ipnt.uk", "flmesh", "corecomms")]
    + [("meshrank", "token"), ("inwmesh", "userpass")]
)

SCAN_NETWORKS = [
    {"ssid": "Wokwi-GUEST", "rssi": -42, "enc": False},
    {"ssid": "HomeNet", "rssi": -55, "enc": True},
    {"ssid": "HomeNet-5G", "rssi": -61, "enc": True},
    {"ssid": "Neighbor 2.4", "rssi": -78, "enc": True},
    {"ssid": "OpenGuest", "rssi": -83, "enc": False},
]


def default_config(setup_mode):
    return {
        "radio": {
            "freq": 910.525, "bw": 62.5, "sf": 7, "cr": 5, "tx": 22, "af": 1.0,
            "rxdelay": 0.0, "txdelay": 0.5, "cad": False, "rxgain": True,
            "repeat": True, "flood_max": 64, "flood_max_advert": 8,
            "flood_max_unscoped": 8, "loop_detect": "moderate",
            "name": "MockNode", "lat": 39.7392, "lon": -104.9903,
            "advert_interval": 240, "flood_advert_interval": 6,
        },
        "wifi": {
            # setup mode = unconfigured (empty ssid -> wizard); LAN mode = joined
            "ssid": "" if setup_mode else "HomeNet",
            "pwd": "" if setup_mode else "secretpw",   # stored raw; masked on GET
            "powersave": "min",
        },
        "mqtt": {
            "origin": "" if setup_mode else "MockNode", "iata": "" if setup_mode else "DEN",
            "status": True, "packets": True, "raw": False, "tx": "advert", "rx": True,
            "interval": 5, "timezone": "MST7MDT,M3.2.0,M11.1.0", "timezone_offset": -7,
            "ntp": "pool.ntp.org", "owner": "", "email": "", "snmp": False,
            "snmp_community": "public",
            "neighbors": False, "neighbors_interval": 24,
            "slots": [_slot() for _ in range(6)],
        },
        # Settings the CLI reaches but no portal form does, so they are absent
        # from /api/config (see config_json) and live only here. Without them
        # the terminal answers "unknown config key" for perfectly real commands.
        "cli": {
            "radio.watchdog": 0, "int.thresh": 0, "agc.reset.interval": 0,
            "direct.txdelay": 0.0, "multi.acks": 0, "allow.read.only": False,
            "path.hash.mode": 0, "owner.info": "", "guest.password": "",
            "adc.multiplier": 1.0,
            "alert": False, "alert.psk": "", "alert.hashtag": "",
            "alert.region": "", "alert.interval": 15,
            "alert.mqtt": False, "alert.wifi": False,
            "bridge.enabled": False, "bridge.source": "rx", "bridge.baud": 115200,
            "bridge.delay": 0, "bridge.channel": 0, "bridge.secret": "",
        },
    }


def _slot():
    return {"preset": "none", "server": "", "port": 8883, "username": "",
            "password": "", "token": "", "topic": "", "audience": "",
            "filter": "all"}


class State:
    def __init__(self, args):
        self.lock = threading.Lock()
        self.setup_mode = args.setup
        self.active_slots = args.active_slots
        self.cfg = default_config(args.setup)
        # latched at AP start, like WebConfigServer::_initial_setup
        self.initial_setup = args.setup and self.cfg["wifi"]["ssid"] == ""
        self.start = time.time()
        self.session = None            # cookie token when logged in (LAN mode)
        self.batch = {"state": "idle"}
        self.cli = {"state": "idle"}   # deferred CLI sequence, see /api/cli
        self.admin_pwd_set = False     # satisfies the initial-setup invariant
        self.scan_started = None

    # ---- auth -------------------------------------------------------------
    def is_authed(self, headers):
        if self.setup_mode:
            return True                # setup mode: proximity trust, no auth
        if not self.session:
            return False
        cookie = headers.get("Cookie", "")
        m = re.search(r"wcs=([0-9a-f]+)", cookie)
        return bool(m and m.group(1) == self.session)

    # ---- config serialization (masks secrets, like handleConfigGet) -------
    def config_json(self):
        c = copy.deepcopy(self.cfg)
        c.pop("cli")                       # CLI-only settings: not part of this contract
        c["wifi"]["pwd"] = SENTINEL if self.cfg["wifi"]["pwd"] else ""
        for s in c["mqtt"]["slots"]:
            s["password"] = SENTINEL if s["password"] else ""
            s["token"] = SENTINEL if s["token"] else ""
        return c

    def status_json(self, authed):
        return {
            "mode": "setup" if self.setup_mode else "lan",
            "auth": authed,
            "needs_setup": self.cfg["wifi"]["ssid"] == "",
            "name": self.cfg["radio"]["name"], "node_id": "a1b2c3d4e5f60718",
            # Shaped like build.sh's EMBEDDED_VERSION_STRING
            # (base[.build][-observer][-channel]-hash) so the console's channel
            # labelling is exercised against a real version, not "v1.x-mock".
            "fw": FW_VERSION, "build_date": "6 Jun 2026",
            "role": "Repeater", "board": "Heltec V3 (mock)",
            "uptime_s": int(time.time() - self.start),
            "runtime_slots": 6, "max_slots": 6, "active_slots": self.active_slots,
            "max_cmds": CLI_MAX_CMDS,
        }


# ---------------------------------------------------------------------------
# set-command application + validation (mirrors the firmware's setters enough
# to produce realistic per-field OK / Error replies for the UI chips).
# ---------------------------------------------------------------------------
BOOL_KEYS = {"cad": ("radio", "cad"), "radio.rxgain": ("radio", "rxgain"),
             "repeat": ("radio", "repeat"), "mqtt.status": ("mqtt", "status"),
             "mqtt.packets": ("mqtt", "packets"), "mqtt.raw": ("mqtt", "raw"),
             "mqtt.rx": ("mqtt", "rx"), "snmp": ("mqtt", "snmp"),
             "mqtt.neighbors": ("mqtt", "neighbors")}
INT_KEYS = {"tx": ("radio", "tx"), "flood.max": ("radio", "flood_max"),
            "flood.max.advert": ("radio", "flood_max_advert"),
            "flood.max.unscoped": ("radio", "flood_max_unscoped"),
            "advert.interval": ("radio", "advert_interval"),
            "flood.advert.interval": ("radio", "flood_advert_interval"),
            "mqtt.interval": ("mqtt", "interval"),
            "mqtt.neighbors.interval": ("mqtt", "neighbors_interval"),
            "timezone.offset": ("mqtt", "timezone_offset")}
FLOAT_KEYS = {"lat": ("radio", "lat"), "lon": ("radio", "lon"),
              "af": ("radio", "af"), "rxdelay": ("radio", "rxdelay"),
              "txdelay": ("radio", "txdelay")}
STR_KEYS = {"name": ("radio", "name"), "wifi.ssid": ("wifi", "ssid"),
            "wifi.powersave": ("wifi", "powersave"), "loop.detect": ("radio", "loop_detect"),
            "mqtt.origin": ("mqtt", "origin"), "mqtt.ntp": ("mqtt", "ntp"),
            "mqtt.email": ("mqtt", "email"), "timezone": ("mqtt", "timezone"),
            "snmp.community": ("mqtt", "snmp_community"), "mqtt.tx": ("mqtt", "tx")}
SECRET_STR_KEYS = {"wifi.pwd": ("wifi", "pwd")}

# The CLI-only settings, typed the same way so apply_set/cli_read_key reach them
# through the existing lookups rather than a parallel code path.
for _k, _v in default_config(False)["cli"].items():
    _t = {bool: BOOL_KEYS, int: INT_KEYS, float: FLOAT_KEYS, str: STR_KEYS}[type(_v)]
    _t[_k] = ("cli", _k)
SECRET_STR_KEYS.update({k: ("cli", k) for k in
                        ("guest.password", "alert.psk", "bridge.secret")})
for _k in SECRET_STR_KEYS:
    STR_KEYS.pop(_k, None)


def _hex64(v):
    return len(v) == 64 and all(c in "0123456789abcdefABCDEF" for c in v)


def apply_set(cfg, key, val):
    """Return (ok, reply) and mutate cfg. Mirrors the firmware's validation for
    the fields where it matters (length, IATA, owner key, port, radio combo)."""
    # length guard for the plain string fields
    if key in LEN_LIMITS and len(val) > LEN_LIMITS[key]:
        return False, "Error: %s too long (max %d chars)" % (key, LEN_LIMITS[key])

    if key == "password":
        # Stored outside cfg: it must never appear in the /api/config GET. The
        # firmware overwrites the CLI's "password now: <secret>" echo, so the
        # reply carries no secret either.
        global ADMIN_PASSWORD
        ADMIN_PASSWORD = val
        return True, "OK"

    if key == "radio.fem.rxgain":
        return False, "Error: unsupported"   # no FEM on the mock board, see GETTERS

    if key == "dutycycle":
        try:
            dc = float(val)
        except ValueError:
            return False, "Error: expected a number"
        if not 0 < dc <= 100:
            return False, "Error, must be 1-100"
        cfg["radio"]["af"] = 100.0 / dc - 1   # the CLI stores it as airtime_factor
        return True, "OK"

    if key in ("freq", "bw", "sf", "cr"):
        # single-component radio setters, reachable from the CLI but not from
        # the form batch (which always sends the whole `radio` combo)
        try:
            cfg["radio"][key] = int(val) if key in ("sf", "cr") else float(val)
        except ValueError:
            return False, "Error: expected a number"
        return True, "OK - reboot to apply"

    if key == "radio":
        try:
            f, bw, sf, cr = val.split(",")
            f, bw, sf, cr = float(f), float(bw), int(sf), int(cr)
        except ValueError:
            return False, "Error, invalid radio params"
        if not (150 <= f <= 2500 and 7 <= bw <= 500 and 5 <= sf <= 12 and 5 <= cr <= 8):
            return False, "Error, invalid radio params"
        cfg["radio"].update(freq=f, bw=bw, sf=sf, cr=cr)
        return True, "OK - reboot to apply"

    if key == "mqtt.iata":
        if val == "":
            cfg["mqtt"]["iata"] = ""
            return True, "OK - IATA cleared"
        if len(val) != 3 or not val.isalnum() or not val.isascii():
            return False, "Error: IATA code must be exactly 3 letters/digits (e.g. DEN)"
        cfg["mqtt"]["iata"] = val.upper()
        return True, "OK"

    if key == "prv.key":
        # write-only by design: the identity goes in, nothing reads it back
        if not _hex64(val):
            return False, "Error: private key must be 64 hex characters"
        return True, "OK - identity restored, reboot to apply"

    if key == "mqtt.owner":
        if val == "":
            cfg["mqtt"]["owner"] = ""
            return True, "OK - owner key cleared"
        if not _hex64(val):
            return False, "Error: public key must be 64 hex characters (32 bytes)"
        cfg["mqtt"]["owner"] = val
        return True, "OK"

    m = re.match(r"^mqtt([1-6])\.(\w+)$", key)
    if m:
        return apply_slot_set(cfg, int(m.group(1)) - 1, m.group(2), val)

    if key in BOOL_KEYS:
        sec, f = BOOL_KEYS[key]
        cfg[sec][f] = (val == "on")
        return True, "OK"
    if key in INT_KEYS:
        sec, f = INT_KEYS[key]
        try:
            cfg[sec][f] = int(val)
        except ValueError:
            return False, "Error: expected a number"
        return True, "OK"
    if key in FLOAT_KEYS:
        sec, f = FLOAT_KEYS[key]
        try:
            cfg[sec][f] = float(val)
        except ValueError:
            return False, "Error: expected a number"
        return True, "OK"
    if key in SECRET_STR_KEYS:
        sec, f = SECRET_STR_KEYS[key]
        cfg[sec][f] = val
        return True, "OK"
    if key in STR_KEYS:
        sec, f = STR_KEYS[key]
        cfg[sec][f] = val
        return True, "OK"
    # Strict fallthrough: this function is the single authority on what can be
    # set, for the batch and the CLI alike. Accepting unknown keys here once hid
    # the fact that the CLI could not reach `dutycycle` or `radio.fem.rxgain`.
    # Verbatim shape from CommonCLI::handleSetCmd's fallthrough.
    return False, "unknown config: %s" % key


# Payload-type names accepted alongside the decimal form. Mirrors
# namedPacketTypes() in src/helpers/MQTTPacketFilter.h; 12-14 are reserved
# upstream and stay reachable by number only.
PACKET_TYPE_NAMES = {
    "req": 0, "response": 1, "txt_msg": 2, "ack": 3, "advert": 4,
    "grp_txt": 5, "grp_data": 6, "anon_req": 7, "path": 8, "trace": 9,
    "multipart": 10, "control": 11, "raw_custom": 15,
}


def packet_filter_mask(text):
    """Canonical filter text -> bitmask, for the stats payload."""
    if text == "all":
        return 0xFFFF
    if text == "none":
        return 0
    mask = 0
    for token in text.split(","):
        mask |= 1 << int(token)
    return mask


def canonical_packet_filter(val):
    """Mirror of MQTTPacketFilter::parse + ::format. Returns None if invalid."""
    stripped = val.strip()
    if stripped == "" or stripped == "all":
        return "all"
    if stripped == "none":
        return "none"
    mask = 0
    for part in stripped.split(","):
        token = part.strip()
        if re.fullmatch(r"[0-9]+", token):
            packet_type = int(token)
            if packet_type > 15:
                return None
        elif token in PACKET_TYPE_NAMES:
            packet_type = PACKET_TYPE_NAMES[token]
        else:
            return None
        mask |= 1 << packet_type
    if mask == 0xFFFF:
        return "all"
    return ",".join(str(i) for i in range(16) if mask & (1 << i))


def apply_slot_set(cfg, idx, field, val):
    slot = cfg["mqtt"]["slots"][idx]
    if field in SLOT_LEN_LIMITS and len(val) > SLOT_LEN_LIMITS[field]:
        return False, "Error: %s too long (max %d chars)" % (field, SLOT_LEN_LIMITS[field])
    if field == "port":
        try:
            p = int(val)
        except ValueError:
            return False, "Error: port must be between 1 and 65535"
        if not (1 <= p <= 65535):
            return False, "Error: port must be between 1 and 65535"
        slot["port"] = p
        return True, "OK"
    if field == "filter":
        canonical = canonical_packet_filter(val)
        if canonical is None:
            return False, ("Error: filter must be all, none, or a CSV of "
                           "types 0-15 / names (advert,txt_msg,...)")
        slot["filter"] = canonical
        return True, "OK - slot %d packet types: %s" % (idx + 1, canonical)
    if field in ("preset", "server", "username", "password", "token", "topic", "audience"):
        slot[field] = val
        if field == "token":
            return True, "OK - slot %d token set" % (idx + 1)
        return True, "OK"
    return False, "Error: unknown slot field"


def is_secret_key(key):
    # The serial console prints these back; the portal is reachable over the
    # LAN, so it masks them in `get` replies the way /api/config already does.
    return key in SECRET_STR_KEYS or bool(re.match(r"^mqtt[1-6]\.(password|token)$", key))


# ---------------------------------------------------------------------------
# CLI command execution (backs /api/cli), mirroring CommonCLI enough to give
# the terminal UI realistic single- and multi-line replies.
#
# The portal's `set` batch is allowlisted (WebConfigKeys.h) because it is driven
# by form fields; the CLI is deliberately NOT, since its whole point is reaching
# the same surface the serial console reaches. Auth is the boundary — exactly
# as it is for the serial console and for remote admin over the mesh.
# ---------------------------------------------------------------------------
# MAX_BATCH in WebConfigServer.h: the CLI shares the config batch's fixed slot,
# so this is the real cap, reported to the page as status.max_cmds.
CLI_MAX_CMDS = 24
CLI_RESULT_PAGE = 8                  # WebConfigBatch::kCliResultPage
CLI_CMD_SECS = 0.25                  # simulated per-command execution time
# Board::reboot() does not return, so the firmware answers `reboot` itself and
# arms the deferred reboot once results have been read (see wcIsDeferredReboot).
CLI_DEFERRED_REBOOT = "reboot"       # matched as a PREFIX, like CommonCLI does

# Commands the CLI reaches but the portal cannot honestly serve; rejected at
# POST. Mirrors wcCliUnavailable() in WebConfigServer.cpp.
CLI_UNAVAILABLE = [
    ("start ota", True, "start ota needs port 80, which this portal is using. "
                        "Run it from the serial console, or use `ota update`."),
    ("clock sync", True, "clock sync takes its time from the caller, which a web request "
                         "has no way to supply. Use `time <epoch-seconds>` instead."),
    ("log", False, "log writes the packet log to the serial console, not here, and "
                   "blocks the radio while it does. Use `log start` / `log stop`."),
    ("get acl", False, "get acl writes to the serial console, not here."),
]


def cli_unavailable(cmd):
    for token, is_prefix, why in CLI_UNAVAILABLE:
        if cmd.startswith(token) if is_prefix else cmd == token:
            return why
    return None


# Failure replies CommonCLI emits that do NOT start with "Err" — the shapes that
# made a naive prefix test call them success. Mirrors
# WebConfigBatch::cliReplyIsFailure.
def cli_reads_secret(cmd):
    """Commands that READ a secret. CommonCLI gates these on the caller being
    the serial console; the portal is not, so the value is masked here the way
    CommonCLI masks it for remote callers. Mirrors wcCliReadsSecret()."""
    if not cmd.startswith("get "):
        return False
    key = cmd[4:].strip()
    return key in ("prv.key", "guest.password", "alert.psk", "bridge.secret") \
        or is_secret_key(key)


def cli_reply_is_failure(reply):
    if not reply:
        return False
    if reply.startswith(("Err", "ERR", "err", "(ERR", "Unknown command",
                         "unknown config", "??", "Can't find")):
        return True
    return ": Err" in reply

# Commands the device answers but that have no config-key equivalent.
GETTERS = {
    "freq": lambda c: "%.3f" % c["radio"]["freq"],
    "bw": lambda c: "%.2f" % c["radio"]["bw"],
    "sf": lambda c: str(c["radio"]["sf"]),
    "cr": lambda c: str(c["radio"]["cr"]),
    "public.key": lambda c: "a1b2c3d4" * 8,
    "wifi.status": lambda c: (
        "SSID: %s\nIP: 192.168.1.42\nRSSI: -58 dBm\nUptime: %dm"
        % (c["wifi"]["ssid"] or "(not set)", int(time.time() - ST.start) // 60)),
    "mqtt.status": lambda c: cli_mqtt_status(c),
    "mqtt.presets": lambda c: "\n".join(
        "%2d. %s%s" % (i + 1, n, "" if nd == "none" else "  (needs %s)" % nd)
        for i, (n, nd) in enumerate(PRESETS)),
    "role": lambda c: "Repeater",
    "acl": lambda c: "a1b2c3d4e5f60718  perms 3\n1122334455667788  perms 1",
    # not its own pref: the CLI derives it from airtime_factor both ways
    "dutycycle": lambda c: "%.1f" % (100.0 / (c["radio"]["af"] + 1)),
    "mqtt.config.valid": lambda c: (
        "yes" if any(s["preset"] != "none" for s in c["mqtt"]["slots"]) else "no - no slot configured"),
    "mqtt.ntp.diag": lambda c: "last sync: 42s ago via %s (offset +0.011s)" % (c["mqtt"]["ntp"] or "none"),
    "mqtt.stats": lambda c: ("published: %d\ndropped: 0\nqueue: 0/24\nreconnects: 1"
                             % (100 + int(time.time() - ST.start))),
    # Runtime-gated on the real device (Board::canControlLoRaFemLna), not
    # compiled out — the command exists everywhere and the board answers for
    # itself. The mock board is a Heltec V3, which has no FEM.
    "radio.fem.rxgain": lambda c: None,
}


def cli_mqtt_status(cfg):
    lines = []
    for i, s in enumerate(cfg["mqtt"]["slots"][:ST.active_slots]):
        if s["preset"] == "none":
            lines.append("slot %d: unconfigured" % (i + 1))
        else:
            lines.append("slot %d: %-16s connected  tx=%d err=0"
                         % (i + 1, s["preset"], 100 + int(time.time() - ST.start)))
    return "\n".join(lines)


def cli_get(cfg, key):
    """Reply to `get <key>`.

    CommonCLI::handleGetCmd answers `> value` — the marker sets the value apart
    on the serial console. Reproduced here because it is load-bearing: a reply
    that starts with "> " does not start with "OK", which is what made the
    firmware's first cut mark every getter a failure.
    """
    ok, val = _cli_get_value(cfg, key)
    return (ok, "> " + val) if ok else (ok, val)


def _cli_get_value(cfg, key):
    if key in GETTERS:
        val = GETTERS[key](cfg)
        return (True, val) if val is not None else (False, "Error: unsupported")
    if is_secret_key(key):
        # The serial console prints these; the portal is reachable over the LAN,
        # so it masks them the same way /api/config does.
        return True, SENTINEL if cli_read_key(cfg, key) else "(not set)"
    val = cli_read_key(cfg, key)
    if val is None:
        return False, "??: %s" % key      # CommonCLI::handleGetCmd fallthrough
    return True, str(val)


def cli_read_key(cfg, key):
    """Current value of a `set` key, or None when the key is unknown."""
    m = re.match(r"^mqtt([1-6])\.(\w+)$", key)
    if m:
        slot = cfg["mqtt"]["slots"][int(m.group(1)) - 1]
        return slot.get(m.group(2))
    for table in (BOOL_KEYS, INT_KEYS, FLOAT_KEYS, STR_KEYS, SECRET_STR_KEYS):
        if key in table:
            sec, f = table[key]
            v = cfg[sec][f]
            return ("on" if v else "off") if key in BOOL_KEYS else v
    # keys apply_set() special-cases, so they appear in none of the tables above
    r = cfg["radio"]
    return {
        "name": r["name"], "lat": r["lat"], "lon": r["lon"],
        "radio": "%.3f,%.2f,%d,%d" % (r["freq"], r["bw"], r["sf"], r["cr"]),
        "bw": r["bw"], "sf": r["sf"], "cr": r["cr"],
        "mqtt.iata": cfg["mqtt"]["iata"], "mqtt.owner": cfg["mqtt"]["owner"],
    }.get(key)


def run_cli(cfg, line):
    """Execute one command line. Returns (ok, reply); reply may be multi-line."""
    cmd = line.strip()
    if cmd == "":
        return True, ""
    if cmd == "ver":
        # Same source as /api/status's fw on the device: both are
        # FIRMWARE_VERSION, so they must not disagree here either.
        return True, "%s (Build: 6 Jun 2026)" % FW_VERSION
    if cmd == "board":
        return True, "Heltec V3 (mock)"
    if cmd == "clock":
        return True, time.strftime("%d/%m/%Y %H:%M:%S", time.gmtime()) + " UTC"
    if cmd == "advert":
        return True, "OK - Advert sent (zero hop)"
    if cmd == "advert.zerohop":
        return True, "OK - Advert sent (zero hop)"
    if cmd in ("reboot", "clkreboot"):
        return True, "OK - rebooting"
    if cmd in ("poweroff", "shutdown"):
        return True, "OK - powering off"
    if cmd == "erase":
        return True, "File system erase: OK"
    if cmd == "memory":
        return True, ("heap free: 142000\nheap min: 118000\n"
                      "largest block: 96000\npsram free: 3980000")
    if cmd == "neighbors":
        return True, ("d4e5f60718  -71 dBm  snr 9.5   2m ago\n"
                      "1122334455  -94 dBm  snr 2.0  14m ago")
    # Handled by MyMesh::handleCommand before it delegates to CommonCLI.
    if cmd == "discover.neighbors":
        return True, "OK - Discover sent"
    if cmd == "discover.scopes":
        return True, "OK - scopes queued (18s discovery remaining)"
    if cmd.startswith("setperm "):
        parts = cmd[8:].split()
        if len(parts) != 2 or not _hex64(parts[0]):
            return False, "Err - bad params"
        return True, "OK"
    if cmd.startswith("clock sync"):
        # Rejected at POST, but modelled anyway: over the web the caller's
        # timestamp is 0, so CommonCLI always takes this branch.
        return False, "(ERR: clock cannot go backwards)"
    if cmd == "region":
        return True, "US915"
    if cmd == "sensor list":
        return True, "0: battery (mV)\n1: temperature (C)\n2: humidity (%)"
    if cmd.startswith("sensor get "):
        return True, "> 22.4"
    if cmd.startswith("sensor set "):
        return True, "OK"
    if cmd.startswith("gps advert "):
        mode = cmd[11:]
        if mode not in ("none", "share", "prefs"):
            return False, "Error, must be none, share or prefs"
        return True, "OK - advert position: %s" % mode
    if cmd in ("gps on", "gps off"):
        return True, "OK - GPS %s" % cmd[4:]
    if cmd == "gps sync":
        return True, "OK - clock and location set from GPS"
    if cmd == "gps setloc":
        return True, "OK - lat/lon set from the current fix"
    if cmd == "gps":
        return True, "GPS: no fix (0 satellites)"
    if cmd in ("powersaving on", "powersaving off"):
        return True, "OK - power saving %s" % cmd[12:]
    if cmd == "powersaving":
        return True, "off"
    if cmd.startswith("alert test"):
        if not ST.cfg["cli"]["alert.psk"]:
            return False, "Error: alert channel not configured (set alert.psk or set alert.hashtag)"
        return True, "OK - test alert sent"
    if cmd.startswith("ota "):
        return True, ("v1.7.2 available (current v1.7.1-mock)" if cmd == "ota check"
                      else "OK - downloading v1.7.2, will reboot when flashed")
    if cmd.startswith("start webconfig"):
        return True, "OK - already running (you are using it)"
    if cmd == "stop webconfig":
        return True, "OK - portal stopping"
    if cmd == "start ota":
        return True, "OK - upload AP raised at 192.168.4.1"
    if cmd.startswith("neighbor.remove "):
        return (True, "OK") if _hex64(cmd[16:]) else (False, "ERR: bad pubkey")
    if cmd.startswith("tempradio "):
        return True, "OK - temporary radio params applied (not saved)"
    if cmd == "clear stats":
        return True, "OK - stats cleared"
    if cmd.startswith("stats-"):
        return True, "recv=512 sent=88 rx_err=3 airtime=41s"
    if cmd == "log":
        return True, "packet log: 128 entries, 14 KB"
    if cmd.startswith("log "):
        return True, "OK"
    if cmd.startswith("password "):
        global ADMIN_PASSWORD
        ADMIN_PASSWORD = cmd[9:]
        return True, "OK - password changed"
    if cmd.startswith("time "):
        return True, "OK - clock set"
    if cmd.startswith("get "):
        return cli_get(cfg, cmd[4:].strip())
    if cmd.startswith("set "):
        rest = cmd[4:].strip()
        key, _, val = rest.partition(" ")
        if not key:
            return False, "Error: set what?"
        # apply_set owns the "is this settable" decision; gating on whether the
        # key is *readable* rejected write-only and computed ones (`dutycycle`,
        # `prv.key`, `radio.fem.rxgain`).
        return apply_set(cfg, key, val.strip())
    return False, "Unknown command"


def valid_reqid(reqid):
    return isinstance(reqid, str) and bool(re.fullmatch(r"[0-9A-Fa-f]{16}", reqid))


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):   # concise one-line log
        print("  %s %s" % (self.command, self.path))

    # -- helpers --
    def _json(self, code, obj, extra_headers=None):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra_headers or {}):
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self):
        n = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(n) if n else b""

    def _need_auth(self):
        if not ST.is_authed(self.headers):
            self._json(401, {"error": "auth"})
            return True
        return False

    # -- GET --
    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/":
            return self._serve_index()
        if path == "/api/status":
            return self._json(200, ST.status_json(ST.is_authed(self.headers)))
        if path == "/api/presets":
            return self._json(200, {"presets": [{"name": n, "needs": nd} for n, nd in PRESETS]})
        if path == "/api/config":
            if self._need_auth():
                return
            with ST.lock:
                return self._json(200, ST.config_json())
        if path == "/api/config/result":
            if self._need_auth():
                return
            return self._config_result()
        if path == "/api/cli/result":
            if self._need_auth():
                return
            return self._cli_result()
        if path == "/api/stats":
            if self._need_auth():
                return
            return self._json(200, self._stats())
        if path == "/api/scan":
            if self._need_auth():
                return
            return self._scan()
        return self._json(404, {"error": "not found"})

    # -- POST --
    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/login":
            return self._login()
        if path == "/api/logout":
            ST.session = None
            return self._json(200, {"ok": True}, [("Set-Cookie", "wcs=; Max-Age=0; Path=/")])
        if path == "/api/config":
            if self._need_auth():
                return
            return self._config_post()
        if path == "/api/cli":
            if self._need_auth():
                return
            return self._cli_post()
        if path == "/api/reboot":
            if self._need_auth():
                return
            return self._json(200, {"ok": True})
        if path == "/api/portal/exit":
            return self._json(200, {"ok": True, "url": "http://localhost:%d/" % PORT})
        return self._json(404, {"error": "not found"})

    # -- endpoint impls --
    def _serve_index(self):
        try:
            with open(INDEX_HTML, "rb") as f:      # re-read each time -> live edits
                html = f.read()
        except OSError:
            self.send_error(500, "webui/index.html not found")
            return
        if MINIFY:
            # Serve what the device actually serves. The generator strips
            # comments and indentation before compressing, so --minify is how
            # you exercise those bytes in a browser rather than trusting that
            # stripping a 100 KB page never changes its behaviour.
            html = strip_source(html.decode("utf-8")).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(html)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(html)

    def _login(self):
        if ST.setup_mode:
            return self._json(200, {"ok": True})
        try:
            body = json.loads(self._read_body() or b"{}")
        except ValueError:
            return self._json(400, {"error": "bad request"})
        if body.get("password") != ADMIN_PASSWORD:
            return self._json(401, {"error": "wrong password"})
        ST.session = secrets.token_hex(16)
        return self._json(200, {"ok": True},
                          [("Set-Cookie", "wcs=%s; HttpOnly; SameSite=Lax; Path=/" % ST.session)])

    def _config_post(self):
        raw = self._read_body()
        if len(raw) > 4096:
            return self._json(413, {"error": "body too large"})
        try:
            body = json.loads(raw or b"{}")
        except ValueError:
            return self._json(400, {"error": "bad json"})
        reqid = body.get("reqid", "")
        if not valid_reqid(reqid):
            return self._json(400, {"error": "bad reqid"})
        reboot = bool(body.get("reboot", False))
        setmap = body.get("set", {}) or {}

        # `password` maps to the top-level CLI command rather than a setter. It
        # is accepted in both modes (LAN already required a login), but first
        # onboarding cannot finish without it.
        if "password" in setmap:
            pwd = str(setmap["password"])
            if not 0 < len(pwd) <= 15 or "\r" in pwd or "\n" in pwd:
                return self._json(400, {"error": "admin password must be 1-15 characters with no line breaks"})
        elif ST.setup_mode and ST.initial_setup and (reboot or "wifi.ssid" in setmap):
            return self._json(400, {"error": "admin password required for initial setup"})

        with ST.lock:
            if ST.batch.get("state") != "idle" and ST.batch.get("reqid") == reqid:
                return self._json(202, {
                    "state": ST.batch["state"], "count": len(ST.batch.get("results", [])),
                    "reqid": reqid,
                })
            if ST.batch.get("state") == "pending":
                return self._json(409, {"error": "busy", "reqid": ST.batch.get("reqid", "")})
            # drop unchanged secrets (sentinel), like the firmware does
            entries = [(k, v) for k, v in setmap.items()
                       if not (is_secret_key(k) and v == SENTINEL)]
            # Same command-budget rejection the firmware applies while building
            # BatchEntry::cmd; CR/LF are stripped there and don't count.
            for k, v in entries:
                prefix = "password " if k == "password" else "set %s " % k
                stripped = str(v).replace("\r", "").replace("\n", "")
                if len(prefix) + len(stripped) > BATCH_CMD_SIZE - 1:
                    return self._json(400, {"error": "value too long", "key": k[:32]})
            if not entries and not reboot:
                return self._json(400, {"error": "no changes"})
            # apply now, but expose as pending->done to exercise polling
            results, all_ok = [], True
            for k, v in entries:
                ok, reply = apply_set(ST.cfg, k, str(v))
                if not ok:
                    all_ok = False
                results.append({"key": k, "reply": reply})
            ST.batch = {"state": "pending", "reqid": reqid, "results": results,
                        "all_ok": all_ok, "reboot": reboot,
                        "done_at": time.time() + BATCH_PENDING_SECS}
            return self._json(202, {"state": "pending", "count": len(entries), "reqid": reqid})

    def _config_result(self):
        query = parse_qs(urlsplit(self.path).query)
        reqid = query.get("reqid", [""])[0]
        if not valid_reqid(reqid):
            return self._json(400, {"error": "bad reqid"})
        with ST.lock:
            b = ST.batch
            if b.get("state") == "idle":
                return self._json(200, {"state": "idle", "reqid": reqid})
            if b.get("reqid") != reqid:
                return self._json(404, {"error": "unknown request"})
            if b["state"] == "pending" and time.time() < b["done_at"]:
                return self._json(200, {"state": "pending", "reqid": b["reqid"]})
            b["state"] = "done"      # stays readable until next POST
            return self._json(200, {
                "state": "done", "reqid": b["reqid"], "all_ok": b["all_ok"],
                "reboot": b["reboot"] and b["all_ok"], "results": b["results"],
            })

    # ---- CLI ---------------------------------------------------------------
    # Same 202 + reqid + poll shape as /api/config, for the same reason: the
    # commands have to run on the main loop, not the web server's task. The
    # difference is that results stream -- a pasted sequence fills the terminal
    # command by command instead of appearing all at once at the end.
    def _cli_post(self):
        raw = self._read_body()
        if len(raw) > 8192:
            return self._json(413, {"error": "body too large"})
        try:
            body = json.loads(raw or b"{}")
        except ValueError:
            return self._json(400, {"error": "bad json"})
        reqid = body.get("reqid", "")
        if not valid_reqid(reqid):
            return self._json(400, {"error": "bad reqid"})
        cmds = body.get("cmds")
        if not isinstance(cmds, list) or not cmds:
            return self._json(400, {"error": "no commands"})
        if len(cmds) > CLI_MAX_CMDS:
            return self._json(413, {"error": "too many commands", "max": CLI_MAX_CMDS})
        cmds = [str(c).replace("\r", "").replace("\n", "").strip() for c in cmds]
        cmds = [c for c in cmds if c]
        if not cmds:
            return self._json(400, {"error": "no commands"})
        for c in cmds:
            if len(c) > BATCH_CMD_SIZE - 1:
                return self._json(400, {"error": "command too long"})
            why = cli_unavailable(c)
            if why:
                return self._json(400, {"error": why})
        # Same invariant handleConfigPost enforces (see wcCliUnavailable's
        # neighbour in WebConfigServer.cpp): first onboarding is committed by the
        # reboot, and must not commit the factory password onto someone's LAN.
        if (ST.setup_mode and ST.initial_setup and not ST.admin_pwd_set
                and not any(c.startswith("password ") for c in cmds)
                and (any(c.startswith(CLI_DEFERRED_REBOOT) for c in cmds)
                     or any(c.startswith("set wifi.ssid ") for c in cmds))):
            return self._json(400, {"error": "admin password required for initial setup — "
                                             "run `password <new-password>` first"})

        with ST.lock:
            self._cli_advance(ST.cli)
            if ST.cli.get("state") != "idle" and ST.cli.get("reqid") == reqid:
                return self._json(202, {"state": ST.cli["state"], "reqid": reqid,
                                        "total": len(ST.cli["cmds"])})
            if ST.cli.get("state") == "running":
                return self._json(409, {"error": "busy", "reqid": ST.cli.get("reqid", "")})
            ST.cli = {"state": "running", "reqid": reqid, "cmds": cmds, "results": [],
                      "all_ok": True,
                      "reboot": any(c.startswith(CLI_DEFERRED_REBOOT) for c in cmds),
                      "next_at": time.time() + CLI_CMD_SECS}
            return self._json(202, {"state": "running", "reqid": reqid, "total": len(cmds)})

    @staticmethod
    def _cli_advance(job):
        """Run whichever queued commands are now due. Execution belongs to the
        node's loop, not to the client's polling — a client that walks away must
        not leave the executor claimed forever."""
        now = time.time()
        while (job.get("state") == "running" and len(job["results"]) < len(job["cmds"])
               and now >= job["next_at"]):
            cmd = job["cmds"][len(job["results"])]
            if cmd.startswith(CLI_DEFERRED_REBOOT):
                reply = "OK - reboot queued"
            else:
                _, reply = run_cli(ST.cfg, cmd)
                if cmd.startswith("password "):
                    reply = "OK"      # never echo the new password back
                    ST.admin_pwd_set = True
                elif cli_reads_secret(cmd):
                    val = reply[2:] if reply.startswith("> ") else reply
                    reply = ("> (not set)" if val in ("", "(not set)")
                             else "> ******** (serial only)")
            ok = not cli_reply_is_failure(reply)
            # Only writes gate the reboot, and only on the "OK" convention every
            # setter keeps (WebConfigBatch::cliReplyGatesReboot).
            if cmd.startswith(("set ", "password ")):
                job["all_ok"] = job.get("all_ok", True) and reply.startswith("OK")
            # The command is NOT echoed: it may carry a password or token, and
            # the client matches results to its own sequence by index.
            job["results"].append({"ok": ok, "reply": reply})
            job["next_at"] = now + CLI_CMD_SECS
        if job.get("state") == "running" and len(job["results"]) == len(job["cmds"]):
            job["state"] = "done"     # stays readable until the next POST

    def _cli_result(self):
        query = parse_qs(urlsplit(self.path).query)
        reqid = query.get("reqid", [""])[0]
        if not valid_reqid(reqid):
            return self._json(400, {"error": "bad reqid"})
        # `from` lets the client ask only for results it has not rendered yet,
        # so a long sequence isn't re-sent on every poll.
        try:
            frm = max(0, int(query.get("from", ["0"])[0]))
        except ValueError:
            frm = 0
        with ST.lock:
            j = ST.cli
            if j.get("state") == "idle":
                return self._json(200, {"state": "idle", "reqid": reqid})
            if j.get("reqid") != reqid:
                return self._json(404, {"error": "unknown request"})
            self._cli_advance(j)      # one command per CLI_CMD_SECS
            # Results stream, capped per read so the device's JSON document
            # stays small; a longer sequence pages across reads. "done" means
            # the client has been handed everything, not just that execution
            # finished — a client that stops polling at "done" must lose nothing.
            page = j["results"][frm:frm + CLI_RESULT_PAGE]
            final = j["state"] == "done" and frm + len(page) >= len(j["cmds"])
            body = {"state": "done" if final else "running", "reqid": reqid,
                    "total": len(j["cmds"]), "from": frm, "results": page}
            if final:
                body["all_ok"] = j["all_ok"]
                body["reboot"] = j["reboot"] and j["all_ok"]
                if j["reboot"] and not j["all_ok"]:
                    body["reboot_withheld"] = True
            return self._json(200, body)

    def _scan(self):
        rescan = "rescan=1" in self.path
        now = time.time()
        if rescan or ST.scan_started is None:
            ST.scan_started = now
            return self._json(200, {"state": "scanning"})
        if now - ST.scan_started < SCAN_SECS:
            return self._json(200, {"state": "scanning"})
        return self._json(200, {"state": "done", "networks": SCAN_NETWORKS})

    def _stats(self):
        up = int(time.time() - ST.start)
        slots = []
        for i, s in enumerate(ST.cfg["mqtt"]["slots"]):
            if s["preset"] == "none":
                continue
            row = {"n": i + 1, "name": s["preset"], "state": "ok",
                   "ok": 100 + up, "err": 0}
            # Mirrors buildStatsJson(): "filt" carries the raw mask and is
            # omitted entirely for the all-types default.
            mask = packet_filter_mask(s.get("filter", "all"))
            if mask != 0xFFFF:
                row["filt"] = mask
            slots.append(row)
        return {
            "uptime_s": up, "batt_mv": 4020, "heap_free": 142000, "heap_min": 118000,
            "heap_max_alloc": 96000, "noise": -98, "rssi": -71, "snr": 9.5,
            "airtime_s": up // 20, "rx_airtime_s": up // 8, "recv": 512 + up,
            "sent": 88 + up // 3, "rx_err": 3, "sent_flood": 40, "sent_direct": 48,
            "recv_flood": 300, "recv_direct": 212, "tx_queue": 0, "mqtt_queue": 0,
            "wifi_rssi": -58, "ip": "192.168.1.42", "slots": slots,
        }


def main():
    global ST, PORT, MINIFY, FW_VERSION
    ap = argparse.ArgumentParser(description="Mock WebConfig portal backend")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--setup", action="store_true", help="first-boot setup wizard mode")
    ap.add_argument("--active-slots", type=int, default=5, help="server slots to expose (2 or 5)")
    ap.add_argument("--fw-version", default=FW_VERSION,
                    help="version string to report, shaped like build.sh's embedded one")
    ap.add_argument("--minify", action="store_true",
                    help="serve the comment-stripped page the device ships, not the source")
    args = ap.parse_args()
    ST, PORT, MINIFY = State(args), args.port, args.minify
    FW_VERSION = args.fw_version

    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    mode = "SETUP (wizard)" if args.setup else "LAN (login: %s)" % ADMIN_PASSWORD
    print("WebConfig mock backend — %s%s" % (mode, "  [minified]" if MINIFY else ""))
    print("  open http://localhost:%d/   (Ctrl-C to stop)" % args.port)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()

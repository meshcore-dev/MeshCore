# FindMy / OpenHaystack Beacon

This document describes the experimental FindMy locator beacon for nRF52 MeshCore nodes.

## Purpose

When enabled, a node advertises an Apple [FindMy](https://support.apple.com/find-my) /
[OpenHaystack](https://github.com/seemoo-lab/openhaystack) "offline finding" beacon alongside
its normal mesh duties. Nearby iPhones anonymously relay its encrypted location to Apple's
servers, letting you locate a deployed node through the global Find My network — no extra
infrastructure, GPS, or cellular needed.

This is particularly useful for repeater nodes. To maximise coverage they are often deployed in
exposed, unattended, hard-to-reach spots — rooftops, towers, hilltops, remote solar sites — which
makes them especially prone to going missing, whether through theft, weather, a failed mount, or
simply being forgotten. A FindMy beacon gives you a way to locate (or recover) such a node, or at
least get a last-known position, without having to physically visit the site.

The node only ever broadcasts a **public** advertising key. Decrypting the location reports
requires the matching **private** key, which you keep off-device on your own machine.

## Supported hardware

nRF52840 boards only (Adafruit Bluefruit BLE stack), but the implementation is easily ported to
ESP32 and other BLE-capable chips. The challenge is concurrent BLE usage, i.e. companion nodes.
**TODO / future work:** check whether the FindMy advertisement can be sent while a companion is
disconnected from the phone, and disabled again when it reconnects.

Pre-defined build environments:

| Environment | Board |
|-------------|-------|
| `RAK_4631_repeater_findmy` | RAK4631 / RAK WisMesh (repeater, incl. Solar Repeater Mini) |
| `LilyGo_T-Echo_repeater_findmy` | LilyGo T-Echo |

The feature is gated behind the `WITH_FINDMY_BEACON` build flag, so it adds nothing to other
builds. It is intended for always-on roles (repeater/sensor) where BLE is otherwise unused. It
is **not** for the phone-companion firmware, which needs BLE for its own link — a node can be a
FindMy beacon or a phone companion, not both at once.

## Key rotation

The node stores up to **365 advertising keys** and picks the active one from its clock, where
`now_utc` is the current UTC time in Unix-epoch seconds (so `now_utc / 86400` is the day number):

    slot = (now_utc / 86400) % count

so keys rotate **daily** and cycle every `count` days — the way an AirTag rotates. `count` is
whatever you provision, from **1 (a single static key)** up to 365. The server (holding the
matching private keys) derives the same per-day slot, so it always knows which key to query.

> **Rotation requires the node's clock to be set.** The slot is derived from UTC time, so the
> node must know the real date/time for it to pick — and stay in sync on — the right slot. Until
> the clock is set (it reads a time at/after the firmware build date) the node advertises **slot
> 0** and does not rotate; it begins rotating once the time is set (via the companion app, the
> `time`/`clock` CLI, or GPS). On boards without a battery-backed RTC the clock resets on every
> power loss, so either fit an RTC module or use a single static key (`count = 1`), which needs
> no clock. See [Notes and caveats](#notes-and-caveats).

Why rotate: genuine AirTags rotate their key daily and Apple's network is tuned for that. A
single never-changing key is more easily treated as anomalous and may have its reports throttled
over time. Rotating daily (e.g. `count = 365`, regenerated yearly or left to wrap) keeps it
looking normal. A single static key still works and is simplest; the trade-off is reduced
long-term reliability and that the node is linkable across the cycle when keys repeat.

Each fetch only needs the last ~7 keys (Apple keeps reports about a week), regardless of `count`.

## Generating keys

Use the integrated generator, which derives the whole set deterministically from one random seed
so it can be regenerated later:

```
python3 tools/findmy/genkeys.py --count 365 --out mytag      # needs: pip install cryptography
```

It writes to `mytag/`:

| File | Where it goes | Purpose |
|------|---------------|---------|
| `provision.txt` | the **node** (paste/pipe into serial) | `set findmy.add …` lines + `set findmy on` |
| `keys/*.keys` | your **server** | per-slot Private / Advertisement / Hashed keys for macless-haystack |
| `seed.txt` | keep **safe** | regenerates every key (`--seed <hex>`) |

Each key is a NIST P-224 keypair; only the **advertisement** (public, 28-byte) key goes on the
node, encoded into the BLE advert and MAC. The **private** key and **hashed adv key** stay on
your server (to decrypt reports and to query Apple). Never load a private key onto the device —
it would be broadcast publicly; note it is also 28 bytes, so the firmware cannot reject it by
length.

You can also generate keys with stock [macless-haystack](https://github.com/dchristl/macless-haystack)
/ [OpenHaystack](https://github.com/seemoo-lab/openhaystack) tooling and load the advertisement
keys by hand with the commands below.

## Node configuration

Configure over the local USB serial console (115200 baud) or remotely from the MeshCore app's
Command Line tab when logged in as admin. Configuration is stored in `/findmy` on the node's
internal filesystem (independent of the normal node preferences).

Commands:

```
set findmy.add <base64>           append a key in the next free slot
set findmy.key <index> <base64>   set/replace the key in slot <index> (0..364)
set findmy.clear                  erase all keys
set findmy on | off               enable / disable the beacon
get findmy                        status: enabled, key count, current slot, MAC
get findmy.key <index>            print the (public) advertisement key for a slot
get findmy.keys                   list all keys (local serial console only)
```

- Use `set findmy.add` to build the list without tracking indices — it appends at the current
  count and replies `OK - appended slot N (M keys)`. A single key (`set findmy.add …` once, then
  `set findmy on`) is the static case.
- `set findmy.key <i>` is the explicit form: it **replaces** slot `i` (for rotating one key out)
  or appends when `i == count`; slots stay **contiguous from 0**, so a gap (`i > count`) is
  rejected.
- Keys must decode to exactly 28 bytes, else `Error: decoded N bytes, expected 28`.
- Keys are public, so they can be read back: `get findmy.key <i>` returns one slot's base64 key
  (works remotely), and `get findmy.keys` dumps the whole list to the USB serial console.
- `get findmy` reports e.g. `> on, 365 keys, slot 142, mac DF:41:B5:D7:F3:BF, clock set`. It
  never echoes a key. The MAC is `key[0]|0xC0 : key[1] : … : key[5]` for the active slot. For a
  rotating set (`count > 1`) it also shows the **clock state** — `clock set`, or
  `CLOCK NOT SET - no rotation` if the node's time isn't valid yet.
- **Automatic daily rotation requires the node's time to be set.** Since the slot is derived from
  UTC, with `count > 1` the keys only advance once the clock is set; until then the node stays on
  slot 0. `set findmy on` warns if the clock isn't set, and `get findmy` shows it — use the
  `clock` / `time <epoch>` commands (or the companion app / GPS) to set it. A single static key
  (`count = 1`) needs no clock.
- Provisioning/enable changes apply at boot, so `reboot` (or power cycle) after them; on boot the
  node prints `FindMy beacon started`. Daily rotation after that is automatic and needs **no
  reboot** — the MAC and payload change live at the day boundary.

### Provisioning a full key set (USB)

The generator's `provision.txt` is a ready-to-send script. Pipe it into the node's serial port
(it starts with `set findmy.clear` and ends with `set findmy on`), then reboot:

```
while read l; do echo "$l"; sleep 0.2; done < mytag/provision.txt > /dev/ttyACM0
```

### Configuring over the air (OTA)

All commands also work remotely from the MeshCore app's admin Command Line, since each is one
short text packet. This is ideal for **rotating out a single slot** (`set findmy.key <i> <b64>`)
or toggling the beacon on a deployed node. Bulk-loading hundreds of keys over LoRa is impractical
— do the initial full provisioning over USB and use OTA for tweaks.

### Verifying

1. **On air:** scan with a BLE tool (e.g. nRF Connect). You should see a non-connectable
   advertiser at the address `get findmy` reported (address type *Random*) carrying Apple
   manufacturer data beginning `4C 00 12 19 …`.
2. **Key sanity:** run heystack's `tools/showmac.py` on the advertisement key you loaded — it
   must print the same MAC as `get findmy`. A mismatch means the wrong key was loaded.

## Retrieving location data

Use your own server with the **private** keys — the node cannot retrieve anything itself. Import
the generated `keys/*.keys` into [macless-haystack](https://github.com/dchristl/macless-haystack)
and query/decrypt reports with its scripts, e.g. `fetch_reports.py` (a `findmy.py`-style query),
which authenticates to Apple (via Anisette), pulls the encrypted reports for the hashed adv keys,
and decrypts them with the private keys. See the macless-haystack README for setup
(Anisette/Apple-ID requirements) and exact invocation.

With rotation you only need to query the **last ~7 days of slots** (Apple keeps reports about a
week). Compute the current slot as `(now_utc / 86400) % count` and query a couple of slots
either side of it to absorb clock skew between the node and your server.

## Notes and caveats

- **Latency:** Find My is deliberately latency-tolerant. Reports appear only after the node is
  near a passing iPhone and can take minutes to a few hours.
- **Power:** continuous BLE advertising keeps the SoftDevice awake, so idle current is higher
  than a node with BLE off. The advertising interval defaults to ~2 s; override with
  `-D FINDMY_ADV_INTERVAL=<0.625ms-units>`. Rotation itself is cheap: the clock is read only about
  once an hour (not every loop), so a hardware RTC isn't polled over I2C continuously. The day
  rollover need not be precise; tune with `-D FINDMY_CHECK_INTERVAL_MS=<ms>`.
- **Clock dependency:** rotation relies on the node's UTC clock matching the server's. A day of
  skew just shifts which slot is "today" — query a slot either side on the server to absorb it.
  Until the clock reads a time at/after the firmware build date it is treated as **unset** and
  the node advertises **slot 0** (then switches to the correct slot once time is set). Boards
  without a battery-backed RTC (e.g. a bare RAK4631) reset their clock on every power loss and
  rely on the companion app / `time` CLI / GPS to set it — for an unattended or solar node that
  may power-cycle, fit an RTC module (e.g. RAK12002) or use a single static key (`count = 1`),
  which is immune to clock state.
- **Privacy / linkability:** with `count > 1` the node looks like a normally-rotating device, but
  keys repeat once the cycle wraps (every `count` days), so it is linkable across that period. A
  single static key (`count = 1`) is always linkable and more likely to be throttled long-term.
- **Storage / RAM:** the key table is held in RAM (`FINDMY_MAX_KEYS` × 28 B ≈ 10 KB at the
  default 365) and persisted to `/findmy`. Lower `FINDMY_MAX_KEYS` to shrink it if needed.
- **Future work:** true AirTag-style derivation (store a seed + primary public key and derive
  each slot's key on-device with P-224 point math) would remove the key table and the yearly
  re-provision, at the cost of on-device EC. Today's scheme uses a precomputed per-day list.
- **OTA:** the beacon coexists with `start ota` — triggering a firmware update reuses the
  running BLE stack and switches to the DFU advertiser automatically.

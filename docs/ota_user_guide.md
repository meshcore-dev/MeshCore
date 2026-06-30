# Updating your node over the air (OTA) — user guide

This guide is for **node operators**: how to update your MeshCore device's firmware over the radio, in
plain language. No cables, no programmer — your node can download a new firmware from a neighbour and
install it. (For the technical wire format, see [the OTA protocol spec](ota_protocol.md).)

> **Is my node supported?** OTA works on **ESP32** boards (e.g. Heltec V3) and on the **RAK4631** (nRF52,
> which needs the special MeshCore bootloader). Other boards build fine but can't self-update yet.

---

## The important part first: it's safe

- **Nothing installs by itself.** Your node can *discover* and *download* an update in the background, but
  it only **installs** when you say so (unless you deliberately turn on auto-install — see below).
- **Bad downloads can't sneak in.** Every piece of the firmware is checked against a cryptographic
  fingerprint as it arrives, and the whole image is verified again before install. A corrupt or tampered
  download is rejected, not installed.
- **You choose who to trust.** Updates can be *signed* by their author. You can tell your node to only
  auto-install firmware signed by keys you've added.
- **It won't disrupt your mesh.** OTA traffic is always the **lowest priority** — your node only spends
  spare airtime on it. Messages and routing always come first; a busy node simply updates later. Think of
  it as *"eventually upgradable."*
- **It can recover.** If an install ever fails, the node falls back to a safe recovery mode (you can
  re-flash a known-good firmware over USB) — it won't be left bricked.

---

## How to talk to your node

Connect to your node's **console** — usually a USB serial terminal at **115200 baud** (or whatever tool
you already use to manage the node). You type `ota ...` commands and the node replies in plain words.

The commands have short, friendly names (and most accept aliases, so you don't have to remember exact
spelling): type **`ota help`** any time to see the list, or just **`ota`** for a status summary.

---

## Common tasks

### 1. See what I'm running and whether anything is going on

```
ota status
```

Shows your current firmware version, your node's update "target" (its hardware/role id), and whether a
download is in progress.

### 2. Find updates available near me

```
ota ls
```

Your node asks around and lists the firmware updates other nodes nearby are offering, in plain words —
each with a **number**, its version, whether it's a full image or a small delta, how many nodes have it,
and how recently it was seen. For example:

```
Updates nearby (2 src) — `ota get <#>` to download:
 1) v1.2.3 delta [yours] 3n 5s
 2) v1.2.0 full [other hw] 1n 12s [downloading]
```

Each row shows the version, full-vs-delta, **whether it fits your node**, how many nodes have it, and how
long ago it was seen. The fit marker:

- **[yours]** — built for your exact hardware **and** role; safe to install.
- **[other hw]** — a different board or role (e.g. a companion image, or another board). Don't install it.
- **[?]** — can't tell (a build with no target id set, e.g. a bare IDE build rather than a release build).

Run it again after a few seconds — discovery happens in the background, so the list fills in. Nothing is
downloaded yet; this is just looking around. (`ota neighbors` / `ota updates` also work.)

### 3. Download an update

Pick one from the list by its **number**:

```
ota get 1
```

The node starts fetching it in the background, **at low priority**, a piece at a time — possibly from
several neighbours at once. Check progress any time with `ota status` (you'll see it climb, e.g.
`download: downloading 120/525 (23%)`). You can keep using your node normally meanwhile.

To **stop** a download you no longer want:

```
ota cancel
```

### 4. Install a downloaded update

Once `ota status` shows the download is **ready to install**:

```
ota install
```

The node verifies the firmware one last time, and if everything checks out it installs it and **reboots
into the new version**. If the check fails, it tells you why and does **not** install. (If you haven't
added the signer's key, an unsigned/untrusted image will only install with this explicit command — never
automatically.)

After it reboots, run `ota status` to confirm the new version.

### 5. If something goes wrong

- A download that stalls or gets interrupted just **resumes** later, or you can `ota cancel` and try again.
- If an **install** fails, the node won't boot a broken image — it lands in **recovery mode**:
  - **RAK4631 / nRF52:** it appears as a USB drive; drag a known-good firmware `.uf2` onto it to recover.
  - **ESP32:** it keeps the previous firmware in the other slot and rolls back.
- When in doubt, you can always re-flash over USB the normal way.

---

## Optional: let it update automatically

By default your node only *discovers* updates — it won't download or install on its own. If you want more
automation (e.g. for a remote node you can't easily reach), you can opt in. These settings are saved.

```
ota config autofetch any        # auto-DOWNLOAD any compatible update for this node (still won't install)
ota config autofetch signed     # auto-download only signed updates
ota config autofetch off        # back to manual (default)

ota config autoinstall trusted  # auto-INSTALL a downloaded update IF it's signed by a key you trust
ota config autoinstall off      # never auto-install (default)

ota config advert 1440          # re-advertise this node every N minutes (default 1440 = 24h)
ota config advert 0             # disable periodic re-advertise (still advertises briefly at boot)

ota config                      # show the current settings
```

Recommended for most people: leave both **off** and update by hand. Use `autoinstall trusted` only once
you've added the signer's key (next section) and you trust them to push updates unattended.

---

## Optional: only trust updates from specific people

If you'll use auto-install, tell your node which signing keys to trust. The firmware author shares their
**public** key (a hex string); you add it:

```
ota key add <public-key-hex>    # trust this signer
ota key list                    # show trusted signers
ota key rm <public-key-hex>     # stop trusting one
```

Only updates signed by a trusted key are eligible for auto-install. Manual `ota install` still lets you
install anything yourself, on your own responsibility.

---

## Sharing updates with others (advanced)

### Relay a folder of firmware from a computer

If your node is connected to a computer (e.g. a gateway on a Raspberry Pi), it can **hand out** a whole
folder of firmware files to the mesh — without storing them itself. Useful for seeding a new release to a
remote area.

1. Put the firmware files (`.mota` files — see below) in a folder on the computer.
2. Build the helper tool once (`tools/motatool/`), then point it at your node and the folder — over the
   node's **USB serial**, or over **WiFi** if it's an ESP32 companion on your network:
   ```
   cmake -S tools/motatool -B tools/motatool/build && cmake --build tools/motatool/build
   # over USB serial:
   ./tools/motatool/build/motatool serve --dir ./my_firmware/ --serial /dev/ttyACM0 -v
   # …or over WiFi (ESP32 companion): the seeder is on a DEDICATED port (5001), separate from the
   # phone-app port (5000), so a phone can stay connected while you serve:
   ./tools/motatool/build/motatool serve --dir ./my_firmware/ --tcp 192.168.1.50:5001 -v
   ```
   It answers the node's requests; your node then advertises those updates to neighbours, who can
   `ota get` them like any other. (A WiFi node prints its IP + seeder port to the serial log on connect.
   Details: [tools/motatool/README.md](../tools/motatool/README.md).)

To stop, just stop the daemon — over WiFi the node auto-detaches when the connection closes; over USB you
can also run `ota folder off` on the node. `ota folder` on its own lists what your node is offering.

### Everyone helps share

You don't have to be a gateway to help. Once **any** node finishes downloading an update, it automatically
offers it to *its* neighbours too. So a new firmware spreads outward node-to-node, instead of everyone
hammering the one node that had it first — and no node is ever overloaded, because all of this stays
lowest-priority.

---

## Where firmware files come from

OTA distributes **`.mota`** files — a packaged, verifiable firmware image (full image or a small "delta"
that only contains what changed). You get them by:

- **Downloading a build.** This fork publishes a rolling **`dev-latest`** release on GitHub with the
  current firmware for many boards, each accompanied by a `.full.mota` and a tiny `.delta.mota`. Grab the
  one for your board to test.
- **Building your own** with the `mota` packaging tool — see [tools/mota/README.md](../tools/mota/README.md)
  (this is for people distributing updates, not everyday operators).

---

## Quick reference

| I want to… | Command |
|---|---|
| List all commands | `ota help` |
| See my firmware + any download | `ota status` (or just `ota`) |
| Find updates nearby | `ota ls` |
| Download update #1 | `ota get 1` |
| Cancel a download | `ota cancel` |
| Install a finished download | `ota install` |
| Turn on auto-download | `ota config autofetch any` |
| Turn on auto-install (trusted only) | `ota config autoinstall trusted` |
| Trust a signer | `ota key add <hex>` |
| Relay a folder (gateway) | `ota folder on` + the seeder daemon |
| List what I'm offering | `ota folder` |

(Older names still work too: `neighbors`/`updates` = `ls`, `pull` = `get`, `applydelta`/`apply` = `install`, `drop`/`stop` = `cancel`.)

---

## A few terms

- **Firmware** — the software running your node. Updating it can add features or fix bugs.
- **`.mota`** — a packaged firmware update file, with built-in integrity checks.
- **Target** — your node's hardware + role identity. Your node only auto-fetches updates built for the
  same target, so it won't grab firmware meant for a different board.
- **Delta** — a small update containing only the changes from your current firmware (faster to send than a
  full image). Your node rebuilds the complete firmware from it and verifies the result before installing.
- **Signed** — the update carries the author's cryptographic signature, so you can verify who made it.

For the full technical details (the file format and the radio protocol), see
[the OTA protocol spec](ota_protocol.md).

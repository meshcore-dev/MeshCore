# MeshCore OTA — `.mota` container & LoRa protocol

This is the **single source of truth** for MeshCore's over-the-air firmware update system ("mOTA"). It is
written for developers who want to implement an interoperable peer (server, fetcher, relay, or host tool)
in another codebase or project. Everything below is implemented and hardware-verified in this repository;
where a section names a source file, that file is the authoritative reference for byte-level details.

> **Just want to update your node?** See the plain-language [OTA user guide](ota_user_guide.md) — this
> document is the technical/wire specification.

**Design goals**

- Distribute firmware over LoRa as a **self-verifying, resumable, BitTorrent-style block transfer** that
  survives reboots and never auto-applies without explicit consent.
- **Trustless transport / relay:** any node may carry or relay any block; integrity is content-addressed
  against a signed merkle root, so a relay need not be trusted and never needs the signing keys.
- **Lowest priority, always:** OTA traffic is enqueued behind all mesh traffic — "eventually upgradable".
  A busy node delays OTA indefinitely rather than competing with real traffic.
- **Portable:** the engine (`src/helpers/ota/OtaManager`) is Arduino/radio/crypto-free and host-testable,
  so the same logic drives a device, a simulation, or a third-party implementation.

**Source map** (all under `src/helpers/ota/` unless noted)

| Concern | File |
|---|---|
| Constants, enums, flags | `OtaFormat.h` |
| Container/manifest parse | `MotaContainer.{h,cpp}` |
| Merkle tree + proofs | `MerkleTree.{h,cpp}` |
| EndF self-identity | `FirmwareInfo.{h,cpp}` |
| Wire message codec | `OtaProtocol.{h,cpp}` |
| Session engine (serve+fetch+discovery) | `OtaManager.{h,cpp}` |
| Multi-mota / folder relay | `OtaSource.h`, `MotaSourceSerial.{h,cpp}`, `MotaSeederProto.h` |
| Staging stores | `OtaStore.h`, `OtaStoreFlashNrf52.*`, `OtaStoreFlashEsp32.*` |
| Apply | `OtaApply.*`, bootloader `Adafruit_nRF52_Bootloader_OTAFIX` |
| Device glue (CLI/context) | `OtaCli.cpp`, `OtaContext.h` |
| Host tooling | `tools/motatool/` (C++ CLI: build/verify/inspect/serve); `tools/mota/` (Python reference lib `motalib.py` + build/test glue) |

---

## 1. Conventions

- **Endianness:** all multi-byte integers are little-endian unless stated.
- **Hashes (multihash):** the hash family is declared once per manifest via `hash_algo` =
  `0x12` = **SHA-256** (the [multihash](https://github.com/multiformats/multihash) code for sha2-256).
  Truncations used:
  - `sha2-256:4` — first 4 bytes of the SHA-256 digest. Merkle leaves, internal nodes, root, proofs,
    `manifest_id`, and the discovery `set_digest`.
  - `sha2-256:8` — first 8 bytes. Base-firmware identity (`base_hash`, `EndF.body_hash`).
  - `sha2-256:32` — full digest. The image security anchor (`image_hash`).
  Digests are stored **bare** (just the truncated bytes); the family is implied by `hash_algo`.
- **Signatures:** Ed25519 (RFC 8032), 64-byte detached signature, 32-byte public key.

**Reference constants** (`OtaFormat.h`):

| Name | Value | ASCII / note |
|---|---|---|
| Container `MAGIC` | `6D 4F 54 41` | `mOTA` |
| Container `TRAILER` | `76 6B 34 39 36` | `vk496` |
| `EndF` marker | `45 6E 64 46` | `EndF` |
| `hash_algo` (sha2-256) | `0x12` | multihash code |
| `format_ver` | `0x02` | this spec |
| `approval` = not approved | `FF FF FF FF` | erased NOR word |
| `approval` = approved | `41 50 52 56` | `APRV` |
| `MFLAG_FULL` | `0x01` | flags bit0 |
| `MFLAG_SIGNED` | `0x02` | flags bit1 |
| `CODEC_FULL` / `_SEQUENTIAL` / `_INPLACE` | `0` / `1` / `2` | §5 |
| `PAYLOAD_TYPE_OTA` | `0x0C` | MeshCore packet type (`src/Packet.h`) |
| `MAX_PACKET_PAYLOAD` | `184` | usable bytes per packet (`src/MeshCore.h`) |
| Default block size | `1024` | `block_size_log2 = 0x0A` |
| OTA TX priority | `250` | lowest (`OTA_TX_PRIORITY`, `src/Mesh.h`) |

---

## 2. Firmware image & the `EndF` trailer

Every OTA-capable build appends a fixed **56-byte** `EndF` trailer to its flashed image so a running node
can discover its own size **and self-describing identity** on any MCU (no linker symbols needed). Every
field is always present at a constant offset. Implemented by `FirmwareInfo.cpp`; appended at build time by
`tools/mota/pio_endf.py` (post-build hook).

```
flashed image = BODY (image bytes) || EndF trailer
EndF trailer (fixed 56 bytes):
  off 0   4   "EndF"        45 6E 64 46
  off 4   4   body_len      uint32 LE — length of BODY (excludes the whole trailer)
  off 8   8   body_hash     sha2-256:8 of BODY
  off 16  4   fw_version    uint32 LE, packed MAJOR<<24|MINOR<<16|PATCH<<8|pre  (0 = unknown)
  off 20  4   target_id     uint32 LE — sha2-256:4(pio_env): hardware + role + partition (fetch routing)
  off 24  32  hw_id         NUL-padded ASCII hardware tag (brick-safety), e.g. "RAK4631" ("" = unknown)
```

- **Self-describing identity.** `pio_endf.py` computes `target_id` from the PlatformIO env name itself (so
  it's correct even without `build.sh`'s `-D MOTA_TARGET_ID`), `hw_id` from `MOTA_HW_ID`, and `fw_version`
  from `FIRMWARE_VERSION`. The device reads them back (`ota_self_firmware()`), so a node's advertised
  identity is correct regardless of how it was built — and the packaging tool reads them straight from a raw
  `.bin` (no `--target-env`/`--fw-version` flags, no reliance on filenames; §9, §13). A dev build with no
  dotted version simply carries `fw_version = 0` / empty `hw_id` (= unknown) — still a full 56-byte trailer.
- **Size discovery:** scan flash from the partition top downward for the `EndF` marker; the byte before it
  is the last BODY byte (the trailer is always 56 bytes). See `ota_self_firmware()`.
- **Delta base matching:** a node's `body_hash` is read directly from its own `EndF`; a delta's `base_hash`
  (§5) must equal it. `body_hash` is over BODY only.
- **No circularity:** `EndF` hashes only the BODY, never itself.

The "reconstructed image" referenced by the manifest is the full `BODY || EndF` (what gets flashed).

> **Implementer note:** the bootloader (and any non-Arduino consumer) MUST locate the body extent by
> scanning for `EndF`, never by trusting a stored size — see the bootloader contract in §12.

---

## 3. The `.mota` container

The distributed form (host-built, wire-transferred). Parsed by `mota_parse()` in `MotaContainer.cpp`.

```
off            size   field
0              4      MAGIC = 6D 4F 54 41
4              4      MOTA_TOTAL_SIZE  uint32 LE — total container bytes (incl. manifest, leaves[],
                                       payload, trailer). Lets a node pre-reserve staging and compute
                                       write_start = staging_region_end − MOTA_TOTAL_SIZE.
8              M      MANIFEST         (§4; M = 197 fixed + leaves[], 4*BC; no length field — BC from payload_size)
8 + M          P      PAYLOAD          (payload_size bytes; delta or full image)
8 + M + P      5      TRAILER = 76 6B 34 39 36
```

`MOTA_TOTAL_SIZE = 4 + 4 + M + P + 5`. The manifest `M` **includes** `leaves[]`; the manifest-minus-leaves
prefix (`mfl`, sent over the wire as `OTA_MANIFEST`) is `[8, leaves_off)`.

**Staged (in-flash) form.** Written bottom-aligned so `TRAILER` ends at `staging_region_end`. Identical
bytes, except the device mutates two regions in place (both NOR-safe, no re-erase): the `leaves[]` slots
(filled as blocks arrive — §7) and the 4-byte `approval` field (on owner consent — §4.2). Everything else
is immutable.

---

## 4. The manifest

**Fixed layout.** Every field sits at a constant offset and is always present — `base_hash`,
`signer_pubkey` and `signature` are zero-filled when not applicable (a full image / an unsigned container).
Only `leaves[]` is variable (one 4-byte hash per block). So the manifest-minus-leaves (`mfl`) is **always
197 bytes** and the parser is plain offset reads — no conditionals. Parsed by `mota_parse_manifest()`.

```
off  size   field            notes
0    1      format_ver       = 0x02
1    1      flags            bit0 FULL (0=delta/partial, 1=full image); bit1 SIGNED; bits2-7 reserved 0
2    1      hash_algo        0x12 = sha2-256
3    4      target_id        device/arch/role discriminator (§9)
7    4      fw_version       MAJOR<<24 | MINOR<<16 | PATCH<<8 | pre   (comparable uint32)
11   4      image_size       size of the reconstructed image (BODY||EndF)
15   4      payload_size     PAYLOAD bytes in this container
19   1      block_size_log2  e.g. 0x0A = 1024
20   4      merkle_root      sha2-256:4 over PAYLOAD blocks (§6) — also the manifest_id
24   32     image_hash       sha2-256:32 of the reconstructed image — SECURITY anchor
56   1      codec_id         0=full/raw, 1=detools-sequential, 2=detools-in-place
57   32     hw_id            NUL-padded ASCII hardware tag (e.g. "RAK4631"); same tag => bootable-compatible.
                             SIGNED. Applier refuses a mismatch (brick-safety); empty on either side = skip.
89   8      base_hash        sha2-256:8 of the BASE image's BODY (== that build's EndF.body_hash). 0 if FULL.
97   32     signer_pubkey    Ed25519 public key. 0 if not SIGNED.
129  64     signature        Ed25519 over manifest[0, 129). 0 if not SIGNED.
193  4      approval         FF FF FF FF = not approved; 41 50 52 56 ("APRV") = approved
--- end of manifest-minus-leaves: mfl = 197 (constant); leaves_off = 8 + 197 = 205 in the container ---
197  4*BC   leaves[]         BC = ceil(payload_size / 2^block_size_log2). sha2-256:4 each (the only variable field)
```

The signature always covers `manifest[0, 129)` (the head + `base_hash` + `signer_pubkey`). `approval` is
outside the signed region so it can be flipped in place on consent without breaking the signature.

Manifest-minus-leaves size (`mfl`) is a constant **197 bytes** for every container (full or delta, signed
or unsigned). At 197 bytes the manifest exceeds one packet, so `OTA_MANIFEST` is always sent multi-fragment
(§8.4, 2 fragments) and reassembled by the fetcher.

### 4.1 Signed region

`signature` covers manifest bytes `[0, 129)` — the head + `base_hash` + `signer_pubkey`. It does **not**
cover `approval` or `leaves[]`:

- `leaves[]` are verified against the signed `merkle_root` (§6), so they need no separate signature.
- `approval` is device-local consent (§4.2), deliberately outside the signature.

### 4.2 The `approval` field

- Distributed and **forced on ingest** to `FF FF FF FF` (a peer can never pre-approve).
- The local owner's `ota applydelta` writes `41 50 52 56` (`"APRV"`) — a single NOR-safe write (only clears
  bits from the erased word). Any partial/other value reads as not-approved (fail-safe).
- Bound to this image (lives in this `.mota`'s manifest, re-erased when a new `.mota` is staged).
- A **consent** marker, not a security primitive. Authenticity = `signature` + `image_hash` + `hw_id`.

---

## 5. Payload, codecs & delta base

`PAYLOAD` is either the full reconstructed image (`FULL`) or a delta (`!FULL`).

| `codec_id` | Meaning | Used by |
|---|---|---|
| 0 | full / raw | PAYLOAD = reconstructed image (`BODY‖EndF`). ESP32 A/B (and any board for a full image). |
| 1 | detools **sequential** | random read of base + sequential write of result → ESP32 A→B inactive slot. |
| 2 | detools **in-place** | bounded scratch; rewrites the app region in place → nRF52 single-slot. |

For deltas, `base_hash` = the base build's `EndF.body_hash` (sha2-256:8 of its BODY). A node applies a
delta only if `base_hash` matches its own `EndF.body_hash`. After applying, the result MUST hash
(sha2-256:32) to `image_hash` before it is booted — the hard security gate.

**A fetcher only requests firmware it can apply.** Each node declares the codec(s) it can apply
(`set_apply_codec`/`set_apply_codec2`): ESP32 accepts `full` + `sequential` (+ `in-place`), nRF52 accepts
`full` + `in-place`. `CODEC_FULL` is always acceptable. A `.mota` with an unsupported codec is rejected at
discovery time, before any blocks are requested.

Compression is internal to the detools patch and must be supported by the applier. Patches are produced by
**detools 0.53.0** (`tools/mota` → `detools.create_patch`) and decoded on-device by detools' embeddable C
decoder, vendored verbatim at `src/helpers/ota/detools/` (see its `README.meshcore.txt`). That build
enables only the self-contained `NONE` + `CRLE` compressions (no malloc/liblzma/heatshrink), so MeshCore
deltas use `--compression crle`. **Do not reimplement the codec** — use the vendored decoder.

---

## 6. Merkle tree (sha2-256:4)

Verifies each PAYLOAD block against the signed `merkle_root` **before** the whole payload exists, so
corruption/forgery is localized to a block. Implemented in `MerkleTree.cpp`.

- **Blocks:** PAYLOAD splits into `BC = ceil(payload_size / B)` blocks, `B = 2^block_size_log2` (default
  1024). The last block is its real length (**no zero padding**).
- **Leaf:** `leaves[i] = sha2-256:4( block_i_bytes )`.
- **Internal node:** `node = sha2-256:4( left ‖ right )` (4+4 input bytes).
- **Odd level:** an odd count promotes the **last node unchanged** to the next level (no duplication).
- **Root:** reduce until one node remains. `BC == 1` → root = `leaves[0]`. `BC == 0` is invalid.

### 6.1 Proofs

A proof for block `i` is the ordered list of sibling digests from leaf to root. Promoted levels contribute
**no** element. Verification (needs `BC` to know the tree shape):

```
h = leaf_i ; idx = i ; n = BC ; p = 0
while n > 1:
    if (n is odd) and (idx == n-1):        # this node was promoted
        pass
    else:
        sib, side = proof[p] ; p += 1
        h = sha2-256:4( sib ‖ h ) if side==left else sha2-256:4( h ‖ sib )
    idx //= 2 ; n = (n + 1) // 2
accept iff h == merkle_root and p == len(proof)
```

Over LoRa, `leaves[]` are **omitted** from the manifest transfer; a serving node computes a block's proof
on demand from its stored `leaves[]` (`OTA_REQ_PROOF`/`OTA_PROOF`, §8.5), and the fetcher fills its own
`leaves[i]` as each verified block lands.

---

## 7. Block availability, staging & resume

There is no separate availability structure. **Block `i` is present ⟺ `leaves[i]` is non-erased**
(`!= FF FF FF FF`). Because `leaves[]` live in the staged flash region, availability **survives reboot**.

**Commit order per block (crash-safe):** (1) verify proof, (2) write block payload to its offset, (3) write
`leaves[i]` **last**. A power loss before step 3 leaves the slot erased → the block is simply re-fetched
(idempotent). On boot a node rebuilds an in-RAM present-bitmap by scanning `leaves[]`.

**Resume (`OtaManager::resumeStaged` + `OtaStore::checkpoint`/`reopen`):** an interrupted fetch resumes from
the staged container after a reboot — re-parse the stored manifest, recompute geometry, count present
blocks, continue fetching the holes (or jump straight to COMPLETE). The checkpoint cadence (persist progress
every N committed blocks) is runtime-tunable (`ota config checkpoint <N>`, 0 = only finalized containers
resume). Stores keep `leaves[]` in RAM until flush and never auto-GC, preserving resumable progress.

**Flash-store note (RX-safe writes):** a flash page-erase halts the CPU (~85 ms on nRF52) and starves LoRa
RX, so the flash stores (`OtaStoreFlashNrf52`/`OtaStoreFlashEsp32`) **coalesce writes to the erase unit**
(4 KB page / sector) and commit each once off the per-packet path — RAM stays O(one page), not O(image). A
small delta that fits page 0 does zero flash I/O until COMPLETE.

---

## 8. LoRa OTA protocol

Carried in MeshCore packets with **`PAYLOAD_TYPE_OTA = 0x0C`**. Every OTA packet payload is:

```
[0]    ota_msg_type      (OtaMsgType, OtaFormat.h)
[1..]  body              (fixed per type; encode/decode in OtaProtocol.cpp)
```

Message types:

| `ota_msg_type` | val | routing | purpose |
|---|---|---|---|
| `OTA_ADV`          | 0x01 | flood  | tiny per-node beacon (discovery tier 1) |
| `OTA_QUERY`        | 0x02 | flood  | ask a source for its catalog (discovery tier 2) |
| `OTA_HAVE`         | 0x03 | flood  | the catalog reply (fragmented, digest-tagged) |
| `OTA_GET_MANIFEST` | 0x04 | direct | request a manifest by `manifest_id` |
| `OTA_MANIFEST`     | 0x05 | direct | the manifest-minus-leaves, fragmented |
| `OTA_REQ`          | 0x06 | direct | request a window of blocks' DATA |
| `OTA_DATA`         | 0x07 | direct | one self-describing fragment of a block's data |
| `OTA_REQ_PROOF`    | 0x08 | direct | request the merkle proof for one block |
| `OTA_PROOF`        | 0x09 | direct | the merkle proof for one block |

- **`manifest_id`** = the manifest's `merkle_root` (4 bytes) — a compact content id present in every
  transfer message, so a multi-mota server dispatches each request to the right image.
- **Priority:** all OTA packets enqueue at `OTA_TX_PRIORITY = 250` (lowest). OTA never competes with mesh
  traffic; on a busy node it is delayed indefinitely.
- **Reliability is *eventual*:** the fetcher re-requests missing fragments/blocks after a timeout, possibly
  from a different peer. No hard ACKs, no global ordering.
- **Relay:** replies are flooded, so transparent relay needs no per-requester addressing, and the transfer
  is trustless (the fetcher verifies every block against the signed root). Any neighbor may serve any
  fragment it has.
- **Hop limit + duty cycle:** OTA floods accumulate one path-hash per relay (the mesh's flood routing). A
  node *accepts* a packet only if it arrived within `ota config hops` hops (default 3; `0` = direct only)
  and *relays* it only while still under that limit, appending its own hash. Relays are lowest-priority and
  are skipped when the packet pool runs low (the source retries), so heavy OTA can never monopolise a
  repeater's RAM or starve real traffic.

### 8.1 Two-tier discovery

Because a node may serve **many** mOTAs (its own firmware plus an external folder — §10), discovery is split
so the periodic beacon stays tiny regardless of catalog size:

**Tier 1 — `OTA_ADV` beacon** (10 bytes, constant). Flooded as a short burst at boot, then every
`advert_mins` minutes (default 24h; runtime-tunable via `ota config advert`, `0` disables the periodic
re-advertise). It is also emitted immediately whenever the served set changes (e.g. a `motatool` folder is
attached/detached), so peers learn about newly-available firmware without waiting for the next interval:

```
seeder_id[4]    advertiser node id = pubkey[0:4]; the QUERY address + distinct-source id
n_motas         uint8 — count of complete servable mOTAs (saturates at 255)
set_digest[4]   sha2-256:4 over the SORTED set of served manifest_ids (see below)
```

`set_digest` is a **content hash of the offering**, not a counter: canonical across nodes, and it changes
iff the set of served mids changes. A peer that has already catalogued this `{seeder, set_digest}` ignores
the beacon (steady state is query-free). For a single served mota, `set_digest = sha2-256:4(mid)`.

**Tier 2 — `OTA_QUERY` → `OTA_HAVE`** (on interest only):

```
OTA_QUERY  (flood):  seeder_id[4]  set_digest[4]  filter_target(uint32)   # filter_target 0 = everything
OTA_HAVE   (flood):  seeder_id[4]  set_digest[4]  frag_idx(1) frag_total(1) n_rows(1)  rows[]
  HaveRow (16 bytes, OTA_HAVE_ROW_BYTES): mid[4] target_id(4) fw_version(4) codec_id(1) flags(1) have_count(2)
```

`have_count` is how many blocks the advertiser currently holds (`== block_count` for a full copy, less for a
partial/in-progress source). It lets a fetcher see, per mid, **how many peers have it and at what progress**
— so it knows the firmware is on multiple peers and can trust the swarm (§8.6) rather than depend on one.

A node interested in a source's offering schedules a QUERY; the source replies with its full catalog as
`OTA_HAVE` rows (fragmented if they exceed one packet — up to 12 rows per fragment). The heavy manifest is
fetched per-mid only on commit (§8.3).

### 8.2 Anti-storm (mandatory at mesh scale)

If 50 neighbours all queried a new beacon at once, the mesh would collapse. Mitigations (gossip/mDNS
pattern), all in `OtaManager`:

- **`OTA_HAVE` is flooded and digest-tagged.** EVERY node that overhears it caches the rows **passively**
  (keyed by `{seeder, set_digest}`) — no query of its own needed.
- **Jittered query:** a peer needing a catalog schedules its `OTA_QUERY` after a random delay
  `OTA_QUERY_MIN_MS (300) + rand(OTA_QUERY_SPREAD_MS (4000))`, derived from `id ⊕ digest ⊕ self`.
- **Overhear suppression:** during the jitter window, overhearing *another* QUERY **or** a HAVE for the same
  `{seeder, set_digest}` CANCELS the pending query.

Net effect: a digest change costs ~1 query + ~1 HAVE flood mesh-wide; a stable mesh is query-free.

### 8.3 Fetch handshake

```
fetcher                                   server (any node that has the mid)
  OTA_GET_MANIFEST(mid)        ───────►
                               ◄───────   OTA_MANIFEST(mid, frag_idx, frag_total, bytes)   × frag_total
  (reassemble manifest, verify, compute geometry: BC, block_size, payload_size)
  for each missing block window:
    OTA_REQ(mid, start_block, count)  ►
                               ◄───────   OTA_DATA(mid, block_idx, frag_off, data) × (per block)
    (reassemble block from frag_off slices)
    OTA_REQ_PROOF(mid, block_idx) ────►
                               ◄───────   OTA_PROOF(mid, block_idx, n_proof, proof)
    (verify proof vs merkle_root → write block → write leaves[i])
  when all blocks present: verify full merkle_root + image_hash → COMPLETE
```

### 8.4 Message bodies (transfer)

All offsets after the 1-byte type. Encoders/decoders in `OtaProtocol.cpp`; constants in `OtaManager.h`.

```
OTA_GET_MANIFEST:  manifest_id[4]
OTA_MANIFEST:      manifest_id[4]  frag_idx(1)  frag_total(1)  bytes[]     # up to OTA_MF_FRAG=176 B/frag
OTA_REQ:           manifest_id[4]  start_block(uint16)  count(1)
OTA_DATA:          manifest_id[4]  block_idx(uint16)  frag_off(uint16)  data[]   # up to OTA_FRAG_DATA=160 B
OTA_REQ_PROOF:     manifest_id[4]  block_idx(uint16)
OTA_PROOF:         manifest_id[4]  block_idx(uint16)  n_proof(1)  proof[]   # n_proof × 4 bytes
```

- **Block ⇆ fragments:** a 1 KB block is split into self-describing `OTA_DATA` fragments. `frag_off` is the
  byte offset of `data` within the block, so the global position is `block_idx*block_size + frag_off` —
  a fragment is self-placing and may be requested from **any** peer (BitTorrent-style). The fetcher tracks a
  per-block slice bitmap and reassembles before requesting the proof.
- **Data and proof are separate phases.** `OTA_DATA` carries no proof; the proof is fetched once per block
  via `OTA_REQ_PROOF`/`OTA_PROOF` after the block's data is complete.

### 8.5 Sizing against `MAX_PACKET_PAYLOAD = 184`

| message | fixed overhead | payload/packet |
|---|---|---|
| `OTA_DATA` | 9 B (type+mid4+idx2+off2) | `OTA_FRAG_DATA = 160` → 7 frags per 1 KB block |
| `OTA_MANIFEST` | 7 B | `OTA_MF_FRAG = 176` → signed manifest ≈ 2 frags |
| `OTA_HAVE` | 12 B | 12 rows × 14 B per fragment |
| `OTA_PROOF` | 8 B | up to ~44 sibling digests (≫ any real tree) |

A served mota supports up to `OTA_MAX_BLOCK/4` leaves in the default 4 KB proof scratch (≤1024 blocks ≈ 1 MB
payload); larger self-images pass a bigger scratch buffer.

### 8.6 Swarm load distribution (don't hammer one seeder)

The discovery anti-storm (§8.2) stops 50 neighbours all *querying* one node. The same hazard exists for the
*transfer*: if one node has new firmware and 50 want it, naïve fetchers would all REQ the same blocks from
the same seeder. Because OTA is always lowest-priority (§8) the mesh won't collapse, but the transfer would
be needlessly slow and centralized. Mitigations (all in `OtaManager`, reusing the §8.2 jitter/suppress idea):

- **Overhearing fills holes for free.** Every fetcher accepts any *broadcast* `OTA_DATA` for its mid, not
  just data it requested. So within a broadcast neighbourhood, one peer's request serves everyone who hears it.
- **De-correlated requests.** A fetcher picks a **random** missing block (not lowest-first), so N fetchers
  don't lockstep on the same block; collectively they pull different blocks and everyone overhears them all.
  Each fetch also holds its first REQ a random `OTA_REQ_SPREAD_MS` so simultaneous starters don't burst together.
- **Request suppression.** Overhearing a peer's `OTA_REQ` for a block makes a fetcher spend its next REQ on a
  *different* block (`OTA_REQ_SUPPRESS_MS`) — the broadcast DATA will fill the overheard one anyway.
- **Sources multiply (the key to "don't pull one node"):**
  - **Re-seed after COMPLETE (epidemic).** A node that finishes a download advertises + serves it (it now has
    all blocks *and* leaves, so it serves DATA and proofs). The origin seeds a few peers, they seed the next
    ring, etc. — load on the origin drops from O(N) to ~O(log N). (Default `autoinstall=off` means a completed
    node lingers as a seeder until the operator applies.)
  - **Partial re-serve during the transfer.** A still-fetching node serves the **DATA** of blocks it already
    holds (not proofs — it may lack sibling leaves), so peers can source bytes from it, not only the origin.
- **Serve de-dup.** A holder about to serve a block it just overheard *another* holder broadcast suppresses
  its own send (`OTA_SERVE_SUPPRESS_MS`), so multiple sources of one mota don't duplicate-broadcast it.

All serving stays reactive and lowest-priority, so seeding never competes with real traffic — the system is
"eventually upgradable": a busy node simply delays OTA until it has spare airtime.

---

## 9. Identity, trust & versioning

- **`target_id`** (4 B): `sha2-256:4(pio_env_name)` (little-endian uint32). The env name uniquely captures
  hardware **and** role/partition, so a node auto-fetches only matching firmware (a companion image is not
  fetched onto a repeater even though it shares `hw_id`). It is **self-described in the firmware's EndF**
  (§2, written by `pio_endf.py`) and read via `ota_self_firmware()`, so it is correct on any build;
  `-D MOTA_TARGET_ID` / `MainBoard::getOtaTargetId()` is the fallback when no EndF identity is present.
  `tools/mota` reads it from the firmware's EndF (or `--target-env`). A manual `ota pull`/`want` can override
  target (deliberate role switch); the `hw_id` brick-safety gate (§4) still applies at apply time.
- **`target_id` vs `hw_id`** — complementary, not redundant: `target_id` is the fetch-routing key
  (hw + role + partition); `hw_id` is the human-readable brick-safety key (hardware only). Same board, two
  roles ⇒ same `hw_id`, different `target_id`.
- **Naming a `target_id` locally:** only the 4-byte `target_id` ever travels on the wire. To show *which*
  board/role a target is, a node (and `motatool`) reverse-looks-it-up in `src/helpers/ota/OtaTargets.h` —
  a generated `target_id → env-name` table covering every `ENABLE_OTA` env (`tools/mota/gen_targets.py`,
  resolved from `pio project config`). So `ota ls` can render `[Heltec_v3_repeater]` for a neighbour's
  beacon without the string being transmitted. Unknown ids show as `other hw` / `N/A`.
- **`fw_version`:** packed comparable uint32 (`MAJOR<<24 | MINOR<<16 | PATCH<<8 | pre`); also self-described
  in EndF. `ota ls` decodes it for display and flags each update `[yours]` / `[other hw]` / `[?]` by
  comparing the advertised `target_id` to the node's own.
- **`hw_id`:** 32-byte NUL-padded ASCII hardware tag inside the signed head. The applier refuses a `.mota`
  whose `hw_id` differs from the device's own tag (empty on either side = permissive). Brick-safety
  independent of signature.
- **Signing & allowlist:** a node keeps a runtime allowlist of trusted Ed25519 signer pubkeys (none embedded
  in firmware; `ota key add/list/rm`). A `.mota` is eligible for **auto-install** only if signed by an
  allowlisted key, the signature verifies, and `image_hash` matches; otherwise it is manual-apply only with
  explicit confirmation. **Transfer needs no trust** — blocks are content-addressed against the signed root.
- **Policies (persisted):** `autofetch` ∈ {off, any, signed} (default off) gates automatic block fetching of
  own-target adverts; `autoinstall` ∈ {off, trusted} (default off) gates auto-apply of a COMPLETE signed +
  allowlisted fetch. Conservative defaults: a fresh node discovers + announces but never fetches/installs
  without operator intent.
- **Supersession:** a newer version announced mid-download does not abort the in-progress transfer
  (finish-current).

---

## 10. Multi-mota serve & the external "folder" relay

A node serves a **set** of mOTAs: its own firmware plus, optionally, an external folder of `.mota` files it
relays without holding them in flash. To peers it simply "has N mOTAs"; the relay is trustless (fetchers
verify everything). The serve side (`OtaManager`) keeps a lightweight registry of what it advertises and two
resident "views": `view0` (its own firmware) and one on-demand view loaded from a source when a request
targets an external mota. Every fetch message carries `manifest_id`, so dispatch is a registry lookup.

### 10.1 The `MotaSource` abstraction (`OtaSource.h`)

Transport-agnostic provider of one or more complete `.mota` as random-access bytes. The same serve code
drives USB-serial, BLE, a WiFi URL list, an NFS/samba mount, etc. — only `read()` differs.

```cpp
struct MotaDesc {                      // catalog metadata + region offsets (no whole image in RAM)
  uint8_t mid[4]; uint32_t target_id, fw_version; uint8_t codec_id, flags;
  uint32_t total_size, leaves_off, block_count, payload_off, payload_size;
};
class MotaSource {
  virtual uint8_t count();                                  // # mOTAs offered
  virtual bool    describe(uint8_t idx, MotaDesc& out);     // metadata + offsets
  virtual bool    read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len);   // random-access bytes
};
```

To serve an external mota the node reads its manifest-minus-leaves + `leaves[]` into RAM (≤4 KB for ≤1024
blocks) and streams payload blocks from the source on demand; proofs are generated from the read leaves.

### 10.2 The `mota-seeder` transport (`MotaSeederProto.h`)

A `MotaSource` is fed by a host that serves a folder over the device's **USB serial** (the same console the
CLI uses — no extra hardware) or, on an ESP32 WiFi companion, over **WiFi (TCP)**. The host is the
self-contained C++ tool `tools/motatool/` (`motatool serve --serial <port>` / `--tcp <host[:port]>`, which
also builds + validates `.mota` and runs on small hardware). The device only emits request frames *while
actively serving a fetch*, and reads the reply synchronously, so over the shared USB console binary frames
coexist with the text CLI/logs (resync on magic + checksum). Little-endian, XOR-checksummed:

```
request  (device → host):  'M' 'S'  op(1)  args...                 xsum(1 = XOR of op+args)
response (host → device):  'm' 's'  op(1)  status(1)  payload...    xsum(1 = XOR of all prior)

OP_COUNT     0x01   args: -            → payload: count(1)
OP_DESCRIBE  0x02   args: idx(1)       → payload: MotaDesc wire (38 B)
OP_READ      0x03   args: idx(1) off(4) len(2)  → payload: len bytes
MotaDesc wire (38 B): mid[4] target_id(4) fw_version(4) codec(1) flags(1)
                      total_size(4) leaves_off(4) block_count(4) payload_off(4) payload_size(4)
status: 0 = OK, non-zero = error (out of range / past EOF).
```

Device CLI: `ota folder on` (attach + announce), `ota folder` (list), `ota folder off`. Build flag
`OTA_FOLDER_SERIAL` (default stream = console `Serial`; override `OTA_FOLDER_SERIAL_STREAM` + define
`OTA_FOLDER_SERIAL_BEGIN` for a dedicated UART). On an ESP32 WiFi companion the node also runs a second
`WiFiServer` on a **dedicated seeder port** (`OTA_SEEDER_TCP_PORT`, default `5001`), separate from the
companion app port (`TCP_PORT`, default `5000`) — so `motatool serve --tcp` can feed updates while a phone
app stays connected. The node auto-attaches the source when a seeder client connects and detaches when it
closes (no `ota folder on` needed over TCP). Verified on hardware: a RAK4631 relays a host folder to a
Heltec V3 over one USB cable, and a host feeds a Heltec V3 over WiFi (`:5001`) while the companion serves a
phone on `:5000` — every block merkle-checked.

**Transport-agnostic by design.** The request/response *semantics* (`COUNT` / `DESCRIBE(idx)` /
`READ(idx, off, len)` over a folder catalog) are independent of the link. The 2-byte magic + XOR checksum +
resync framing above exists for the shared USB-UART (an unframed byte stream); it is harmless over a
reliable stream and the **WiFi (TCP)** transport reuses it as-is — both ends just treat the socket as a
byte stream (on-device, `SerialMotaSource` runs verbatim over an Arduino `Stream`-compatible `WiFiClient`;
`motatool`'s `TcpTransport` mirrors its `SerialTransport`). A future framed link such as **BLE GATT** (an
Android phone relaying a folder) could carry the same ops with no magic/checksum at all — a request
characteristic write delivers `op + args`, the reply notifies `status + payload`. `motatool` reflects this
split: a transport-free `SeederCore` (the catalog logic) under a swappable framing/transport layer.

---

## 11. CLI surface (`OtaCli.cpp`)

User-facing OTA data should travel via `CMD_OTA_*` companion binary frames; the text CLI below is
debug/operator oriented and replies are `snprintf`-bounded into a 160-byte buffer.

Commands take intuitive aliases (matched by the first word; see `is_cmd` in `OtaCli.cpp`) so they're easy
to type and read — `status`/`neighbors`/`pull`/`drop`/`applydelta` are the canonical names, the aliases are
the recommended user-facing forms. Output is plain-language (a user-facing guide lives at
[ota_user_guide.md](ota_user_guide.md)).

```
ota help | ?                       list the commands
ota status | st  (or bare `ota`)   plain-language: running fw, the one fetch session (state/%/id), serving, keys
ota ls | neighbors | nbrs | updates | n   discovered updates (queries sources; rows arrive async via OTA_HAVE)
ota get | pull | download <#|mid8> fetch a chosen mOTA (manual; works regardless of autofetch)
ota install | apply | applydelta   verify + approve + (ESP32) apply / (nRF52) reboot-to-bootloader
ota cancel | drop | stop           drop the current fetch session (frees the slot; stops re-seeding)
ota announce | adv                 serve self + send a beacon now
ota self | id                      print this firmware's EndF (body/image size, base_hash)
ota folder | fold [on|off]         attach/detach an external .mota folder (host daemon) ; bare = list
ota config | cfg | set [autofetch|autoinstall|checkpoint] ...   show/set persisted policy
ota key | keys [add|rm <hex>]      trusted signer allowlist ; bare = list
ota dev ...                        bring-up helpers (stage/recv/serve/verify)
```

---

## 12. Apply & bootloader contract

- **ESP32 (A/B):** applied in-firmware via the detools decoder into the inactive OTA slot
  (`OtaApply.cpp::ota_apply_detools_mota` + `OtaStoreFlashEsp32`), then set-boot + reboot (power-safe,
  rollback-capable). No bootloader changes. Erase ranges must be sector-aligned (4096).
- **nRF52 (single-slot):** the running firmware **never** flashes the app. `ota applydelta` verifies fully
  (`image_hash`, `base_hash`, signature/allowlist, `hw_id`), writes `approval = "APRV"`, then reboots into
  the modified bootloader (`Adafruit_nRF52_Bootloader_OTAFIX`). The bootloader:
  1. **scans flash for `MAGIC`** to find the staged `.mota` (it must NOT trust any stored size),
  2. re-checks `TRAILER`, `image_hash`, `approval == "APRV"`, and that the delta's `base_hash` equals the
     running firmware's `EndF.body_hash` (recomputed by scanning for `EndF` — never trust `bank_0_size`),
  3. applies the in-place codec over the app region and boots only if the result hashes to `image_hash`.

The signature proves author authenticity; `approval` proves local owner consent — both required to apply.

> **Bootloader testing note:** always test apply with a *real different* image (base ≠ target). A same-image
> (X→X) "delta" trivially reproduces the target and gives a false positive.

---

## 13. Versioning of this spec

`format_ver = 2`. A parser accepts exactly this value and rejects anything else — there is one container
format, fixed-layout, and no compatibility shims to carry. If the format ever needs to change, bump
`format_ver`; the multihash `hash_algo` separately allows swapping the digest family without a format
bump. Unknown `codec_id` / `ota_msg_type` values are ignored (a node simply won't fetch what it can't apply).

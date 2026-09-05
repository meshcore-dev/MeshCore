# Shrinking the per-connection TLS footprint on non-PSRAM observers

## Why

On a non-PSRAM Heltec V3 running two WSS/JWT broker slots, the largest allocatable block in
internal DRAM walks down in ~16 KiB steps at every TLS reconnect while total free heap stays
flat. Measured on hardware over 50 reconnect cycles: 62,452 → 16,372 bytes, permanently.

The step size is not a coincidence. `framework-arduinoespressif32 3.20017` (Arduino 2.0.17,
IDF 4.4) builds mbedTLS with the **symmetric** buffer configuration:

```
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN 16384      # sizes BOTH the in and out record buffers
CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE 1
# CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN       — not defined
# CONFIG_MBEDTLS_DYNAMIC_BUFFER               — not defined
# CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH   — not defined
```

Read from `packages/framework-arduinoespressif32/tools/sdk/esp32s3/qio_qspi/include/sdkconfig.h`.
Note the separate `framework-arduinoespressif32-libs` package in `~/.platformio` belongs to
the esp32c6 env (pioarduino, IDF 5.3) and is **not** what this env links against — though it
happens to carry identical mbedTLS values.

So each broker slot costs **2 × 16 KiB = 32 KiB** of contiguous internal DRAM in record
buffers alone, and two slots cost 64 KiB on a board with roughly 80 KiB free. Every reconnect
frees and re-allocates those buffers, and anything that lands in the vacated hole in between
prevents them from going back, which is the ratchet.

Confirmed by two independent observations: losing a whole TLS session returned exactly 16,384
bytes of contiguity on one device and exactly 32,768 on another, and per-connection teardown
frees ~41.6–44.7 KB total.

## What the firmware could already do, and its limit

`softDisconnect()` (branch `perf/mqtt-renewal-no-stop`, commit `6c51e468`) stops the JWT
renewal bounce from destroying and recreating the esp-mqtt task, keeping its 6 KiB stack out
of the hole. Measured: the staircase arrests after 2 steps at 36,852 through cycle 16, where
the baseline took 4 steps and settled at 16,372 by cycle 11 — about 20 KB better.

That is as far as the application layer reaches. MQTT 3.1.1 has no re-authentication packet,
so presenting a fresh JWT *requires* a transport reconnect; mbedTLS's internal allocation
order during the handshake is not controllable from the application. The remaining cost is
the record buffers themselves.

## The changes

All three are compile-time in mbedTLS, and the Arduino framework ships precompiled `.a`
archives (`tools/sdk/esp32s3/lib/libmbedtls.a`), so a project-level `-D` cannot change them.
A custom framework build is required.

| Setting | From | To | Saving per connection |
|---|---|---|---|
| `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` | unset | `y` | enables the two below |
| `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` | 16384 (implied) | 16384 | none — keep it |
| `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN` | 16384 (implied) | 4096 | **~12 KiB** |
| `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` | `1` | `n` | ~4 KiB |

Roughly 16 KiB per connection, 32 KiB across two slots.

### Why inbound stays at 16 KiB

A peer may legitimately send a 16 KiB TLS record. Lowering the inbound limit only works if
both brokers negotiate the max-fragment-length extension or provably never send larger, and
getting it wrong produces invalid-record and handshake failures rather than a clean error.
Outbound is ours to choose: this firmware's MQTT and WebSocket frames are far below 4 KiB
(`MAX_TRANS_UNIT`-bounded packets plus small JSON), so 4 KiB is comfortable.

### Risk on the peer certificate

Dropping `KEEP_PEER_CERTIFICATE` means `mbedtls_ssl_get_peer_cert()` returns NULL after the
handshake. Chain validation still happens — only retention of the parsed leaf changes. This
firmware verifies against a CA (`GTS_ROOT_R4` / the bundle) and never inspects the peer
certificate or a fingerprint, so it should be safe. It does change `mbedtls_ssl_session`
layout, which is exactly why the whole framework must be rebuilt together rather than
swapping a single archive in.

## Build procedure

### Do not use esp32-arduino-lib-builder for this

`release/v4.4` is the branch matching Arduino 2.0.x, but its `update-components.sh` clones
every dependency at **master**, so it no longer resolves. Five successive failures, each a
different repo: the arduino branch name it passes to `-A` does not exist; `jq` is absent
from the IDF image and its absence makes `build.sh`'s target loop a **silent no-op that
still exits 0**; `esp_littlefs` and `esp32-camera` master require IDF ≥5.0/≥5.1;
`esp32-camera` later needs an `esp_jpeg` version the 4.4 registry cannot satisfy; and
tinyusb's source layout no longer matches `arduino_tinyusb/CMakeLists.txt`. Pinning each
one in turn just surfaces the next.

### Rebuild only the mbedTLS archives

More rigorous anyway, because it reuses the shipped `sdkconfig` verbatim — so the archives
differ from stock *only* by the intended change, with no arduino-version or
`DYNAMIC_BUFFER` drift.

This is ABI-safe for the content-length change specifically: `ssl.h` declares `in_buf` and
`out_buf` as `unsigned char *`, allocated in `ssl_setup()`, and no public struct embeds a
CONTENT_LEN-sized array. The other precompiled archives (esp-tls, esp_http_client,
esp-mqtt) therefore remain compatible. **It is not safe for
`CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE`**, which changes `mbedtls_ssl_session` layout —
that one needs everything rebuilt together, so it is excluded here.

1. Minimal IDF project whose only component requirement is `mbedtls`.
2. `sdkconfig.defaults` = the shipped
   `packages/framework-arduinoespressif32/tools/sdk/esp32s3/sdkconfig`, with
   `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384` replaced by the three asymmetric lines.
   Verify the diff is exactly 5 lines before building.
3. `docker run --rm -v $PWD:/project -w /project espressif/idf:v4.4.7 idf.py -DIDF_TARGET=esp32s3 build`
4. Confirm `build/config/sdkconfig.h` carries `OUT_CONTENT_LEN 4096`.
5. Stage the four archives under the framework's names — note the rename:

   | built | framework name | stock size | rebuilt |
   |---|---|---:|---:|
   | `esp-idf/mbedtls/libmbedtls.a` | `libmbedtls.a` | 113,914 | 113,338 |
   | `esp-idf/mbedtls/mbedtls/library/libmbedtls.a` | **`libmbedtls_2.a`** | 1,252,150 | 1,245,718 |
   | `.../libmbedcrypto.a` | `libmbedcrypto.a` | 4,302,698 | 4,259,458 |
   | `.../libmbedx509.a` | `libmbedx509.a` | 676,590 | 673,318 |

   All within ~1% of stock, which is a good check that only the config differs.

### Wire it in with -L, never platform_packages

```
PLATFORMIO_BUILD_FLAGS="-L/path/to/staged/archives" pio run -e Heltec_v3_repeater_observer_mqtt
```

Verify with `grep -oE "[^ ]*libmbed[a-z0-9_]*\.a" .pio/build/<env>/firmware.map | sort -u` —
every path must be the staged directory.

**Do not** point `platform_packages` at a `file://` copy of the framework. PlatformIO
installs it *over* the shared `~/.platformio/packages/framework-arduinoespressif32`,
silently changing mbedTLS for every other ESP32 env and project on the machine. It does this
even when the copy's `package.json` version differs — verified twice here, and both times the
fix was `rm -rf` the package and `pio pkg install` to re-download stock. A prepended library
search path keeps the change scoped to one env, because the linker takes each archive member
from the first archive that satisfies an undefined symbol.

## How to verify it worked

1. `strings`/`grep` the new `sdkconfig.h` for the four settings.
2. Build and check the RAM figure; static usage should be unchanged (these are heap buffers).
3. On hardware, `get mqtt.stats` at boot with two slots connected: the largest free block
   should start roughly 24–32 KiB higher than the current 62–67 KiB.
4. Soak across reconnect cycles and compare the floor against the two recorded runs:
   baseline settled 16,372 (cycle 11); `softDisconnect` holds 36,852 (cycle 16).

## Prior art in this investigation

`.scratch/mqtt-non-psram-heap-staircase-analysis-2026-08-05.md` (untracked — `.scratch/` is
globally gitignored) holds the full allocation inventory. `~/mqtt-soak/STATE.md` holds the
soak evidence, including two retracted hypotheses worth not repeating: the perf commits were
not the cause, and waev does not cap connections per IP.

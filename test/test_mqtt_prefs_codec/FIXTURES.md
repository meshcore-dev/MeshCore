# MQTT preferences fixture provenance

The codec tests construct synthetic, non-secret byte vectors at hard-coded
offsets. They intentionally do not serialize the production structs: a layout
change must disagree with the frozen fixture bytes and fail the field checks.

| Bytes | Historical layout | Source history |
|---:|---|---|
| 472 | Pre-slot, before and after `wifi_power_save` | Initial `/mqtt_prefs` layout; `34c8bea7` inserted WiFi power without changing the padded total size |
| 1032 | Initial three-slot layout | `b43e9618` |
| 1464 | Three slots with token/topic tails | `95874f0c` |
| 2452 | Six slots with token/topic tails | `1b5884bd` |
| 2836 | Six slots with audience tail | `1263e71d` |
| 2840 | Six slots with RX flag | `47b632aa` |
| 2904 | Six slots with NTP server | `7416d632` |
| 2736 / 2860 | Version-1 payload before observer tail / complete payload | `58b9cb66` introduced the eight-byte versioned header; the shorter form exercises its append-compatible prefix contract |

The 3024-byte raw observer-tail form is not accepted as deployed fleet data:
repository history indicates it existed briefly before versioning but was not
shipped. Tests require it to be preserved rather than guessed and rewritten.

Headerless formats have no checksum. Plausibility checks reject obvious random
or malformed content, but cannot authenticate a byte sequence that happens to
look like a valid historical struct.

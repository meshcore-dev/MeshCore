Vendored detools embeddable C decoder
=====================================

Source : https://github.com/eerimoq/detools  (tag 0.53.0, c/ directory)
Files  : detools.c, detools.h
License : BSD 2-Clause (Erik Moqvist; original bsdiff (c) Colin Percival).
          sais.c (host packager only, not vendored) is MIT.
          Compatible with MeshCore's MIT license; notices retained in-file.

Why vendored
------------
The detools PyPI package (used by tools/mota to *create* patches with
detools.create_patch) ships only the Python library + patch-creation C
extensions -- NOT the embeddable decoder. The on-device delta applier needs
detools' own decoder, so we vendor c/detools.{c,h} verbatim. This is detools'
official C implementation; MeshCore does not reimplement the delta codec.

Local modifications
-------------------
Only the config defaults at the top of detools.h were changed (upstream = 1):
  DETOOLS_CONFIG_FILE_IO                -> 0   (no <stdio> file IO on device)
  DETOOLS_CONFIG_COMPRESSION_LZMA       -> 0   (would need liblzma)
  DETOOLS_CONFIG_COMPRESSION_HEATSHRINK -> 0   (would need malloc + heatshrink/)
  DETOOLS_CONFIG_COMPRESSION_NONE       =  1   (kept)
  DETOOLS_CONFIG_COMPRESSION_CRLE       =  1   (kept)
detools.c is byte-for-byte upstream.

With this config the decoder is self-contained (no malloc, no liblzma, no
heatshrink/, no file IO) and applies `--codec sequential --compression crle`
patches, which is what tools/mota produces for MeshCore .mota deltas.

Usage on device
---------------
src/helpers/ota/OtaApply.cpp wraps detools_apply_patch_callbacks():
  from_read/from_seek -> running OTA slot (the delta base, via esp_partition_read)
  patch_read          -> the .mota payload held in RAM (fetched over LoRa)
  to_write            -> inactive OTA slot (via esp_ota_write) + running SHA-256
The decoded image is verified against the signed manifest image_hash before the
slot is armed as boot partition.

To update: re-copy c/detools.{c,h} from the pinned detools tag and re-apply the
three config-default edits above.

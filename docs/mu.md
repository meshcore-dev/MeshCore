# CLI Commands

This document provides an overview of CLI commands which are specific to the MU firmware version.

## Navigation

- [Wifi Companion Configuration via Rescue CLI](#wifi-companion-configuration-via-rescue-cli)
- [View or change the maximum direct-route resend attempts](#view-or-change-the-maximum-direct-route-resend-attempts)
  
---
### Wifi Companion Configuration via Rescue CLI

**Description:** Configure Wifi SSID and Password for a ESP32 companion via the rescue command line interface

**Usage:**
- `wifi_ssid <ssid>`
- `wifi_pwd <pwd>`
- `wifi_commit`
- `wifi_clear`

---
### View or change the maximum direct-route resend attempts

**Usage:**
- `get max.resend`
- `set max.resend <value>`

**Parameters:**
- `value`: Maximum number of resend attempts for direct-routed packets (0–3). `0` disables resending entirely.

**Default:** `2`

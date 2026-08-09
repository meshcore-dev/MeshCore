# CLI Commands

This document provides an overview of CLI commands which are specific to the MU firmware version.

## Navigation

- [View or change the maximum direct-route resend attempts](#view-or-change-the-maximum-direct-route-resend-attempts)
  
---
### View or change the maximum direct-route resend attempts

**Usage:**
- `get max.resend`
- `set max.resend <value>`

**Parameters:**
- `value`: Maximum number of resend attempts for direct-routed packets (0–3). `0` disables resending entirely.

**Default:** `2`


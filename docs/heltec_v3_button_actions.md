# Heltec V3 Button Actions

This page documents current button behavior for `Heltec_v3_companion_radio_tcp_usb_ble` UI flows.

## Global Click Mapping

On single-button Heltec V3 builds:

- `single click` = next page
- `double click` = previous page
- `long press` = enter/select action on current page
- `triple click` = `KEY_SELECT` action on current page

## Startup Special Case

- Long press within first ~8 seconds after boot enters CLI Rescue mode.

## Screen Actions

### BLE Transport Page

- Long press: switch transport to WiFi/TCP.
- After transport switch, UI shows a 10-second reboot countdown and auto-reboots.

### WiFi Transport Page

- Long press: toggle WiFi mode (`AP <-> Client`).
- Triple click: switch transport back to BLE.
- UI footer alternates/scrolls usage hints:
  - `Long press: AP/Client`
  - `Triple click: BLE`

### Advert Page

- Long press: send advert.

### Power Page

- Long press: start hibernate/power-off flow.

## Notes

- Transport switches are runtime-applied, then device auto-reboots after countdown for clean handoff.
- WiFi page shows `IP:PORT` in both AP and client mode, with AP SSID/password rows alternating.

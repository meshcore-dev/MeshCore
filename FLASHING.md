# Flashing Notes

## RAK4631 + RAK13800/W5100S Ethernet Companion

This board-support layer is a follow-up to the Ethernet foundation merged in #1983. It keeps the framed MeshCore Companion TCP API and does not switch to raw line mode, MQTT, or the repeater CLI.

Use the canonical Ethernet companion targets when flashing Crow or validating the RAK13800 wiring:

- `RAK_RAK13800_companion_radio_eth`
- `RAK_RAK13800_companion_radio_eth_static_diag`

Compatibility aliases are still present for older workflows:

- `RAK_4631_companion_radio_ethernet`
- `RAK_4631_companion_radio_eth_clean`
- `RAK_4631_companion_radio_eth_static_diag`

## Build Targets

```bash
pio run -e RAK_RAK13800_companion_radio_eth
pio run -e RAK_RAK13800_companion_radio_eth_static_diag
```

## Connection Details

- Ethernet uses the framed MeshCore Companion TCP protocol.
- Do not use the repeater CLI target for companion validation.
- Do not use raw line mode.
- Crow-compatible target port: `4403`
- Upstream #1983 default port: `5000`

## Static Diagnostic Target

`RAK_RAK13800_companion_radio_eth_static_diag` boots with:

- IP `10.245.94.47`
- Gateway `10.245.94.33`
- DNS `10.245.94.33`
- Subnet `255.255.255.224`

It is intended for repeatable Ethernet bring-up and Companion protocol validation.

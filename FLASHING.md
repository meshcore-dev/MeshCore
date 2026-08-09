# Flashing RAK13800 Ethernet Companion Targets

Use the canonical PlatformIO target names:

- `RAK_RAK13800_companion_radio_eth`
- `RAK_RAK13800_companion_radio_eth_static_diag`

These are RAK13800 Ethernet Companion board-support targets built on the RAK4631 MCU platform. The MCU, bootloader, SoftDevice, and board package remain RAK4631 / nRF52840, but the hardware stack being supported is `RAK 4631 / RAK13800 + W5100S`.

## Production Target

- Target: `RAK_RAK13800_companion_radio_eth`
- Networking: DHCP first, or DHCP with static fallback
- TCP port: `4403`
- `ETH_DEBUG_LOGGING=0`
- `FORCE_CLIENT_REPEAT=0`
- `MAX_CONTACTS=128` unless RAM testing proves a higher value is safe

## Static Diagnostic Target

- Target: `RAK_RAK13800_companion_radio_eth_static_diag`
- IP: `10.245.94.47`
- Gateway: `10.245.94.33`
- DNS: `10.245.94.33`
- Subnet: `255.255.255.224`
- TCP port: `4403`
- `ETH_STATIC_ONLY=1`
- `ETH_DEBUG_LOGGING=1`

## Compatibility Aliases

The following names may remain temporarily for compatibility:

- `RAK_4631_companion_radio_eth_clean` -> `RAK_RAK13800_companion_radio_eth`
- `RAK_4631_companion_radio_eth_static_diag` -> `RAK_RAK13800_companion_radio_eth_static_diag`

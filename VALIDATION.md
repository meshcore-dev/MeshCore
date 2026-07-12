# Validation for RAK13800 Ethernet Companion Targets

Validate the canonical targets:

- `RAK_RAK13800_companion_radio_eth`
- `RAK_RAK13800_companion_radio_eth_static_diag`

## What to Verify

- The target name is the RAK13800 board-support name, not a generic RAK4631 label.
- The build still extends the RAK4631 PlatformIO environment.
- The static diagnostic target keeps:
  - IP `10.245.94.47`
  - Gateway `10.245.94.33`
  - DNS `10.245.94.33`
  - Subnet `255.255.255.224`
  - TCP port `4403`
  - `ETH_STATIC_ONLY=1`
  - `ETH_DEBUG_LOGGING=1`
- The production target uses:
  - DHCP first or DHCP with static fallback
  - TCP port `4403`
  - `ETH_DEBUG_LOGGING=0`
  - `FORCE_CLIENT_REPEAT=0`
  - `MAX_CONTACTS=128` unless RAM testing proves a higher value is safe

## Compatibility Check

- Keep `RAK_4631_companion_radio_eth_clean` as an alias to `RAK_RAK13800_companion_radio_eth` if older scripts still use it.
- Keep `RAK_4631_companion_radio_eth_static_diag` as an alias to `RAK_RAK13800_companion_radio_eth_static_diag` if older scripts still use it.

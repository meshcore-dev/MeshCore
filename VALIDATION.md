# Validation Notes

## Build Checks

Run:

```bash
pio run -e RAK_RAK13800_companion_radio_eth
pio run -e RAK_RAK13800_companion_radio_eth_static_diag
pio run -e RAK_4631_companion_radio_ethernet || true
pio run -e RAK_4631_repeater_ethernet || true
pio run -e RAK_4631_room_server_ethernet || true
```

## Static Checks

```bash
grep -R "server.available" -n src/helpers/nrf52 examples variants || true
grep -R "server.accept" -n src/helpers/nrf52 examples variants || true
grep -R "ETHERNET_RAW_LINE" -n variants examples src || true
```

Expected:

- Companion Ethernet accept paths use `server.accept()`
- No Companion Ethernet target defines `ETHERNET_RAW_LINE`
- `server.available()` is not used for TCP client acceptance
- The canonical companion target uses TCP port `4403`

## Smoke Tests

```bash
python3 scripts/meshcore_companion_tcp_smoke_test.py DEVICE_IP 4403
python3 scripts/meshcore_companion_multi_client_test.py DEVICE_IP 4403
```

## Static Diagnostic Hardware Test

Flash `RAK_RAK13800_companion_radio_eth_static_diag` and verify:

- Serial output shows Ethernet initialized
- IP `10.245.94.47`
- Subnet `255.255.255.224`
- Gateway `10.245.94.33`
- TCP server listening on `4403`
- Ping, TCP connect, and neighbor resolution succeed
- Companion smoke tests pass
- Both TCP clients remain connected during the multi-client test

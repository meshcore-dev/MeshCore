# Companion DFU Implementation Notes

The companion firmware now supports a remote DFU trigger over the normal companion BLE protocol.

## BLE UUIDs

- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX characteristic (app writes here): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX characteristic (app listens here): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

## Start DFU Command

- Command ID: `53` (`0x35`)
- Payload: ASCII `"dfu"`
- Full BLE write payload: `35 64 66 75`

Important:

- This is the raw companion protocol payload only.
- Do not wrap it in the serial `<len>` framing used on UART.

## Expected Behavior

1. Connect to the companion device normally.
2. Write `0x35 0x64 0x66 0x75` to the RX characteristic.
3. Listen for the normal companion TX reply.
4. If accepted, firmware returns `PACKET_OK` (`0x00`).
5. Shortly after that, the current BLE connection will drop.
6. The board then becomes available to the Nordic DFU app for firmware update.

## Recommended App Behavior

- Treat this like a mode switch, not a normal in-session command.
- After sending the command and receiving `OK`, stop normal companion interaction.
- Expect disconnect.
- Then hand off to the DFU workflow or instruct the user to scan in the Nordic DFU app.

## Known-Good Validation

- Sending this payload from a desktop BLE client successfully triggered DFU mode.
- After that transition, the Nordic DFU app was able to perform the firmware update successfully.

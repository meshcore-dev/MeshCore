# WiFi/Transport Command Examples

This file shows the same settings commands in three formats:

1. Raw companion protocol frames (USB/TCP)
2. Python (build/send framed command)
3. Text CLI commands

## Command Keys

These are the custom-var keys used by firmware:

- `transport:ble`
- `transport:wifi`
- `wifi.mode:ap`
- `wifi.mode:client`
- `wifi.ssid:<ssid>`
- `wifi.pwd:<password>`
- `wifi.ap.ssid:<ap_name>`
- `wifi.ap.pwd:<ap_password>`

## 1) Raw Companion Protocol Form

Frame format:

- App -> radio: `0x3c` + `<len_le16>` + `<payload>`
- Payload for set custom var: `0x29` + ASCII string

Example raw bytes (hex):

- `transport:wifi`
  - Payload: `29 74 72 61 6e 73 70 6f 72 74 3a 77 69 66 69`
  - Full frame: `3c 0f 00 29 74 72 61 6e 73 70 6f 72 74 3a 77 69 66 69`

- `wifi.mode:ap`
  - Payload: `29 77 69 66 69 2e 6d 6f 64 65 3a 61 70`
  - Full frame: `3c 0d 00 29 77 69 66 69 2e 6d 6f 64 65 3a 61 70`

- `wifi.mode:client`
  - Payload: `29 77 69 66 69 2e 6d 6f 64 65 3a 63 6c 69 65 6e 74`
  - Full frame: `3c 11 00 29 77 69 66 69 2e 6d 6f 64 65 3a 63 6c 69 65 6e 74`

## 2) Python Form

### Helper function

```python
import os

def send_custom_var(port, kv):
    fd = os.open(port, os.O_WRONLY)
    payload = bytes([0x29]) + kv.encode("ascii")
    frame = bytes([0x3c, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF]) + payload
    os.write(fd, frame)
    os.close(fd)
```

### Example usage

```python
send_custom_var("/dev/ttyUSB0", "transport:wifi")
send_custom_var("/dev/ttyUSB0", "wifi.mode:ap")
send_custom_var("/dev/ttyUSB0", "wifi.ap.ssid:MeshCore-TestAP")
send_custom_var("/dev/ttyUSB0", "wifi.ap.pwd:testpass123")

send_custom_var("/dev/ttyUSB0", "wifi.mode:client")
send_custom_var("/dev/ttyUSB0", "wifi.ssid:YourSSID")
send_custom_var("/dev/ttyUSB0", "wifi.pwd:YourPassword")

send_custom_var("/dev/ttyUSB0", "transport:ble")
```

### One-liner pattern

```bash
python3 -c "import os;fd=os.open('/dev/ttyUSB0',os.O_WRONLY);s='wifi.mode:ap';p=bytes([0x29])+s.encode();os.write(fd,bytes([0x3c,len(p)&255,(len(p)>>8)&255])+p);os.close(fd)"
```

## 3) Text CLI Form

There are two text-CLI contexts:

1. `meshcore-cli` companion commands
2. CLI Rescue (device text console)

### 3.1 meshcore-cli (companion mode)

Use serial companion mode (no `-r`):

```bash
meshcore-cli -s /dev/ttyUSB0 -b 115200 infos
```

Then set values:

```bash
meshcore-cli -s /dev/ttyUSB0 -b 115200 set transport tcp
meshcore-cli -s /dev/ttyUSB0 -b 115200 set wifi.mode ap
meshcore-cli -s /dev/ttyUSB0 -b 115200 set wifi.ap.ssid MeshCore-TestAP
meshcore-cli -s /dev/ttyUSB0 -b 115200 set wifi.ap.pwd testpass123

meshcore-cli -s /dev/ttyUSB0 -b 115200 set wifi.mode client
meshcore-cli -s /dev/ttyUSB0 -b 115200 set wifi.ssid YourSSID
meshcore-cli -s /dev/ttyUSB0 -b 115200 set wifi.pwd YourPassword

meshcore-cli -s /dev/ttyUSB0 -b 115200 set transport ble
```

### 3.2 CLI Rescue text commands

Inside CLI Rescue terminal:

```text
set transport tcp
set wifi.mode ap
set wifi.ap.ssid MeshCore-TestAP
set wifi.ap.pwd testpass123

set wifi.mode client
set wifi.ssid YourSSID
set wifi.pwd YourPassword

set transport ble
```

## TCP Test Example

If device client IP is known (example `192.168.40.55`):

```bash
meshcore-cli -t 192.168.40.55 -p 5000 infos
```

## Notes

- `transport:wifi` / `set transport tcp` enables WiFi/TCP transport and disables BLE.
- `transport:ble` / `set transport ble` enables BLE and disables WiFi/TCP.
- AP password should be empty (open AP) or at least 8 characters.
- Do not commit real SSIDs/passwords to git.

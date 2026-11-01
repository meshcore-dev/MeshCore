# Companion CLI

Since version 1.18 meshcore companion radio firmware has a CLI interface, which can be accessed locally (through companion radio protocol) or remotely via the network.

## Navigation
- [Operational](#operational)
- [Configuration](#configuration)
  - [System](#system)
  - [Radio](#radio)
  - [BLE](#ble)
  - [Wifi](#wifi)
  - [UI](#ui)

## Operational

### Reboot the node
**Usage:** 
- `reboot`

**Note:** No reply is sent.

---

### Power-off the node
**Usage:**
- `poweroff`, or
- `shutdown`

**Note:** No reply is sent.

---

## Configuration

### System

#### View or change this node's name
**Usage:**
- `get name`
- `set name <name>`

**Parameters:**
- `name`: Node name

**Set by build flag:** `ADVERT_NAME`

**Default:** Varies by board

**Note:** Advertised names can use up to 23 bytes when location is included and 31 bytes otherwise. Emoji and Unicode characters may take more than one byte. Names that exceed the available advert space are truncated at a valid UTF-8 code point boundary.

---

#### View this node's firmware version
**Usage:** `ver`

---

#### Configure this node's timezone
**Usage:** 
- `set tz.offset <offset>`
- `get tz.offset`

**Parameters:**
- `offset`: float value of the offset in hours

---

### Radio

Radio parameters are common to all roles and handled by `CommonRadioPrefs`.

#### View or change this node's radio parameters
**Usage:**
- `get radio`
- `set radio <freq>,<bw>,<sf>,<cr>`

**Parameters:**
- `freq`: Frequency in MHz
- `bw`: Bandwidth in kHz
- `sf`: Spreading factor (5-12)
- `cr`: Coding rate (5-8)

**Set by build flag:** `LORA_FREQ`, `LORA_BW`, `LORA_SF`, `LORA_CR`

**Default:** `869.618,62.5,8,5`

**Note:** Requires reboot to apply

---

#### View or change this node's transmit power
**Usage:**
- `get tx`
- `set tx <dbm>`

**Parameters:**
- `dbm`: Power level in dBm (1-22)

**Set by build flag:** `LORA_TX_POWER`

**Default:** Varies by board

**Notes:** This setting only controls the power level of the LoRa chip. Some nodes have an additional power amplifier stage which increases the total output. Refer to the node's manual for the correct setting to use. **Setting a value too high may violate the laws in your country.**

---

#### View or change this node's frequency
**Usage:**
- `get freq`
- `set freq <frequency>`

**Parameters:**
- `frequency`: Frequency in MHz

**Default:** `869.618`

**Note:** Requires reboot to apply
**Serial Only:** `set freq <frequency>`

---

#### View or change this node's rx boosted gain mode (SX12xx and LR1110)
**Usage:**
- `get radio.rxgain`
- `set radio.rxgain <state>`

**Parameters:**
  - `state`: `on`|`off`

**Default:** `on`

---

#### View or change the LoRa FEM receive-path gain state on supported boards
**Usage:**
- `get radio.fem.rxgain`
- `set radio.fem.rxgain <state>`

**Parameters:**
- `state`: `on`|`off`

**Notes:**
- This controls the external LoRa FEM receive-path LNA where the board supports it.
- This is separate from `radio.rxgain`, which controls the radio chip receive gain mode.

---

#### View or change the LoRa FEM transmit-path gain state on supported boards
**Usage:**
- `get radio.fem.txgain`
- `set radio.fem.txgain <state>`

**Parameters:**
- `state`: `on`|`off`

**Notes:**
- This controls a software-selectable external LoRa FEM transmit gain where the board supports it.
- On Station G3, remove the PA PL1 jumper to allow software control. `on` selects PA PL1 high/short and `off` selects PA PL1 low/open. The PA PL2 hardware jumper determines whether this switches between power levels 1/3 or 2/4.
- Select an operating level and SX1262 transmit power that comply with local RF limits and the Station G3 power-supply requirements.
- The setting is saved immediately, but on Station G3 the level is applied to the hardware at the start of the next transmit, so that the PA supply rail is never re-targeted while the PA is being driven. `get` reports the configured state, which may lead the hardware until the node next transmits.

---

#### View or change the duty cycle limit
**Usage:**
- `get dutycycle`
- `set dutycycle <value>`

**Parameters:**
- `value`: Duty cycle percentage (1-100)

**Default:** `50%` (equivalent to airtime factor 1.0)

**Examples:**
- `set dutycycle 100` — no duty cycle limit
- `set dutycycle 50` — 50% duty cycle (default)
- `set dutycycle 10` — 10% duty cycle
- `set dutycycle 1` — 1% duty cycle (strictest EU requirement)

> **Note:** Added in firmware v1.15.0

---

#### View or change the airtime factor (duty cycle limit)
> **Deprecated** as of firmware v1.15.0. Use [`get/set dutycycle`](#view-or-change-the-duty-cycle-limit) instead.

**Usage:**
- `get af`
- `set af <value>`

**Parameters:**
- `value`: Airtime factor (0-9). After each transmission, the repeater enforces a silent period of approximately the on-air transmission time multiplied by the value. This results in a long-term duty cycle of roughly 1 divided by (1 plus the value). For example:
  - `af = 1` → ~50% duty
  - `af = 2` → ~33% duty
  - `af = 3` → ~25% duty
  - `af = 9` → ~10% duty
  You are responsible for choosing a value that is appropriate for your jurisdiction and channel plan (for example EU 868 Mhz 10% duty cycle regulation).

**Default:** `1.0`

---

#### View or change the local interference threshold
**Usage:**
- `get int.thresh`
- `set int.thresh <value>`

**Parameters:**
- `value`: Interference threshold value

**Default:** `0.0`

---

#### Enable or disable hardware Channel Activity Detection (CAD)
**Usage:**
- `get cad`
- `set cad <on|off>`

**Description:** When enabled, the radio performs a hardware Channel Activity Detection scan before transmitting and defers if the channel is busy. Runs independently of `int.thresh` — either, both, or none may be active.

**Parameters:**
- `on|off`: Enable or disable hardware CAD

**Default:** `off`

---

#### View or change the AGC Reset Interval
**Usage:**
- `get agc.reset.interval`
- `set agc.reset.interval <value>`

**Parameters:**
- `value`: Interval in seconds, 0-1020, rounded down to a multiple of 4 (17 becomes 16). 0 to disable.

**Default:** `0.0`

---

#### Enable or disable Multi-Acks support
**Usage:**
- `get multi.acks`
- `set multi.acks <state>`

**Parameters:**
- `state`: `0` (disable) or `1` (enable)

**Default:** `0`

---

#### View or change this node's advert path hash size
**Usage:**
- `get path.hash.mode`
- `set path.hash.mode <value>`

**Parameters:**
- `value`: Path hash size (0-2)
  - `0`: 1 Byte hash size (256 unique ids)[64 max flood]
  - `1`: 2 Byte hash size (65,536 unique ids)[32 max flood]
  - `2`: 3 Byte hash size (16,777,216 unique ids)[21 max flood]
  - `3`: DO NOT USE (Reserved) 

**Default:** `0`

**Note:** the 'path.hash.mode' sets the low-level ID/hash encoding size used when the repeater adverts. This setting has no impact on what packet ID/hash size this repeater forwards, all sizes should be forwarded on firmware >= 1.14. This feature was added in firmware 1.14
#### View or change the retransmit delay factor for flood traffic
**Usage:**
- `get txdelay`
- `set txdelay <value>`

**Parameters:**
- `value`: Transmit delay factor (0-2)

**Default:** `0.5`

**Note:** When multiple nearby repeaters all hear the same flood packet, each waits a random amount of time before retransmitting to avoid simultaneous collisions. This factor scales the size of that random window. Higher values reduce collision risk at the cost of added latency. `0` disables the window entirely.

---

#### View or change the retransmit delay factor for direct traffic
**Usage:**
- `get direct.txdelay`
- `set direct.txdelay <value>`

**Parameters:**
- `value`: Direct transmit delay factor (0-2)

**Default:** `0.3` (Repeater) - `0.2` (Room Server, Sensor)

**Note:** Same collision-avoidance random window as `txdelay`, but applied to direct (non-flood, routed) traffic. The default is lower because direct packets are addressed to a specific next hop, so far fewer nodes compete to retransmit them.

---

#### [Experimental] View or change the processing delay for received traffic
**Usage:**
- `get rxdelay`
- `set rxdelay <value>`

**Parameters:**
- `value`: Receive delay base (0-20)

**Default:** `0.0`

**Note:** When enabled, repeaters that received a flood packet with a weak signal are held in a delay queue before processing, while those that received it with a strong signal process it immediately. This gives strong-signal paths forwarding priority. By the time weak-signal nodes process their copy, the packet may have already propagated and will be suppressed as a duplicate, reducing redundant retransmissions.

---

### BLE

#### Change BLE PIN
**Usage:**
- `set pin <pin>`

**Parameters:**
- `pin`: New BLE PIN

**Set by build flag:** `BLE_PIN_CODE`

---

### Wifi

#### Get status
**Usage:**
- `get wifi.status`
- `get wifi.ip`

---

#### Toggle
**Usage:**
- `set wifi.enabled <value>`
- `get wifi.enabled`

**Parameters:**
- `value`: `0` (disable) or `1` (enable)

---

#### Set SSID
**Usage:**
- `set wifi.ssid <value>`
- `get wifi.ssid`

**Parameters:**
- `value`: ssid as a string

---

#### Set password
**Usage:**
- `set wifi.pwd <value>`
- `get wifi.pwd`

**Parameters:**
- `value`: password string

---

### UI

#### Show clock
**Usage:**
- `set ui.show_clock <state>`
- `get ui.show_clock`

**Parameters:**
- `state`: `0` (disable) or `1` (enable)

**Default:** `0` (or `UI_SHOW_CLOCK` build flag)

---

#### Node discovery screen shows nodes names
**Usage:**
- `set ui.nd_name <state>`
- `get ui.nd_name`

**Parameters:**
- `state`: `1` (show name) or `0` (show pubkey_prefix)

**Default:** `1`

**Note:** When showing name, there is not always enough space to draw both incoming and outgoing snr values.

---

#### Auto-off screen
**Usage:**
- `set ui.auto_off_delay <delay>`
- `get ui.auto_off_delay`

**Parameters:**
- `delay`: in seconds (`0` means always on)

**Default:** `15` (or set by `AUTO_OFF_MILLIS` build flag (in ms))

**Note:**
On e-ink displays, when turned off, the screen is not necessarily blanked, see `ui.auto_off_blank` and `ui.backlight_delay`.

When screen is not blanked, it is not refreshed any more. To let the user aware of that, a sleep icon is shown.

---

#### Display backlight timeout
**Usage:**
- `set ui.backlight_delay <delay>`
- `get ui.backlight_delay`

**Parameters:**
- `delay`: in seconds 
  - `0` means no timeout
  - `-2` means always off
  - `-1` means always on

**Note:**
When set to `0`, backlight can only be triggered using backlight btn.

---

#### When to turn backlight on
**Usage:**
- `set ui.backlight_on_keys <state>`
- `get ui.backlight_on_keys`
- `set ui.backlight_on_msgs <state>`
- `get ui.backlight_on_msgs`

**Parameters:**
- `state`: `0` (disable) or `1` (enable)

**Default:** 
- `ui.backlight_on_msgs = 0`
- `ui.backlight_on_keys = 0` if there is a backlight button on the board, else `1`

**Note:**
By default backlight is triggered by backlight button if present (as long as the button is pressed backlight is on, and is turned off after a delay).
These toggles permits to toggle backlight when keys are pressed or when a message arrives (it acts as a notification signal).
If `backlight_delay` is set to `0` then this setting has no effect, backlight won't be turned on.

---

#### Control if eink screen should be blanked
**Usage:**
- `set ui.auto_off_blank <state>`
- `get ui.auto_off_blank`

**Parameters:**
- `state`: `0` (disable) or `1` (enable)

**Default:** `0` (or set by `UI_SCREENOFF_BLANK` build flag) 

**Note:**
If disabled, when screen is shut the displays stays on screen and is not updated (an sleep icons covers battery to show the screen is sleeping).

When screen is blanked, the controller is also put into hibernation, which should preserve the screen a little and some battery ...

---

#### Maximum number of partial refreshes
**Usage:**
- `set ui.max_partial <value>`
- `get ui.max_partial`

**Parmeters:**
- `value`: max partial refreshes before doing a full refresh

**Default:** 0 (no partial refresh or set by `EINK_MAX_PARTIAL_REFRESH` build flag)

**Note:**
Forcing full refresh helps revives the screen when some tearing appears (under certain environmental conditions). If you see some tearing appearing, you can force full refresh on your screen manually by double clicking the backlight button. If tearing reappears often, use this setting to force full refresh every `value` partial refreshes.
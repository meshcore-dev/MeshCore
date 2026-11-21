# LILYGO T-LoRa Pager

Choose the companion build that matches the installed radio module and connection.
The SX1262 and LR1121 modules use the same connector and USB identification.

| Radio | USB serial companion | Bluetooth companion |
| --- | --- | --- |
| SX1262 | `LilyGo_TLoRa_Pager_SX1262_companion_radio_usb` | `LilyGo_TLoRa_Pager_SX1262_companion_radio_ble` |
| LR1121 | `LilyGo_TLoRa_Pager_LR1121_companion_radio_usb` | `LilyGo_TLoRa_Pager_LR1121_companion_radio_ble` |

Press the rotary encoder to operate the companion UI. The board supplies battery voltage
from its BQ27220 gauge and location from its onboard GNSS receiver. The LCD and radio
share an SPI bus. The renderer finishes each transfer and releases the display chip select
before servicing the radio profile scheduler.

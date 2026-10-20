# LilyGO T3 STM32 V1.0

MVP support for the STM32WL55CCU6 board with its integrated Sub-GHz radio and
128×64 SSD1315 SPI OLED. Supports a USB companion and a repeater, with a default
transmit power of **20 dBm**. The companion uses USB serial; the board has no BLE.

## Build

```sh
pio run -e LilyGo_T3_STM32_companion_radio_usb
pio run -e LilyGo_T3_STM32_repeater
```

The companion supports 100 contacts and eight channels. Radio settings inherit
the repository defaults; select settings appropriate to the board's RF band.
Existing saved preferences override build defaults.

## Flash and connect

Connect the USB serial port. Hold BOOT, press and release RST, then release BOOT:

```sh
pio run -e LilyGo_T3_STM32_companion_radio_usb -t upload --upload-port /dev/ttyACM0
```

Choose your actual serial port, then press RST after programming. The repeater
CLI and USB companion protocol both use USART1 at **115200 baud**.

The bundled `stm32flash` 0.7 may reject device ID `0x497` due to an
[upstream packaging issue](https://github.com/platformio/platform-ststm32/issues/899).
Use an updated [stm32flash](https://sourceforge.net/p/stm32flash/code/ci/master/tree/)
with STM32WL support, or STM32CubeProgrammer in UART mode. For CubeProgrammer,
use `.pio/build/<environment>/firmware.hex`, or `firmware.bin` at `0x08000000`.
ST-Link and J-Link uploads are also available through the board's SWD connector.

Application flash is limited to **224 KiB**. InternalFS occupies the final
32 KiB at `0x08038000`–`0x0803FFFF`; preserve it when updating to retain the
identity and settings.

## Hardware mapping

| Function | Pins / setting |
| --- | --- |
| USART1 | TX PA9, RX PA10 |
| Radio RF switch | PB2/PB6: idle 0/0, RX 1/0, HP TX 0/1 |
| Radio oscillator | 32 MHz crystal; TCXO mode disabled |
| SPI1 | SCK PA5, MISO PA6, MOSI PA7 |
| OLED | CS PB12, DC PA8, no software reset pin |
| SD card | CS PA4, held high |
| User button | PH3/BOOT, active high |
| LEDs | PA0/PA1, left off |

Initialization follows the manufacturer's
[hardware files](https://github.com/Xinyuan-LilyGO/T3-STM32/tree/master/hardware)
and [reference firmware](https://github.com/Xinyuan-LilyGO/T3-STM32/tree/master/examples/6_SubGHz_TXRX).

## MVP validation and limitations

Hardware checks covered OLED startup, USB companion communication, advertisement
reception, Public message transmission, and matching repeater trace replies at
20 dBm. Public packets were captured and decrypted independently. Some repeater
pings timed out; long-term reliability has not been established. Higher transmit
powers are outside the validated MVP setting.

The repeater builds successfully; forwarding has not been hardware-tested.
Battery telemetry, SD storage, and low-power optimization are not implemented.

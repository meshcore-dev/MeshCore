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

## Optional INA219 telemetry

The INA219 builds use MeshCore's existing environment sensor driver to report
voltage, current, and power. Build either role with:

```sh
pio run -e LilyGo_T3_STM32_repeater_ina219
pio run -e LilyGo_T3_STM32_companion_radio_usb_ina219
```

Connect the sensor's logic supply to **3.3 V** and use a common ground:

| INA219 connection | T3 STM32 connection |
| --- | --- |
| VCC / logic supply | 3.3 V |
| GND | GND |
| SDA | PB7 (JP2 pin 9) |
| SCL | PB8 (JP2 pin 10) |

PB7/PB8 are the alternate I2C1 pins exposed on the board headers. The default
STM32 I2C pins, PA9/PA10, are reserved for USB serial. The OLED uses SPI.
The sensor is detected at startup at address **0x40**; to use another address,
override `TELEM_INA219_ADDRESS` in the build flags. An absent sensor produces no
additional telemetry. The base builds do not initialize I2C or include the driver.

The measurement terminals (VIN+ and VIN-) go in series with the positive supply
of the circuit being measured; these are separate from the sensor's logic supply.
See the [INA219 wiring guide](https://learn.adafruit.com/adafruit-ina219-current-sensor-breakout/wiring).
USB power can bypass a sensor placed only in the battery lead.

Readings use environment telemetry permissions. Existing telemetry encodes current
in 1 mA steps and power in whole watts; it does not provide accumulated mAh or
battery percentage. INA219 telemetry has been confirmed on hardware with the
repeater build; the USB companion sensor build has been compile-tested only.

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
Built-in battery telemetry, SD storage, and low-power optimization are not implemented.

# Meshcore linux target

Based on https://github.com/meshtastic/framework-portduino by geeksville
RPI docs: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#raspberry-pi-zero-2-w

## RPI setup

The pinout: https://pinout.xyz/

Enable SPI in /boot/firmware/config.txt

```
dtparam=spi=on
dtoverlay=spi0-0cs
```

In case you use second SPI (CE1 pin on RPI GPIO) use: `spi0-0cs`

In case you use both SPI (CE0 and CE1 pin on RPI GPIO) use: `spi0-0cs`
I use it for hybrid meshtastic / meshcore setup in single box.

## Building

```
sudo apt-get install -y libbluetooth-dev libgpiod-dev openssl libssl-dev libusb-1.0-0-dev libi2c-dev libuv1-dev

python3 -m venv venv
source venv/bin/activate
pip install --no-cache-dir -U platformio

FIRMWARE_VERSION=dev ./build.sh build-firmware linux_repeater
```

The output should be in `./out/meshcored`

### Limitations

Currently following options DO NOT WORK:

```
advert_name = "Sample Router"
admin_password = "password"
lat = 0.0
lon = 0.0
```

It requires significant refactor in lots of files. It can be done in another PR.

You need to set them on compile stage with:

```
-D ADVERT_NAME='"Linux Repeater"'
-D ADVERT_LAT=0.0
-D ADVERT_LON=0.0
-D ADMIN_PASSWORD='"password"'
```

## Meshcored – Systemd service installation

```bash
# Install the binary

sudo cp ./variants/linux/meshcored.ini /etc/meshcored/meshcored.ini
sudo install -m 755 ./out/meshcored /usr/bin/meshcored

# Create a dedicated system user (recommended for security)

sudo useradd --system --no-create-home --shell /usr/sbin/nologin meshcore
sudo usermod -aG gpio,spi,dialout,plugdev meshcore
sudo mkdir -p /var/lib/meshcore
sudo chown meshcore:meshcore /var/lib/meshcore

# Install the systemd service file

sudo cp ./variants/linux/meshcored.service /etc/systemd/system/meshcored.service

# Enable and start the service

sudo systemctl daemon-reload
sudo systemctl enable --now meshcored.service
```

The default `meshcored.ini` includes optional GPIO pins you can override without rebuilding:
```
lora_nss_pin = 21
lora_busy_pin = 20
lora_irq_pin = 16
lora_reset_pin = 18
```

## Logs

TODO: add more logging

```
journalctl -u meshcored
```

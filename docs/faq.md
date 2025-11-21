**MeshCore-FAQ**<!-- omit from toc -->
A list of frequently-asked questions and answers for MeshCore

---

- [1. Introduction](#1-introduction)
  - [1.1. Q: What is MeshCore?](#11-q-what-is-meshcore)
  - [1.2. Q: What do you need to start using MeshCore?](#12-q-what-do-you-need-to-start-using-meshcore)
    - [1.2.1. Hardware](#121-hardware)
    - [1.2.2. Firmware](#122-firmware)
    - [1.2.3. Companion Radio Firmware](#123-companion-radio-firmware)
    - [1.2.4. Repeater](#124-repeater)
    - [1.2.5. Room Server](#125-room-server)
- [2. Initial Setup](#2-initial-setup)
  - [2.1. Q: How many devices do I need to start using MeshCore?](#21-q-how-many-devices-do-i-need-to-start-using-meshcore)
  - [2.2. Q: Does MeshCore cost any money?](#22-q-does-meshcore-cost-any-money)
  - [2.3. Q: What frequencies are supported by MeshCore?](#23-q-what-frequencies-are-supported-by-meshcore)
  - [2.4. Q: What is an "advert" in MeshCore?](#24-q-what-is-an-advert-in-meshcore)
  - [2.5. Q: Is there a hop limit?](#25-q-is-there-a-hop-limit)
- [3. Server Administration](#3-server-administration)
  - [3.1. Q: How do you configure a repeater or a room server?](#31-q-how-do-you-configure-a-repeater-or-a-room-server)
  - [3.2. Q: Do I need to set the location for a repeater?](#32-q-do-i-need-to-set-the-location-for-a-repeater)
  - [3.3. Q: What is the password to administer a repeater or a room server?](#33-q-what-is-the-password-to-administer-a-repeater-or-a-room-server)
  - [3.4. Q: What is the password to join a room server?](#34-q-what-is-the-password-to-join-a-room-server)
- [4. T-Deck Related](#4-t-deck-related)
  - [4.1. Q: Is there a user guide for T-Deck, T-Pager, T-Watch, or T-Display Pro?](#41-q-is-there-a-user-guide-for-t-deck-t-pager-t-watch-or-t-display-pro)
  - [4.2. Q: What are the steps to get a T-Deck into DFU (Device Firmware Update) mode?](#42-q-what-are-the-steps-to-get-a-t-deck-into-dfu-device-firmware-update-mode)
  - [4.3. Q: Why is my T-Deck Plus not getting any satellite lock?](#43-q-why-is-my-t-deck-plus-not-getting-any-satellite-lock)
  - [4.4. Q: Why is my OG (non-Plus) T-Deck not getting any satellite lock?](#44-q-why-is-my-og-non-plus-t-deck-not-getting-any-satellite-lock)
  - [4.5. Q: What size of SD card does the T-Deck support?](#45-q-what-size-of-sd-card-does-the-t-deck-support)
  - [4.6. Q: what is the public key for the default public channel?](#46-q-what-is-the-public-key-for-the-default-public-channel)
  - [4.7. Q: How do I get maps on T-Deck?](#47-q-how-do-i-get-maps-on-t-deck)
  - [4.8. Q: Where do the map tiles go?](#48-q-where-do-the-map-tiles-go)
  - [4.9. Q: How to unlock deeper map zoom and server management features on T-Deck?](#49-q-how-to-unlock-deeper-map-zoom-and-server-management-features-on-t-deck)
  - [4.10. Q: How to decipher the diagnostics screen on T-Deck?](#410-q-how-to-decipher-the-diagnostics-screen-on-t-deck)
  - [4.11. Q: The T-Deck sound is too loud?](#411-q-the-t-deck-sound-is-too-loud)
  - [4.12. Q: Can you customize the sound?](#412-q-can-you-customize-the-sound)
  - [4.13. Q: What is the 'Import from Clipboard' feature on the t-deck and is there a way to manually add nodes without having to receive adverts?](#413-q-what-is-the-import-from-clipboard-feature-on-the-t-deck-and-is-there-a-way-to-manually-add-nodes-without-having-to-receive-adverts)
  - [4.14. Q: How to capture a screenshot on T-Deck?](#414-q-how-to-capture-a-screenshot-on-t-deck)
- [5. General](#5-general)
  - [5.1. Q: What are BW, SF, and CR?](#51-q-what-are-bw-sf-and-cr)
  - [5.2. Q: Do MeshCore clients repeat?](#52-q-do-meshcore-clients-repeat)
  - [5.3. Q: What happens when a node learns a route via a mobile repeater, and that repeater is gone?](#53-q-what-happens-when-a-node-learns-a-route-via-a-mobile-repeater-and-that-repeater-is-gone)
  - [5.4. Q: How does a node discovery a path to its destination and then use it to send messages in the future, instead of flooding every message it sends like Meshtastic?](#54-q-how-does-a-node-discovery-a-path-to-its-destination-and-then-use-it-to-send-messages-in-the-future-instead-of-flooding-every-message-it-sends-like-meshtastic)
  - [5.5. Q: Do public channels always flood? Do private channels always flood?](#55-q-do-public-channels-always-flood-do-private-channels-always-flood)
  - [5.6. Q: what is the public key for the default public channel?](#56-q-what-is-the-public-key-for-the-default-public-channel)
  - [5.7. Q: Is MeshCore open source?](#57-q-is-meshcore-open-source)
  - [5.8. Q: How can I support MeshCore?](#58-q-how-can-i-support-meshcore)
  - [5.9. Q: How do I build MeshCore firmware from source?](#59-q-how-do-i-build-meshcore-firmware-from-source)
  - [5.10. Q: Are there other MeshCore related open source projects?](#510-q-are-there-other-meshcore-related-open-source-projects)
  - [5.11. Q: Does MeshCore support ATAK](#511-q-does-meshcore-support-atak)
  - [5.12. Q: How do I add a node to the MeshCore Map](#512-q-how-do-i-add-a-node-to-the-meshcore-map)
  - [5.13. Q: Can I use a Raspberry Pi to update a MeshCore radio?](#513-q-can-i-use-a-raspberry-pi-to-update-a-meshcore-radio)
  - [5.14. Q: Are there are projects built around MeshCore?](#514-q-are-there-are-projects-built-around-meshcore)
    - [5.14.1. meshcoremqtt](#5141-meshcoremqtt)
    - [5.14.2. MeshCore for Home Assistant](#5142-meshcore-for-home-assistant)
    - [5.14.3. Python MeshCore](#5143-python-meshcore)
    - [5.14.4. meshcore-cli](#5144-meshcore-cli)
    - [5.14.5. meshcore.js](#5145-meshcorejs)
    - [5.14.6. pyMC\_core](#5146-pymc_core)
- [6. Troubleshooting](#6-troubleshooting)
  - [6.1. Q: My client says another client or a repeater or a room server was last seen many, many days ago.](#61-q-my-client-says-another-client-or-a-repeater-or-a-room-server-was-last-seen-many-many-days-ago)
  - [6.2. Q: A repeater or a client or a room server I expect to see on my discover list (on T-Deck) or contact list (on a smart device client) are not listed.](#62-q-a-repeater-or-a-client-or-a-room-server-i-expect-to-see-on-my-discover-list-on-t-deck-or-contact-list-on-a-smart-device-client-are-not-listed)
  - [6.3. Q: How to connect to a repeater via BLE (Bluetooth)?](#63-q-how-to-connect-to-a-repeater-via-ble-bluetooth)
  - [6.4. Q: My companion isn't showing up over Bluetooth?](#64-q-my-companion-isnt-showing-up-over-bluetooth)
  - [6.5. Q: I can't connect via Bluetooth, what is the Bluetooth pairing code?](#65-q-i-cant-connect-via-bluetooth-what-is-the-bluetooth-pairing-code)
  - [6.6. Q: My Heltec V3 keeps disconnecting from my smartphone.  It can't hold a solid Bluetooth connection.](#66-q-my-heltec-v3-keeps-disconnecting-from-my-smartphone--it-cant-hold-a-solid-bluetooth-connection)
  - [6.7. Q: My RAK/T1000-E/xiao\_nRF52 device seems to be corrupted, how do I wipe it clean to start fresh?](#67-q-my-rakt1000-exiao_nrf52-device-seems-to-be-corrupted-how-do-i-wipe-it-clean-to-start-fresh)
  - [6.8. Q: WebFlasher fails on Linux with failed to open](#68-q-webflasher-fails-on-linux-with-failed-to-open)
- [7. Other Questions:](#7-other-questions)
  - [7.1. Q: How to update nRF (RAK, T114, Seed XIAO) repeater and room server firmware over the air using the new simpler DFU app?](#71-q-how-to-update-nrf-rak-t114-seed-xiao-repeater-and-room-server-firmware-over-the-air-using-the-new-simpler-dfu-app)
  - [7.2. Q: How to update ESP32-based devices over the air?](#72-q-how-to-update-esp32-based-devices-over-the-air)
  - [7.3. Q: Is there a way to lower the chance of a failed OTA device firmware update (DFU)?](#73-q-is-there-a-way-to-lower-the-chance-of-a-failed-ota-device-firmware-update-dfu)
  - [7.4. Q: are the MeshCore logo and font available?](#74-q-are-the-meshcore-logo-and-font-available)
  - [7.5. Q: What is the format of a contact or channel QR code?](#75-q-what-is-the-format-of-a-contact-or-channel-qr-code)
  - [7.6. Q: How do I connect to the companion via WIFI, e.g. using a heltec v3?](#76-q-how-do-i-connect-to-the-companion-via-wifi-eg-using-a-heltec-v3)

## 1. Introduction

### 1.1. Q: What is MeshCore?

**A:** MeshCore is a multi platform system for enabling secure text based communications utilising LoRa radio hardware. It can be used for Off-Grid Communication, Emergency Response & Disaster Recovery, Outdoor Activities, Tactical Security including law enforcement and private security and also IoT sensor networks. ([source](https://meshcore.co.uk/))

MeshCore is free and open source:
* MeshCore is the routing and firmware etc, available on GitHub under MIT license
* There are clients made by the community, such as the web clients, these are free to use, and some are open source too
* The cross platform mobile app developed by [Liam Cottle](https://liamcottle.net) for Android/iOS/PC etc is free to download and use
* The T-Deck firmware is developed by Scott at Ripple Radios, the creator of MeshCore, is also free to flash on your devices and use


Some more advanced, but optional features are available on T-Deck if you register your device for a key to unlock.  On the MeshCore smartphone clients for Android and iOS/iPadOS, you can unlock the wait timer for repeater and room server remote management over RF feature.

These features are completely optional and aren't needed for the core messaging experience. They're like super bonus features and to help the developers continue to work on these amazing features, they may charge a small fee for an unlock code to utilise the advanced features.

Anyone is able to build anything they like on top of MeshCore without paying anything.

### 1.2. Q: What do you need to start using MeshCore?
**A:** Everything you need for MeshCore is available at:
 Main web site: [https://meshcore.co.uk/](https://meshcore.co.uk/)
 Firmware Flasher: https://flasher.meshcore.co.uk/
 Phone Client Applications: https://meshcore.co.uk/apps.html
 MeshCore Firmware GitHub: https://github.com/ripplebiz/MeshCore

 NOTE: Andy Kirby has a very useful [intro video](https://www.youtube.com/watch?v=t1qne8uJBAc) for beginners.


 You need LoRa hardware devices to run MeshCore firmware as clients or server (repeater and room server).

#### 1.2.1. Hardware
MeshCore is available on a variety of 433MHz, 868MHz and 915MHz LoRa devices. For example, Lilygo T-Deck, T-Pager, RAK Wireless WisBlock RAK4631 devices (e.g. 19003, 19007, 19026), Heltec V3, Xiao S3 WIO, Xiao C3, Heltec T114, Station G2, Nano G2 Ultra, Seeed Studio T1000-E. More devices are being added regularly.

For an up-to-date list of supported devices, please go to https://flasher.meshcore.co.uk/

To use MeshCore without using a phone as the client interface, you can run MeshCore on a LiLygo's T-Deck, T-Deck Plus, T-Pager, T-Watch, or T-Display Pro. MeshCore Ultra firmware running on these devices are a complete off-grid secure communication solution.

#### 1.2.2. Firmware
MeshCore has four firmware types that are not available on other LoRa systems. MeshCore has the following:

#### 1.2.3. Companion Radio Firmware
Companion radios are for connecting to the Android app or web app as a messenger client. There are two different companion radio firmware versions:

1. **BLE Companion**
   BLE Companion firmware runs on a supported LoRa device and connects to a smart device running the Android or iOS MeshCore client over BLE
   <https://meshcore.co.uk/apps.html>

2. **USB Serial Companion**
   USB Serial Companion firmware runs on a supported LoRa device and connects to a smart device or a computer over USB Serial running the MeshCore web client
   <https://meshcore.liamcottle.net/#/>
   <https://client.meshcore.co.uk/tabs/devices>

#### 1.2.4. Repeater
Repeaters are used to extend the range of a MeshCore network. Repeater firmware runs on the same devices that run client firmware. A repeater's job is to forward MeshCore packets to the destination device. It does **not** forward or retransmit every packet it receives, unlike other LoRa mesh systems.

A repeater can be remotely administered using a T-Deck running the MeshCore firmware with remote administration features unlocked, or from a BLE Companion client connected to a smartphone running the MeshCore app.

#### 1.2.5. Room Server
A room server is a simple BBS server for sharing posts. T-Deck devices running MeshCore firmware or a BLE Companion client connected to a smartphone running the MeshCore app can connect to a room server.

Room servers store message history on them and push the stored messages to users.  Room servers allow roaming users to come back later and retrieve message history.  With channels, messages are either received when it's sent, or not received and missed if the channel user is out of range.  Room servers are different and more like email servers where you can come back later and get your emails from your mail server.

A room server can be remotely administered using a T-Deck running the MeshCore firmware with remote administration features unlocked, or from a BLE Companion client connected to a smartphone running the MeshCore app.

When a client logs into a room server, the client will receive the previously 32 unseen messages.

Although room server can also repeat with the command line command `set repeat on`, it is not recommended nor encouraged.  A room server with repeat set to `on` lacks the full set of repeater and remote administration features that are only available in the repeater firmware.

The recommendation is to run repeater and room server on separate devices for the best experience.



---

## 2. Initial Setup

### 2.1. Q: How many devices do I need to start using MeshCore?
**A:** If you have one supported device, flash the BLE Companion firmware and use your device as a client.  You can connect to the device using the Android or iOS client via Bluetooth.  You can start communicating with other MeshCore users near you.

If you have two supported devices, and there are not many MeshCore users near you, flash both to BLE Companion firmware so you can use your devices to communicate with your near-by friends and family.

If you have two supported devices, and there are other MeshCore users nearby, you can flash one of your devices with BLE Companion firmware and flash another supported device to repeater firmware.  Place the repeater high above ground to extend your MeshCore network's reach.

After you flashed the latest firmware onto your repeater device, keep the device connected to your computer via USB serial, use the console feature on the web flasher and set the frequency for your region or country, so your client can remote administer the repeater or room server over RF:

`set freq {frequency}`

The repeater and room server CLI reference is here: https://github.com/meshcore-dev/MeshCore/wiki/Repeater-&-Room-Server-CLI-Reference

If you have more supported devices, you can use your additional devices with the room server firmware.

### 2.2. Q: Does MeshCore cost any money?

**A:** All radio firmware versions (e.g. for Heltec V3, RAK, T-1000E, etc) are free and open source developed by Scott at Ripple Radios.

The native Android and iOS client uses the freemium model and is developed by Liam Cottle, developer of meshtastic map at [meshtastic.liamcottle.net](https://meshtastic.liamcottle.net) on [GitHub](https://github.com/liamcottle/meshtastic-map) and [reticulum-meshchat on github](https://github.com/liamcottle/reticulum-meshchat).

The T-Deck firmware is free to download and most features are available without cost.  To support the firmware developer, you can pay for a registration key to unlock your T-Deck for deeper map zoom and remote server administration over RF using the T-Deck.  You do not need to pay for the registration to use your T-Deck for direct messaging and connecting to repeaters and room servers.


### 2.3. Q: What frequencies are supported by MeshCore?
**A:** It supports the 868MHz range in the UK/EU and the 915MHz range in New Zealand, Australia, and the USA. Countries and regions in these two frequency ranges are also supported.

Use the smartphone client or the repeater setup feature on there web flasher to set your radios' RF settings by choosing the preset for your regions.

Recently, as of October 2025, many regions have moved to the "narrow" setting, aka using BW62.5 and a lower SF number (instead of the original SF11).  For example, USA/Canada (Recommended) preset is 910.525MHz, SF7, BW62.5, CR5.

After extensive testing, many regions have switched or about to switch over to BW62.5 and SF7, 8, or 9.  Narrower bandwidth setting and lower SF setting allow MeshCore's radio signals to fit between interference in the ISM band, provide for a lower noise floor, better SNR, and faster transmissions.

If you have consensus from your community in your region to update your region's preset recommendation, please post your update request on  the [#meshcore-app](https://discord.com/channels/1343693475589263471/1391681655911088241) channel on the [MeshCore Discord server ](https://discord.gg/cYtQNYCCRK) to let Liam Cottle know.



### 2.4. Q: What is an "advert" in MeshCore?
**A:**
Advert means to advertise yourself on the network. In Reticulum terms it would be to announce. In Meshtastic terms it would be the node sending its node info.

MeshCore allows you to manually broadcast your name, position and public encryption key, which is also signed to prevent spoofing.  When you click the advert button, it broadcasts that data over LoRa.  MeshCore calls that an Advert. There's two ways to advert, "zero hop" and "flood".

* Zero hop means your advert is broadcasted out to anyone that can hear it, and that's it.
* Flooded means it's broadcasted out and then repeated by all the repeaters that hear it.

MeshCore clients only advertise themselves when the user initiates it. A repeater sends a flood advert once every 3 hours by default. This interval can be configured using the following command:

`set advert.interval {minutes}`

As of Aug 20 2025, a pending PR on github will change the flood advert to 12 hours to minimize airtime utilization caused by repeaters' flood adverts.

### 2.5. Q: Is there a hop limit?

**A:** Internally the firmware has maximum limit of 64 hops.  In real world settings it will be difficult to get close to the limit due to the environments and timing as packets travel further and further.  We want to hear how far your MeshCore conversations go.


---


## 3. Server Administration

### 3.1. Q: How do you configure a repeater or a room server?

**A:** - When MeshCore is flashed onto a LoRa device is for the first time, it is necessary to set the server device's frequency to make it utilize the frequency that is legal in your country or region.

Repeater or room server can be administered with one of the options below:

- After a repeater or room server firmware is flashed on to a LoRa device, go to <https://config.meshcore.dev> and use the web user interface to connect to the LoRa device via USB serial.  From there you can set the name of the server, its frequency and other related settings, location, passwords etc.

![image](https://github.com/user-attachments/assets/2a9d9894-e34d-4dbe-b57c-fc3c250a2d34)

- Connect the server device using a USB cable to a computer running Chrome on https://flasher.meshcore.co.uk/, then use the `console` feature to connect to the device

- Use a MeshCore smartphone clients  to remotely administer servers via LoRa.

- A T-Deck running unlocked/registered MeshCore firmware. Remote server administration is enabled through registering your T-Deck with Ripple Radios. It is one of the ways to support MeshCore development. You can register your T-Deck at:

<https://buymeacoffee.com/ripplebiz/e/249834>



### 3.2. Q: Do I need to set the location for a repeater?
**A:** While not required, with location set for a repeater it will show up on the MeshCore map in the future. Set location with the following command:

`set lat <GPS Lat> set long <GPS Lon>`

You can get the latitude and longitude from Google Maps by right-clicking the location you are at on the map.

### 3.3. Q: What is the password to administer a repeater or a room server?
**A:** The default admin password to a repeater and room server is `password`. Use the following command to change the admin password:

`password {new-password}`


### 3.4. Q: What is the password to join a room server?
**A:** The default guest password to a room server is `hello`. Use the following command to change the guest password:

`set guest.password {guest-password}`


---

## 4. T-Deck Related

### 4.1. Q: Is there a user guide for T-Deck, T-Pager, T-Watch, or T-Display Pro?

**A:** Yes, it is available on https://buymeacoffee.com/ripplebiz/ultra-v7-7-guide-meshcore-users

### 4.2. Q: What are the steps to get a T-Deck into DFU (Device Firmware Update) mode?
**A:**
1. Device off
2. Connect USB cable to device
3. Hold down trackball (keep holding)
4. Turn on device
5. Hear USB connection sound
6. Release trackball
7. T-Deck in DFU mode now
8. At this point you can begin flashing using <https://flasher.meshcore.co.uk/>

### 4.3. Q: Why is my T-Deck Plus not getting any satellite lock?
**A:** For T-Deck Plus, the GPS baud rate should be set to **38400**. Also, some T-Deck Plus devices were found to have the GPS module installed upside down, with the GPS antenna facing down instead of up. If your T-Deck Plus still doesn't get any satellite lock after setting the baud rate to 38400, you might need to open the device to check the GPS orientation.

GPS on T-Deck is always enabled.  You can skip the "GPS clock sync" and the T-Deck will continue to try to get a GPS lock.  You can go to the `GPS Info` screen; you should see the `Sentences:` counter increasing if the baud rate is correct.

[Source](https://discord.com/channels/826570251612323860/1330643963501351004/1356609240302616689)

### 4.4. Q: Why is my OG (non-Plus) T-Deck not getting any satellite lock?
**A:** The OG (non-Plus) T-Deck doesn't come with a GPS. If you added a GPS to your OG T-Deck, please refer to the manual of your GPS to see what baud rate it requires. Alternatively, you can try to set the baud rate from 9600, 19200, etc., and up to 115200 to see which one works.

### 4.5. Q: What size of SD card does the T-Deck support?
**A:** Users have had no issues using 16GB or 32GB SD cards. Format the SD card to **FAT32**.

### 4.6. Q: what is the public key for the default public channel?
**A:**
T-Deck uses the same key the smartphone apps use but in base64
`izOH6cXN6mrJ5e26oRXNcg==`
The third character is the capital letter 'O', not zero `0`

The smartphone app key is in hex:
` 8b3387e9c5cdea6ac9e5edbaa115cd72`

[Source](https://discord.com/channels/826570251612323860/1330643963501351004/1354194409213792388)

### 4.7. Q: How do I get maps on T-Deck?
**A:** You need map tiles. You can get pre-downloaded map tiles here (a good way to support development):
- <https://buymeacoffee.com/ripplebiz/e/342543> (Europe)
- <https://buymeacoffee.com/ripplebiz/e/342542> (US)

Another way to download map tiles is to use this Python script to get the tiles in the areas you want:
<https://github.com/fistulareffigy/MTD-Script>

There is also a modified script that adds additional error handling and parallel downloads:
<https://discord.com/channels/826570251612323860/1330643963501351004/1338775811548905572>

UK map tiles are available separately from Andy Kirby on his discord server:
<https://discord.com/channels/826570251612323860/1330643963501351004/1331346597367386224>

### 4.8. Q: Where do the map tiles go?
Once you have the tiles downloaded, copy the `\tiles` folder to the root of your T-Deck's SD card.

### 4.9. Q: How to unlock deeper map zoom and server management features on T-Deck?
**A:** You can download, install, and use the T-Deck firmware for free, but it has some features (map zoom, server administration) that are enabled if you purchase an unlock code for \$10 per T-Deck device.
Unlock page: <https://buymeacoffee.com/ripplebiz/e/249834>

### 4.10. Q: How to decipher the diagnostics screen on T-Deck?

**A: ** Space is tight on T-Deck's screen, so the information is a bit cryptic.  The format is :
`{hops} l:{packet-length}({payload-len}) t:{packet-type} snr:{n} rssi:{n}`

See here for packet-type:
https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h#L19


    #define PAYLOAD_TYPE_REQ 0x00 // request (prefixed with dest/src hashes, MAC) (enc data: timestamp, blob)
    #define PAYLOAD_TYPE_RESPONSE 0x01 // response to REQ or ANON_REQ (prefixed with dest/src hashes, MAC) (enc data: timestamp, blob)
    #define PAYLOAD_TYPE_TXT_MSG 0x02 // a plain text message (prefixed with dest/src hashes, MAC) (enc data: timestamp, text)
    #define PAYLOAD_TYPE_ACK 0x03 // a simple ack #define PAYLOAD_TYPE_ADVERT 0x04 // a node advertising its Identity
    #define PAYLOAD_TYPE_GRP_TXT 0x05 // an (unverified) group text message (prefixed with channel hash, MAC) (enc data: timestamp, "name: msg")
    #define PAYLOAD_TYPE_GRP_DATA 0x06 // an (unverified) group datagram (prefixed with channel hash, MAC) (enc data: timestamp, blob)
    #define PAYLOAD_TYPE_ANON_REQ 0x07 // generic request (prefixed with dest_hash, ephemeral pub_key, MAC) (enc data: ...)
    #define PAYLOAD_TYPE_PATH 0x08 // returned path (prefixed with dest/src hashes, MAC) (enc data: path, extra)

[Source](https://discord.com/channels/1343693475589263471/1343693475589263474/1350611321040932966)

### 4.11. Q: The T-Deck sound is too loud?
### 4.12. Q: Can you customize the sound?

**A:** You can customise the sounds on the T-Deck, by placing `.mp3` files onto the `root` dir of the SD card. The files are:

* `startup.mp3`
* `error.mp3`
* `alert.mp3`
* `new-advert.mp3`
* `existing-advert.mp3`

### 4.13. Q: What is the 'Import from Clipboard' feature on the t-deck and is there a way to manually add nodes without having to receive adverts?

**A:** 'Import from Clipboard' is for importing a contact via a file named 'clipboard.txt' on the SD card. The opposite, is in the Identity screen, the 'Card to Clipboard' menu, which writes to 'clipboard.txt' so you can share yourself (call these 'biz cards', that start with "meshcore://...")

### 4.14. Q: How to capture a screenshot on T-Deck?

**A:** To capture a screenshot on a T-Deck, long press the top-left corner of the screen.  The screenshot is saved to the microSD card, if one is inserted into the device.

---

## 5. General

### 5.1. Q: What are BW, SF, and CR?

**A:**

**BW is bandwidth** - width of frequency spectrum that is used for transmission

**SF is spreading factor** - how much should the communication spread in time

**CR is coding rate** - from: https://www.thethingsnetwork.org/docs/lorawan/fec-and-code-rate/

TL;DR: default CR to 5 for good stable links.  If it is not a solid link and is intermittent, change to CR to 7 or 8.

Forward Error Correction is a process of adding redundant bits to the data to be transmitted. During the transmission, data may get corrupted by interference (changes from 0 to 1 / 1 to 0). These error correction bits are used at the receivers for restoring corrupted bits.

The Code Rate of a forward error correction expresses the proportion of bits in a data stream that actually carry useful information.

There are 4 code rates used in LoRaWAN:

4/5
4/6
5/7
4/8

For example, if the code rate is 5/7, for every 5 bits of useful information, the coder generates a total of 7 bits of data, of which 2 bits are redundant.

Making the bandwidth 2x wider (from BW125 to BW250) allows you to send 2x more bytes in the same time. Making the spreading factor 1 step lower (from SF10 to SF9) allows you to send 2x more bytes in the same time.

Lowering the spreading factor makes it more difficult for the gateway to receive a transmission, as it will be more sensitive to noise. You could compare this to two people taking in a noisy place (a bar for example). If you’re far from each other, you have to talk slow (SF10), but if you’re close, you can talk faster (SF7)

So, it's balancing act between speed of the transmission and resistance to noise.
things network is mainly focused on LoRaWAN, but the LoRa low-level stuff still checks out for any LoRa project

### 5.2. Q: Do MeshCore clients repeat?
**A:** No, MeshCore clients do not repeat.  This is the core of MeshCore's messaging-first design.  This is to avoid devices flooding the air ware and create endless collisions, so messages sent aren't received.
In MeshCore, only repeaters and room server with `set repeat on` repeat.

### 5.3. Q: What happens when a node learns a route via a mobile repeater, and that repeater is gone?

**A:** If you used to reach a node through a repeater and the repeater is no longer reachable, the client will send the message using the existing (but now broken) known path, the message will fail after 3 retries, and the app will reset the path and send the message as flood on the last retry by default.  This can be turned off in settings.  If the destination is reachable directly or through another repeater, the new path will be used going forward.  Or you can set the path manually if you know a specific repeater to use to reach that destination.

In the case if users are moving around frequently, and the paths are breaking, they just see the phone client retries and revert to flood to attempt to re-establish a path.

### 5.4. Q: How does a node discovery a path to its destination and then use it to send messages in the future, instead of flooding every message it sends like Meshtastic?

Routes are stored in sender's contact list.  When you send a message the first time, the message first gets to your destination by flood routing. When your destination node gets the message, it will send back a delivery report to the sender with all repeaters that the original message went through. This delivery report is flood-routed back to you the sender and is a basis for future direct path. When you send the next message, the path will get embedded into the packet and be evaluated by repeaters. If the hop and address of the repeater matches, it will retransmit the message, otherwise it will not retransmit, hence minimizing utilization.

[Source](https://discord.com/channels/826570251612323860/1330643963501351004/1351279141630119996)

### 5.5. Q: Do public channels always flood? Do private channels always flood?

**A:** Yes, group channels are A to B, so there is no defined path.  They have to flood.  Repeaters can however deny flood traffic up to some hop limit, with the `set flood.max` CLI command. Administrators of repeaters get to set the rules of their repeaters.

[Source](https://discord.com/channels/1343693475589263471/1343693475589263474/1350023009527664672)


### 5.6. Q: what is the public key for the default public channel?
**A:** The smartphone app key is in hex:
` 8b3387e9c5cdea6ac9e5edbaa115cd72`

T-Deck uses the same key but in base64
`izOH6cXN6mrJ5e26oRXNcg==`
The third character is the capital letter 'O', not zero `0`
[Source](https://discord.com/channels/826570251612323860/1330643963501351004/1354194409213792388)

### 5.7. Q: Is MeshCore open source?
**A:** Most of the firmware is freely available. Everything is open source except the T-Deck firmware and Liam's native mobile apps.
- Firmware repo: https://github.com/meshcore-dev/MeshCore

### 5.8. Q: How can I support MeshCore?
**A:** Provide your honest feedback on GitHub and on [MeshCore Discord server](https://discord.gg/BMwCtwHj5V). Spread the word of MeshCore to your friends and communities; help them get started with MeshCore. Support Scott's MeshCore development at <https://buymeacoffee.com/ripplebiz>.

Support Liam Cottle's smartphone client development by unlocking the server administration wait gate with in-app purchase

Support Rastislav Vysoky (recrof)'s flasher web site and the map web site development through [PayPal](https://www.paypal.com/donate/?business=DREHF5HM265ES&no_recurring=0&item_name=If+you+enjoy+my+work%2C+you+can+support+me+here%3A&currency_code=EUR) or [Revolut](https://revolut.me/recrof)

### 5.9. Q: How do I build MeshCore firmware from source?
**A:** See instructions here:
https://discord.com/channels/826570251612323860/1330643963501351004/1341826372120608769

Build instructions for MeshCore:

For Windows, first install WSL and Python+pip via: https://plainenglish.io/blog/setting-up-python-on-windows-subsystem-for-linux-wsl-26510f1b2d80

(Linux, Windows+WSL) In the terminal/shell:
```
sudo apt update
sudo apt install libpython3-dev
sudo apt install python3-venv
```
Mac: python3 should be already installed.

Then it should be the same for all platforms:
```
python3 -m venv meshcore
cd meshcore && source bin/activate
pip install -U platformio
git clone https://github.com/ripplebiz/MeshCore.git
cd MeshCore
```
open platformio.ini and in `[arduino_base]` edit the `LORA_FREQ=867.5`
save, then run:
```
pio run -e RAK_4631_Repeater
```
then you'll find `firmware.zip` in `.pio/build/RAK_4631_Repeater`

Andy also has a video on how to build using VS Code:
*How to build and flash Meshcore repeater firmware | Heltec V3*
<https://www.youtube.com/watch?v=WJvg6dt13hk> *(Link referenced in the Discord post)*

### 5.10. Q: Are there other MeshCore related open source projects?

**A:** [Liam Cottle](https://liamcottle.net)'s MeshCore web client and MeshCore Javascript library are open source under MIT license.

Web client: https://github.com/liamcottle/meshcore-web
Javascript: https://github.com/liamcottle/meshcore.js

### 5.11. Q: Does MeshCore support ATAK
**A:** ATAK is not currently on MeshCore's roadmap.

Meshcore would not be best suited to ATAK because MeshCore:
clients do not repeat and therefore you would need a network of repeaters in place
will not have a stable path where all clients are constantly moving between repeaters

MeshCore clients would need to reset path constantly and flood traffic across the network which could lead to lots of collisions with something as chatty as ATAK.

This could change in the future if MeshCore develops a client firmware that repeats.
[Source](https://discord.com/channels/826570251612323860/1330643963501351004/1354780032140054659)

### 5.12. Q: How do I add a node to the [MeshCore Map]([url](https://meshcore.co.uk/map.html))
**A:**

To add a BLE Companion radio, connect to the BLE Companion radio from the MeshCore smartphone app.  In the app, tap the `3 dot` menu icon at the top right corner, then tap `Internet Map`.  Tap the `3 dot` menu icon again and choose `Add me to the Map`

To add a Repeater or Room Server to the map, go to the Contact List, tap the `3 dot` next to the Repeater or Room Server you want to add to the Internet Map, tap `Share`, then tap `Upload to Internet Map`.

You can use the same companion (same public key) that you used to add your repeaters or room servers to remove them from the Internet Map.


### 5.13. Q: Can I use a Raspberry Pi to update a MeshCore radio?
** A:** Yes.
Below are the instructions to flash firmware onto a supported LoRa device using a Raspberry Pi over USB serial.

> Instructions for nRF devices like RAK, T1000-E, T114 are immediately after the ESP instructions

For ESP-based devices (e.g. Heltec V3) you need:
- Download firmware file from flasher.meshcore.co.uk
    - Go to the web site on a browser, find the section that has the firmware up need
    - Click the Download button, right click on the file you need, for example,
        - `Heltec_V3_companion_radio_ble-v1.7.1-165fb33.bin`
            - Non-merged bin keeps the existing Bluetooth pairing database
        - `Heltec_v3_companion_radio_usb-v1.7.1-165fb33-merged.bin`
            - Merged bin overwrites everything including the bootloader, existing Bluetooth pairing database, but keeps configurations.
        - Right click on the file name and copy the link and note it for later use here is an example: `https://flasher.meshcore.dev/releases/download/companion-v1.7.1/Heltec_v3_companion_radio_ble-v1.7.1-165fb33.bin`
        - Run:
            - `wget https://flasher.meshcore.dev/releases/download/companion-v1.7.1/Heltec_v3_companion_radio_ble-v1.7.1-165fb33.bin` to download the firmware file for your device type. or the version you need  - USB, BLE, Repeater, Room Server, merged bin or non-merged bin
            - If the above wget command only downloads a very small file (10K bytes instead of more than 100K byte, use this command instead:
                - `wget --user-agent="Mozilla/5.0" --content-disposition "https://flasher.meshcore.dev/releases/download/companion-v1.7.1/Heltec_v3_companion_radio_usb-v1.7.1-165fb33.bin"`
    - Confirm the `ttyXXXX` device path on your Raspberry Pi:
        - Go to `/dev` directory, run ls command to find confirm your device path
        - They are usually `/dev/ttyUSB0` for ESP devices
        - For ESP-based devices, install esptool from the shell:
            - `pip install esptool --break-system-packages`
    - To flash, use the following command:
        - For non-merged bin:
            - `esptool.py -p /dev/ttyUSB0 --chip esp32-s3 write_flash 0x10000 <non-merged_firmware>.bin`
        - For merged bin:
            - `esptool.py -p /dev/ttyUSB0 --chip esp32-s3 write_flash 0x00000 <merged_firmware>.bin`



**Instructions for nRF devices:**

For nRF devices (e.g. RAK, Heltec T114) you need the following:
- Download firmware file from flasher.meshcore.co.uk
    - Go to the web site on a browser, find the section that has the firmware up need
    - You need the ZIP version for the adafruit flash tool (below)
    - Click the Download button, right click on the ZIP file, for example:
        - `RAK_4631_companion_radio_ble-v1.7.1-165fb33.zip`
        - Right click on the file name and copy the link and note it for later use here is an example: `https://flasher.meshcore.dev/releases/download/companion-v1.7.1/RAK_4631_companion_radio_ble-v1.7.1-165fb33.zip`
        - Run:
        - `wget https://flasher.meshcore.dev/releases/download/companion-v1.7.1/RAK_4631_companion_radio_ble-v1.7.1-165fb33.zip` to download the firmware file for your device type. or the version you need  - USB, BLE, Repeater, Room Server, ZIP file only
    - Confirm the `ttyXXXX` device path on your Raspberry Pi:
        - Go to `/dev` directory, run ls command to find confirm your device path
        - They are usually `/dev/ttyACM0` for nRF devices
    - For nRF-based devices, install adafruit-nrfutil
        - `pip install adafruit-nrfutil --break-system-packages`
        - Use this command to flash the nRF device:
            - `adafruit-nrfutil --verbose dfu serial --package RAK_4631_companion_radio_usb-v1.7.1-165fb33.zip -p /dev/ttyACM0 -b 115200 --singlebank --touch 1200`


To manage a repeater or room server connected to a Pi over USB serial using shell commands, you need to install `picocom`.  To install `picocom`, run the following command:
- `sudo apt install picocom`

To start managing your USB serial-connected device using picocom, use the following command:
    - `picocom -b 115200 /dev/ttyUSB0 --imap lfcrlf`

From here, reference repeater and room server command line commands on MeshCore github wiki here:
    - https://github.com/meshcore-dev/MeshCore/wiki/Repeater-&-Room-Server-CLI-Reference


### 5.14. Q: Are there are projects built around MeshCore?

**A:** Yes.  See the following:

#### 5.14.1. meshcoremqtt
A Python script to send meshcore debug and packet capture data to MQTT for analysis.  Cisien's version is a fork of Andrew-a-g's and is being used to to collect data for https://map.w0z.is/messages and https://analyzer.letsme.sh/
https://github.com/Cisien/meshcoretomqtt
https://github.com/Andrew-a-g/meshcoretomqtt

#### 5.14.2. MeshCore for Home Assistant
A custom Home Assistant integration for MeshCore mesh radio nodes. It allows you to monitor and control MeshCore nodes via USB, BLE, or TCP connections.
https://github.com/awolden/meshcore-ha

#### 5.14.3. Python MeshCore
Bindings to access your MeshCore companion radio nodes in python.
https://github.com/fdlamotte/meshcore_py

#### 5.14.4. meshcore-cli
CLI interface to MeshCore companion radio over BLE, TCP, or serial.  Uses Python MeshCore above.
 https://github.com/fdlamotte/meshcore-cli

#### 5.14.5. meshcore.js
A JavaScript library for interacting with a MeshCore device running the companion radio firmware
https://github.com/liamcottle/meshcore.js

#### 5.14.6. pyMC_core
pyMC_Core is a Python port of MeshCore, designed for Raspberry Pi and similar hardware, it talks to LoRa modules over SPI.
https://github.com/rightup/pyMC_core

---

## 6. Troubleshooting

### 6.1. Q: My client says another client or a repeater or a room server was last seen many, many days ago.
### 6.2. Q: A repeater or a client or a room server I expect to see on my discover list (on T-Deck) or contact list (on a smart device client) are not listed.
**A:**
- If your client is a T-Deck, it may not have its time set (no GPS installed, no GPS lock, or wrong GPS baud rate).
- If you are using the Android or iOS client, the other client, repeater, or room server may have the wrong time.

You can get the epoch time on <https://www.epochconverter.com/> and use it to set your T-Deck clock. For a repeater and room server, the admin can use a T-Deck to remotely set their clock (clock sync), or use the `time` command in the USB serial console with the server device connected.

### 6.3. Q: How to connect to a repeater via BLE (Bluetooth)?
**A:** You can't connect to a device running repeater firmware  via Bluetooth.  Devices running the BLE companion firmware you can connect to it via Bluetooth using the android app

### 6.4. Q: My companion isn't showing up over Bluetooth?

**A:** make sure that you flashed the Bluetooth companion firmware and not the USB-only companion firmware.

### 6.5. Q: I can't connect via Bluetooth, what is the Bluetooth pairing code?

**A:** the default Bluetooth pairing code is `123456`

### 6.6. Q: My Heltec V3 keeps disconnecting from my smartphone.  It can't hold a solid Bluetooth connection.

**A:** Heltec V3 has a very small coil antenna on its PCB for Wi-Fi and Bluetooth connectivity.  It has a very short range, only a few feet.  It is possible to remove the coil antenna  and replace it with a 31mm wire.  The BT range is much improved with the modification.

### 6.7. Q: My RAK/T1000-E/xiao_nRF52 device seems to be corrupted, how do I wipe it clean to start fresh?

**A:**
1. Connect USB-C cable to your device, per your device's instruction, get it to flash mode:
    - For RAK, click the reset button **TWICE**
    - For T1000-e, quickly disconnect and reconnect the magnetic side of the cable from the device **TWICE**
    - For Heltec T114, click the reset button **TWICE** (the bottom button)
    - For Xiao nRF52, click the reset button once.  If that doesn't work, quickly double click the reset button twice.  If that doesn't work, disconnection the board from your PC and reconnect again ([seeed studio wiki](https://wiki.seeedstudio.com/XIAO_BLE/#access-the-swd-pins-for-debugging-and-reflashing-bootloader))
5. A new folder will appear on your computer's desktop
6. Download the `flash_erase*.uf2` file for your device on flasher.meshcore.co.uk
    - RAK WisBlock and Heltec T114: `Flash_erase-nRF32_softdevice_v6.uf2`
    - Seeed Studio Xiao nRF52 WIO: `Flash_erase-nRF52_softdevice_v7.uf2`
8. drag and drop the uf2 file for your device to the root of the new folder
9. Wait for the copy to complete.  You might get an error dialog, you can ignore it
10. Go to https://flasher.meshcore.co.uk/, click `Console` and select the serial port for your connected device
11. In the console, press enter.  Your flash should now be erased
12. You may now flash the latest MeshCore firmware onto your device

Separately, starting in firmware version 1.7.0, there is a CLI Rescue mode.  If your device has a user button (e.g. some RAK, T114), you can activate the rescue mode by hold down the user button of the device within 8 seconds of boot.  Then you can use the 'Console' on flasher.meshcore.co.uk

### 6.8. Q: WebFlasher fails on Linux with failed to open

**A:** If the usb port doesn't have the right ownership for this task, the process fails with the following error:
`NetworkError: Failed to execute 'open' on 'SerialPort': Failed to open serial port.`

Allow the browser user on it:
`# setfacl -m u:YOUR_USER_HERE:rw /dev/ttyUSB0`

---
## 7. Other Questions:

### 7.1. Q: How to update nRF (RAK, T114, Seed XIAO) repeater and room server firmware over the air using the new simpler DFU app?

**A:** The steps below work on both Android and iOS as nRF has made both apps' user interface the same on both platforms:

1. Download nRF's DFU app from iOS App Store or Android's Play Store, you can find the app by searching for `nrf dfu`, the app's full name is `nRF Device Firmware Update`
2. On flasher.meshcore.co.uk, download the **ZIP** version of the firmware for your nRF device (e.g. RAK or Heltec T114 or Seeed Studio's Xiao)
3. From the MeshCore app, login remotely to the repeater you want to update with admin privilege
4. Go to the Command Line tab, type `start ota` and hit enter.
5. you should see `OK` to confirm the repeater device is now in OTA mode
6. Run the DFU app,tab `Settings` on the top right corner
7. Enable `Packets receipt notifications`, and change `Number of Packets` to 10 for RAK, 8 for T114.  8 also works for RAK.
9. Select the firmware zip file you downloaded
10. Select the device you want to update. If the device you want to update is not on the list, try enabling`OTA` on the device again
11. If the device is not found, enable `Force Scanning` in the DFU app
12. Tab the `Upload` to begin OTA update
13. If it fails, try turning off and on Bluetooth on your phone.  If that doesn't work, try rebooting your phone.
14. Wait for the update to complete.  It can take a few minutes.


### 7.2. Q: How to update ESP32-based devices over the air?

**A:** For ESP32-based devices (e.g. Heltec V3):
1. On flasher.meshcore.co.uk, download the **non-merged** version of the firmware for your ESP32 device (e.g. `Heltec_v3_repeater-v1.6.2-4449fd3.bin`, no `"merged"` in the file name)
2. From the MeshCore app, login remotely to the repeater you want to update with admin privilege
4. Go to the Command Line tab, type `start ota` and hit enter.
5. you should see `OK` to confirm the repeater device is now in OTA mode
6. The command `start ota` on an ESP32-based device starts a wifi hotspot named `MeshCore OTA`
7. From your phone or computer connect to the 'MeshCore OTA' hotspot
8. From a browser, go to http://192.168.4.1/update and upload the non-merged bin from the flasher


### 7.3. Q: Is there a way to lower the chance of a failed OTA device firmware update (DFU)?

**A:** Yes, developer `che aporeps` has an enhanced OTA DFU bootloader for nRF52 based devices.  With this bootloader, if it detects that the application firmware is invalid, it falls back to OTA DFU mode so you can attempt to flash again to recover.  This bootloader has other changes to make the OTA DFU process more fault tolerant.

Refer to https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX for the latest information.

Currently, the following boards are supported:
- Nologo ProMicro
- Seeed Studio XIAO nRF52840 BLE
- Seeed Studio XIAO nRF52840 BLE SENSE
- RAK 4631

### 7.4. Q: are the MeshCore logo and font available?

**A:** Yes, it is on the MeshCore github repo here:
https://github.com/meshcore-dev/MeshCore/tree/main/logo

### 7.5. Q: What is the format of a contact or channel QR code?

**A:**
Channel:
`meshcore://channel/add?name=<name>&secret=<secret>`

Contact:
`meshcore://contact/add?name=<name>&public_key=<secret>&type=<type>`

where `&type` is:
`chat = 1`
`repeater = 2`
`room = 3`
`sensor = 4`

### 7.6. Q: How do I connect to the companion via WIFI, e.g. using a heltec v3?
 **A:**
WiFi firmware requires you to compile it yourself, as you need to set the wifi ssid and password.
Edit WIFI_SSID and WIFI_PWD in `./variants/heltec_v3/platformio.ini` and then flash it to your device.

---

# StoopNet

Stoop Net is a solar-powered LoRa mesh node that runs a local bulletin board over WiFi. One node serves everyone nearby with a phone or computer. Built on [MeshCore](https://github.com/meshcore-dev/MeshCore) as the mesh transport.

After the fires in LA and with the potential for bad floods with the changing climate, it seemed like a good idea to have a system for distributed communication that will still function if the main infrasture goes down. This project is partly inspired by projects like disaster.radio, but attempts to make it even more accessible without the need to install an app and running on very low cost hardware.

## The idea

Existing LoRa bulletin boards (MeshCore room servers, Meshtastic BBS) require all the users to own a radio. Stoop Net puts the radio at a fixed location and serves phones over WiFi instead. Apps and accounts increase friction for use and adoption and also won't be accessible to download/setup if infrastructure goes down. All you have to do is connect to the WiFi and use a browser to read or post. 

## How it works

Each node is an ESP32-S3 broadcasting an open WiFi access point with a captive portal. Phones connect and are automatically redirected to the main web page in their browser. Posts made locally are stored on the node and forwarded to other Stoop Net nodes over Meshcore.

The mesh has very little bandwidth to spend. The whole network can carry a few thousand SMS-length posts a day. This behaves like a village noticeboard with a strict word limit and post frequency.

During disaster situations this can be a source of information as well as a way for people to communicate in disparate locations when they can't physically reach each other.

## Trust model

**Phone to node.** The WiFi network is open and the username is just a label someone typed in, with no security or login behind it. Anyone in range can post as anyone, but the username is appended with the site name so it is difficult to forge site names.

**Node to node.** The main Stoop Net network is private to the nodes and is encrypted by the standard meshcore transport. MeshCore channels encrypt traffic but don't attribute a message to a sender, so attribution happens at the application layer instead of being inherited from the transport.

A post renders as `name@node`. The node half is static. The name half is just text someone typed. Forging another node's identity is generally not possible. Standing at a node and posting under a neighbor's name is possible, and that's an accepted tradeoff for a system with no accounts considering imposters will be sharing the same physical space.

The channel itself uses a private, randomly generated secret rather than one derived from its name. This means that updating the channel requires physical access to the device and its credentials.

## Rate limits

There are a lot of people on Meshcore in SoCal. In order to be respectful of the airspace, rate limits for both users and nodes have been implemented on the firmware level. It's built on a bucket system so a user starts with a max number of messages (5) and gets a new one back every 30 seconds. The node operates on a similar system, but all connected users share this larger bucket.

## Staying useful on a normal day

If a disaster were to occur this device might be useless if nobody has connected to it before. Stoop Net nodes are meant to carry routine, low-stakes content too. This includes basic communications as well as information like the weather forecast.

## Constraints

- No internet connection, so no CDNs and no external assets. Everything the browser needs ships inside the firmware.
- No TLS, so nothing sensitive should be typed into this browser.
- Only a handful of WiFi clients can be connected at once, around 4 to 8.
- Pages render inside the captive portal WebView on iOS, so plain HTML and light JavaScript only, no frameworks.
- Web assets are gzipped and embedded into the firmware at build time; see `bin/embed_web.py`.

## Building

This is a PlatformIO project. The current Stoop Net firmware target is `heltec_v4_stoop_radio`:

```
pio run -e heltec_v4_stoop_radio
```

For active development, `stoop_dev` builds the same firmware with debug logging enabled and uploads plus opens a serial monitor automatically:

```
pio run -e stoop_dev
```

To make the `stoop` channel private, copy `platformio.local.ini.template` to `platformio.local.ini` (already gitignored) and set your own channel key there.

## Updating firmware over WiFi (OTA)

Once a node is out in the field, you don't need to pull it to re-flash over USB. [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) serves an update page at `/update` on the same web server as the rest of the site.

1. Build the firmware normally:

   ```
   pio run -e heltec_v4_stoop_radio
   ```

   This produces `.pio/build/heltec_v4_stoop_radio/firmware.bin` — that's the file to upload. Don't use the `*-merged.bin` from the `mergebin` target; that one bundles the bootloader and partition table too and is only for a first-time flash over USB.

2. Connect to the node's WiFi (or its mDNS address if it's configured as a WiFi client) and go to `http://<node address>/update`.
3. Log in with username `admin` and the node's `ADMIN_PASSWORD` (set in `platformio.local.ini`, the same password used on the `/admin` page to enable/disable the radio interfaces).
4. Upload `firmware.bin`. The node reboots into the new firmware automatically once the upload finishes.

This only replaces the application code — anything in the node's SPIFFS partition (mesh identity, channel config) survives the update. As noted above, this site has no TLS, so the upload and the admin password it's gated behind both travel in the clear like everything else here.

## Repo layout

Stoop Net is a fork of MeshCore and stays mergeable with upstream on purpose. All Stoop Net-specific code lives under `examples/stoop_net_radio/`; shared MeshCore source under `src/` is not modified.

```
examples/stoop_net_radio/
  main.cpp             setup/loop, WiFi AP, captive portal, HTTP routes
  MyMesh.cpp/.h         mesh behavior for this node
  RateLimiter.cpp/.h    the two token buckets described above
  web/                  the HTML and JS served to phones
client-esp-32/          M5StickC test client + scripts to flash both devices,
                        join the node's AP and verify changes end-to-end
                        (see client-esp-32/README.md)
```

## Status

A user can connect to the WiFi hotspot and is taken to the main page by a captive portal. The user can specify a username and send messages over the private stoop channel. They can search through previous messages. 

## ToDo
- need to add a partition in flash to persist messages through reboots and add the ability to save 100s or 1000s of messages without competing with ESP flash for meshcore.
- need to stress test the system with multiple phones connected and sending messages.
- want to add another node type as a weather station or other types of admin data that can be published to the network.

## AI Disclaimer
(from @heidtn) The firmware has all been almost entirely written by hand, but I'm not very experienced with web dev so AI was mostly used for that and to get the bones of this README. Some of the FW is copied from reference sources. I've reviewed the JS parts myself, but they have been written entirely by Claude. AI also checked the code for bugs or other issues and has written documenting comments including for the firmware. All AI generated code has been reviewed by a human.

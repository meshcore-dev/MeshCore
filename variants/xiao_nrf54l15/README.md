# MeshCore variant for the Semtech LR2021 Evaluation Kit

The kit hosts a Seeed Studio XIAO nRF54L15 Core Board driving the [Semtech LR2021 transceivers](https://www.semtech.fr/products/wireless-rf/lora-plus/lr2021)

https://wiki.seeedstudio.com/semtech_lr2021_evk_getting_started/

Supports LoRa CSS on 868 MHz and 2.4 GHz bands

Recommanded channels for ISM2400 (2400-2483 MHz) : 2403 MHz, 2425 MHz, 2479 MHz with DC 100%. [ref1](https://www.semtech.com/uploads/technology/LoRa/phy-layer-2g4.pdf), [ref2](https://github.com/CampusIoT/datasets/tree/main/TourPerret2G4)

## TODO
* [ ] floor the bandwith provided by the app on LoRa CSS 2G4 bandwidths (203kHz, 406kHz, 812kHz 1625kHz?)
* [ ] add Long Interleaver (Li) Coding Rate for LoRa CSS on 2.4 GHz
* [ ] support USER button (send advert on double click)
* [ ] support OLED screen
* [ ] fix persistence of BLE pairing
* [ ] add FLRC modulation for high-speed datarates (EU868 and [ISM2400](https://hal.science/hal-05429890))

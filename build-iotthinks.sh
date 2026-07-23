# sh ./build-repeaters-iotthinks.sh
export FIRMWARE_VERSION="PowerSaving16"

############# Repeaters #############
# Commonly-used boards
## ESP32 - 17 boards
sh build.sh build-firmware \
Heltec_v3_repeater \
Heltec_WSL3_repeater \
heltec_v4_repeater \
Station_G2_repeater \
T_Beam_S3_Supreme_SX1262_repeater \
Tbeam_SX1262_repeater \
LilyGo_T3S3_sx1262_repeater \
Xiao_S3_WIO_repeater \
Xiao_C3_repeater \
Xiao_C6_repeater_ \
Heltec_E290_repeater \
Heltec_Wireless_Tracker_repeater \
LilyGo_TBeam_1W_repeater \
Xiao_S3_repeater \
heltec_tracker_v2_repeater \
Heltec_Wireless_Paper_repeater \
Heltec_ct62_repeater

## NRF52 - 17 boards
sh build.sh build-firmware \
RAK_4631_repeater \
Heltec_t114_repeater \
Xiao_nrf52_repeater \
Heltec_mesh_solar_repeater \
ProMicro_repeater \
SenseCap_Solar_repeater \
t1000e_repeater \
LilyGo_T-Echo_repeater \
WioTrackerL1_repeater \
RAK_3401_repeater \
RAK_WisMesh_Tag_repeater \
GAT562_30S_Mesh_Kit_repeater \
GAT562_Mesh_Tracker_Pro_repeater \
ikoka_nano_nrf_22dbm_repeater \
ikoka_nano_nrf_30dbm_repeater \
ikoka_nano_nrf_33dbm_repeater \
ThinkNode_M1_repeater \
Heltec_t096_repeater

## ESP32, SX1276 - 3 boards
sh build.sh build-firmware \
Heltec_v2_repeater \
LilyGo_TLora_V2_1_1_6_repeater \
Tbeam_SX1276_repeater

############# Room Server #############
# ESP32 - 7 boards
sh build.sh build-firmware \
Heltec_v3_room_server \
heltec_v4_room_server \
LilyGo_TBeam_1W_room_server \
Heltec_WSL3_room_server \
Xiao_S3_room_server \
heltec_tracker_v2_room_server \
Heltec_Wireless_Paper_room_server

# NRF52 - 6 boards
sh build.sh build-firmware \
RAK_4631_room_server \
Heltec_t114_room_server \
Xiao_nrf52_room_server \
t1000e_room_server \
WioTrackerL1_room_server \
RAK_3401_room_server \
Heltec_t096_room_server

############# Companions BLE #############
# NRF52 - 12 boards
sh build.sh build-firmware \
RAK_4631_companion_radio_ble \
Heltec_t114_companion_radio_ble \
Xiao_nrf52_companion_radio_ble \
t1000e_companion_radio_ble \
LilyGo_T-Echo_companion_radio_ble \
WioTrackerL1_companion_radio_ble \
RAK_3401_companion_radio_ble \
RAK_WisMesh_Tag_companion_radio_ble \
SenseCap_Solar_companion_radio_ble \
ThinkNode_M1_companion_radio_ble \
Heltec_t096_companion_radio_ble \
Heltec_t096_companion_radio_ble_femoff

############# Companions BLE PS #############
# ESP32 - 18 boards
sh build.sh build-firmware \
Heltec_v3_companion_radio_ble_ps \
heltec_v4_companion_radio_ble_ps \
heltec_v4_3_companion_radio_ble_ps_femoff \
Xiao_C3_companion_radio_ble_ps \
Xiao_S3_companion_radio_ble_ps \
Xiao_S3_WIO_companion_radio_ble_ps \
Heltec_v2_companion_radio_ble_ps \
LilyGo_TBeam_1W_companion_radio_ble_ps \
Heltec_WSL3_companion_radio_ble_ps \
Heltec_Wireless_Tracker_companion_radio_ble_ps \
heltec_tracker_v2_companion_radio_ble_ps \
Heltec_Wireless_Paper_companion_radio_ble_ps \
LilyGo_TLora_V2_1_1_6_companion_radio_ble_ps \
Heltec_ct62_companion_radio_ble_ps \
T_Beam_S3_Supreme_SX1262_companion_radio_ble_ps \
Tbeam_SX1262_companion_radio_ble_ps \
heltec_v4_expansionkit_tft_companion_radio_ble_ps \
LilyGo_T3S3_sx1262_companion_radio_ble_ps

# Not working
Tbeam_SX1276_companion_radio_ble_ps \

############# Companions USB #############
sh build.sh build-firmware \
Heltec_t096_companion_radio_usb

############# Sample builds #############
# 14 boards
sh build.sh build-firmware \
Heltec_v3_repeater \
heltec_v4_repeater \
Xiao_C3_repeater \
Xiao_C6_repeater_ \
RAK_4631_repeater \
Heltec_t096_repeater \
Heltec_v3_companion_radio_ble_ps \
heltec_v4_companion_radio_ble_ps \
heltec_v4_3_companion_radio_ble_ps_femoff \
Xiao_C3_companion_radio_ble_ps \
Xiao_C6_companion_radio_ble_ \
RAK_4631_companion_radio_ble \
Heltec_t096_companion_radio_ble \
Heltec_t096_companion_radio_ble_femoff

############# GPS PowerSaving #############
export FIRMWARE_VERSION="PowerSaving16.1.1"
sh build.sh build-firmware \
heltec_v4_repeater \
Heltec_t114_repeater \
Heltec_t096_repeater \
WioTrackerL1_repeater \
LilyGo_T-Echo_repeater \
Station_G2_repeater \
Heltec_Wireless_Tracker_repeater \
heltec_tracker_v2_repeater \
Heltec_mesh_solar_repeater \
SenseCap_Solar_repeater

sh build.sh build-firmware \
Heltec_t114_companion_radio_ble \
t1000e_companion_radio_ble \
LilyGo_T-Echo_companion_radio_ble \
WioTrackerL1_companion_radio_ble \
SenseCap_Solar_companion_radio_ble \
Heltec_t096_companion_radio_ble \
Heltec_t096_companion_radio_ble_femoff \
heltec_v4_companion_radio_ble_ps \
heltec_v4_3_companion_radio_ble_ps_femoff \
LilyGo_TBeam_1W_companion_radio_ble_ps \
Heltec_Wireless_Tracker_companion_radio_ble_ps \
heltec_tracker_v2_companion_radio_ble_ps \
T_Beam_S3_Supreme_SX1262 \
Tbeam_SX1262_companion_radio_ble_ps \
heltec_v4_expansionkit_tft_companion_radio_ble_ps

export FIRMWARE_VERSION="PowerSaving16.1.2"
sh build.sh build-firmware \
RAK_4631_repeater \
RAK_3401_repeater \
RAK_WisMesh_Tag_repeater \
GAT562_30S_Mesh_Kit_repeater \
GAT562_Mesh_Tracker_Pro_repeater \
RAK_4631_companion_radio_ble \
RAK_3401_companion_radio_ble \
RAK_WisMesh_Tag_companion_radio_ble

export FIRMWARE_VERSION="PowerSaving16.1.2"
sh build.sh build-firmware \
Tbeam_SX1262_repeater \
T_Beam_S3_Supreme_SX1262_repeater \
LilyGo_TBeam_1W_repeater \
Tbeam_SX1262_companion_radio_ble_ps \
T_Beam_S3_Supreme_SX1262_companion_radio_ble_ps \
LilyGo_TBeam_1W_companion_radio_ble_ps

export FIRMWARE_VERSION="PowerSaving16.1.2"
sh build.sh build-firmware \
t1000e_repeater \
t1000e_companion_radio_ble \
Heltec_mesh_solar_repeater \
Heltec_mesh_solar_companion_radio_ble \
Heltec_Wireless_Tracker_repeater \
Heltec_Wireless_Tracker_companion_radio_ble_ps \


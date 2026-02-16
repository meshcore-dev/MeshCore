# sh ./build-repeaters-iotthinks.sh
export FIRMWARE_VERSION="PowerSaving11.2.3"

# 12 boards
sh build.sh build-firmware \
Heltec_v3_repeater Heltec_WSL3_repeater heltec_v4_repeater LilyGo_T3S3_sx1262_repeater \
T_Beam_S3_Supreme_SX1262_repeater Tbeam_SX1262_repeater Station_G2_repeater Station_G2_logging_repeater  \
Xiao_C3_repeater Xiao_S3_WIO_repeater Heltec_Wireless_Tracker_repeater heltec_tracker_v2_repeater

# 13 boards - NRF52
sh build.sh build-firmware \
RAK_4631_repeater SenseCap_Solar_repeater Heltec_t114_repeater t1000e_repeater \
Xiao_nrf52_repeater LilyGo_T-Echo_repeater LilyGo_T-Echo-Lite_repeater ProMicro_repeater \
WioTrackerL1_repeater Heltec_mesh_solar_repeater RAK_WisMesh_Tag_repeater ThinkNode_M1_repeater \
Mesh_pocket_repeater

# 6 boards
sh build.sh build-firmware \
Heltec_v2_repeater LilyGo_T3S3_sx1276_repeater Tbeam_SX1276_repeater LilyGo_TLora_V2_1_1_6_repeater \
Heltec_Wireless_Paper_repeater Heltec_Wireless_Paper_repeater_bridge_rs232

# 6 boards - NRF52
sh build.sh build-firmware \
ikoka_nano_nrf_22dbm_repeater ikoka_nano_nrf_30dbm_repeater ikoka_nano_nrf_33dbm_repeater \
ikoka_stick_nrf_22dbm_repeater ikoka_stick_nrf_30dbm_repeater ikoka_stick_nrf_33dbm_repeater

# Custom boards - NRF52
sh build.sh build-firmware Washtastic_repeater

# LR1110, Likely supported but not built yet, to wait for requests
# Minewsemi_me25ls01_repeater 
# t1000e_repeater => Built
# wio_wm1110_repeater
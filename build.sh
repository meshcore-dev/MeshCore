#!/usr/bin/env bash

global_usage() {
  cat - <<EOF
Usage:
sh build.sh <command> [target]

Commands:
  help|usage|-h|--help: Shows this message.
  build-firmware <target>: Build the firmware for the given build target.
  build-firmwares: Build all firmwares for all targets.
  build-matching-firmwares <build-match-spec>: Build all firmwares for build targets containing the string given for <build-match-spec>.
  build-companion-firmwares: Build all companion firmwares for all build targets.
  build-repeater-firmwares: Build all repeater firmwares for all build targets.
  build-room-server-firmwares: Build all chat room server firmwares for all build targets.

Examples:
Build firmware for the "RAK_4631_repeater" device target
$ sh build.sh build-firmware RAK_4631_repeater

Build all firmwares for device targets containing the string "RAK_4631"
$ sh build.sh build-matching-firmwares <build-match-spec>

Build all companion firmwares
$ sh build.sh build-companion-firmwares

Build all repeater firmwares
$ sh build.sh build-repeater-firmwares

Build all chat room server firmwares
$ sh build.sh build-room-server-firmwares

Environment Variables:
  DISABLE_DEBUG=1: Disables all debug logging flags (MESH_DEBUG, MESH_PACKET_LOGGING, etc.)
                   If not set, debug flags from variant platformio.ini files are used.

Examples:
Build without debug logging:
$ export FIRMWARE_VERSION=v1.0.0
$ export DISABLE_DEBUG=1
$ sh build.sh build-firmware RAK_4631_repeater

Build with debug logging (default, uses flags from variant files):
$ export FIRMWARE_VERSION=v1.0.0
$ sh build.sh build-firmware RAK_4631_repeater
EOF
}

# Catch cries for help before doing anything else.
case $1 in
  help|usage|-h|--help)
    global_usage
    exit 1
    ;;
esac


# get a list of pio env names that start with "env:"
get_pio_envs() {
  echo $(pio project config | grep 'env:' | sed 's/env://')
}

# $1 should be the string to find (case insensitive)
get_pio_envs_containing_string() {
  shopt -s nocasematch
  envs=($(get_pio_envs))
  for env in "${envs[@]}"; do
      if [[ "$env" == *${1}* ]]; then
        echo $env
      fi
  done
}

# $1 should be the string to find (case insensitive)
get_pio_envs_ending_with_string() {
  shopt -s nocasematch
  envs=($(get_pio_envs))
  for env in "${envs[@]}"; do
    if [[ "$env" == *${1} ]]; then
      echo $env
    fi
  done
}

# disable all debug logging flags if DISABLE_DEBUG=1 is set
disable_debug_flags() {
  if [ "$DISABLE_DEBUG" == "1" ]; then
    export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -UMESH_DEBUG -UBLE_DEBUG_LOGGING -UWIFI_DEBUG_LOGGING -UBRIDGE_DEBUG -UGPS_NMEA_DEBUG -UCORE_DEBUG_LEVEL -UESPNOW_DEBUG_LOGGING -UDEBUG_RP2040_WIRE -UDEBUG_RP2040_SPI -UDEBUG_RP2040_CORE -UDEBUG_RP2040_PORT -URADIOLIB_DEBUG_SPI -UCFG_DEBUG -URADIOLIB_DEBUG_BASIC -URADIOLIB_DEBUG_PROTOCOL"
  fi
}

# build firmware for the provided pio env in $1
build_firmware() {

  # get git commit sha
  COMMIT_HASH=$(git rev-parse --short HEAD)

  # set firmware build date
  FIRMWARE_BUILD_DATE=$(date '+%d-%b-%Y')

  # get FIRMWARE_VERSION, which should be provided by the environment
  if [ -z "$FIRMWARE_VERSION" ]; then
    echo "FIRMWARE_VERSION must be set in environment"
    exit 1
  fi

  # set firmware version string
  # e.g: v1.0.0-abcdef
  FIRMWARE_VERSION_STRING="${FIRMWARE_VERSION}-${COMMIT_HASH}"

  # craft filename
  # e.g: RAK_4631_Repeater-v1.0.0-SHA
  FIRMWARE_FILENAME="$1-${FIRMWARE_VERSION_STRING}"

  # add firmware version info to end of existing platformio build flags in environment vars
  export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -DFIRMWARE_BUILD_DATE='\"${FIRMWARE_BUILD_DATE}\"' -DFIRMWARE_VERSION='\"${FIRMWARE_VERSION_STRING}\"'"

  # disable debug flags if requested
  disable_debug_flags

  # build firmware target
  pio run -e $1

  # build merge-bin for esp32 fresh install
  if [ -f .pio/build/$1/firmware.bin ]; then
    pio run -t mergebin -e $1
  fi

  # build .uf2 for nrf52 boards
  if [[ -f .pio/build/$1/firmware.zip && -f .pio/build/$1/firmware.hex ]]; then
    python3 bin/uf2conv/uf2conv.py .pio/build/$1/firmware.hex -c -o .pio/build/$1/firmware.uf2 -f 0xADA52840
  fi

  # copy .bin, .uf2, and .zip to out folder
  # e.g: Heltec_v3_room_server-v1.0.0-SHA.bin
  # e.g: RAK_4631_Repeater-v1.0.0-SHA.uf2

  # copy .bin for esp32 boards
  cp .pio/build/$1/firmware.bin out/${FIRMWARE_FILENAME}.bin 2>/dev/null || true
  cp .pio/build/$1/firmware-merged.bin out/${FIRMWARE_FILENAME}-merged.bin 2>/dev/null || true

  # copy .zip and .uf2 of nrf52 boards
  cp .pio/build/$1/firmware.uf2 out/${FIRMWARE_FILENAME}.uf2 2>/dev/null || true
  cp .pio/build/$1/firmware.zip out/${FIRMWARE_FILENAME}.zip 2>/dev/null || true

}

# firmwares containing $1 will be built
build_all_firmwares_matching() {
  envs=($(get_pio_envs_containing_string "$1"))
  for env in "${envs[@]}"; do
      build_firmware $env
  done
}

# firmwares ending with $1 will be built
build_all_firmwares_by_suffix() {
  envs=($(get_pio_envs_ending_with_string "$1"))
  for env in "${envs[@]}"; do
    build_firmware $env
  done
}

build_repeater_firmwares() {

#  # build specific repeater firmwares
#  build_firmware "Heltec_v2_repeater"
#  build_firmware "Heltec_v3_repeater"
#  build_firmware "Xiao_C3_Repeater_sx1262"
#  build_firmware "Xiao_S3_WIO_Repeater"
#  build_firmware "LilyGo_T3S3_sx1262_Repeater"
#  build_firmware "RAK_4631_Repeater"

  # build all repeater firmwares
  build_all_firmwares_by_suffix "_repeater"

}

build_companion_firmwares() {

#  # build specific companion firmwares
#  build_firmware "Heltec_v2_companion_radio_usb"
#  build_firmware "Heltec_v2_companion_radio_ble"
#  build_firmware "Heltec_v3_companion_radio_usb"
#  build_firmware "Heltec_v3_companion_radio_ble"
#  build_firmware "Xiao_S3_WIO_companion_radio_ble"
#  build_firmware "LilyGo_T3S3_sx1262_companion_radio_usb"
#  build_firmware "LilyGo_T3S3_sx1262_companion_radio_ble"
#  build_firmware "RAK_4631_companion_radio_usb"
#  build_firmware "RAK_4631_companion_radio_ble"
#  build_firmware "t1000e_companion_radio_ble"

  # build all companion firmwares
  build_all_firmwares_by_suffix "_companion_radio_usb"
  build_all_firmwares_by_suffix "_companion_radio_ble"

}

build_room_server_firmwares() {

#  # build specific room server firmwares
#  build_firmware "Heltec_v3_room_server"
#  build_firmware "RAK_4631_room_server"

  # build all room server firmwares
  build_all_firmwares_by_suffix "_room_server"

}

build_firmwares() {
  build_companion_firmwares
  build_repeater_firmwares
  build_room_server_firmwares
}

# clean build dir
rm -rf out
mkdir -p out

# handle script args
if [[ $1 == "build-firmware" ]]; then
  TARGETS=${@:2}
  if [ "$TARGETS" ]; then
    for env in $TARGETS; do
      build_firmware $env
    done
  else
    echo "usage: $0 build-firmware <target>"
    exit 1
  fi
elif [[ $1 == "build-matching-firmwares" ]]; then
  if [ "$2" ]; then
     build_all_firmwares_matching $2
  else
     echo "usage: $0 build-matching-firmwares <build-match-spec>"
    exit 1
  fi
elif [[ $1 == "build-firmwares" ]]; then
  build_firmwares
elif [[ $1 == "build-companion-firmwares" ]]; then
  build_companion_firmwares
elif [[ $1 == "build-repeater-firmwares" ]]; then
  build_repeater_firmwares
elif [[ $1 == "build-room-server-firmwares" ]]; then
  build_room_server_firmwares
fi

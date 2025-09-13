#!/usr/bin/env bash

# usage
# sh build.sh build-firmware RAK_4631_Repeater
# sh build.sh build-firmwares
# sh build.sh build-matching-firmwares RAK_4631
# sh build.sh build-companion-firmwares
# sh build.sh build-repeater-firmwares
# sh build.sh build-room-server-firmwares

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

  # export build flags for pio so we can inject firmware version info
  export PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_BUILD_DATE='\"${FIRMWARE_BUILD_DATE}\"' -DFIRMWARE_VERSION='\"${FIRMWARE_VERSION_STRING}\"'"

  # build firmware target
  pio run -e $1

  # build merge-bin for esp32 fresh install
  if [ -f .pio/build/$1/firmware.bin ]; then
    pio run -t mergebin -e $1
  fi

  # build .uf2 for nrf52 boards
  if [[ -f .pio/build/$1/firmware.zip && -f .pio/build/$1/firmware.hex ]]; then
    python bin/uf2conv/uf2conv.py .pio/build/$1/firmware.hex -c -o .pio/build/$1/firmware.uf2 -f 0xADA52840
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

build_repeater_firmwares() {

#  # build specific repeater firmwares
#  build_firmware "Heltec_v2_repeater"
#  build_firmware "Heltec_v3_repeater"
#  build_firmware "Xiao_C3_Repeater_sx1262"
#  build_firmware "Xiao_S3_WIO_Repeater"
#  build_firmware "LilyGo_T3S3_sx1262_Repeater"
#  build_firmware "RAK_4631_Repeater"

  # build all repeater firmwares
  build_all_firmwares_matching "repeater"

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
  build_all_firmwares_matching "companion_radio_usb"
  build_all_firmwares_matching "companion_radio_ble"

}

build_room_server_firmwares() {

#  # build specific room server firmwares
#  build_firmware "Heltec_v3_room_server"
#  build_firmware "RAK_4631_room_server"

  # build all room server firmwares
  build_all_firmwares_matching "room_server"

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
  if [ "$2" ]; then
    build_firmware $2
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

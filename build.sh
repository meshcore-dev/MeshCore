# cache project config json for use in get_platform_for_env()
PIO_CONFIG_JSON=$(pio project config --json-output)

#!/usr/bin/env bash

# exit when any command fails
set -e

global_usage() {
  cat - <<EOF
Usage:
sh build.sh <command> [target]

Commands:
  help|usage|-h|--help: Shows this message.
  list|-l: List firmwares available to build.
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

Build all kiss radio firmwares
$ sh build.sh build-kiss-radio-firmwares

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

# get a list of pio env names that start with "env:"
get_pio_envs() {
  pio project config | grep 'env:' | sed 's/env://'
}

# Catch cries for help before doing anything else.
case $1 in
  help|usage|-h|--help)
    global_usage
    exit 1
    ;;
  list|-l)
    get_pio_envs
    exit 0
    ;;
esac

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

# get platform flag for a given environment
# $1 should be the environment name
get_platform_for_env() {
  local env_name=$1
  printf '%s' "$PIO_CONFIG_JSON" | python3 -c "
import sys, json, re
raw = sys.stdin.read()
data = json.loads(raw, strict=False)
for section, options in data:
    if section == 'env:$env_name':
        for key, value in options:
            if key == 'build_flags':
                for flag in value:
                    match = re.search(r'(ESP32_PLATFORM|NRF52_PLATFORM|STM32_PLATFORM|RP2040_PLATFORM)', str(flag))
                    if match:
                        print(match.group(1))
                        sys.exit(0)
" 2>/dev/null || true
}

# disable all debug logging flags if DISABLE_DEBUG=1 is set
disable_debug_flags() {
  if [ "$DISABLE_DEBUG" == "1" ]; then
    export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -UMESH_DEBUG -UBLE_DEBUG_LOGGING -UWIFI_DEBUG_LOGGING -UBRIDGE_DEBUG -UGPS_NMEA_DEBUG -UCORE_DEBUG_LEVEL -UESPNOW_DEBUG_LOGGING -UDEBUG_RP2040_WIRE -UDEBUG_RP2040_SPI -UDEBUG_RP2040_CORE -UDEBUG_RP2040_PORT -URADIOLIB_DEBUG_SPI -UCFG_DEBUG -URADIOLIB_DEBUG_BASIC -URADIOLIB_DEBUG_PROTOCOL"
  fi
}

# build firmware for the provided pio env in $1
build_firmware() {
  # get env platform for post build actions
  ENV_PLATFORM=($(get_platform_for_env $1))

  # get git commit sha
  COMMIT_HASH=$(git rev-parse --short HEAD)

  # set firmware build date (e.g. "6 Jun 2026"; %-d drops the leading zero on GNU date / the Linux CI runner)
  FIRMWARE_BUILD_DATE=$(date '+%-d %b %Y')

  # get FIRMWARE_VERSION, which should be provided by the environment
  if [ -z "$FIRMWARE_VERSION" ]; then
    echo "FIRMWARE_VERSION must be set in environment"
    exit 1
  fi

  # Observer build number: when CI provides FIRMWARE_BUILD_NUMBER (the per-base
  # published-build counter), it becomes a 4th version component (e.g. .5 ->
  # v1.16.0.5). Computed up front because it now feeds BOTH the filename and the
  # embedded version. Local dev builds leave it unset → no 4th component.
  BUILD_NUMBER_SUFFIX=""
  if [ -n "$FIRMWARE_BUILD_NUMBER" ]; then
    BUILD_NUMBER_SUFFIX=".${FIRMWARE_BUILD_NUMBER}"
  fi

  # set firmware version string (used for the output filename)
  # e.g: v1.0.0-abcdef — or v1.16.0.5-dev-abcdef with a build number and the
  # dev channel's FILENAME_CHANNEL_TAG. The build number is now IN the filename
  # so the web flasher's Version dropdown (parsed from the asset name by the
  # /releases Worker) shows the true published build, matching the embedded
  # version that `ver` reports. Every filename parser (flasher gen-slim-manifests
  # ASSET_RE, the /releases Worker VERSION_RE, flasher.js stale-URL recovery)
  # accepts an optional 4th ".<n>" component followed by the lowercase
  # (?:-[a-z]+)? channel tag between version and hash.
  FIRMWARE_VERSION_STRING="${FIRMWARE_VERSION}${BUILD_NUMBER_SUFFIX}${FILENAME_CHANNEL_TAG:-}-${COMMIT_HASH}"

  # craft filename
  # e.g: RAK_4631_Repeater-v1.0.0-SHA
  FIRMWARE_FILENAME="$1-${FIRMWARE_VERSION_STRING}"

  # Tag the *embedded* version for observer builds, e.g. v1.0.0-observer-abcdef,
  # so `ver`, the MQTT firmware_version/client_version, and SNMP all identify the
  # fork. The filename above carries the same version + build number but no
  # variant/channel tag: the env name already contains "observer", and the web
  # flasher keys off that existing pattern.
  VARIANT_TAG=""
  case "$1" in
    *observer*) VARIANT_TAG="-observer" ;;
  esac

  # Optional release-channel marker (e.g. OTA_CHANNEL_TAG=beta -> "-observer-beta"),
  # so `ver` / MQTT firmware_version / SNMP identify which channel a node runs
  # without having to infer it from log behavior. Safe for the OTA version logic:
  # ota_parseVersion() reads only up to the first '-' and ota_extractHash() takes
  # the token after the LAST '-', so extra tags in between change neither.
  if [ -n "$OTA_CHANNEL_TAG" ]; then
    VARIANT_TAG="${VARIANT_TAG}-${OTA_CHANNEL_TAG}"
  fi

  # Embedded version: base + build number (4th component) + variant/channel tag
  # + hash, e.g. v1.16.0.5-observer-abcdef, so the node reports its build and
  # `ota check` can show how many builds behind it is.
  EMBEDDED_VERSION_STRING="${FIRMWARE_VERSION}${BUILD_NUMBER_SUFFIX}${VARIANT_TAG}-${COMMIT_HASH}"

  # Release channel. The observer pull-OTA fetches its slim per-variant manifest
  # from <OTA_MANIFEST_BASE>/<OTA_VARIANT>.json, so this URL IS the channel: a
  # device only ever sees updates published under the base it was built with.
  # Override OTA_MANIFEST_BASE_URL to publish a parallel channel (e.g. beta);
  # unset gives the production channel.
  #
  # Deliberately injected here rather than declared in variants/*/platformio.ini
  # (where it used to be duplicated 28 times), for symmetry with OTA_VARIANT and
  # so a plain `pio run` leaves BOTH macros undefined — which is what makes
  # ESP32Board.cpp's "ERR: OTA not configured (build via build.sh)" guard fire on
  # dev builds. Do not add a default in a header: that would silently arm OTA on
  # locally built firmware. Note that PLATFORMIO_BUILD_FLAGS cannot reliably
  # override a -D coming from build_flags (SCons reorders -U/-D), which is why
  # the .ini declarations were removed rather than overridden.
  OTA_MANIFEST_BASE_URL="${OTA_MANIFEST_BASE_URL:-https://observer.gessaman.com/v}"

  # add firmware version info to end of existing platformio build flags in environment vars.
  # OTA_VARIANT is the env name ($1) — it selects this build's slim per-variant manifest
  # (<OTA_MANIFEST_BASE>/<OTA_VARIANT>.json) that the observer pull-OTA fetches.
  export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -DFIRMWARE_BUILD_DATE='\"${FIRMWARE_BUILD_DATE}\"' -DFIRMWARE_VERSION='\"${EMBEDDED_VERSION_STRING}\"' -DOTA_VARIANT='\"$1\"' -DOTA_MANIFEST_BASE='\"${OTA_MANIFEST_BASE_URL}\"'"

  # disable debug flags if requested
  disable_debug_flags

  # build firmware target
  pio run -e $1

  # Build merged binaries where supported (ESP32 targets).
  pio run -t mergebin -e $1 >/dev/null 2>&1 || true

  # Generate UF2 from HEX when useful and UF2 is not already present.
  if [ -f ".pio/build/$1/firmware.hex" ] && [ ! -f ".pio/build/$1/firmware.uf2" ]; then
    python3 bin/uf2conv/uf2conv.py .pio/build/$1/firmware.hex -c -o .pio/build/$1/firmware.uf2 -f 0xADA52840 >/dev/null 2>&1 || true
  fi

  # Copy any produced artifacts to out folder.
  cp .pio/build/$1/firmware.bin out/${FIRMWARE_FILENAME}.bin 2>/dev/null || true
  cp .pio/build/$1/firmware-merged.bin out/${FIRMWARE_FILENAME}-merged.bin 2>/dev/null || true
  cp .pio/build/$1/firmware.hex out/${FIRMWARE_FILENAME}.hex 2>/dev/null || true
  cp .pio/build/$1/firmware.uf2 out/${FIRMWARE_FILENAME}.uf2 2>/dev/null || true
  cp .pio/build/$1/firmware.zip out/${FIRMWARE_FILENAME}.zip 2>/dev/null || true

  # Emit the partition-table signature (ESP32) for OTA partition-compatibility
  # checks. Keyed by env name so the slim-manifest generator can find it; the
  # firmware computes the same signature at runtime from its flashed table.
  if [ -f ".pio/build/$1/partitions.bin" ]; then
    python3 scripts/partition_signature.py ".pio/build/$1/partitions.bin" > "out/$1.partsig" 2>/dev/null || true
  fi

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

build_kiss_modem_firmwares() {

#  # build specific kiss radio firmwares
#  build_firmware "Heltec_v3_kiss_modem"
#  build_firmware "RAK_4631_kiss_modem"

  # build all room server firmwares
  build_all_firmwares_by_suffix "_kiss_modem"

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
elif [[ $1 == "build-kiss-radio-firmwares" ]]; then
  build_kiss_modem_firmwares
elif [[ $1 == "get-companion-firmwares-to-build" ]]; then
  get_pio_envs_ending_with_string "_companion_radio_usb"
  get_pio_envs_ending_with_string "_companion_radio_ble"
elif [[ $1 == "get-repeater-firmwares-to-build" ]]; then
  get_pio_envs_ending_with_string "_repeater"
elif [[ $1 == "get-room-server-firmwares-to-build" ]]; then
  get_pio_envs_ending_with_string "_room_server"
fi

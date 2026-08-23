#!/usr/bin/env bash
# Build firmware (+ optional .mota) for targets listed in scripts/targets.txt.
#
# Usage:
#   ./scripts/build-mota.sh                    # version from envyos/VERSION
#   ./scripts/build-mota.sh v0.1.1             # override version (output + FIRMWARE_VERSION stamp)
#   ./scripts/build-mota.sh --target wismesh-tag-repeater
#   ./scripts/build-mota.sh v0.1.2 --base v0.1.0
#   ./scripts/build-mota.sh --hex-only
#   ./scripts/build-mota.sh --list-targets
#
# Requires: PlatformIO (`pio`), motatool on PATH (or MOTATOOL=).
# From EnvyOS bench: ENVYOS_ROOT=/path/to/envyos sets RELEASED_VERSIONS immutability.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/version.sh
source "$ROOT/scripts/version.sh"

if [[ -z "${ENVYOS_ROOT:-}" && -f "$ROOT/../ENVYOS_VERSIONS" ]]; then
  ENVYOS_ROOT="$(cd "$ROOT/.." && pwd)"
  RELEASED_VERSIONS_FILE="$ENVYOS_ROOT/RELEASED_VERSIONS"
fi

OUT_ROOT="$MOTAS_ROOT"
TARGETS_FILE="$ROOT/scripts/targets.txt"
MC="$ROOT"

TARGET_SLUGS=()
TARGET_ENVS=()
TARGET_DESCS=()

usage() {
  cat >&2 <<EOF
usage: $0 [version] [--target <slug>]… [--base <version>] [--hex-only] [--targets-file <path>]
       $0 --list-targets [--targets-file <path>]

  version         Optional override for output dir and -DFIRMWARE_VERSION (default: envyos/VERSION)
  --target        Build one target slug (repeatable; default: all in targets file)
  --base          Build delta from one base only (default: all prior versions with base hex)
  --hex-only      Build hex/uf2 only — skip .mota packaging
  --targets-file  Target map (default: scripts/targets.txt)
  --list-targets  Print configured targets and exit

examples:
  $0
  $0 v0.1.1
  $0 --target wismesh-tag-repeater
  $0 v0.1.2 --base v0.1.0
  $0 --hex-only
EOF
  exit 2
}

load_targets() {
  local file="$1"
  [[ -f "$file" ]] || {
    echo "error: targets file not found: $file" >&2
    exit 1
  }

  TARGET_SLUGS=()
  TARGET_ENVS=()
  TARGET_DESCS=()

  local line slug env desc
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    [[ -n "$line" ]] || continue

    read -r slug env desc <<<"$line"
    [[ -n "$slug" && -n "$env" ]] || {
      echo "error: bad targets line (want: slug env [description]): $line" >&2
      exit 1
    }

    TARGET_SLUGS+=("$slug")
    TARGET_ENVS+=("$env")
    TARGET_DESCS+=("${desc:-}")
  done <"$file"

  [[ ${#TARGET_SLUGS[@]} -gt 0 ]] || {
    echo "error: no targets in $file" >&2
    exit 1
  }
}

list_targets() {
  local file="$1"
  load_targets "$file"
  local i
  printf '%-24s  %-36s  %s\n' "SLUG" "PLATFORMIO_ENV" "DESCRIPTION"
  for i in "${!TARGET_SLUGS[@]}"; do
    printf '%-24s  %-36s  %s\n' "${TARGET_SLUGS[$i]}" "${TARGET_ENVS[$i]}" "${TARGET_DESCS[$i]}"
  done
}

target_index() {
  local want="$1"
  local i
  for i in "${!TARGET_SLUGS[@]}"; do
    if [[ "${TARGET_SLUGS[$i]}" == "$want" ]]; then
      printf '%s' "$i"
      return 0
    fi
  done
  return 1
}

build_target() {
  local slug="$1"
  local env_name="$2"
  local out="$OUT_ROOT/$VER/$slug"
  local build_dir="$MC/.pio/build/$env_name"
  local mt=""

  echo "==> $VER  target=$slug  env=$env_name"

  assert_version_not_released "$VER"
  rm -rf "$out"
  mkdir -p "$out"

  (
    cd "$MC"
    pio run -e "$env_name"
    pio run -e "$env_name" -t create_uf2
  )

  local hex="$build_dir/firmware.hex"
  local uf2="$build_dir/firmware.uf2"
  local zip="$build_dir/firmware.zip"

  [[ -f "$hex" ]] || { echo "error: missing $hex" >&2; exit 1; }

  cp -f "$hex" "$out/firmware.hex"
  [[ -f "$uf2" ]] && cp -f "$uf2" "$out/firmware.uf2"
  [[ -f "$zip" ]] && cp -f "$zip" "$out/firmware.zip"
  write_mota_version_txt "$out" "$VER" "$BUILD_DATE"

  echo "    saved $out/firmware.hex (+ uf2/zip if present)"

  if [[ "$HEX_ONLY" -eq 1 ]]; then
    echo "    (--hex-only: skipping .mota packaging)"
  else
    mt="$(resolve_motatool)"
    echo "==> packaging .mota ($slug) with $mt"

    "$mt" build --fw "$out/firmware.hex" --out-dir "$out"
    echo "    full .mota → $out/"

    local base_versions=()
    if [[ -n "$BASE_VER" ]]; then
      base_versions=("$BASE_VER")
    else
      while IFS= read -r bv || [[ -n "$bv" ]]; do
        [[ -n "$bv" ]] || continue
        base_versions+=("$bv")
      done < <(list_delta_base_versions "$VER")
    fi

    local base_ver base_hex delta_out
    for base_ver in "${base_versions[@]}"; do
      if ! base_hex="$(resolve_base_hex "$slug" "$base_ver")"; then
        echo "    skip delta $base_ver → $VER ($slug): no base hex" >&2
        continue
      fi
      delta_out="$out/delta_from_${base_ver}.mota"
      echo "==> in-place delta ($slug) $base_ver → $VER"
      echo "    base: $base_hex"
      echo "    fw:   $out/firmware.hex"
      "$mt" build --base "$base_hex" --fw "$out/firmware.hex" --patch-type in-place --out "$delta_out"
      echo "    delta: $delta_out"
    done
  fi

  echo "==> done $VER/$slug"
  ls -la "$out"
}

LIST_ONLY=0
HEX_ONLY=0
VER=""
VER_EXPLICIT=0
BASE_VER=""
SELECTED=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --hex-only)
      HEX_ONLY=1
      shift
      ;;
    --list-targets)
      LIST_ONLY=1
      shift
      ;;
    --targets-file)
      [[ $# -ge 2 ]] || usage
      TARGETS_FILE="$2"
      shift 2
      ;;
    --target)
      [[ $# -ge 2 ]] || usage
      SELECTED+=("$2")
      shift 2
      ;;
    --base)
      [[ $# -ge 2 ]] || usage
      BASE_VER="$(normalize_version "$2")" || usage
      shift 2
      ;;
    -h | --help)
      usage
      ;;
    -*)
      usage
      ;;
    *)
      [[ -z "$VER" ]] || usage
      VER="$(normalize_version "$1")" || usage
      VER_EXPLICIT=1
      shift
      ;;
  esac
done

if [[ "$LIST_ONLY" -eq 1 ]]; then
  list_targets "$TARGETS_FILE"
  exit 0
fi

if [[ -z "$VER" ]]; then
  VER="$(read_firmware_version_file)" || usage
fi

FW_VER="$VER"
if [[ "$VER_EXPLICIT" -eq 0 ]]; then
  local_file_ver="$(read_firmware_version_file 2>/dev/null || true)"
  if [[ -n "$local_file_ver" && "$local_file_ver" != "$FW_VER" ]]; then
    echo "error: envyos/VERSION ($local_file_ver) != build version ($FW_VER)" >&2
    exit 1
  fi
fi

load_targets "$TARGETS_FILE"

BUILD_SLUGS=()
BUILD_ENVS=()
if [[ ${#SELECTED[@]} -eq 0 ]]; then
  BUILD_SLUGS=("${TARGET_SLUGS[@]}")
  BUILD_ENVS=("${TARGET_ENVS[@]}")
else
  local_slug=""
  local_idx=""
  for local_slug in "${SELECTED[@]}"; do
    local_idx="$(target_index "$local_slug")" || {
      echo "error: unknown target '$local_slug' (see --list-targets)" >&2
      exit 1
    }
    BUILD_SLUGS+=("${TARGET_SLUGS[$local_idx]}")
    BUILD_ENVS+=("${TARGET_ENVS[$local_idx]}")
  done
fi

OUT="$OUT_ROOT/$VER"
assert_version_not_released "$VER"
if [[ ${#SELECTED[@]} -eq 0 ]]; then
  rm -rf "$OUT"
fi
mkdir -p "$OUT"
BUILD_DATE="$(format_firmware_build_date)"
write_mota_version_txt "$OUT" "$VER" "$BUILD_DATE"

if [[ "$HEX_ONLY" -eq 1 ]]; then
  echo "mode: hex-only (no .mota)"
else
  MT="$(resolve_motatool)"
  echo "motatool: $MT"
fi
echo "version: $VER  build: $BUILD_DATE"
echo "targets: ${BUILD_SLUGS[*]}"

export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS:-} -DFIRMWARE_VERSION='\"${FW_VER#v}\"' -DFIRMWARE_BUILD_DATE='\"${BUILD_DATE}\"'"

i=0
for i in "${!BUILD_SLUGS[@]}"; do
  build_target "${BUILD_SLUGS[$i]}" "${BUILD_ENVS[$i]}"
done

echo "==> all done $VER (${#BUILD_SLUGS[@]} target(s))"
ls -la "$OUT"

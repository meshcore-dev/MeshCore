#!/usr/bin/env bash
# Firmware build helpers for meshcore-firmware (envycore).
set -euo pipefail

FIRMWARE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="$FIRMWARE_ROOT/build"
MOTAS_ROOT="${MOTAS_ROOT:-$BUILD_ROOT/motas}"
VERSION_FILE="$FIRMWARE_ROOT/envyos/VERSION"
# When built from an EnvyOS bench checkout, immutability follows the distro manifest.
RELEASED_VERSIONS_FILE="${RELEASED_VERSIONS_FILE:-${ENVYOS_ROOT:+$ENVYOS_ROOT/RELEASED_VERSIONS}}"
RELEASED_VERSIONS_FILE="${RELEASED_VERSIONS_FILE:-$FIRMWARE_ROOT/RELEASED_VERSIONS}"

normalize_version() {
  local v="${1#v}"
  if [[ ! "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: invalid version '$1' (want vMAJOR.MINOR.PATCH)" >&2
    return 1
  fi
  printf 'v%s' "$v"
}

read_firmware_version_file() {
  [[ -f "$VERSION_FILE" ]] || {
    echo "error: missing $VERSION_FILE" >&2
    return 1
  }
  normalize_version "$(tr -d '[:space:]' <"$VERSION_FILE")"
}

format_firmware_build_date() {
  local d
  d="$(LC_TIME=C date '+%d %b %Y')"
  printf '%s' "${d#0}"
}

write_mota_version_txt() {
  local dir=$1 ver=$2 build_date=$3
  printf '%s\n%s\n' "$ver" "$build_date" >"$dir/version.txt"
}

resolve_motatool() {
  if [[ -n "${MOTATOOL:-}" && -x "$MOTATOOL" ]]; then
    printf '%s\n' "$MOTATOOL"
    return 0
  fi
  if command -v motatool >/dev/null 2>&1; then
    command -v motatool
    return 0
  fi
  echo "error: motatool not on PATH (install from MeshEnvy/motatool releases or set MOTATOOL=)" >&2
  return 1
}

parse_version() {
  local v="${1#v}"
  local major minor patch
  IFS=. read -r major minor patch <<<"$v"
  printf '%s %s %s' "$major" "$minor" "$patch"
}

is_released_version() {
  local ver line
  ver="$(normalize_version "$1")" || return 1
  [[ -f "$RELEASED_VERSIONS_FILE" ]] || return 1
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -n "$line" ]] || continue
    if [[ "$(normalize_version "$line")" == "$ver" ]]; then
      return 0
    fi
  done <"$RELEASED_VERSIONS_FILE"
  return 1
}

assert_version_not_released() {
  local ver="$1"
  if is_released_version "$ver"; then
    echo "error: $ver is released — $MOTAS_ROOT/$ver/ is immutable" >&2
    exit 1
  fi
}

version_sort_key() {
  local major minor patch
  read -r major minor patch <<<"$(parse_version "$1")"
  printf '%03d.%03d.%03d' "$major" "$minor" "$patch"
}

sort_versions() {
  local v seen=""
  for v in "$@"; do
    case "$seen" in
      *"|$v|"*) continue ;;
    esac
    seen="${seen}|$v|"
    printf '%s\t%s\n' "$(version_sort_key "$v")" "$v"
  done | sort -t $'\t' -k1,1 | cut -f2-
}

version_lt() {
  local a b
  a="$(normalize_version "$1")" || return 1
  b="$(normalize_version "$2")" || return 1
  local am aj ap bm bj bp
  read -r am aj ap <<<"$(parse_version "$a")"
  read -r bm bj bp <<<"$(parse_version "$b")"
  if (( am != bm )); then
    (( am < bm ))
    return
  fi
  if (( aj != bj )); then
    (( aj < bj ))
    return
  fi
  (( ap < bp ))
}

list_known_mota_versions() {
  local line ver d
  local tmp=()
  if [[ -f "$RELEASED_VERSIONS_FILE" ]]; then
    while IFS= read -r line || [[ -n "$line" ]]; do
      line="${line%%#*}"
      line="${line#"${line%%[![:space:]]*}"}"
      line="${line%"${line##*[![:space:]]}"}"
      [[ -n "$line" ]] || continue
      ver="$(normalize_version "$line" 2>/dev/null)" || continue
      tmp+=("$ver")
    done <"$RELEASED_VERSIONS_FILE"
  fi
  if [[ -d "$MOTAS_ROOT" ]]; then
    for d in "$MOTAS_ROOT"/v[0-9]*.[0-9]*.[0-9]*; do
      [[ -d "$d" ]] || continue
      ver="$(normalize_version "$(basename "$d")" 2>/dev/null)" || continue
      tmp+=("$ver")
    done
  fi
  if [[ ${#tmp[@]} -eq 0 ]]; then
    return 0
  fi
  sort_versions "${tmp[@]}"
}

list_delta_base_versions() {
  local target ver
  target="$(normalize_version "$1")" || return 1
  while IFS= read -r ver || [[ -n "$ver" ]]; do
    [[ -n "$ver" ]] || continue
    if version_lt "$ver" "$target"; then
      printf '%s\n' "$ver"
    fi
  done < <(list_known_mota_versions)
}

resolve_base_hex() {
  local slug="$1"
  local base_ver="$2"
  local candidates=(
    "$MOTAS_ROOT/$base_ver/$slug/firmware.hex"
  )
  if [[ "$slug" == "wismesh-tag-repeater" ]]; then
    candidates+=(
      "$MOTAS_ROOT/$base_ver/repeater/firmware.hex"
      "$MOTAS_ROOT/$base_ver/firmware.hex"
    )
  fi
  local p
  for p in "${candidates[@]}"; do
    if [[ -f "$p" ]]; then
      printf '%s' "$p"
      return 0
    fi
  done
  return 1
}

verify_release_delta_matrix() {
  local ver="$1"
  local targets_file="$2"
  local line slug base_ver delta base_hex
  local missing=0

  [[ -f "$targets_file" ]] || {
    echo "error: targets file not found: $targets_file" >&2
    return 1
  }

  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    [[ -n "$line" ]] || continue
    read -r slug _ <<<"$line"
    [[ -n "$slug" ]] || continue

    while IFS= read -r base_ver || [[ -n "$base_ver" ]]; do
      [[ -n "$base_ver" ]] || continue
      resolve_base_hex "$slug" "$base_ver" >/dev/null || continue
      delta="$MOTAS_ROOT/$ver/$slug/delta_from_${base_ver}.mota"
      if [[ ! -f "$delta" ]]; then
        echo "error: missing $delta (base hex exists for $base_ver/$slug)" >&2
        missing=1
      fi
    done < <(list_delta_base_versions "$ver")
  done <"$targets_file"

  [[ "$missing" -eq 0 ]]
}

#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHANGELOG="$ROOT/CHANGELOG.md"

usage() {
  cat <<'EOF'
Usage: changelog.sh notes <vX.Y.Z>   Print release body for GitHub
       changelog.sh check <vX.Y.Z>   Fail if section missing or empty
EOF
  exit 2
}

normalize_heading() {
  local raw="${1#v}"
  printf 'v%s' "$raw"
}

section_body() {
  local heading="$1"
  awk -v heading="$heading" '
    BEGIN { in_section = 0 }
    /^## \[/ {
      if (in_section) exit
      line = $0
      sub(/^## \[/, "", line)
      sub(/\].*$/, "", line)
      if (line == heading) in_section = 1
      next
    }
    in_section { print }
  ' "$CHANGELOG"
}

cmd_notes() {
  local tag="${1:-}"
  [[ -n "$tag" ]] || usage
  local heading
  heading="$(normalize_heading "$tag")"
  [[ -f "$CHANGELOG" ]] || {
    echo "error: missing $CHANGELOG" >&2
    exit 1
  }
  local body
  body="$(section_body "$heading")"
  if [[ -z "${body//[$'\t\r\n ']/}" ]]; then
    echo "error: CHANGELOG.md missing or empty ## [$heading]" >&2
    exit 1
  fi
  printf '%s\n' "$body"
}

cmd_check() {
  local tag="${1:-}"
  [[ -n "$tag" ]] || usage
  cmd_notes "$tag" >/dev/null
  echo "changelog: $(normalize_heading "$tag") OK"
}

main() {
  case "${1:-}" in
    notes)
      shift
      cmd_notes "${1:-}"
      ;;
    check)
      shift
      cmd_check "${1:-}"
      ;;
    *)
      usage
      ;;
  esac
}

main "$@"

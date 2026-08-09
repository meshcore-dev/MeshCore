#!/usr/bin/env bash
# Cherry-pick vk496 OTA commits + EnvyOS overlay onto companion-v1.16.0.
# Run from envycore/ after: git reset --hard companion-v1.16.0
set -euo pipefail

COMMITS=(
  # vk496 OTA stack (chronological)
  07b20f04
  6b4f2355
  5da78304
  9b61169c
  9beaf684
  543c9011
  4680d105
  2092500d
  e3dd6b41
  54c8b586
  280b9b15
  4cb118b4
  0a1cf2b2
  9ff1c883
  259802b7
  bd2ecb80
  10fa4344
  28e3df02
  49820b57
  4a601486
  89532147
  b159737e
  0319a81d
  cbdc629f
  ee6e5487
  87241323
  71841856
  # EnvyOS overlay
  dcf051f8
  97470f41
  18be8ace
  f0e6b538
  beadfa6b
  fc013f42
  7dedbdd6
  1f7e70a9
  7fd0f6b7
  59790f36
  5116d0ba
  beeef280
  31621a3e
  705724b2
  376a9d3f
  2e7d5a61
  899ba168
  0e70236c
)

for sha in "${COMMITS[@]}"; do
  echo "=== cherry-pick $sha ==="
  git cherry-pick -x "$sha"
done

echo "Done: $(git log --oneline -1)"

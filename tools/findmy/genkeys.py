#!/usr/bin/env python3
"""
Generate FindMy / OpenHaystack advertising keys for the MeshCore nRF52 FindMy beacon.

Keys are derived deterministically from a random 32-byte seed so the whole set can be
regenerated from that seed alone. Each slot is a NIST P-224 (secp224r1) keypair:

    d_i  = SHA256(seed || "findmy" || i) reduced into [1, n-1]   (private scalar)
    P_i  = d_i * G                                               (public point)
    adv  = X coordinate of P_i (28 bytes)   -> goes on the device
    hash = SHA256(adv)                       -> server lookup id for Apple's network

The device only ever stores the public `adv` keys and picks the active slot from its clock:
    slot = (now_utc / 86400) % count
so the keys rotate daily and cycle every `count` days. count == 1 is a single static key.

Outputs (into --out):
  seed.txt        the master seed (hex) - keep this safe, it regenerates everything
  provision.txt   CLI script: `set findmy.clear`, one `set findmy.add <b64>` per slot,
                  then `set findmy on`. Paste/pipe into the node's serial console.
  keys/<i>.keys   per-slot OpenHaystack/macless-haystack key file (Private/Advertisement/
                  Hashed adv key) for the server side.

Requires: cryptography  (pip install cryptography)

Examples:
  python3 genkeys.py --count 365 --out mytag
  python3 genkeys.py --count 1   --out mytag            # single static key
  python3 genkeys.py --count 30  --seed <hex> --out mytag   # reproduce from a seed
"""

import argparse
import base64
import hashlib
import os
import sys

try:
    from cryptography.hazmat.primitives.asymmetric import ec
except ImportError:
    sys.exit("This script needs the 'cryptography' package: pip install cryptography")

# Order of the secp224r1 / NIST P-224 curve.
P224_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D


def derive_private_scalar(seed: bytes, index: int) -> int:
    """Deterministic private scalar in [1, n-1] from seed and slot index."""
    counter = 0
    while True:
        h = hashlib.sha256(seed + b"findmy" + index.to_bytes(4, "big") + bytes([counter])).digest()
        d = int.from_bytes(h, "big") % P224_N
        if d != 0:
            return d
        counter += 1


def slot_keys(seed: bytes, index: int):
    """Return (private_b64, adv_b64, hashed_b64) for a slot."""
    d = derive_private_scalar(seed, index)
    priv = ec.derive_private_key(d, ec.SECP224R1())
    x = priv.public_key().public_numbers().x
    adv = x.to_bytes(28, "big")                 # advertised public key (X coordinate)
    private = d.to_bytes(28, "big")
    hashed = hashlib.sha256(adv).digest()
    b64 = lambda b: base64.b64encode(b).decode()
    return b64(private), b64(adv), b64(hashed)


def main():
    ap = argparse.ArgumentParser(description="Generate FindMy keys for the MeshCore nRF52 beacon.")
    ap.add_argument("--count", type=int, default=365, help="number of daily slots (1..365)")
    ap.add_argument("--seed", help="32-byte master seed as hex (random if omitted)")
    ap.add_argument("--out", default="findmy_keys", help="output directory")
    args = ap.parse_args()

    if not 1 <= args.count <= 365:
        sys.exit("--count must be between 1 and 365")

    seed = bytes.fromhex(args.seed) if args.seed else os.urandom(32)

    os.makedirs(os.path.join(args.out, "keys"), exist_ok=True)

    with open(os.path.join(args.out, "seed.txt"), "w") as f:
        f.write(seed.hex() + "\n")

    with open(os.path.join(args.out, "provision.txt"), "w") as prov:
        prov.write("set findmy.clear\n")
        for i in range(args.count):
            private_b64, adv_b64, hashed_b64 = slot_keys(seed, i)
            prov.write(f"set findmy.add {adv_b64}\n")
            with open(os.path.join(args.out, "keys", f"{i:03d}.keys"), "w") as kf:
                kf.write(f"Private key: {private_b64}\n")
                kf.write(f"Advertisement key: {adv_b64}\n")
                kf.write(f"Hashed adv key: {hashed_b64}\n")
        prov.write("set findmy on\n")

    print(f"Generated {args.count} key(s) in '{args.out}/'")
    print(f"  seed:        {args.out}/seed.txt   (keep safe - regenerates all keys)")
    print(f"  device:      {args.out}/provision.txt   (pipe/paste into the node serial console)")
    print(f"  server keys: {args.out}/keys/*.keys   (import into macless-haystack)")
    print()
    print("Provision a node over USB, e.g.:")
    print(f"  while read line; do echo \"$line\"; sleep 0.2; done < {args.out}/provision.txt > /dev/ttyACM0")
    print("then `reboot` the node.")


if __name__ == "__main__":
    main()

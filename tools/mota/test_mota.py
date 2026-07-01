#!/usr/bin/env python3
"""
Tests for motalib — run with the meshcore venv:

    ./meshcore/bin/python tools/mota/test_mota.py

(Also pytest-compatible: functions are named test_*.)
"""

from __future__ import annotations

import io
import os
import random
import struct

import motalib as ml
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


def _fw(seed, size):
    random.seed(seed)
    return bytes(random.getrandbits(8) for _ in range(size))


# --- multihash / version ---------------------------------------------------

def test_version_pack_roundtrip():
    assert ml.pack_version("1.16.0") == (1 << 24) | (16 << 16)
    assert ml.unpack_version(ml.pack_version("1.16.0.2")) == "1.16.0.2"
    assert ml.pack_version(0x01100000) == 0x01100000


def test_target_id_for_env():
    import hashlib
    env = "RAK_4631_companion_radio_usb"
    expect = int.from_bytes(hashlib.sha256(env.encode()).digest()[:4], "little")
    assert ml.target_id_for_env(env) == expect
    # distinct envs (same board, different role) get distinct ids
    assert ml.target_id_for_env("RAK_4631_repeater") != ml.target_id_for_env("RAK_4631_companion_radio_usb")


# --- EndF ------------------------------------------------------------------

def test_endf_roundtrip_and_idempotent():
    body = _fw(1, 5000)
    img, h8 = ml.ensure_endf(body)
    assert len(img) == 5000 + ml.ENDF_LEN
    assert ml.has_endf(img)
    pbody, ph8 = ml.parse_endf(img)
    assert pbody == body and ph8 == h8 == ml.mh8(body)
    # idempotent: feeding an already-EndF'd image returns it unchanged
    img2, h82 = ml.ensure_endf(img)
    assert img2 == img and h82 == h8


def test_endf_rejects_garbage_tail():
    assert not ml.has_endf(b"too short")
    body = _fw(2, 1000)
    img = body + ml.ENDF_MAGIC + struct.pack("<I", 999) + ml.mh8(body)  # wrong body_len
    assert not ml.has_endf(img)


def test_endf_identity():
    body = _fw(3, 4096)
    ident = ml.FwIdent(fw_version=ml.pack_version("1.16.0"),
                       target_id=ml.target_id_for_env("RAK_4631_repeater"), hw_id="RAK4631")
    img, h8 = ml.ensure_endf(body, ident)
    assert len(img) == len(body) + ml.ENDF_LEN              # fixed 56-byte trailer
    assert ml.parse_endf(img) == (body, h8)                 # body + body_hash parse
    assert h8 == ml.mh8(body)                               # body_hash is over BODY only
    gi = ml.parse_endf_ident(img)
    assert gi is not None and gi.hw_id == "RAK4631"
    assert gi.target_id == ml.target_id_for_env("RAK_4631_repeater")
    assert gi.fw_version == ml.pack_version("1.16.0")
    # no identity supplied -> zero-filled (still fixed size, still self-consistent)
    z, _ = ml.ensure_endf(body)
    assert len(z) == len(body) + ml.ENDF_LEN and ml.parse_endf_ident(z) == ml.FwIdent(0, 0, "")


# --- merkle ----------------------------------------------------------------

def test_merkle_single_block():
    leaves = [ml.mh4(b"x")]
    assert ml.merkle_root(leaves) == leaves[0]


def test_merkle_proofs_all_indices_various_counts():
    for count in [1, 2, 3, 4, 5, 7, 8, 9, 16, 17, 100]:
        payload = _fw(count, count * 1024 - 13)  # last block short, no padding
        leaves = ml.leaf_hashes(payload, 1024)
        assert len(leaves) == count
        root = ml.merkle_root(leaves)
        for i in range(count):
            proof = ml.merkle_proof(leaves, i)
            assert ml.verify_proof(leaves[i], i, proof, root, count), (count, i)
        # a tampered leaf must fail its proof
        bad = bytes([leaves[0][0] ^ 0xFF]) + leaves[0][1:]
        assert not ml.verify_proof(bad, 0, ml.merkle_proof(leaves, 0), root, count)


# --- full container --------------------------------------------------------

def test_full_build_parse_verify():
    fw = _fw(10, 33 * 1024 + 7)
    image, _ = ml.ensure_endf(fw)
    m = ml.build_manifest(
        target_id=0xDEADBEEF, fw_version=ml.pack_version("1.16.0"),
        image_size=len(image), payload=image, block_size=1024,
        image_hash=ml.mh32(image), codec_id=ml.CODEC_FULL, is_full=True)
    blob = ml.build_container(m, image)

    parsed = ml.parse_container(blob)
    assert parsed.manifest.target_id == 0xDEADBEEF
    assert parsed.manifest.is_full and not parsed.manifest.is_signed
    assert parsed.manifest.image_hash == ml.mh32(image)
    assert parsed.payload == image
    assert ml.verify(parsed) == []


def test_hw_id_roundtrip_and_signed():
    # the v2 hw_id is a 32-byte NUL-padded ASCII tag in the SIGNED head; it must round-trip + be covered
    # by the signature (tampering it breaks verification).
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    fw = _fw(77, 4 * 1024)
    image, _ = ml.ensure_endf(fw)
    priv = Ed25519PrivateKey.from_private_bytes(bytes(range(32)))
    m = ml.build_manifest(
        target_id=0xABCD, fw_version=ml.pack_version("2.0.0"),
        image_size=len(image), payload=image, block_size=1024,
        image_hash=ml.mh32(image), codec_id=ml.CODEC_FULL, is_full=True,
        sign_priv=priv, hw_id="RAK4631")
    blob = ml.build_container(m, image)
    parsed = ml.parse_container(blob)
    assert parsed.manifest.format_ver == 2
    assert parsed.manifest.hw_id == b"RAK4631" + b"\0" * (32 - 7)
    assert parsed.manifest.hw_id.rstrip(b"\0").decode() == "RAK4631"
    assert ml.verify(parsed) == []
    # flip a byte of the on-wire hw_id -> signature must fail (it's in the signed region)
    bad = bytearray(blob)
    hw_off = 8 + 57            # MAGIC(4)+total(4) + fixed head up to codec(57) = start of hw_id
    bad[hw_off] ^= 0xFF
    assert ml.verify(ml.parse_container(bytes(bad))) != []


def test_tampered_payload_detected():
    fw = _fw(11, 10 * 1024)
    image, _ = ml.ensure_endf(fw)
    m = ml.build_manifest(target_id=1, fw_version=1, image_size=len(image), payload=image,
                          block_size=1024, image_hash=ml.mh32(image),
                          codec_id=ml.CODEC_FULL, is_full=True)
    blob = bytearray(ml.build_container(m, image))
    # flip a byte inside the payload region
    payload_off = blob.index(image)
    blob[payload_off + 50] ^= 0xFF
    problems = ml.verify(ml.parse_container(bytes(blob)))
    assert any("leaves" in p or "merkle" in p or "image_hash" in p for p in problems), problems


# --- signing ---------------------------------------------------------------

def test_signed_build_and_verify():
    priv = Ed25519PrivateKey.generate()
    fw = _fw(12, 20 * 1024)
    image, _ = ml.ensure_endf(fw)
    m = ml.build_manifest(target_id=7, fw_version=ml.pack_version("2.0.0"),
                          image_size=len(image), payload=image, block_size=1024,
                          image_hash=ml.mh32(image), codec_id=ml.CODEC_FULL,
                          is_full=True, sign_priv=priv)
    parsed = ml.parse_container(ml.build_container(m, image))
    assert parsed.manifest.is_signed
    assert ml.verify(parsed, expect_pub=priv.public_key().public_bytes_raw()) == []
    # wrong expected key -> flagged
    other = Ed25519PrivateKey.generate().public_key().public_bytes_raw()
    assert any("signer_pubkey" in p for p in ml.verify(parsed, expect_pub=other))


def test_tampered_signature_detected():
    priv = Ed25519PrivateKey.generate()
    fw = _fw(13, 8 * 1024)
    image, _ = ml.ensure_endf(fw)
    m = ml.build_manifest(target_id=7, fw_version=1, image_size=len(image), payload=image,
                          block_size=1024, image_hash=ml.mh32(image),
                          codec_id=ml.CODEC_FULL, is_full=True, sign_priv=priv)
    blob = bytearray(ml.build_container(m, image))
    # flip a byte of target_id (inside signed region) without re-signing
    blob[10] ^= 0xFF
    problems = ml.verify(ml.parse_container(bytes(blob)))
    assert any("signature INVALID" in p for p in problems), problems


# --- approval enforcement --------------------------------------------------

def test_approval_default_and_flagged_if_preapproved():
    fw = _fw(14, 4 * 1024)
    image, _ = ml.ensure_endf(fw)
    m = ml.build_manifest(target_id=1, fw_version=1, image_size=len(image), payload=image,
                          block_size=1024, image_hash=ml.mh32(image),
                          codec_id=ml.CODEC_FULL, is_full=True)
    assert m.approval == ml.APPROVAL_NOT
    # simulate a malicious pre-approved container -> verify must flag it
    m.approval = ml.APPROVAL_YES
    parsed = ml.parse_container(ml.build_container(m, image))
    assert any("approval" in p for p in ml.verify(parsed))


# --- delta -----------------------------------------------------------------

def test_delta_build_apply_verify():
    old_body = _fw(20, 40 * 1024)
    # new = old with a chunk changed + appended -> a real, small-ish delta
    new_body = bytearray(old_body)
    for i in range(1000, 1500):
        new_body[i] = (new_body[i] + 1) & 0xFF
    new_body += _fw(21, 2048)
    old_image, base_hash = ml.ensure_endf(bytes(old_body))
    new_image, _ = ml.ensure_endf(bytes(new_body))

    import detools
    fp = io.BytesIO()
    detools.create_patch(io.BytesIO(old_image), io.BytesIO(new_image), fp,
                         patch_type="sequential", compression="crle")
    delta = fp.getvalue()
    # with compression a near-identical-base delta is a fraction of the full image
    assert len(delta) < len(new_image) // 2, (len(delta), len(new_image))

    m = ml.build_manifest(target_id=0xABCD, fw_version=ml.pack_version("1.2.0"),
                          image_size=len(new_image), payload=delta, block_size=1024,
                          image_hash=ml.mh32(new_image), codec_id=ml.CODEC_DETOOLS_SEQUENTIAL,
                          is_full=False, base_hash=base_hash)
    parsed = ml.parse_container(ml.build_container(m, delta))
    assert parsed.manifest.base_hash == base_hash == ml.mh8(bytes(old_body))
    # full verify incl. applying the delta to the base and checking image_hash
    assert ml.verify(parsed, base_image=old_image) == []
    # wrong base must fail the delta->image_hash check
    wrong = ml.verify(parsed, base_image=_fw(99, 40 * 1024))
    assert wrong, "delta verify against a wrong base should fail"


# --- runner ----------------------------------------------------------------

def _run():
    tests = {k: v for k, v in sorted(globals().items())
             if k.startswith("test_") and callable(v)}
    failed = 0
    for name, fn in tests.items():
        try:
            fn()
            print(f"ok   {name}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            import traceback
            print(f"FAIL {name}: {e}")
            traceback.print_exc()
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(_run())

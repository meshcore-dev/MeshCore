"""
motalib — build/parse/verify MeshCore ``.mota`` firmware-update containers.

Pure logic, no CLI. Implements docs/ota_protocol.md (format_ver=2, fixed layout).

The wire format (all integers little-endian):

    container = MAGIC(4) | MOTA_TOTAL_SIZE(4) | MANIFEST | PAYLOAD | TRAILER(5)

    manifest  = format_ver(1) flags(1) hash_algo(1) target_id(4) fw_version(4)
                image_size(4) payload_size(4) block_size_log2(1) merkle_root(4)
                image_hash(32) codec_id(1) hw_id(32)
                base_hash(8) signer_pubkey(32) signature(64) approval(4)
                leaves[](4*BC)

Fixed layout: every field is always present at a constant offset (base_hash/signer_pubkey/signature are
zero-filled for a full / unsigned container), so manifest-minus-leaves is always 197 bytes and `leaves[]`
is the only variable-length field. Hashes are SHA-256, truncated per multihash (sha2-256:N = first N bytes).
"""

from __future__ import annotations

import hashlib
import io
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Reference constants (must match docs/ota_protocol.md and the device code)
# ---------------------------------------------------------------------------

MAGIC = b"mOTA"           # 6D 4F 54 41
TRAILER = b"vk496"        # 76 6B 34 39 36
ENDF_MAGIC = b"EndF"      # 45 6E 64 46
# Fixed-length trailer: marker(4) + body_len(4) + body_hash8(8) + fw_version(4) + target_id(4) + hw_id(32).
ENDF_LEN = 56

FORMAT_VER = 2          # the one manifest format (fixed layout, see Manifest); other values are rejected
HASH_ALGO_SHA256 = 0x12   # multihash code for sha2-256

FLAG_FULL = 0x01
FLAG_SIGNED = 0x02

CODEC_FULL = 0
CODEC_DETOOLS_SEQUENTIAL = 1   # detools `sequential` patch (decoded on-device by vendored detools C)
CODEC_DETOOLS_INPLACE = 2      # detools `in-place` patch (nRF52 bootloader-handoff path; TBD)
CODEC_NAMES = {CODEC_FULL: "full", CODEC_DETOOLS_SEQUENTIAL: "detools-sequential",
               CODEC_DETOOLS_INPLACE: "detools-in-place"}

APPROVAL_NOT = b"\xff\xff\xff\xff"   # erased = not approved
APPROVAL_YES = b"APRV"               # 41 50 52 56 = approved

DEFAULT_BLOCK_SIZE = 1024


# ---------------------------------------------------------------------------
# Multihash helpers (sha2-256 truncations)
# ---------------------------------------------------------------------------

def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def mh4(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()[:4]


def mh8(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()[:8]


def mh32(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


# ---------------------------------------------------------------------------
# fw_version packing
# ---------------------------------------------------------------------------

def pack_version(s) -> int:
    """'1.16.0' or '1.16.0.2' -> uint32 (MAJOR<<24|MINOR<<16|PATCH<<8|pre). Ints pass through."""
    if isinstance(s, int):
        return s & 0xFFFFFFFF
    s = s.strip().lstrip("vV")
    parts = [int(p) for p in s.split(".")]
    parts += [0] * (4 - len(parts))
    maj, mnr, pat, pre = parts[:4]
    return ((maj & 0xFF) << 24) | ((mnr & 0xFF) << 16) | ((pat & 0xFF) << 8) | (pre & 0xFF)


def unpack_version(v: int) -> str:
    return f"{(v >> 24) & 0xFF}.{(v >> 16) & 0xFF}.{(v >> 8) & 0xFF}.{v & 0xFF}"


# ---------------------------------------------------------------------------
# target_id
# ---------------------------------------------------------------------------

def target_id_for_env(env_name: str) -> int:
    """4-byte build-target id = sha2-256:4(env_name), little-endian uint32.

    The PlatformIO env name (e.g. 'RAK_4631_companion_radio_usb') uniquely captures hardware AND
    role/partition layout. build.sh injects the same value as -D MOTA_TARGET_ID so the device's
    getOtaTargetId() matches what the packager stamps into the manifest. (Must match build.sh.)
    """
    d = hashlib.sha256(env_name.encode()).digest()[:4]
    return int.from_bytes(d, "little")


# ---------------------------------------------------------------------------
# EndF trailer
# ---------------------------------------------------------------------------

@dataclass
class FwIdent:
    """Self-describing firmware identity carried in the EndF trailer (docs/ota_protocol.md §2) so a node /
    the packaging tool reads it straight from the firmware instead of relying on build flags or filenames."""
    fw_version: int = 0      # packed MAJOR<<24 | MINOR<<16 | PATCH<<8 | pre
    target_id: int = 0       # sha2-256:4(pio_env) as uint32 LE — hw + role + partition (fetch routing)
    hw_id: str = ""          # readable hardware tag (brick-safety), e.g. "RAK4631"


def build_endf(body: bytes, ident: Optional["FwIdent"] = None) -> bytes:
    """The fixed 56-byte EndF trailer for a firmware BODY (identity zero-filled if not given)."""
    ident = ident or FwIdent()
    hw = ident.hw_id.encode("ascii", "replace")[:32].ljust(32, b"\0")
    return (ENDF_MAGIC + struct.pack("<I", len(body)) + mh8(body)
            + struct.pack("<II", ident.fw_version & 0xFFFFFFFF, ident.target_id & 0xFFFFFFFF) + hw)


def has_endf(image: bytes) -> bool:
    """True iff `image` ends with a self-consistent (fixed 56-byte) EndF trailer (image == BODY || EndF)."""
    if len(image) < ENDF_LEN:
        return False
    t = image[-ENDF_LEN:]
    return (t[:4] == ENDF_MAGIC and struct.unpack("<I", t[4:8])[0] == len(image) - ENDF_LEN
            and t[8:16] == mh8(image[:-ENDF_LEN]))


def parse_endf(image: bytes) -> Tuple[bytes, bytes]:
    """Return (body, body_hash8) for an image that ends with a valid EndF. Raises otherwise."""
    if not has_endf(image):
        raise ValueError("image has no valid EndF trailer")
    return image[:-ENDF_LEN], image[-ENDF_LEN + 8:-ENDF_LEN + 16]


def parse_endf_ident(image: bytes) -> Optional["FwIdent"]:
    """The self-describing identity from a valid EndF trailer, or None if there is no valid trailer."""
    if not has_endf(image):
        return None
    t = image[-ENDF_LEN:]
    fw, tgt = struct.unpack("<II", t[16:24])
    return FwIdent(fw, tgt, t[24:56].rstrip(b"\0").decode("ascii", "replace"))


def ensure_endf(image: bytes, ident: Optional["FwIdent"] = None) -> Tuple[bytes, bytes]:
    """Return (image_with_endf, body_hash8). Appends EndF (with `ident` if given) if not already present."""
    if has_endf(image):
        _, h8 = parse_endf(image)
        return image, h8
    body_hash8 = mh8(image)
    return image + build_endf(image, ident), body_hash8


# ---------------------------------------------------------------------------
# Merkle tree (sha2-256:4 leaves/nodes, promote-odd, no padding)
# ---------------------------------------------------------------------------

def block_count(payload_size: int, block_size: int) -> int:
    return (payload_size + block_size - 1) // block_size


def leaf_hashes(payload: bytes, block_size: int) -> List[bytes]:
    return [mh4(payload[i:i + block_size]) for i in range(0, len(payload), block_size)]


def merkle_root(leaves: List[bytes]) -> bytes:
    if not leaves:
        raise ValueError("empty payload / no leaves")
    level = list(leaves)
    while len(level) > 1:
        nxt = []
        n = len(level)
        for i in range(0, n, 2):
            if i + 1 < n:
                nxt.append(mh4(level[i] + level[i + 1]))
            else:
                nxt.append(level[i])  # promote lone last node unchanged
        level = nxt
    return level[0]


def merkle_proof(leaves: List[bytes], index: int) -> List[Tuple[bytes, bool]]:
    """Proof for block `index`: list of (sibling_digest, sibling_is_left)."""
    proof: List[Tuple[bytes, bool]] = []
    level = list(leaves)
    idx = index
    while len(level) > 1:
        n = len(level)
        is_last_odd = (n % 2 == 1) and (idx == n - 1)
        if not is_last_odd:
            if idx % 2 == 0:
                proof.append((level[idx + 1], False))   # sibling on the right
            else:
                proof.append((level[idx - 1], True))     # sibling on the left
        nxt = [mh4(level[i] + level[i + 1]) if i + 1 < n else level[i]
               for i in range(0, n, 2)]
        idx //= 2
        level = nxt
    return proof


def proof_siblings(leaves: List[bytes], index: int) -> bytes:
    """Wire form of a proof: just the ordered sibling digests, concatenated.

    The left/right direction is derived by the verifier from the block index + count
    (sibling is on the left iff the current index is odd), so no direction bits are sent.
    """
    return b"".join(sib for sib, _ in merkle_proof(leaves, index))


def verify_proof(leaf: bytes, index: int, proof: List[Tuple[bytes, bool]],
                 root: bytes, count: int) -> bool:
    h = leaf
    idx = index
    n = count
    p = 0
    while n > 1:
        is_last_odd = (n % 2 == 1) and (idx == n - 1)
        if is_last_odd:
            pass  # promoted, no proof element
        else:
            if p >= len(proof):
                return False
            sib, is_left = proof[p]
            p += 1
            h = mh4(sib + h) if is_left else mh4(h + sib)
        idx //= 2
        n = (n + 1) // 2
    return h == root and p == len(proof)


# ---------------------------------------------------------------------------
# Manifest + container
# ---------------------------------------------------------------------------

@dataclass
class Manifest:
    format_ver: int = FORMAT_VER
    flags: int = 0
    hash_algo: int = HASH_ALGO_SHA256
    target_id: int = 0
    fw_version: int = 0
    image_size: int = 0
    payload_size: int = 0
    block_size_log2: int = 10
    merkle_root: bytes = b"\0\0\0\0"
    image_hash: bytes = b"\0" * 32
    codec_id: int = CODEC_FULL
    hw_id: bytes = b"\0" * 32                  # 32-byte NUL-padded ASCII hardware tag (signed)
    # Fixed-layout: these are ALWAYS present (zero-filled when not applicable), so the manifest has a
    # constant size and a trivial offset-based parser; only leaves[] is variable.
    base_hash: bytes = b"\0" * 8               # 8 bytes; zero for a full image (meaningful iff !FULL)
    signer_pubkey: bytes = b"\0" * 32          # 32 bytes; zero when unsigned (meaningful iff SIGNED)
    signature: bytes = b"\0" * 64              # 64 bytes; zero when unsigned (meaningful iff SIGNED)
    approval: bytes = APPROVAL_NOT
    leaves: List[bytes] = field(default_factory=list)

    @property
    def is_full(self) -> bool:
        return bool(self.flags & FLAG_FULL)

    @property
    def is_signed(self) -> bool:
        return bool(self.flags & FLAG_SIGNED)

    @property
    def block_size(self) -> int:
        return 1 << self.block_size_log2

    @property
    def block_count(self) -> int:
        return block_count(self.payload_size, self.block_size)

    def signed_region(self) -> bytes:
        """Bytes the Ed25519 signature covers: format_ver .. signer_pubkey (fixed 129 bytes)."""
        out = bytearray()
        out += bytes([self.format_ver, self.flags, self.hash_algo])
        out += struct.pack("<IIII", self.target_id, self.fw_version,
                           self.image_size, self.payload_size)
        out += bytes([self.block_size_log2])
        out += self.merkle_root
        out += self.image_hash
        out += bytes([self.codec_id])
        out += self.hw_id                      # 32-byte hardware tag (part of the signed head)
        out += self.base_hash                  # always present (zero for a full image)
        out += self.signer_pubkey              # always present (zero when unsigned)
        return bytes(out)

    def serialize(self) -> bytes:
        """Fixed layout: signed_region(129) + signature(64) + approval(4) + leaves[4*BC]."""
        out = bytearray(self.signed_region())
        out += self.signature                  # always present (zero when unsigned)
        out += self.approval
        for lf in self.leaves:
            out += lf
        return bytes(out)


def hw_id_bytes(s) -> bytes:
    """Pack a hardware tag (str or bytes) into the fixed 32-byte NUL-padded field."""
    if s is None:
        return b"\0" * 32
    raw = s.encode("ascii") if isinstance(s, str) else bytes(s)
    if len(raw) > 32:
        raise ValueError("hw_id must be <= 32 bytes")
    return raw + b"\0" * (32 - len(raw))


def _validate_lengths(m: Manifest):
    assert len(m.merkle_root) == 4
    assert len(m.image_hash) == 32
    assert len(m.hw_id) == 32
    assert len(m.approval) == 4
    assert len(m.base_hash) == 8               # fixed layout: always present (zero for full)
    assert len(m.signer_pubkey) == 32
    assert len(m.signature) == 64


def build_manifest(*, target_id: int, fw_version: int, image_size: int, payload: bytes,
                   block_size: int, image_hash: bytes, codec_id: int, is_full: bool,
                   base_hash: Optional[bytes] = None, sign_priv=None, hw_id=None) -> Manifest:
    assert (block_size & (block_size - 1)) == 0, "block_size must be a power of two"
    leaves = leaf_hashes(payload, block_size)
    m = Manifest(
        flags=(FLAG_FULL if is_full else 0) | (FLAG_SIGNED if sign_priv is not None else 0),
        target_id=target_id,
        fw_version=fw_version,
        image_size=image_size,
        payload_size=len(payload),
        block_size_log2=block_size.bit_length() - 1,
        merkle_root=merkle_root(leaves),
        image_hash=image_hash,
        codec_id=codec_id,
        hw_id=hw_id_bytes(hw_id),
        base_hash=(b"\0" * 8 if is_full else base_hash),   # always 8 bytes; zero for a full image
        leaves=leaves,
    )
    if not is_full and (base_hash is None or len(base_hash) != 8):
        raise ValueError("delta requires an 8-byte base_hash")
    if sign_priv is not None:
        m.signer_pubkey = sign_priv.public_key().public_bytes_raw()
        m.signature = sign_priv.sign(m.signed_region())    # else signer_pubkey/signature stay zero
    _validate_lengths(m)
    return m


def build_container(manifest: Manifest, payload: bytes) -> bytes:
    assert len(payload) == manifest.payload_size
    mser = manifest.serialize()
    total = 4 + 4 + len(mser) + len(payload) + len(TRAILER)
    return MAGIC + struct.pack("<I", total) + mser + payload + TRAILER


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

@dataclass
class Parsed:
    manifest: Manifest
    payload: bytes
    total_size: int


def parse_container(blob: bytes) -> Parsed:
    if blob[:4] != MAGIC:
        raise ValueError("bad MAGIC")
    if blob[-5:] != TRAILER:
        raise ValueError("bad TRAILER")
    total = struct.unpack("<I", blob[4:8])[0]
    if total != len(blob):
        raise ValueError(f"MOTA_TOTAL_SIZE {total} != actual {len(blob)}")

    r = io.BytesIO(blob[8:-5])  # manifest + payload (trailer already validated/stripped)

    def take(n):
        b = r.read(n)
        if len(b) != n:
            raise ValueError("truncated manifest")
        return b

    m = Manifest()
    m.format_ver = take(1)[0]
    if m.format_ver != FORMAT_VER:
        raise ValueError(f"unsupported format_ver {m.format_ver}")
    m.flags = take(1)[0]
    m.hash_algo = take(1)[0]
    m.target_id, m.fw_version, m.image_size, m.payload_size = struct.unpack("<IIII", take(16))
    m.block_size_log2 = take(1)[0]
    m.merkle_root = take(4)
    m.image_hash = take(32)
    m.codec_id = take(1)[0]
    m.hw_id = take(32)
    m.base_hash = take(8)               # fixed layout: always present (zero for full)
    m.signer_pubkey = take(32)
    m.signature = take(64)
    m.approval = take(4)
    bc = m.block_count
    m.leaves = [take(4) for _ in range(bc)]

    payload = r.read(m.payload_size)
    if len(payload) != m.payload_size:
        raise ValueError("truncated payload")
    rest = r.read()  # trailer was stripped above, so nothing should remain
    if rest != b"":
        raise ValueError("trailing bytes after payload")
    return Parsed(manifest=m, payload=payload, total_size=total)


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def verify(parsed: Parsed, *, expect_pub: Optional[bytes] = None,
           base_image: Optional[bytes] = None) -> List[str]:
    """Return a list of problem strings (empty == fully valid for what could be checked)."""
    problems: List[str] = []
    m, payload = parsed.manifest, parsed.payload

    # block_count / leaves
    if len(m.leaves) != m.block_count:
        problems.append(f"leaves count {len(m.leaves)} != block_count {m.block_count}")

    # merkle root must match recomputation from the actual payload blocks
    recomputed_leaves = leaf_hashes(payload, m.block_size)
    if recomputed_leaves != m.leaves:
        problems.append("stored leaves[] do not match payload blocks")
    try:
        if merkle_root(recomputed_leaves) != m.merkle_root:
            problems.append("merkle_root does not match payload")
    except ValueError as e:
        problems.append(f"merkle: {e}")

    # spot-check a proof round-trips (block 0 and last)
    if recomputed_leaves:
        for idx in {0, len(recomputed_leaves) - 1}:
            pr = merkle_proof(recomputed_leaves, idx)
            if not verify_proof(recomputed_leaves[idx], idx, pr, m.merkle_root, len(recomputed_leaves)):
                problems.append(f"merkle proof failed for block {idx}")

    # approval must be 'not approved' in a distributed container
    if m.approval != APPROVAL_NOT:
        problems.append(f"approval is not the erased sentinel (got {m.approval.hex()})")

    # signature
    if m.is_signed:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        from cryptography.exceptions import InvalidSignature
        pub = Ed25519PublicKey.from_public_bytes(m.signer_pubkey)
        try:
            pub.verify(m.signature, m.signed_region())
        except InvalidSignature:
            problems.append("Ed25519 signature INVALID")
        if expect_pub is not None and m.signer_pubkey != expect_pub:
            problems.append("signer_pubkey != expected key")

    # image_hash: directly checkable only for full images (payload IS the image)
    if m.is_full:
        if mh32(payload) != m.image_hash:
            problems.append("image_hash does not match full payload")
    elif base_image is not None:
        # delta: optionally apply against a provided base to confirm image_hash
        try:
            import detools
            out = io.BytesIO()
            detools.apply_patch(io.BytesIO(_ensure_base(base_image)), io.BytesIO(payload), out)
            rebuilt = out.getvalue()
            if mh32(rebuilt) != m.image_hash:
                problems.append("delta applied to base does not match image_hash")
            if len(rebuilt) != m.image_size:
                problems.append("delta result size != image_size")
        except Exception as e:  # noqa: BLE001
            problems.append(f"delta apply check failed: {e}")
    return problems


def _ensure_base(base_image: bytes) -> bytes:
    img, _ = ensure_endf(base_image)
    return img

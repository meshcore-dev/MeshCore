#include "mota.h"

#include "OtaTargets.h" // shared with the firmware (generated): target_id -> env name
#include "crypto.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace mota {

// ---- merkle (mirror of src/helpers/ota/MerkleTree.cpp) --------------------------------------------
static void merkle_leaf(uint8_t out[4], const uint8_t *block, size_t len) {
  sha256_trunc(out, 4, block, len);
}
static void merkle_combine(uint8_t out[4], const uint8_t *left, const uint8_t *right) {
  uint8_t buf[8];
  std::memcpy(buf, left, 4);
  std::memcpy(buf + 4, right, 4);
  sha256_trunc(out, 4, buf, 8);
}
// Root via binary-counter / Merkle-Mountain-Range with right-to-left bagging (must stay byte-identical to
// src/helpers/ota/MerkleTree.cpp; the native tests cross-check both against the Python reference).
// peaks[k] holds the root of a complete 2^k-leaf subtree; adding a leaf "carries" upward like incrementing
// a binary counter, then the leftover peaks are bagged right-to-left into the final root.
static void merkle_root(uint8_t out[4], const uint8_t *leaves, uint32_t count) {
  if (count == 0) {
    std::memset(out, 0, 4);
    return;
  }
  if (count == 1) {
    std::memcpy(out, leaves, 4);
    return;
  }
  uint8_t peaks[32][4];
  bool valid[32] = { false };
  for (uint32_t i = 0; i < count; i++) {
    uint8_t cur[4];
    std::memcpy(cur, leaves + (size_t)i * 4, 4);
    uint32_t level = 0;
    while (valid[level]) {                       // carry: combine with the pending peak at this level
      merkle_combine(cur, peaks[level], cur);    // peak is earlier (left), cur is right
      valid[level] = false;
      level++;
    }
    std::memcpy(peaks[level], cur, 4);
    valid[level] = true;
  }
  // bag peaks right-to-left: acc starts at the lowest set level (rightmost peak)
  int level = 0;
  while (level < 32 && !valid[level])
    level++;
  uint8_t acc[4];
  std::memcpy(acc, peaks[level], 4);
  for (int l = level + 1; l < 32; l++)
    if (valid[l]) merkle_combine(acc, peaks[l], acc);   // higher peak is left, acc is right
  std::memcpy(out, acc, 4);
}
// leaves[] over the payload (last block short, no padding)
static std::vector<uint8_t> leaf_hashes(const uint8_t *payload, uint32_t size, uint32_t bs) {
  uint32_t bc = (size + bs - 1) / bs;
  std::vector<uint8_t> leaves(bc * 4);
  for (uint32_t i = 0; i < bc; i++) {
    uint32_t off = i * bs, blen = (off + bs <= size) ? bs : (size - off);
    merkle_leaf(leaves.data() + (size_t)i * 4, payload + off, blen);
  }
  return leaves;
}

std::string Manifest::hw_id_str() const {
  size_t n = 0;
  while (n < hw_id.size() && hw_id[n])
    n++;
  return std::string((const char *)hw_id.data(), n);
}

// ---- parse -----------------------------------------------------------------------------------------
std::string parse(const std::vector<uint8_t> &b, Manifest &m) {
  m = Manifest();
  if (b.size() < 8 + MOTA_MFL + 5) return "too small for a .mota";
  if (std::memcmp(b.data(), MOTA_MAGIC, 4) != 0) return "bad MAGIC (not a .mota)";
  uint32_t total = rd_u32(b.data() + 4);
  if (total != b.size()) return "MOTA_TOTAL_SIZE != file length";
  if (std::memcmp(b.data() + b.size() - 5, MOTA_TRAILER, 5) != 0) return "bad TRAILER";
  const uint8_t *mf = b.data() + 8; // manifest start
  m.format_ver = mf[M_OFF_FORMAT_VER];
  if (m.format_ver != FORMAT_VER) return "unsupported format_ver";
  m.flags = mf[M_OFF_FLAGS];
  m.hash_algo = mf[M_OFF_HASH_ALGO];
  m.target_id = rd_u32(mf + M_OFF_TARGET_ID);
  m.fw_version = rd_u32(mf + M_OFF_FW_VERSION);
  m.image_size = rd_u32(mf + M_OFF_IMAGE_SIZE);
  m.payload_size = rd_u32(mf + M_OFF_PAYLOAD_SIZE);
  m.block_size_log2 = mf[M_OFF_BLOCK_SIZE_LOG2];
  std::memcpy(m.merkle_root.data(), mf + M_OFF_MERKLE_ROOT, 4);
  std::memcpy(m.image_hash.data(), mf + M_OFF_IMAGE_HASH, 32);
  m.codec_id = mf[M_OFF_CODEC_ID];
  std::memcpy(m.hw_id.data(), mf + M_OFF_HW_ID, 32);
  std::memcpy(m.base_hash.data(), mf + M_OFF_BASE_HASH, 8);
  std::memcpy(m.signer.data(), mf + M_OFF_SIGNER, 32);
  std::memcpy(m.signature.data(), mf + M_OFF_SIGNATURE, 64);
  std::memcpy(m.approval.data(), mf + M_OFF_APPROVAL, 4);
  if (m.block_size_log2 == 0 || m.block_size_log2 > 24 || m.payload_size == 0)
    return "bad block_size/payload";
  m.block_count = (m.payload_size + m.block_size() - 1) / m.block_size();
  if (m.block_count == 0 || m.block_count > 0xFFFFu) return "block_count out of range";
  if (m.total_size() != b.size()) return "geometry (leaves+payload) != file length";
  return "";
}

// ---- verify ----------------------------------------------------------------------------------------
std::vector<std::string> verify(const std::vector<uint8_t> &b) {
  std::vector<std::string> probs;
  Manifest m;
  std::string e = parse(b, m);
  if (!e.empty()) {
    probs.push_back(e);
    return probs;
  } // unparseable: a single, fatal problem

  const uint8_t *leaves = b.data() + m.leaves_off();
  const uint8_t *payload = b.data() + m.payload_off();

  // recompute leaves[] from the payload -> catches payload corruption
  std::vector<uint8_t> calc = leaf_hashes(payload, m.payload_size, m.block_size());
  if (calc.size() != m.block_count * 4u || std::memcmp(calc.data(), leaves, calc.size()) != 0)
    probs.push_back("leaves[] do not match the payload (corruption)");

  uint8_t root[4];
  merkle_root(root, leaves, m.block_count);
  if (std::memcmp(root, m.merkle_root.data(), 4) != 0) probs.push_back("merkle_root mismatch");

  if (m.is_full()) {
    auto h = mh32(payload, m.payload_size); // full: payload IS the image
    if (std::memcmp(h.data(), m.image_hash.data(), 32) != 0)
      probs.push_back("image_hash mismatch (full image)");
  }
  // (delta image_hash needs the base image -> not checked at relay time; payload integrity is covered above)

  if (m.is_signed()) {
    if (!ed25519_verify(m.signature.data(), b.data() + 8, MOTA_SIGNED_LEN, m.signer.data()))
      probs.push_back("Ed25519 signature INVALID");
  }
  // a distributed .mota must not be pre-approved
  if (std::memcmp(m.approval.data(), APPROVAL_YES, 4) == 0)
    probs.push_back("container is pre-approved (must be FF FF FF FF on the wire)");
  return probs;
}

// ---- EndF -----------------------------------------------------------------------------------------
bool has_endf(const std::vector<uint8_t> &img) {
  if (img.size() < ENDF_LEN) return false;
  const uint8_t *t = img.data() + img.size() - ENDF_LEN;
  if (std::memcmp(t, ENDF_MAGIC, 4) != 0) return false;
  if (rd_u32(t + 4) != img.size() - ENDF_LEN) return false;
  auto h = mh8(img.data(), img.size() - ENDF_LEN);
  return std::memcmp(h.data(), t + 8, 8) == 0;
}

FwIdent parse_endf_ident(const std::vector<uint8_t> &img) {
  FwIdent id;
  if (!has_endf(img)) return id;
  const uint8_t *t = img.data() + img.size() - ENDF_LEN;
  id.fw_version = rd_u32(t + ENDF_OFF_FWVER);
  id.target_id = rd_u32(t + ENDF_OFF_TARGET);
  size_t n = 0;
  while (n < HW_ID_LEN && t[ENDF_OFF_HWID + n])
    n++;
  id.hw_id.assign((const char *)t + ENDF_OFF_HWID, n);
  return id;
}

std::vector<uint8_t> ensure_endf(const std::vector<uint8_t> &img, const FwIdent &id,
                                 std::array<uint8_t, 8> &body_hash8) {
  if (has_endf(img)) { // already trailed: keep its identity, read its hash
    const uint8_t *t = img.data() + img.size() - ENDF_LEN;
    std::memcpy(body_hash8.data(), t + 8, 8);
    return img;
  }
  body_hash8 = mh8(img.data(), img.size());
  std::vector<uint8_t> out = img;
  out.insert(out.end(), ENDF_MAGIC, ENDF_MAGIC + 4);
  uint8_t u[4];
  wr_u32(u, (uint32_t)img.size());
  out.insert(out.end(), u, u + 4);
  out.insert(out.end(), body_hash8.begin(), body_hash8.end());
  wr_u32(u, id.fw_version);
  out.insert(out.end(), u, u + 4);
  wr_u32(u, id.target_id);
  out.insert(out.end(), u, u + 4);
  uint8_t hw[HW_ID_LEN] = { 0 };
  std::memcpy(hw, id.hw_id.data(), id.hw_id.size() < HW_ID_LEN ? id.hw_id.size() : HW_ID_LEN);
  out.insert(out.end(), hw, hw + HW_ID_LEN);
  return out;
}

uint32_t target_id_for_env(const std::string &env) {
  auto h = mh4((const uint8_t *)env.data(), env.size());
  return rd_u32(h.data());
}

std::string target_env_name(uint32_t target_id) {
  // Exhaustive target_id -> env-name table, shared verbatim with the firmware and generated from the live
  // PlatformIO config (every ENABLE_OTA env) by tools/mota/gen_targets.py. See OtaTargets.h.
  const char *env = mesh::ota::ota_target_env_name(target_id);
  return env ? env : "";
}

bool pack_version(const std::string &s, uint32_t &out) {
  uint32_t parts[4] = { 0, 0, 0, 0 };
  int n = 0;
  bool any = false;
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, '.') && n < 4) {
    if (tok.empty()) return false;
    for (char c : tok)
      if (c < '0' || c > '9') return false;
    parts[n++] = (uint32_t)std::strtoul(tok.c_str(), nullptr, 10);
    any = true;
  }
  if (!any) return false;
  out = ((parts[0] & 0xFF) << 24) | ((parts[1] & 0xFF) << 16) | ((parts[2] & 0xFF) << 8) | (parts[3] & 0xFF);
  return true;
}

// ---- build ----------------------------------------------------------------------------------------
// detools delta encode: write base/new images to temp files, run the detools CLI, read the patch back.
static std::string detools_delta(const std::string &detools, uint8_t codec,
                                 const std::vector<uint8_t> &base_img, const std::vector<uint8_t> &new_img,
                                 uint32_t mem, uint32_t seg, std::vector<uint8_t> &patch) {
  char fb[] = "/tmp/motaXXXXXX", fn[] = "/tmp/motaXXXXXX", fp[] = "/tmp/motaXXXXXX";
  int a = mkstemp(fb), c = mkstemp(fn), d = mkstemp(fp);
  if (a < 0 || c < 0 || d < 0) return "mkstemp failed";
  close(a);
  close(c);
  close(d);
  std::string err;
  if (!write_file(fb, base_img.data(), base_img.size()) || !write_file(fn, new_img.data(), new_img.size())) {
    err = "writing temp images failed";
  } else {
    std::vector<std::string> argv;
    if (codec == CODEC_DETOOLS_SEQUENTIAL)
      argv = { detools, "create_patch", "-c", "crle", "-t", "sequential", fb, fn, fp };
    else
      argv = { detools,
               "create_patch_in_place",
               "-c",
               "crle",
               "--memory-size",
               std::to_string(mem),
               "--segment-size",
               std::to_string(seg),
               fb,
               fn,
               fp };
    int rc = run_argv(argv, /*quiet=*/true); // hide detools' success chatter; errors keep stderr
    if (rc == 127)
      err = "could not run detools (install it / pass --detools <path>)";
    else if (rc != 0)
      err = "detools exited with code " + std::to_string(rc);
    else if (!read_file(fp, patch) || patch.empty())
      err = "reading detools patch failed";
  }
  unlink(fb);
  unlink(fn);
  unlink(fp);
  return err;
}

std::string detools_apply_seq(const std::string &detools, const std::vector<uint8_t> &base_img,
                              const std::vector<uint8_t> &patch, std::vector<uint8_t> &out) {
  char ff[] = "/tmp/motaXXXXXX", fpp[] = "/tmp/motaXXXXXX", ft[] = "/tmp/motaXXXXXX";
  int a = mkstemp(ff), c = mkstemp(fpp), d = mkstemp(ft);
  if (a < 0 || c < 0 || d < 0) return "mkstemp failed";
  close(a);
  close(c);
  close(d);
  std::string err;
  if (!write_file(ff, base_img.data(), base_img.size()) || !write_file(fpp, patch.data(), patch.size())) {
    err = "writing temp files failed";
  } else {
    int rc = run_argv({ detools, "apply_patch", ff, fpp, ft }, /*quiet=*/true); // <from> <patch> <to>
    if (rc == 127)
      err = "could not run detools (install it / pass --detools <path>)";
    else if (rc != 0)
      err = "detools apply_patch exited with code " + std::to_string(rc);
    else if (!read_file(ft, out) || out.empty())
      err = "reading reconstructed image failed";
  }
  unlink(ff);
  unlink(fpp);
  unlink(ft);
  return err;
}

std::string build(const BuildOpts &o, std::vector<uint8_t> &out, std::string &suggested_name) {
  bool is_delta = !o.base.empty();

  // resolve identity: explicit flags override the firmware's self-describing EndF
  FwIdent fid = parse_endf_ident(o.fw);
  uint32_t target = o.have_target ? o.target_id : fid.target_id;
  uint32_t fwver = o.have_fwver ? o.fw_version : fid.fw_version;
  std::string hw = !o.hw_id.empty() ? o.hw_id : fid.hw_id;
  FwIdent ident{ fwver, target, hw };

  std::array<uint8_t, 8> new_bh{}, base_bh{};
  std::vector<uint8_t> new_img = ensure_endf(o.fw, ident, new_bh);

  uint8_t codec = o.codec;
  std::vector<uint8_t> payload;
  std::array<uint8_t, 8> base_hash{};

  if (!is_delta) {
    codec = CODEC_FULL;
    payload = new_img; // full: payload IS the image
  } else {
    if (codec == CODEC_FULL) return "a base image was given but --codec is full";
    FwIdent base_id = parse_endf_ident(o.base);
    std::vector<uint8_t> base_img = ensure_endf(o.base, base_id, base_bh);
    base_hash = base_bh;
    // cross-hardware delta guard (read from EndF identity, not filenames)
    if (!o.force && base_id.any() && fid.any()) {
      bool hw_ok = base_id.hw_id.empty() || fid.hw_id.empty() || base_id.hw_id == fid.hw_id;
      bool tgt_ok = !base_id.target_id || !fid.target_id || base_id.target_id == fid.target_id;
      if (!hw_ok || !tgt_ok)
        return "base/target firmware identity differ (hw '" + base_id.hw_id + "' vs '" + fid.hw_id +
               "') — refusing cross-hardware delta (use --force to override)";
    }
    std::string e =
        detools_delta(o.detools, codec, base_img, new_img, o.inplace_memory, o.inplace_segment, payload);
    if (!e.empty()) return e;
  }

  uint32_t bs = o.block_size, image_size = (uint32_t)new_img.size();
  std::vector<uint8_t> leaves = leaf_hashes(payload.data(), (uint32_t)payload.size(), bs);
  uint32_t bc = (uint32_t)leaves.size() / 4;
  if (bc == 0 || bc > 0xFFFFu) return "payload yields an invalid block count";
  uint8_t root[4];
  merkle_root(root, leaves.data(), bc);
  auto image_hash = mh32(new_img.data(), new_img.size());

  bool signed_ = !o.sign_priv.empty();
  uint8_t flags = (is_delta ? 0 : MFLAG_FULL) | (signed_ ? MFLAG_SIGNED : 0);

  // assemble the fixed 197-byte manifest-minus-leaves
  std::vector<uint8_t> mf(MOTA_MFL, 0);
  mf[M_OFF_FORMAT_VER] = FORMAT_VER;
  mf[M_OFF_FLAGS] = flags;
  mf[M_OFF_HASH_ALGO] = HASH_ALGO_SHA256;
  wr_u32(mf.data() + M_OFF_TARGET_ID, target);
  wr_u32(mf.data() + M_OFF_FW_VERSION, fwver);
  wr_u32(mf.data() + M_OFF_IMAGE_SIZE, image_size);
  wr_u32(mf.data() + M_OFF_PAYLOAD_SIZE, (uint32_t)payload.size());
  // block_size_log2
  {
    uint32_t v = bs, l = 0;
    while (v > 1) {
      v >>= 1;
      l++;
    }
    mf[M_OFF_BLOCK_SIZE_LOG2] = (uint8_t)l;
  }
  std::memcpy(mf.data() + M_OFF_MERKLE_ROOT, root, 4);
  std::memcpy(mf.data() + M_OFF_IMAGE_HASH, image_hash.data(), 32);
  mf[M_OFF_CODEC_ID] = codec;
  std::memcpy(mf.data() + M_OFF_HW_ID, hw.data(), hw.size() < HW_ID_LEN ? hw.size() : HW_ID_LEN);
  if (is_delta) std::memcpy(mf.data() + M_OFF_BASE_HASH, base_hash.data(), 8); // zero for full
  if (signed_) {
    uint8_t pub[32];
    if (o.sign_priv.size() != 32) return "signing key must be a 32-byte raw private seed";
    if (!ed25519_pub_from_priv(pub, o.sign_priv.data())) return "bad signing key";
    std::memcpy(mf.data() + M_OFF_SIGNER, pub, 32);
    uint8_t sig[64];
    if (!ed25519_sign(sig, mf.data(), MOTA_SIGNED_LEN, o.sign_priv.data())) return "signing failed";
    std::memcpy(mf.data() + M_OFF_SIGNATURE, sig, 64);
  }
  std::memcpy(mf.data() + M_OFF_APPROVAL, APPROVAL_NOT, 4);

  // container = MAGIC(4) total(4) manifest leaves[] payload trailer(5)
  uint32_t total = 8 + MOTA_MFL + bc * 4 + (uint32_t)payload.size() + 5;
  // nRF52 in-place: the staged container sits below the apply workspace; if it's too big it overruns the
  // workspace and the bootloader apply fails (DETOOLS_IO_FAILED). Warn so it's caught before shipping.
  if (is_delta && codec == CODEC_DETOOLS_INPLACE && total > NRF52_MAX_INPLACE_MOTA)
    std::fprintf(stderr, "warning: in-place delta is %u B, exceeding the nRF52 staging room (%u B) — it will "
                         "NOT apply on the device. Shrink the delta (smaller change) or use a smaller image.\n",
                 total, NRF52_MAX_INPLACE_MOTA);
  out.clear();
  out.reserve(total);
  out.insert(out.end(), MOTA_MAGIC, MOTA_MAGIC + 4);
  uint8_t u[4];
  wr_u32(u, total);
  out.insert(out.end(), u, u + 4);
  out.insert(out.end(), mf.begin(), mf.end());
  out.insert(out.end(), leaves.begin(), leaves.end());
  out.insert(out.end(), payload.begin(), payload.end());
  out.insert(out.end(), MOTA_TRAILER, MOTA_TRAILER + 5);

  // suggested name: <hw|fw>_<target8>_v<ver>_<kind>_<mid8>.mota (descriptive + unique, one folder)
  const char *kind = !is_delta ? "full" : (codec == CODEC_DETOOLS_INPLACE ? "ipdelta" : "seqdelta");
  char tgt[9];
  std::snprintf(tgt, sizeof(tgt), "%08X", target);
  char vbuf[24];
  std::snprintf(vbuf, sizeof(vbuf), "%u.%u.%u", (fwver >> 24) & 0xFF, (fwver >> 16) & 0xFF,
                (fwver >> 8) & 0xFF);
  suggested_name = (hw.empty() ? std::string("fw") : hw) + "_" + tgt + "_v" + vbuf + "_" + kind + "_" +
                   to_hex(root, 4) + ".mota";
  return "";
}

} // namespace mota

// Unit tests for motatool's core logic (no external framework — a tiny built-in harness keeps the
// project self-contained). Run: ./build/motatool_tests   or   ctest --test-dir build --output-on-failure
#include "mota.h"
#include "crypto.h"
#include "serve.h"
#include "input.h"
#include "util.h"
#include "mota_format.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace mota;

static int g_checks = 0, g_fail = 0;
static const char* g_test = "";
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
  std::cerr << "  FAIL [" << g_test << "] " << __LINE__ << ": " #cond "\n"; } } while (0)

// deterministic synthetic firmware body
static std::vector<uint8_t> body(unsigned seed, size_t n) {
  std::vector<uint8_t> v(n);
  uint32_t s = seed * 2654435761u + 1;
  for (size_t i = 0; i < n; i++) { s = s * 1103515245u + 12345u; v[i] = (uint8_t)(s >> 16); }
  return v;
}

static bool detools_available() {
  return run_argv({"detools", "--help"}, /*quiet=*/true) != 127;   // 127 = exec failed (not installed)
}

// ---------------------------------------------------------------------------
static void t_crypto() {
  g_test = "crypto";
  // sha256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  const char* abc = "abc";
  auto h4 = mh4((const uint8_t*)abc, 3);
  auto h8 = mh8((const uint8_t*)abc, 3);
  auto h32 = mh32((const uint8_t*)abc, 3);
  std::vector<uint8_t> exp;
  from_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", exp);
  CHECK(std::memcmp(h4.data(), exp.data(), 4) == 0);
  CHECK(std::memcmp(h8.data(), exp.data(), 8) == 0);
  CHECK(std::memcmp(h32.data(), exp.data(), 32) == 0);

  uint8_t priv[32], pub[32], pub2[32];
  CHECK(ed25519_keygen(priv, pub));
  CHECK(ed25519_pub_from_priv(pub2, priv));
  CHECK(std::memcmp(pub, pub2, 32) == 0);
  auto msg = body(1, 100);
  uint8_t sig[64];
  CHECK(ed25519_sign(sig, msg.data(), msg.size(), priv));
  CHECK(ed25519_verify(sig, msg.data(), msg.size(), pub));
  msg[0] ^= 0xFF;                                   // tampered message -> reject
  CHECK(!ed25519_verify(sig, msg.data(), msg.size(), pub));
  msg[0] ^= 0xFF;
  uint8_t other[32], opub[32]; ed25519_keygen(other, opub);
  CHECK(!ed25519_verify(sig, msg.data(), msg.size(), opub));   // wrong key -> reject
}

static void t_version_target() {
  g_test = "version/target";
  uint32_t v;
  CHECK(pack_version("1.16.0", v) && v == 0x01100000u);
  CHECK(pack_version("2.0.0", v) && v == 0x02000000u);
  CHECK(pack_version("1.16.0.2", v) && (v & 0xFF) == 2);
  CHECK(!pack_version("", v));
  CHECK(!pack_version("x.y", v));
  // known target ids from the firmware builds (ties motatool's hashing to the device)
  CHECK(target_id_for_env("Heltec_v3_repeater") == 0xd1b29b18u);
  CHECK(target_id_for_env("RAK_4631_repeater")  == 0x04d413fdu);
  // reverse lookup table (human-readable target_id) -> env name, "" when unknown
  CHECK(target_env_name(0xd1b29b18u) == "Heltec_v3_repeater");
  CHECK(target_env_name(0x04d413fdu) == "RAK_4631_repeater");
  CHECK(target_env_name(0xDEADBEEFu).empty());
  // the shared (generated) table must round-trip with our own hashing for a spread of OTA envs —
  // this catches any drift between the table's stored ids and sha2-256:4(env)
  for (const char* e : {"Tbeam_SX1262_repeater", "Xiao_C3_repeater", "ThinkNode_M2_room_server",
                        "Ebyte_EoRa-S3_companion_radio_ble", "Heltec_v3_companion_radio_usb"})
    CHECK(target_env_name(target_id_for_env(e)) == e);
}

static void t_intel_hex() {
  g_test = "intel hex";
  fs::path hx = fs::temp_directory_path() / "motatool_test.hex";
  // 3 data bytes (01 02 03) at addr 0, then EOF  (checksum F7 = two's-complement of the byte sum)
  std::string ok = ":03000000010203F7\n:00000001FF\n";
  write_file(hx.string(), (const uint8_t*)ok.data(), ok.size());
  std::vector<uint8_t> out;
  CHECK(read_input(hx.string(), out).empty());
  CHECK(out.size() == 3 && out[0] == 1 && out[1] == 2 && out[2] == 3);
  // wrong checksum -> rejected
  std::string bad = ":03000000010203F8\n:00000001FF\n";
  write_file(hx.string(), (const uint8_t*)bad.data(), bad.size());
  std::vector<uint8_t> o2;
  CHECK(!read_input(hx.string(), o2).empty());
  fs::remove(hx);
}

static void t_endf() {
  g_test = "endf";
  auto b = body(2, 1500);
  std::array<uint8_t,8> bh{};
  FwIdent id{0x01100000u, 0x04d413fdu, "RAK4631"};
  auto img = ensure_endf(b, id, bh);
  CHECK(img.size() == b.size() + ENDF_LEN);
  CHECK(has_endf(img));
  CHECK(std::memcmp(bh.data(), mh8(b.data(), b.size()).data(), 8) == 0);   // body_hash is over BODY only
  auto gi = parse_endf_ident(img);
  CHECK(gi.fw_version == 0x01100000u && gi.target_id == 0x04d413fdu && gi.hw_id == "RAK4631");
  // idempotent: feeding an already-EndF'd image keeps it + reads the same hash
  std::array<uint8_t,8> bh2{};
  auto img2 = ensure_endf(img, FwIdent{}, bh2);
  CHECK(img2 == img && bh2 == bh);
  // zero identity -> still a 56-byte trailer, identity reads empty
  std::array<uint8_t,8> bz{};
  auto z = ensure_endf(b, FwIdent{}, bz);
  CHECK(z.size() == b.size() + ENDF_LEN);
  auto zi = parse_endf_ident(z);
  CHECK(zi.fw_version == 0 && zi.target_id == 0 && zi.hw_id.empty());
  CHECK(!has_endf(std::vector<uint8_t>{1,2,3}));     // too short
}

static BuildOpts full_opts(const std::vector<uint8_t>& fw, const std::vector<uint8_t>& priv = {}) {
  BuildOpts o; o.fw = fw; o.codec = CODEC_FULL;
  o.have_target = true; o.target_id = 0x04d413fdu;
  o.have_fwver = true;  o.fw_version = 0x01100000u;
  o.hw_id = "RAK4631"; o.sign_priv = priv;
  return o;
}

static void t_build_full() {
  g_test = "build full";
  auto fw = body(3, 5 * 1024 + 100);                 // 6 blocks @1024
  std::vector<uint8_t> blob; std::string name;
  CHECK(build(full_opts(fw), blob, name).empty());
  Manifest m;
  CHECK(parse(blob, m).empty());
  CHECK(m.format_ver == FORMAT_VER && m.is_full() && !m.is_signed());
  CHECK(m.codec_id == CODEC_FULL);
  CHECK(m.target_id == 0x04d413fdu && m.fw_version == 0x01100000u && m.hw_id_str() == "RAK4631");
  CHECK(m.block_count == 6);
  // fixed layout offsets
  CHECK(m.leaves_off() == 8 + MOTA_MFL && m.leaves_off() == 205);
  CHECK(m.payload_off() == 205 + m.block_count * 4);
  CHECK(m.total_size() == blob.size());
  // payload IS the image (body+EndF); image_hash matches
  uint32_t poff = m.payload_off();
  auto img_h = mh32(blob.data() + poff, m.payload_size);
  CHECK(std::memcmp(img_h.data(), m.image_hash.data(), 32) == 0);
  CHECK(m.image_size == m.payload_size);
  CHECK(verify(blob).empty());                       // clean
  CHECK(name.find(".mota") != std::string::npos && name.find("full") != std::string::npos);
}

static void t_build_signed_and_tamper() {
  g_test = "build signed/tamper";
  uint8_t priv[32], pub[32]; ed25519_keygen(priv, pub);
  std::vector<uint8_t> pv(priv, priv + 32);
  auto fw = body(4, 3000);
  std::vector<uint8_t> blob; std::string name;
  CHECK(build(full_opts(fw, pv), blob, name).empty());
  Manifest m; CHECK(parse(blob, m).empty());
  CHECK(m.is_signed());
  CHECK(std::memcmp(m.signer.data(), pub, 32) == 0);
  CHECK(verify(blob).empty());
  // tamper a payload byte -> integrity fails
  auto t1 = blob; t1[m.payload_off() + 10] ^= 0xFF;
  CHECK(!verify(t1).empty());
  // tamper a signed-region byte (target_id @ manifest+3 = blob+11) -> signature invalid
  auto t2 = blob; t2[8 + M_OFF_TARGET_ID] ^= 0xFF;
  bool sig_flagged = false;
  for (auto& p : verify(t2)) if (p.find("signature") != std::string::npos) sig_flagged = true;
  CHECK(sig_flagged);
}

static void t_corruption_and_approval() {
  g_test = "corruption/approval";
  auto fw = body(5, 4096);
  std::vector<uint8_t> blob; std::string name;
  build(full_opts(fw), blob, name);
  Manifest m; parse(blob, m);
  // flip a payload byte -> leaves/merkle/image_hash problems reported
  auto c = blob; c[m.payload_off() + 1] ^= 0xFF;
  CHECK(!verify(c).empty());
  // a pre-approved container must be flagged (approval is outside the signed region)
  auto ap = blob; std::memcpy(ap.data() + 8 + M_OFF_APPROVAL, APPROVAL_YES, 4);
  bool approved_flagged = false;
  for (auto& p : verify(ap)) if (p.find("approved") != std::string::npos) approved_flagged = true;
  CHECK(approved_flagged);
}

static void t_parse_rejects() {
  g_test = "parse rejects";
  auto fw = body(6, 2048);
  std::vector<uint8_t> blob; std::string name; build(full_opts(fw), blob, name);
  Manifest m;
  CHECK(!parse(std::vector<uint8_t>(10, 0), m).empty());            // too small
  auto bad = blob; bad[0] ^= 0xFF;  CHECK(!parse(bad, m).empty());  // bad magic
  bad = blob; bad[bad.size() - 1] ^= 0xFF; CHECK(!parse(bad, m).empty());   // bad trailer
  bad = blob; bad[4] ^= 0xFF;       CHECK(!parse(bad, m).empty());  // wrong total_size
  bad = blob; bad[8 + M_OFF_FORMAT_VER] = 9; CHECK(!parse(bad, m).empty()); // bad format_ver
}

static void t_delta() {
  g_test = "delta";
  if (!detools_available()) { std::cerr << "  SKIP [delta] detools not on PATH\n"; return; }
  auto base_body = body(10, 4000);
  auto new_body = base_body;
  for (int i : {100, 101, 2000, 3999}) new_body[i] ^= 0x33;
  auto tail = body(11, 250);
  new_body.insert(new_body.end(), tail.begin(), tail.end());

  for (uint8_t codec : {CODEC_DETOOLS_SEQUENTIAL, CODEC_DETOOLS_INPLACE}) {
    BuildOpts o; o.fw = new_body; o.base = base_body; o.codec = codec;
    o.have_target = true; o.target_id = 0x04d413fdu; o.have_fwver = true; o.fw_version = 0x02000000u;
    o.hw_id = "RAK4631";
    std::vector<uint8_t> blob; std::string name;
    std::string e = build(o, blob, name);
    CHECK(e.empty());
    if (!e.empty()) continue;
    Manifest m; CHECK(parse(blob, m).empty());
    CHECK(!m.is_full() && m.codec_id == codec);
    // base_hash == mh8 of the base BODY
    CHECK(std::memcmp(m.base_hash.data(), mh8(base_body.data(), base_body.size()).data(), 8) == 0);
    CHECK(verify(blob).empty());
    if (codec == CODEC_DETOOLS_SEQUENTIAL) {          // round-trip: apply -> rebuilds the new image
      std::array<uint8_t,8> bh{};
      auto base_img = ensure_endf(base_body, FwIdent{}, bh);
      std::vector<uint8_t> patch(blob.begin() + m.payload_off(), blob.begin() + m.payload_off() + m.payload_size);
      std::vector<uint8_t> recon;
      CHECK(detools_apply_seq("detools", base_img, patch, recon).empty());
      CHECK(std::memcmp(mh32(recon.data(), recon.size()).data(), m.image_hash.data(), 32) == 0);
    }
  }
}

static void t_folder_and_seeder() {
  g_test = "folder/seeder";
  fs::path dir = fs::temp_directory_path() / "motatool_test_folder";
  fs::remove_all(dir); fs::create_directories(dir / "sub");

  // two valid motas (one in a sub-folder, to exercise recursion) + one corrupt
  std::vector<uint8_t> a, b; std::string n;
  build(full_opts(body(20, 3000)), a, n);            write_file((dir / "a.mota").string(), a.data(), a.size());
  build(full_opts(body(21, 6000)), b, n);            write_file((dir / "sub" / "b.mota").string(), b.data(), b.size());
  auto bad = a; bad[a.size() / 2] ^= 0xFF;           write_file((dir / "bad.mota").string(), bad.data(), bad.size());
  write_file((dir / "ignore.txt").string(), a.data(), 10);   // non-.mota -> skipped silently

  int warns = 0; std::string warned_path;
  Folder folder;
  size_t kept = folder.scan(dir.string(), true,
                            [&](const std::string& p, const std::string&){ warns++; warned_path = p; });
  CHECK(kept == 2);                                  // two valid kept
  CHECK(warns == 1 && warned_path.find("bad.mota") != std::string::npos);   // corrupt warned + excluded

  SeederCore core(folder);
  uint8_t status; std::vector<uint8_t> pl;
  CHECK(core.handle(MS_OP_COUNT, nullptr, 0, status, pl) && status == MS_STATUS_OK && pl.size() == 1 && pl[0] == 2);

  uint8_t arg0[1] = {0};
  CHECK(core.handle(MS_OP_DESCRIBE, arg0, 1, status, pl) && status == MS_STATUS_OK && pl.size() == MOTA_DESC_WIRE);
  const ServedMota* s0 = folder.at(0);
  CHECK(std::memcmp(pl.data(), s0->m.merkle_root.data(), 4) == 0);          // mid
  CHECK(rd_u32(pl.data() + 4) == s0->m.target_id);
  CHECK(rd_u32(pl.data() + 14) == (uint32_t)s0->bytes.size());             // total_size
  CHECK(rd_u32(pl.data() + 18) == s0->m.leaves_off() && rd_u32(pl.data() + 18) == 205);
  CHECK(rd_u32(pl.data() + 22) == s0->m.block_count);
  CHECK(rd_u32(pl.data() + 26) == s0->m.payload_off());
  CHECK(rd_u32(pl.data() + 30) == s0->m.payload_size);

  uint8_t bad_idx[1] = {99};
  CHECK(core.handle(MS_OP_DESCRIBE, bad_idx, 1, status, pl) && status == MS_STATUS_ERR);

  // READ idx=0 off=0 len=8 -> first 8 bytes (MAGIC + total_size)
  uint8_t rd[7] = {0, 0,0,0,0, 8,0};
  CHECK(core.handle(MS_OP_READ, rd, 7, status, pl) && status == MS_STATUS_OK && pl.size() == 8);
  CHECK(std::memcmp(pl.data(), s0->bytes.data(), 8) == 0);
  // READ past EOF -> error
  uint8_t reof[7]; reof[0] = 0; wr_u32(reof + 1, (uint32_t)s0->bytes.size()); reof[5] = 16; reof[6] = 0;
  CHECK(core.handle(MS_OP_READ, reof, 7, status, pl) && status == MS_STATUS_ERR);

  fs::remove_all(dir);
}

int main() {
  t_crypto();
  t_version_target();
  t_intel_hex();
  t_endf();
  t_build_full();
  t_build_signed_and_tamper();
  t_corruption_and_approval();
  t_parse_rejects();
  t_delta();
  t_folder_and_seeder();

  std::cout << (g_fail ? "FAILED " : "OK ") << (g_checks - g_fail) << "/" << g_checks << " checks passed\n";
  return g_fail ? 1 : 0;
}

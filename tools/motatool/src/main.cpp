// motatool — build, verify, and serve MeshCore `.mota` firmware-update containers.
//   build   create a full or delta .mota from a firmware (file or http(s) URL)
//   verify  validate one or more .mota (merkle / hashes / signature)
//   serve   serve a folder of .mota to a node (USB serial); invalid files are warned + skipped
//   keygen  generate an Ed25519 signing keypair (64-char hex)
#include "mota.h"
#include "input.h"
#include "serve.h"
#include "crypto.h"
#include "util.h"
#include <cctype>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

namespace fs = std::filesystem;
using namespace mota;

static volatile bool g_stop = false;
static void on_sigint(int) { g_stop = true; }

// minimal flag parser: --key value  /  --flag
struct Args {
  std::map<std::string,std::string> opt;
  std::vector<std::string> pos;
  bool has(const std::string& k) const { return opt.count(k); }
  std::string get(const std::string& k, const std::string& d = "") const {
    auto it = opt.find(k); return it == opt.end() ? d : it->second;
  }
};
static Args parse_args(int argc, char** argv, int start,
                       const std::vector<std::string>& bool_flags) {
  Args a;
  for (int i = start; i < argc; i++) {
    std::string s = argv[i];
    if (s == "-h" || s == "--help" || s == "help") { a.opt["help"] = "1"; continue; }
    if (s == "-v") { a.opt["verbose"] = "1"; continue; }
    if (s.rfind("--", 0) == 0) {
      std::string k = s.substr(2);
      if (std::find(bool_flags.begin(), bool_flags.end(), k) != bool_flags.end()) a.opt[k] = "1";
      else if (i + 1 < argc) a.opt[k] = argv[++i];
      else a.opt[k] = "";
    } else a.pos.push_back(s);
  }
  return a;
}

static bool load_priv(const std::string& path, std::vector<uint8_t>& priv) {
  std::vector<uint8_t> raw;
  if (!read_file(path, raw)) return false;
  std::string txt((const char*)raw.data(), raw.size());        // try hex (mota.py format) first
  while (!txt.empty() && (std::isspace((unsigned char)txt.back()))) txt.pop_back();
  std::vector<uint8_t> hx;
  if (from_hex(txt, hx) && hx.size() == 32) { priv = hx; return true; }
  if (raw.size() == 32) { priv = raw; return true; }            // else accept a raw 32-byte file
  return false;
}

static std::string version_str(uint32_t v) {
  char b[24]; std::snprintf(b, sizeof(b), "%u.%u.%u", (v>>24)&0xFF, (v>>16)&0xFF, (v>>8)&0xFF);
  return b;
}

// ---- help text (the tool is meant to be usable from --help alone) ---------------------------------
static void help_top() {
  std::cout <<
"motatool — build, verify, and serve MeshCore .mota firmware-update containers.\n"
"\n"
"A .mota is a signed, self-verifying package of a firmware update that MeshCore nodes fetch\n"
"over LoRa, block by block. This tool makes those packages, checks them, and serves a folder\n"
"of them to a node over USB.\n"
"\n"
"USAGE\n"
"  motatool <command> [options]\n"
"  motatool <command> --help        detailed help + examples for a command\n"
"\n"
"COMMANDS\n"
"  build    Package a firmware as a .mota (a full image, or a small delta vs a previous build).\n"
"  verify   Check that .mota files are valid (block hashes, image hash, signature).\n"
"  inspect  Print every field of a .mota's manifest (debugging).\n"
"  serve    Serve a folder of .mota to a node over USB serial or WiFi (corrupt files are skipped).\n"
"  keygen   Generate an Ed25519 signing keypair.\n"
"\n"
"TYPICAL WORKFLOW\n"
"  1. Make a signing key once:\n"
"       motatool keygen --out signer.key\n"
"  2. Package a firmware (identity is read from the firmware itself):\n"
"       motatool build --fw firmware.bin --sign signer.key --out-dir ./motas\n"
"  3. Serve the folder to a node plugged in over USB:\n"
"       motatool serve --dir ./motas --serial /dev/ttyUSB0\n";
}

static void help_build() {
  std::cout <<
"motatool build — package a firmware as a .mota update container.\n"
"\n"
"USAGE\n"
"  motatool build --fw <file|url> [--base <file|url>] [options]\n"
"\n"
"WHAT IT DOES\n"
"  With only --fw: builds a FULL update (the payload is the whole firmware image).\n"
"  With --base too: builds a DELTA (a small patch from the base firmware to the new one),\n"
"  which is far smaller to send over LoRa. The delta codec is picked for the target hardware\n"
"  automatically: nRF52 (single-slot) -> in-place, ESP32 (A/B slots) -> sequential.\n"
"\n"
"  Firmware identity (target, version, hardware tag) is read automatically from the firmware's\n"
"  EndF trailer, so you normally don't pass --target-*/--fw-version/--hw-id. Pass them only to\n"
"  override, or for a raw .bin that has no EndF identity.\n"
"\n"
"INPUT (--fw is required)\n"
"  --fw   <file|url>   NEW firmware. A local path OR an http(s):// URL (downloaded with curl/wget).\n"
"                      A .bin is used as-is; a .hex (nRF52/STM32 build) is parsed to its flat image first.\n"
"  --base <file|url>   previous firmware to diff against -> makes a delta (omit it for a full image).\n"
"\n"
"IDENTITY (optional — auto-read from the firmware's EndF)\n"
"  --target-env <env>  PlatformIO env name (e.g. RAK_4631_repeater); hashed into the target id.\n"
"  --target-id  <hex>  raw target id instead of --target-env (e.g. 0x04D413FD).\n"
"  --fw-version <x.y.z> firmware version (e.g. 1.16.0).\n"
"  --hw-id      <tag>  hardware tag (e.g. RAK4631, Heltec_v3). Same tag = bootable-compatible;\n"
"                      a node refuses a .mota whose hw-id is for different hardware (brick-safety).\n"
"\n"
"DELTA OPTIONS\n"
"  --codec full|sequential|inplace   force the codec (default: full with no --base, else auto-from-hw).\n"
"  --inplace-memory  <n>  nRF52 in-place workspace size (default 0xAE000).\n"
"  --inplace-segment <n>  nRF52 in-place erase/segment size (default 4096).\n"
"  --detools <path>       the detools encoder to call (default: 'detools' on PATH). Needed only for deltas.\n"
"  --force                build the delta even if base and target hardware identities differ.\n"
"\n"
"SIGNING\n"
"  --sign <keyfile>   Ed25519 private key (hex or raw 32 bytes, from 'motatool keygen'). Signing lets a\n"
"                     node auto-install the update if it trusts the matching public key. Unsigned still works\n"
"                     for manual installs.\n"
"\n"
"OUTPUT\n"
"  --out-dir <dir>    where to write the .mota (default: current directory). Keep all your .mota in one\n"
"                     folder and point 'serve' at it. The file is auto-named:\n"
"                       <hw>_<target>_v<version>_<full|seqdelta|ipdelta>_<id>.mota\n"
"  --out     <file>   write to exactly this path instead (overrides --out-dir and the auto-name).\n"
"\n"
"EXAMPLES\n"
"  # full image, signed, into ./motas\n"
"  motatool build --fw firmware.bin --sign signer.key --out-dir ./motas\n"
"\n"
"  # full image fetched straight from a release URL\n"
"  motatool build --fw https://example.org/RAK_4631_repeater.bin --sign signer.key --out-dir ./motas\n"
"\n"
"  # delta from the previous release (codec auto-selected from the hardware)\n"
"  motatool build --fw new.bin --base old.bin --sign signer.key --out-dir ./motas\n"
"\n"
"  # raw .bin with no EndF identity: supply it explicitly\n"
"  motatool build --fw app.bin --hw-id Heltec_v3 --target-env Heltec_v3_repeater --fw-version 1.16.0\n";
}

static void help_verify() {
  std::cout <<
"motatool verify — check that .mota files are valid.\n"
"\n"
"USAGE\n"
"  motatool verify <file.mota> [more.mota ...] [--pub <keyfile>] [--base <file|url>]\n"
"\n"
"For each file it checks the structure, that the per-block hashes match the payload, the merkle\n"
"root, the full-image hash (for full images), and the Ed25519 signature (for signed containers).\n"
"It prints 'OK' or 'FAIL <reasons>' per file; the exit code is non-zero if any file fails. This is\n"
"the same validation 'serve' runs on a folder before serving.\n"
"\n"
"OPTIONS\n"
"  --pub  <keyfile>   require the container to be signed by THIS public key (hex/raw, *.pub from keygen).\n"
"  --base <file|url>  for a sequential delta, apply it to this base and confirm it rebuilds the image.\n"
"                     (Applies to every file given; full images ignore it; in-place deltas are skipped.)\n"
"\n"
"EXAMPLES\n"
"  motatool verify ./motas/*.mota\n"
"  motatool verify update.mota --pub signer.key.pub\n"
"  motatool verify delta.mota --base old_firmware.bin\n";
}

static void help_inspect() {
  std::cout <<
"motatool inspect — print every field of a .mota's manifest.\n"
"\n"
"USAGE\n"
"  motatool inspect <file.mota>\n"
"\n"
"Dumps the parsed manifest (versions, sizes, target/hardware, codec, merkle root, image hash,\n"
"base hash, signer key + signature, approval state, block count) — handy for debugging a package.\n"
"It does not validate integrity; use 'verify' for that.\n"
"\n"
"EXAMPLE\n"
"  motatool inspect ./motas/RAK4631_04D413FD_v1.16.0_full_ABCD1234.mota\n";
}

static void help_serve() {
  std::cout <<
"motatool serve — serve a folder of .mota to a MeshCore node over USB serial or WiFi (TCP).\n"
"\n"
"USAGE\n"
"  motatool serve --dir <folder> --serial <port> [options]      # over USB serial\n"
"  motatool serve --dir <folder> --tcp <host[:port]> [options]  # over WiFi (ESP32 companion)\n"
"\n"
"It scans the folder for .mota files, validates each, and serves the valid ones to the node.\n"
"Corrupt/invalid files are reported and skipped — one bad file never stops the rest. The node\n"
"advertises them to the mesh as if it held them, and any node whose hardware matches can fetch\n"
"them. The relay is trustless: fetchers verify every block, so this host never needs the keys.\n"
"\n"
"OPTIONS (--dir and one of --serial / --tcp are required)\n"
"  --dir    <folder>     folder of .mota to serve (searched recursively by default).\n"
"  --serial <port>       the node's USB serial port (e.g. /dev/ttyUSB0 or /dev/ttyACM0).\n"
"  --tcp    <host[:port]> the node's WiFi seeder address (default port 5001). This is a DEDICATED\n"
"                        port, separate from the companion port (5000), so serving doesn't disturb a\n"
"                        phone app connected to the node. The node auto-enables relaying on connect.\n"
"  --baud   <n>          serial speed (default 115200; --serial only).\n"
"  --no-recursive        serve only the top folder; don't descend into sub-folders.\n"
"  --no-enable           (--serial only) don't auto-send 'ota folder on'/'off' on the node's CLI.\n"
"  -v, --verbose         log each request the node makes (COUNT / DESCRIBE / READ).\n"
"\n"
"Leave it running; press Ctrl-C to stop. Over serial it shares the USB cable with the node's text\n"
"console; over TCP it uses the node's dedicated seeder port. The node only pulls while fetching.\n"
"\n"
"EXAMPLES\n"
"  motatool serve --dir ./motas --serial /dev/ttyUSB0 -v\n"
"  motatool serve --dir ./motas --tcp 192.168.4.234 -v\n";
}

static void help_keygen() {
  std::cout <<
"motatool keygen — generate an Ed25519 signing keypair.\n"
"\n"
"USAGE\n"
"  motatool keygen [--out <keyfile>]\n"
"\n"
"Prints the public key. With --out it writes the private key to <keyfile> and the public key to\n"
"<keyfile>.pub (hex). Sign updates with the private key ('build --sign <keyfile>'); trust the\n"
"public key on a node to let it auto-install updates signed by you.\n"
"\n"
"EXAMPLE\n"
"  motatool keygen --out signer.key\n";
}

static std::string hex8(uint32_t v) { char x[9]; std::snprintf(x, 9, "%08X", v); return x; }

// human-readable label for a target_id (its PlatformIO env name), or "N/A" if not in the known table
static std::string target_label(uint32_t t) {
  std::string n = target_env_name(t);
  return n.empty() ? "N/A" : n;
}

static int cmd_verify(const Args& a) {
  if (a.has("help")) { help_verify(); return 0; }
  if (a.pos.empty()) { help_verify(); return 2; }

  std::vector<uint8_t> expect_pub;
  if (a.has("pub") && !load_priv(a.get("pub"), expect_pub)) {   // load_priv accepts a 32-byte hex/raw key
    std::cerr << "cannot load --pub key (expect 32-byte hex or raw)\n"; return 2;
  }
  std::vector<uint8_t> base_img;
  if (a.has("base")) {
    std::vector<uint8_t> b; std::string e = read_input(a.get("base"), b);
    if (!e.empty()) { std::cerr << "error: " << e << "\n"; return 1; }
    std::array<uint8_t,8> bh; base_img = ensure_endf(b, parse_endf_ident(b), bh);
  }

  int bad = 0;
  for (const auto& f : a.pos) {
    std::vector<uint8_t> blob;
    if (!read_file(f, blob)) { std::cout << "FAIL  " << f << " : cannot read\n"; bad++; continue; }
    auto probs = verify(blob);
    Manifest m; bool parsed = parse(blob, m).empty();
    if (parsed && !expect_pub.empty()) {                       // --pub: must be signed by this exact key
      if (!m.is_signed()) probs.push_back("not signed (but --pub was given)");
      else if (std::memcmp(m.signer.data(), expect_pub.data(), 32) != 0) probs.push_back("signed by a different key than --pub");
    }
    if (parsed && !base_img.empty() && !m.is_full()) {         // --base: prove a delta rebuilds the image
      if (m.codec_id == CODEC_DETOOLS_SEQUENTIAL) {
        std::vector<uint8_t> patch(blob.begin() + m.payload_off(), blob.begin() + m.payload_off() + m.payload_size);
        std::vector<uint8_t> recon;
        std::string e = detools_apply_seq(a.get("detools", "detools"), base_img, patch, recon);
        if (!e.empty()) probs.push_back("delta apply: " + e);
        else { auto h = mota::mh32(recon.data(), recon.size());
               if (std::memcmp(h.data(), m.image_hash.data(), 32) != 0) probs.push_back("delta does not rebuild image_hash against --base"); }
      } else {
        std::cout << "note  " << f << " : in-place delta — --base apply-check skipped (bootloader-applied)\n";
      }
    }
    if (probs.empty()) {
      std::cout << "OK    " << f << " : " << (m.is_full() ? "full" : "delta")
                << " target=" << hex8(m.target_id) << " [" << target_label(m.target_id) << "]"
                << " v" << version_str(m.fw_version) << " hw=" << (m.hw_id_str().empty()? "?":m.hw_id_str())
                << " " << (m.is_signed() ? "signed" : "unsigned")
                << " blocks=" << m.block_count << " size=" << blob.size() << "\n";
    } else {
      bad++;
      std::cout << "FAIL  " << f << " :";
      for (auto& p : probs) std::cout << " [" << p << "]";
      std::cout << "\n";
    }
  }
  return bad ? 1 : 0;
}

static int cmd_inspect(const Args& a) {
  if (a.has("help")) { help_inspect(); return 0; }
  if (a.pos.empty()) { help_inspect(); return 2; }
  std::vector<uint8_t> blob;
  if (!read_file(a.pos[0], blob)) { std::cerr << "cannot read " << a.pos[0] << "\n"; return 1; }
  Manifest m;
  std::string e = parse(blob, m);
  if (!e.empty()) { std::cerr << "not a valid .mota: " << e << "\n"; return 1; }
  auto z = [](const uint8_t* p, size_t n){ for (size_t i=0;i<n;i++) if (p[i]) return false; return true; };
  std::cout
    << "total_size     : " << blob.size() << "\n"
    << "format_ver     : " << (int)m.format_ver << "\n"
    << "flags          : 0x" << [&]{char x[3];std::snprintf(x,3,"%02x",m.flags);return std::string(x);}()
        << "  FULL=" << (m.is_full()?"true":"false") << " SIGNED=" << (m.is_signed()?"true":"false") << "\n"
    << "hash_algo      : 0x12 (sha2-256)\n"
    << "target_id      : 0x" << [&]{char x[9];std::snprintf(x,9,"%08x",m.target_id);return std::string(x);}()
        << "  (" << target_label(m.target_id) << ")\n"
    << "fw_version     : " << version_str(m.fw_version) << "  (0x"
        << [&]{char x[9];std::snprintf(x,9,"%08x",m.fw_version);return std::string(x);}() << ")\n"
    << "image_size     : " << m.image_size << "\n"
    << "payload_size   : " << m.payload_size << "\n"
    << "block_size     : " << m.block_size() << "  (log2=" << (int)m.block_size_log2 << ")  block_count=" << m.block_count << "\n"
    << "codec_id       : " << (int)m.codec_id << " ("
        << (m.codec_id==CODEC_FULL?"full":m.codec_id==CODEC_DETOOLS_SEQUENTIAL?"detools-sequential":
            m.codec_id==CODEC_DETOOLS_INPLACE?"detools-in-place":"?") << ")\n"
    << "merkle_root    : " << to_hex(m.merkle_root.data(), 4) << "\n"
    << "image_hash     : " << to_hex(m.image_hash.data(), 32) << "\n"
    << "hw_id          : " << (m.hw_id_str().empty()?"(none)":m.hw_id_str()) << "\n";
  if (!m.is_full()) std::cout << "base_hash      : " << to_hex(m.base_hash.data(), 8)
                              << (z(m.base_hash.data(),8) ? "  (zero)" : "") << "\n";
  if (m.is_signed()) {
    std::cout << "signer_pubkey  : " << to_hex(m.signer.data(), 32) << "\n"
              << "signature      : " << to_hex(m.signature.data(), 64) << "\n";
  }
  bool approved = std::memcmp(m.approval.data(), APPROVAL_YES, 4) == 0;
  std::cout << "approval       : " << to_hex(m.approval.data(), 4) << "  (" << (approved?"APPROVED":"not approved") << ")\n"
            << "leaves[]       : " << m.block_count << " x 4 bytes\n";
  return 0;
}

// infer the apply codec for a delta from the firmware's hardware tag (nRF52 -> in-place, else sequential)
static int infer_codec(const std::string& hw) {
  std::string h; for (char c : hw) h.push_back((char)std::tolower((unsigned char)c));
  if (h.rfind("rak", 0) == 0 || h.find("nrf") != std::string::npos || h.find("nordic") != std::string::npos)
    return CODEC_DETOOLS_INPLACE;
  if (h.find("heltec") != std::string::npos || h.find("esp32") != std::string::npos ||
      h.find("xiao") != std::string::npos || h.find("tbeam") != std::string::npos ||
      h.find("tlora") != std::string::npos || h.find("tdeck") != std::string::npos)
    return CODEC_DETOOLS_SEQUENTIAL;
  return -1;   // unknown
}

static int cmd_build(const Args& a) {
  if (a.has("help")) { help_build(); return 0; }
  if (!a.has("fw")) { std::cerr << "error: --fw is required\n\n"; help_build(); return 2; }
  BuildOpts o;
  std::string e = read_input(a.get("fw"), o.fw);
  if (!e.empty()) { std::cerr << "error: " << e << "\n"; return 1; }
  if (a.has("base")) {
    e = read_input(a.get("base"), o.base);
    if (!e.empty()) { std::cerr << "error: " << e << "\n"; return 1; }
  }
  if (a.has("target-id")) { o.have_target = true; o.target_id = (uint32_t)std::strtoul(a.get("target-id").c_str(), nullptr, 0); }
  else if (a.has("target-env")) { o.have_target = true; o.target_id = target_id_for_env(a.get("target-env")); }
  if (a.has("fw-version")) {
    uint32_t v; if (!pack_version(a.get("fw-version"), v)) { std::cerr << "bad --fw-version\n"; return 2; }
    o.have_fwver = true; o.fw_version = v;
  }
  if (a.has("hw-id")) o.hw_id = a.get("hw-id");
  o.force = a.has("force");
  if (a.has("detools")) o.detools = a.get("detools");
  if (a.has("inplace-memory")) o.inplace_memory = (uint32_t)std::strtoul(a.get("inplace-memory").c_str(), nullptr, 0);
  if (a.has("inplace-segment")) o.inplace_segment = (uint32_t)std::strtoul(a.get("inplace-segment").c_str(), nullptr, 0);
  if (a.has("sign")) {
    if (!load_priv(a.get("sign"), o.sign_priv)) { std::cerr << "cannot load signing key (expect 32-byte hex or raw)\n"; return 1; }
  }

  bool is_delta = !o.base.empty();
  // resolve hw (flags override EndF) to pick the codec
  FwIdent fid = parse_endf_ident(o.fw);
  std::string hw = o.hw_id.empty() ? fid.hw_id : o.hw_id;
  std::string codec = a.get("codec", is_delta ? "auto" : "full");
  if (!is_delta) o.codec = CODEC_FULL;
  else if (codec == "sequential") o.codec = CODEC_DETOOLS_SEQUENTIAL;
  else if (codec == "inplace")    o.codec = CODEC_DETOOLS_INPLACE;
  else if (codec == "full")       { std::cerr << "a --base delta cannot use --codec full\n"; return 2; }
  else { // auto
    int c = infer_codec(hw);
    if (c < 0) { std::cerr << "cannot infer codec from hw '" << hw << "' — pass --codec sequential|inplace\n"; return 2; }
    o.codec = (uint8_t)c;
    std::cerr << "note: codec auto-selected = " << (c == CODEC_DETOOLS_INPLACE ? "inplace" : "sequential")
              << " (from hw '" << (hw.empty()?"?":hw) << "')\n";
  }

  std::vector<uint8_t> blob; std::string name;
  e = build(o, blob, name);
  if (!e.empty()) { std::cerr << "error: " << e << "\n"; return 1; }

  // sanity: the tool's own output must verify
  auto probs = verify(blob);
  if (!probs.empty()) {
    std::cerr << "internal error: built .mota fails verification:";
    for (auto& p : probs) std::cerr << " [" << p << "]";
    std::cerr << "\n"; return 1;
  }

  std::string outpath;
  if (a.has("out")) {                                  // explicit output path (overrides --out-dir + auto-name)
    outpath = a.get("out");
    fs::path parent = fs::path(outpath).parent_path();
    if (!parent.empty()) { std::error_code ec; fs::create_directories(parent, ec); }
  } else {
    std::string outdir = a.get("out-dir", ".");
    std::error_code ec; fs::create_directories(outdir, ec);
    outpath = (fs::path(outdir) / name).string();
  }
  if (!write_file(outpath, blob.data(), blob.size())) { std::cerr << "cannot write " << outpath << "\n"; return 1; }

  Manifest m; parse(blob, m);
  std::cout << "wrote " << outpath << "\n"
            << "  " << (m.is_full() ? "full" : (m.codec_id == CODEC_DETOOLS_INPLACE ? "in-place delta" : "sequential delta"))
            << "  target=" << [&]{char x[9];std::snprintf(x,9,"%08X",m.target_id);return std::string(x);}()
            << "  v" << version_str(m.fw_version) << "  hw=" << (m.hw_id_str().empty()?"?":m.hw_id_str())
            << "  " << (m.is_signed()?"signed":"unsigned") << "\n"
            << "  image=" << m.image_size << "B  payload=" << m.payload_size << "B  blocks=" << m.block_count
            << "  total=" << blob.size() << "B\n";
  return 0;
}

static int cmd_keygen(const Args& a) {
  if (a.has("help")) { help_keygen(); return 0; }
  uint8_t priv[32], pub[32];
  if (!ed25519_keygen(priv, pub)) { std::cerr << "keygen failed\n"; return 1; }
  std::string out = a.get("out", a.get("out-priv"));
  std::string ph = to_hex(priv, 32), kh = to_hex(pub, 32);
  if (!out.empty()) {
    std::string pp = ph + "\n", kp = kh + "\n";
    if (!write_file(out, (const uint8_t*)pp.data(), pp.size()) ||
        !write_file(out + ".pub", (const uint8_t*)kp.data(), kp.size())) { std::cerr << "write failed\n"; return 1; }
    std::cout << "private -> " << out << "\npublic  -> " << out << ".pub\n";
  }
  std::cout << "pubkey: " << kh << "\n";
  return 0;
}

static int cmd_serve(const Args& a) {
  if (a.has("help")) { help_serve(); return 0; }
  bool use_tcp = a.has("tcp");
  if (!a.has("dir") || (!a.has("serial") && !use_tcp)) {
    std::cerr << "error: --dir and one of --serial / --tcp are required\n\n"; help_serve(); return 2;
  }
  bool recursive = !a.has("no-recursive");
  bool verbose = a.has("verbose");
  Folder folder;
  size_t n = folder.scan(a.get("dir"), recursive,
                         [](const std::string& p, const std::string& why) {
                           std::cerr << "  ! skip " << p << " : " << why << "\n";
                         });
  std::cout << "motatool serve: " << n << " valid .mota in " << a.get("dir")
            << (recursive ? " (recursive)" : "") << "\n";
  for (const auto& s : folder.all()) {
    std::cout << "  - " << fs::path(s.path).filename().string()
              << " : mid=" << to_hex(s.m.merkle_root.data(), 4)
              << " target=" << hex8(s.m.target_id) << " [" << target_label(s.m.target_id) << "]"
              << " v" << version_str(s.m.fw_version)
              << " " << (s.m.is_full() ? "full" : (s.m.codec_id == CODEC_DETOOLS_INPLACE ? "ipdelta" : "seqdelta"))
              << " " << (s.m.is_signed() ? "signed" : "unsigned")
              << " blocks=" << s.m.block_count << " size=" << s.bytes.size() << "\n";
  }
  if (n == 0) std::cerr << "  (nothing valid to serve)\n";

  // Pick the transport: a serial port, or a TCP connection to the node's WiFi seeder port (host[:port],
  // default port 5001). The node runs the seeder on a DEDICATED port, separate from its companion port,
  // so serving over WiFi doesn't disturb a phone app connected to the companion.
  SerialTransport st;
  TcpTransport    tt;
  Transport*  t = nullptr;
  std::string target;
  if (use_tcp) {
    std::string hp = a.get("tcp");
    size_t colon = hp.rfind(':');
    std::string host = (colon == std::string::npos) ? hp : hp.substr(0, colon);
    int port = (colon == std::string::npos) ? 5001 : std::atoi(hp.substr(colon + 1).c_str());
    std::string e = tt.open(host, port);
    if (!e.empty()) { std::cerr << "error: " << e << "\n"; return 1; }
    t = &tt; target = host + ":" + std::to_string(port);
  } else {
    std::string e = st.open(a.get("serial"), std::atoi(a.get("baud", "115200").c_str()));
    if (!e.empty()) { std::cerr << "error: " << e << "\n"; return 1; }
    t = &st; target = a.get("serial") + " @ " + a.get("baud", "115200");
  }

  std::signal(SIGINT, on_sigint);
  // The CLI auto-enable only applies to the serial console; the TCP seeder port auto-enables relaying on
  // the node side when this connection opens (and stops when it closes), so there's nothing to send.
  bool enable = !use_tcp && !a.has("no-enable");
  if (enable) { usleep(500000); t->write_str("ota folder on\r\n"); std::cout << "sent `ota folder on`\n"; }
  std::cout << "serving on " << target << " — Ctrl-C to stop\n";

  SeederCore core(folder);
  serve_loop(*t, core, verbose,
             [](const std::string& l) { std::cout << "  [dev] " << l << "\n"; }, &g_stop);

  if (enable) { t->write_str("ota folder off\r\n"); usleep(200000); }
  std::cout << "\nbye\n";
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { help_top(); return 2; }
  std::string cmd = argv[1];
  if (cmd == "help" || cmd == "-h" || cmd == "--help") { help_top(); return 0; }

  std::vector<std::string> bools = {"force","verbose","no-recursive","no-enable"};
  Args a = parse_args(argc, argv, 2, bools);
  if (cmd == "build")   return cmd_build(a);
  if (cmd == "verify")  return cmd_verify(a);
  if (cmd == "inspect") return cmd_inspect(a);
  if (cmd == "serve")   return cmd_serve(a);
  if (cmd == "keygen")  return cmd_keygen(a);
  std::cerr << "unknown command: " << cmd << "\n\n";
  help_top();
  return 2;
}

#include "OtaCli.h"
#include "OtaContext.h"
#include "OtaVerify.h"
#include "OtaSelf.h"
#include "OtaTargets.h"   // ota_target_env_name(): human-readable name for a target_id (no string on the wire)
#if defined(NRF52_PLATFORM)
  #include "OtaBlInfo.h"  // ota_bootloader_caps(): can this device's bootloader apply a .mota?
#endif
#include "Utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Arduino.h>   // millis() for session-age display (device-only command surface)

namespace mesh {
namespace ota {

static uint32_t parse_u32(const char* s) {
  uint32_t n = 0;
  while (*s == ' ') s++;
  while (*s >= '0' && *s <= '9') n = n * 10 + (uint32_t)(*s++ - '0');
  return n;
}

static char fstate_char(OtaManager::FetchState s) {
  switch (s) {
    case OtaManager::IDLE: return 'I';
    case OtaManager::WANT_MANIFEST: return 'W';
    case OtaManager::FETCHING: return 'F';
    case OtaManager::COMPLETE: return 'C';
    default: return 'X';
  }
}

// For users, the only distinction that matters is full image vs. delta (which delta codec is internal).
static const char* codec_kind(uint8_t c) { return c == CODEC_FULL ? "full" : "delta"; }

// A plain-language word for the fetch state (shown in `ota status`).
static const char* state_word(OtaManager::FetchState s) {
  switch (s) {
    case OtaManager::IDLE:          return "idle";
    case OtaManager::WANT_MANIFEST: return "starting";
    case OtaManager::FETCHING:      return "downloading";
    case OtaManager::COMPLETE:      return "ready to install";
    case OtaManager::FAILED:        return "failed";
    default:                        return "?";
  }
}

// Render the packed fw_version as "v1.2.3" (or "v1.2.3.4" when a prerelease byte is set).
static void ver_str(char* out, size_t cap, uint32_t v) {
  FwVersion fw = FwVersion::unpack(v);
  if (fw.prerelease) snprintf(out, cap, "v%u.%u.%u.%u", fw.major, fw.minor, fw.patch, fw.prerelease);
  else               snprintf(out, cap, "v%u.%u.%u", fw.major, fw.minor, fw.patch);
}

// Match the first word of `a` against any of the '|'-separated names (so commands have intuitive aliases
// and short forms); on a match, point `*rest` at the argument text. Keeps the dispatch table readable.
static bool is_cmd(const char* a, const char* names, const char** rest) {
  size_t tlen = 0; while (a[tlen] && a[tlen] != ' ') tlen++;
  for (const char* s = names; *s; ) {
    const char* d = s; while (*d && *d != '|') d++;
    if ((size_t)(d - s) == tlen && tlen && strncmp(a, s, tlen) == 0) {
      const char* r = a + tlen; while (*r == ' ') r++;
      if (rest) *rest = r;
      return true;
    }
    s = (*d == '|') ? d + 1 : d;
  }
  return false;
}

// The everyday OTA surface is BitTorrent-shaped: `ota` shows what you're holding (your running firmware
// as a full mOTA + your one fetch session), `ota neighbors` shows the mOTAs heard around you, `ota pull`
// starts fetching one, `ota drop` frees the session. The raw primitives (manual content load, low-level
// apply steps) live under `ota dev ...` so they don't clutter the everyday surface. Every reply fits one
// packet so it works as remote-admin over LoRa.
static bool handle_dev(const char* d, char* reply, OtaContext& c);

bool handle_ota_command(const char* command, char* reply, mesh::MainBoard& board) {
  const char* a = command + 3;
  if (*a != 0 && *a != ' ') return false;
  while (*a == ' ') a++;
  OtaContext& c = ota_ctx();
  const char* rest = a;

  // ---- raw / internal primitives, tucked under `ota dev ...` ----
  if (is_cmd(a, "dev", &rest)) {
    return handle_dev(rest, reply, c);
  }

  // ---- help: list the commands in plain words (aliases in parentheses) ----
  if (is_cmd(a, "help|?|h", &rest)) {
    snprintf(reply, 160,
      "OTA: status | ls=find updates | get <#>=download | install | cancel | announce | self | "
      "folder | config | key. Try `ota ls`.");

  // ---- inventory dashboard: running fw (self), the one fetch session, serving state ----
  } else if (*a == 0 || is_cmd(a, "status|st", &rest)) {
    SelfFwInfo fi; bool s = ota_self_firmware(fi);
    char selfhx[9]; if (s && fi.valid) mesh::Utils::toHex(selfhx, fi.body_hash, 4); else strcpy(selfhx, "?");
    OtaManager::FetchState fs = c.manager.fetchState();
    char dl[80];
    if (fs == OtaManager::IDLE) {
      strcpy(dl, "no download");
    } else {
      char midhx[9]; mesh::Utils::toHex(midhx, c.manager.fetchManifestId(), 4);
      unsigned have = (unsigned)c.manager.blocksHave(), tot = (unsigned)c.manager.blocksTotal();
      unsigned pct = tot ? (unsigned)((uint64_t)have * 100 / tot) : 0;
      unsigned age = c.session_started_ms ? (unsigned)((millis() - c.session_started_ms) / 1000) : 0;
      snprintf(dl, sizeof dl, "download: %s %u/%u (%u%%) id=%s %us", state_word(fs), have, tot, pct, midhx, age);
    }
    const char* hw = (c.hw_id[0]) ? c.hw_id : "?";
    const char* tenv = ota_target_env_name(c.manager.target());   // env name, or "?" if not in the table
    int n = snprintf(reply, 160, "OTA | this fw %s (%uK) hw=%s | %s | serving:%s (%u) | keys:%u | target:%08X (%s)",
             selfhx, (unsigned)((s ? fi.image_len : 0) / 1024), hw, dl,
             c.serving ? "on" : "off", (unsigned)c.manager.servedCount(),
             (unsigned)c.allow.count(), (unsigned)c.manager.target(), tenv ? tenv : "?");
#if defined(NRF52_PLATFORM)
    // nRF52 applies via the bootloader — show (cached) whether it can, so `ota get`/`install` won't surprise.
    // blrc = the bootloader's last in-place-apply code (diagnostic; 0xB8=success, see ota_delta.c).
    if (n < 146) n += snprintf(reply + n, 160 - n, " | bl:%s blrc:%02X",
                               c.bootloaderCaps().present ? "apply" : "NONE", ota_bootloader_last_rc());
#endif

  // ---- what's available around me (catalogued from beacons + OTA_HAVE), best/most-recent first ----
  } else if (is_cmd(a, "neighbors|nbrs|updates|ls|n", &rest)) {
    // Kick a fresh round of catalog queries (async — rows arrive over the next seconds); render what we
    // have now in plain words. The reply buffer is 160 B (serial / one LoRa packet for remote-admin), so
    // writes are bounded and extra rows collapse to "+N more".
    c.manager.queryAll();
    const int CAP = 160;
    int n = snprintf(reply, CAP, "Updates nearby (%u src) — `ota get <#>` to download:",
                     (unsigned)c.manager.sourceCount());
    const uint8_t* cur = (c.manager.fetchState() != OtaManager::IDLE) ? c.manager.fetchManifestId() : nullptr;
    uint32_t myt = c.manager.target();   // effective target (EndF identity if present, else build flag)
    uint32_t now = millis(); int shown = 0, more = 0;
    for (uint8_t i = 0; i < c.manager.catalogCount(); i++) {
      const OtaManager::CatRow* h = c.manager.catalogRow(i);
      if (CAP - n < 48) { more++; continue; }
      bool on = cur && memcmp(cur, h->mid, 4) == 0;
      uint32_t age = (now - h->last_ms) / 1000; if (age > 99999) age = 99999;
      char ver[20]; ver_str(ver, sizeof ver, h->fw_version);
      // What is this update for? "yours" if same target (hw+role) as us; else the target's env name when we
      // know it (named locally from its 4-byte target_id — no string travels on the wire); else the raw
      // target_id hex (an env this build's OtaTargets.h table doesn't know) or '?' for an unset target.
      char hwbuf[16];
      const char* fit;
      const char* env = ota_target_env_name(h->target_id);
      if (myt && h->target_id == myt) fit = "yours";
      else if (env)                   fit = env;
      else if (h->target_id == 0)     fit = "?";
      else { snprintf(hwbuf, sizeof hwbuf, "hw %08X", (unsigned)h->target_id); fit = hwbuf; }
      n += snprintf(reply + n, CAP - n, "\n %d) %s %s [%s] %un %us%s", shown + 1, ver,
                    codec_kind(h->codec), fit, (unsigned)h->n_seeders, (unsigned)age,
                    on ? " [downloading]" : "");
      shown++;
    }
    if (more && n < CAP) snprintf(reply + n, CAP - n, "\n +%d more", more);
    if (shown == 0) strcpy(reply, "No updates seen yet — re-run `ota ls` in a few seconds (just asked around).");

  // ---- start fetching a specific catalogued mOTA (by list index or manifest_id) ----
  } else if (is_cmd(a, "pull|get|download", &rest)) {
    const char* p = rest;
    if (*p == 0) { strcpy(reply, "usage: ota get <#>   (see the numbers in `ota ls`)"); return true; }
    const OtaManager::CatRow* sel = nullptr; uint8_t mid[4];
    if (*p == '#' || (p[0] >= '1' && p[0] <= '9' && (p[1] == 0 || p[1] == ' '))) {   // index among catalogue
      int idx = atoi(*p == '#' ? p + 1 : p);
      if (idx >= 1 && idx <= c.manager.catalogCount()) sel = c.manager.catalogRow((uint8_t)(idx - 1));
    } else if (mesh::Utils::fromHex(mid, 4, p)) {                                    // explicit manifest_id
      for (uint8_t i = 0; i < c.manager.catalogCount(); i++)
        if (memcmp(c.manager.catalogRow(i)->mid, mid, 4) == 0) { sel = c.manager.catalogRow(i); break; }
    }
    if (!sel) { strcpy(reply, "ERR no such update (see the numbers in `ota ls`)"); return true; }
    if (c.apply_pending) { strcpy(reply, "ERR busy applying"); return true; }
    uint8_t selmid[4]; uint32_t seltgt = sel->target_id; memcpy(selmid, sel->mid, 4);   // sel may move on reset
    c.manager.reset_session(); c.fetch_store.clear();
    c.manager.pull(selmid, seltgt);                          // sets want + begins the manifest fetch now
    char midhx[9]; mesh::Utils::toHex(midhx, selmid, 4);
    sprintf(reply, "OK pulling mid=%s target=%08X (low priority)", midhx, (unsigned)seltgt);

  // ---- discard the current session (e.g. a stalled old fetch) to free the slot ----
  } else if (is_cmd(a, "drop|cancel|stop", &rest)) {
    OtaManager::FetchState fs = c.manager.fetchState();
    char midhx[9]; strcpy(midhx, "-");
    if (fs != OtaManager::IDLE) mesh::Utils::toHex(midhx, c.manager.fetchManifestId(), 4);
    c.manager.reset_session(); c.manager.want(0); c.manager.want_mid(nullptr);
    c.fetch_store.clear(); c.serving = false; c.serve_expected = 0; c.session_started_ms = 0;
    sprintf(reply, "OK dropped session (was %c mid=%s); slot free for a new pull", fstate_char(fs), midhx);

  // ---- broadcast our tiny beacon so peers discover us. If not already serving, set up flash-backed
  //      self-serve first (so we're a real, fetchable source of our own running firmware). ----
  } else if (is_cmd(a, "announce|adv", &rest)) {
    if (!c.serving) c.serving = ota_serve_self(c, 0);
    c.manager.announce();
    sprintf(reply, "OK beacon sent (serving=%s)", c.serving ? "self fw" : "nothing");

  // ---- running firmware identity (compare against a delta's base_hash) ----
  } else if (is_cmd(a, "self|id", &rest)) {
    SelfFwInfo fi;
    if (!ota_self_firmware(fi) || !fi.valid) { strcpy(reply, "ERR no EndF (firmware lacks the trailer?)"); return true; }
    char hx[17]; mesh::Utils::toHex(hx, fi.body_hash, 8);
    int n = snprintf(reply, 160, "self body=%u image=%u base_hash=%s", (unsigned)fi.body_len, (unsigned)fi.image_len, hx);
#if defined(NRF52_PLATFORM)
    // nRF52 applies via the bootloader, so surface whether THIS device's bootloader can (delta install gate)
    const OtaBlCaps& bl = c.bootloaderCaps();   // cached (flash scanned once)
    if (bl.present) snprintf(reply + n, 160 - n, " | bootloader: apply OK (abi=%u codecs=0x%x)", bl.apply_abi, bl.codec_mask);
    else            snprintf(reply + n, 160 - n, " | bootloader: NO mota-apply support (delta install will refuse)");
#endif

  } else if (is_cmd(a, "install|apply|applydelta", &rest)) {
    // Apply the fetched update. Destructive (reflashes + reboots) and GATED, not interactive (no "type
    // yes" round-trip — unreliable over LoRa): refuse unless the fetch is COMPLETE, then the apply path
    // validates in order (payload hash -> built-for-this-firmware -> signature/trust) and returns the
    // FIRST failing gate, so the operator knows exactly why it refused; it proceeds only if all pass.
    if (c.manager.fetchState() != OtaManager::COMPLETE || c.fetch_store.staged_size() == 0) {
      sprintf(reply, "ERR no complete update fetched (fetch=%c %u/%u)",
              fstate_char(c.manager.fetchState()), (unsigned)c.manager.blocksHave(),
              (unsigned)c.manager.blocksTotal());
      return true;
    }
    // On success the slot is armed but NOT yet rebooted — defer so this reply reaches the operator first;
    // the mesh loop reboots once it has been transmitted (same path used by auto-install).
    char m2[100];
    bool ok = c.apply_fetched(m2);
    sprintf(reply, "%s | %s", ok ? "OK" : "ERR", m2);

  // ---- external folder relay: advertise + serve `.mota` from a host daemon over the seeder UART, so the
  //      node hosts MANY images (any architecture) it doesn't hold in flash. Trustless (fetchers verify). --
  } else if (is_cmd(a, "folder|fold", &rest)) {
    const char* p = rest;
    if (strncmp(p, "on", 2) == 0) {
#if defined(OTA_FOLDER_SERIAL)
      if (!c.serving) c.serving = ota_serve_self(c, 0);   // keep serving our own fw alongside the folder
      char m2[120]; c.attach_folder(m2, sizeof(m2)); c.manager.announce();
      strncpy(reply, m2, 159); reply[159] = 0;
#else
      strcpy(reply, "ERR not built with OTA_FOLDER_SERIAL (set the seeder UART in platformio.ini)");
#endif
    } else if (strncmp(p, "off", 3) == 0) {
      c.detach_folder(); c.manager.announce();
      strcpy(reply, "OK folder detached (still serving own fw)");
    } else {                                              // status + list served entries (* = our own fw)
      int n = snprintf(reply, 159, "folder=%s serving=%u:", c.folder_active ? "on" : "off",
                       (unsigned)c.manager.servedCount());
      for (uint8_t i = 0; i < c.manager.servedCount() && n < 148; i++) {
        const OtaManager::ServeEntry* e = c.manager.servedEntry(i);
        if (!e) break;
        char midhx[9]; mesh::Utils::toHex(midhx, e->mid, 4);
        n += snprintf(reply + n, 159 - n, " %s%s/%08X", e->is_self ? "*" : "", midhx, (unsigned)e->target_id);
      }
    }

  // ---- policy config (persisted via NodePrefs). conservative defaults: autofetch/autoinstall off ----
  } else if (is_cmd(a, "config|cfg|set", &rest)) {
    const char* p = rest;
    if (strncmp(p, "autofetch ", 10) == 0) {
      const char* v = p + 10;
      uint8_t pol = strncmp(v, "any", 3) == 0    ? OtaManager::AUTOFETCH_ANY
                  : strncmp(v, "signed", 6) == 0 ? OtaManager::AUTOFETCH_SIGNED
                  : strncmp(v, "off", 3) == 0    ? OtaManager::AUTOFETCH_OFF : 0xFF;
      if (pol == 0xFF) { strcpy(reply, "ERR usage: ota config autofetch <off|any|signed>"); return true; }
      c.manager.set_autofetch(pol); c.config_dirty = true; strcpy(reply, "OK autofetch updated (saved)");
    } else if (strncmp(p, "autoinstall ", 12) == 0) {
      const char* v = p + 12;
      uint8_t pol = strncmp(v, "trusted", 7) == 0 ? OtaContext::AUTOINSTALL_TRUSTED
                  : strncmp(v, "off", 3) == 0     ? OtaContext::AUTOINSTALL_OFF : 0xFF;
      if (pol == 0xFF) { strcpy(reply, "ERR usage: ota config autoinstall <off|trusted>"); return true; }
      c.autoinstall = pol; c.config_dirty = true; strcpy(reply, "OK autoinstall updated (saved)");
    } else if (strncmp(p, "checkpoint ", 11) == 0) {    // resume checkpoint cadence (blocks; 0=never)
      long n = atol(p + 11);
      if (n < 0 || n > 4096) { strcpy(reply, "ERR usage: ota config checkpoint <0..4096>  (blocks; 0=never)"); return true; }
      c.manager.set_checkpoint_blocks((uint16_t)n); c.config_dirty = true;
      sprintf(reply, "OK checkpoint every %ld blocks (saved)%s", n, n == 0 ? " — periodic resume disabled" : "");
    } else if (strncmp(p, "advert ", 7) == 0) {         // beacon re-advertise cadence (minutes; 0=disable)
      long m = atol(p + 7);
      if (m < 0 || m > 10080) { strcpy(reply, "ERR usage: ota config advert <0..10080>  (minutes; 0=disable)"); return true; }
      c.manager.set_advert_mins((uint16_t)m); c.config_dirty = true;
      sprintf(reply, "OK re-advertise every %ld min (saved)%s", m, m == 0 ? " — periodic advert disabled" : "");
    } else if (strncmp(p, "hops ", 5) == 0) {           // OTA flood reach in hops (0 = direct only)
      long h = atol(p + 5);
      if (h < 0 || h > 8) { strcpy(reply, "ERR usage: ota config hops <0..8>  (hops; 0 = direct only)"); return true; }
      c.manager.set_max_hops((uint8_t)h); c.config_dirty = true;
      sprintf(reply, "OK OTA reach = %ld hop%s (saved)%s", h, h == 1 ? "" : "s", h == 0 ? " — direct only" : "");
    } else {                                            // show current policy
      uint8_t af = c.manager.autofetch();
      sprintf(reply, "ota config: autofetch=%s autoinstall=%s checkpoint=%u advert=%umin hops=%u keys=%u  (persisted)",
              af == OtaManager::AUTOFETCH_ANY ? "any" : af == OtaManager::AUTOFETCH_SIGNED ? "signed" : "off",
              c.autoinstall == OtaContext::AUTOINSTALL_TRUSTED ? "trusted" : "off",
              (unsigned)c.manager.checkpoint_blocks(), (unsigned)c.manager.advert_mins(),
              (unsigned)c.manager.max_hops(), (unsigned)c.allow.count());
    }

  // ---- trusted signer allowlist (security config; persisted): `ota key add|rm <hex>` / `ota key` lists ----
  } else if (is_cmd(a, "key|keys", &rest)) {
    const char* p = rest;
    if (strncmp(p, "add ", 4) == 0) {
      uint8_t pub[32];
      if (mesh::Utils::fromHex(pub, 32, p + 4) && c.allow.add(pub)) { c.config_dirty = true; strcpy(reply, "OK key added (saved)"); }
      else strcpy(reply, "ERR key");
    } else if (strncmp(p, "rm ", 3) == 0 || strncmp(p, "remove ", 7) == 0) {
      uint8_t pub[32]; const char* h = p + (p[0] == 'r' && p[1] == 'm' ? 3 : 7);
      if (mesh::Utils::fromHex(pub, 32, h) && c.allow.remove(pub)) { c.config_dirty = true; strcpy(reply, "OK removed (saved)"); }
      else strcpy(reply, "ERR");
    } else {                                              // bare `ota key` (or `key list`) -> show them
      int n = snprintf(reply, 160, "trusted signer keys (%u):", (unsigned)c.allow.count());
      for (uint8_t i = 0; i < c.allow.count() && n < 140; i++) {
        char hx[17]; mesh::Utils::toHex(hx, c.allow.get(i), 8);
        n += snprintf(reply + n, 160 - n, " %s", hx);
      }
      if (c.allow.count() == 0) strcpy(reply, "no trusted signer keys yet (add one with `ota key add <hex>`)");
    }

  } else {
    strcpy(reply, "Unknown OTA command. Type `ota help`.");
  }
  return true;
}

// Raw / internal primitives (manual content load + low-level apply steps), under `ota dev ...`.
static bool handle_dev(const char* d, char* reply, OtaContext& c) {
  if (strncmp(d, "stage ", 6) == 0) {
    uint32_t sz = parse_u32(d + 6);
    if (sz == 0 || sz > OTA_SERVE_BUF_SIZE) { sprintf(reply, "ERR size 1..%u", OTA_SERVE_BUF_SIZE); }
    else { memset(c.serve_buf, 0xFF, sz); c.serve_expected = sz; c.serving = false;
           sprintf(reply, "OK stage %u bytes", (unsigned)sz); }

  } else if (strncmp(d, "recv ", 5) == 0) {
    const char* p = d + 5; uint32_t off = parse_u32(p);
    const char* hex = strchr(p, ' ');
    if (!hex) { strcpy(reply, "ERR usage: ota dev recv <off> <hex>"); return true; }
    hex++;
    int blen = (int)strlen(hex) / 2;
    uint8_t tmp[80];
    if (blen <= 0 || blen > (int)sizeof(tmp) || !mesh::Utils::fromHex(tmp, blen, hex)) strcpy(reply, "ERR hex");
    else if (off + blen > c.serve_expected) strcpy(reply, "ERR off>size (stage first)");
    else { memcpy(c.serve_buf + off, tmp, blen); sprintf(reply, "OK %d@%u", blen, (unsigned)off); }

  } else if (strncmp(d, "serve self", 10) == 0) {     // host our own running firmware, served from flash
    if (ota_serve_self(c, 0)) {
      c.serving = true;
      char midhx[9]; mesh::Utils::toHex(midhx, c.serve_self_manifest + 20, 4);
      uint32_t img = (uint32_t)c.serve_self_manifest[11] | ((uint32_t)c.serve_self_manifest[12] << 8)
                   | ((uint32_t)c.serve_self_manifest[13] << 16) | ((uint32_t)c.serve_self_manifest[14] << 24);
      sprintf(reply, "OK serving self fw mid=%s (%u B, flash-backed) — peers can pull it", midhx, (unsigned)img);
    } else strcpy(reply, "ERR serve self (no EndF / image too big / OOM)");
  } else if (strncmp(d, "serve", 5) == 0) {
    c.serving = c.manager.serve(c.serve_buf, c.serve_expected);
    if (!c.serving) { strcpy(reply, "ERR serve (bad .mota)"); return true; }
    VerifyResult r = ota_verify(c.serve_buf, c.serve_expected, c.allow);
    sprintf(reply, "OK serving | root=%d img=%d sig=%d trust=%d", r.root_ok, r.image_ok, r.sig_ok, r.trusted);

  } else if (strncmp(d, "resume", 6) == 0) {     // re-adopt a container already staged in flash (test/debug)
    bool ok = c.manager.resumeStaged(nullptr);
    sprintf(reply, "%s resume: sess=%c %u/%u", ok ? "OK" : "ERR", fstate_char(c.manager.fetchState()),
            (unsigned)c.manager.blocksHave(), (unsigned)c.manager.blocksTotal());

  } else if (strncmp(d, "announce", 8) == 0) {
    if (!c.serving) { strcpy(reply, "ERR not serving (ota dev serve first)"); return true; }
    c.manager.announce();
    strcpy(reply, "OK announced");

  } else if (strncmp(d, "verify", 6) == 0) {
    const uint8_t* buf; uint32_t len;
    if (c.manager.fetchState() == OtaManager::COMPLETE) { buf = c.fetch_store.data(); len = c.fetch_store.staged_size(); }
    else { buf = c.serve_buf; len = c.serve_expected; }
    if (len == 0 || !buf) { strcpy(reply, "ERR nothing to verify (flash-staged: applydelta verifies)"); return true; }
    VerifyResult r = ota_verify(buf, len, c.allow);
    sprintf(reply, "verify parsed=%d root=%d img=%d signed=%d sig=%d trust=%d | ok=%d auto=%d",
            r.parsed, r.root_ok, r.image_ok, r.is_signed, r.sig_ok, r.trusted, r.integrity_ok(), r.auto_appliable());

  } else if (strncmp(d, "want ", 5) == 0) {
    const char* p = d + 5; while (*p == ' ') p++;
    if (strncmp(p, "auto", 4) == 0) { c.manager.want(0); c.manager.want_mid(nullptr); strcpy(reply, "OK auto (own target only)"); }
    else { uint32_t t = (uint32_t)strtoul(p, nullptr, 16); c.manager.want(t); c.manager.want_mid(nullptr);
           sprintf(reply, "OK cross-target: will fetch %08X (you ensure HW compatible)", (unsigned)t); }

  } else if (strncmp(d, "apply", 5) == 0) {
    const char* sub = d + 5; while (*sub == ' ') sub++;
    if (strncmp(sub, "slot", 4) == 0) {
      uint32_t addr = 0, size = 0;
      if (ota_apply_slot_info(&addr, &size)) sprintf(reply, "inactive slot addr=0x%X size=%u", (unsigned)addr, (unsigned)size);
      else strcpy(reply, "ERR no A/B slot (apply unsupported on this build)");
    } else if (strncmp(sub, "manifest", 8) == 0) {
      if (ota_apply_set_manifest(c.serve_buf, c.serve_expected, c.allow, c.apply_st))
        sprintf(reply, "manifest ok img=%u sig=%d trust=%d", (unsigned)c.apply_st.image_size, c.apply_st.sig_ok, c.apply_st.trusted);
      else strcpy(reply, "ERR manifest parse / not full-image / unsupported");
    } else if (strncmp(sub, "verify", 6) == 0) {
      bool ok = ota_apply_verify_slot(c.apply_st);
      sprintf(reply, "slot image_hash %s (size=%u)", ok ? "MATCH" : "MISMATCH", (unsigned)c.apply_st.image_size);
    } else if (strncmp(sub, "commit", 6) == 0) {
      if (!c.apply_st.slot_ok) { strcpy(reply, "ERR run 'ota dev apply verify' first (slot must match)"); return true; }
      ota_apply_commit();                 // set boot partition + reboot; no return
      strcpy(reply, "ERR commit failed (no A/B slot?)");
    } else {
      strcpy(reply, "ERR ota dev apply (slot|manifest|verify|commit)");
    }

  } else if (strncmp(d, "clear", 5) == 0) {
    c.serve_expected = 0; c.serving = false; c.fetch_store.clear(); c.manager.reset_session();
    strcpy(reply, "OK cleared");

  } else {
    strcpy(reply, "ota dev: stage|recv|serve|announce|verify|want|apply slot|manifest|verify|commit|clear");
  }
  return true;
}

} // namespace ota
} // namespace mesh

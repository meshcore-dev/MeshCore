#pragma once

#include <stddef.h>   // size_t / NULL for the reply classifiers below
#include <stdint.h>

// Fork-owned, dependency-free spec for the WebConfig "config batch / reboot /
// stop" decision + timing core, plus its host tests (test/test_webconfig_batch/).
//
// This is the Phase 6 counterpart of MQTTLifecycle.h: a PURE state machine that
// captures exactly what src/helpers/esp32/WebConfigServer.cpp decides today, so
// the POST-accept / drain / result-read / reboot / stop transitions can be
// exercised deterministically without AsyncWebServer, ArduinoJson, WiFi, or the
// FreeRTOS mutex/refcount.
//
// WIRED: WebConfigServer.cpp calls these functions directly, so they are
// load-bearing and the host tests cover production behavior rather than a
// parallel copy of it. MAX_BATCH and STOP_WARN_MS in WebConfigServer.h alias
// kMaxBatch/kStopWarnMs here, so the constants cannot drift either.
//
// Two deliberate asymmetries remain between this spec and its caller:
//
//  1. finishRebootAt() returns 0 for "no reboot scheduled", but the caller must
//     only ASSIGN _reboot_at when the result is non-zero. _reboot_at is not
//     solely batch-owned — the manual /api/reboot route arms it from the
//     async_tcp task, possibly while a batch is still draining — so an
//     unconditional assign would silently cancel a manual reboot.
//  2. classifyPost() is consulted in two phases by handleConfigPost, because the
//     change count is only known after the `set` map is parsed, and parsing must
//     not precede the Replay/Busy answer (a replayed POST carrying a bad key
//     must still receive its 202).
//
// The file:line references below point at the behavior each function mirrors.
//
// Behavior source (all line refs against WebConfigServer.{h,cpp} at the time of
// writing): constants at .h:90-96,155-161; POST accept at .cpp:610-719; drain at
// .cpp:289-334; result read at .cpp:721-791; reboot fire at .cpp:262-265;
// isRebootPending at .cpp:70-74; stop gating at .cpp:185-255.
namespace WebConfigBatch {

// Constants, verbatim from WebConfigServer.
static const int kMaxBatch = 24;             // .h:90 MAX_BATCH
static const uint32_t kDrainPacingMs = 25;   // .cpp:296 inter-command gap
static const uint32_t kRebootFallbackMs = 30000;  // .cpp:331 drain-finish fallback
static const uint32_t kRebootConfirmMs = 3000;    // .cpp:784 first result-read arm
static const uint32_t kStopWarnMs = 10000;   // .h:95 STOP_WARN_MS

// The batch lifecycle. A fresh POST moves Idle->Pending; the drainer moves
// Pending->Done; Done stays re-readable until the next POST claims the slot;
// finalizeTeardown() resets to Idle.
enum class State : uint8_t {
  Idle = 0,
  Pending,
  Done,
};

// millis() idioms. elapsedMs uses unsigned wraparound (correct across one 32-bit
// rollover). scheduleAt mirrors the production wrap-guard: every _reboot_at /
// _stop_warn_at assignment does `if (t == 0) t = 1;` so 0 keeps meaning
// "unscheduled" even when the deadline lands exactly on the rollover boundary.
static inline uint32_t elapsedMs(uint32_t now, uint32_t then) { return now - then; }
static inline uint32_t scheduleAt(uint32_t now, uint32_t delay) {
  const uint32_t t = now + delay;
  return t == 0 ? 1u : t;
}
// Signed wrap-safe "deadline reached", matching the production
// `(int32_t)(now - deadline) >= 0` comparisons.
static inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

// --------------------------------------------------------------------------
// POST accept classification (.cpp:637-718). Precedence, verbatim from the
// source: an in-flight/finished batch with the SAME reqid is an idempotent
// replay (commands are NOT re-applied); a DIFFERENT reqid while a batch is still
// PENDING is rejected as busy; otherwise a batch with no changes and no reboot
// is a no-op, and anything else is accepted. Note the asymmetry: a different
// reqid while DONE is NOT busy — the new batch overwrites the DONE slot.
//
// Assumes the request already passed reqid grammar (WebConfigKeys::wcIsValidReqId)
// and per-key allowlist/secret validation, which are covered by test_webconfig_keys.
// --------------------------------------------------------------------------
enum class PostOutcome : uint8_t {
  Replay,     // 202; reqid matches the current batch, commands not re-applied
  Busy,       // 409; a different batch is still PENDING
  Accept,     // 202; a new batch is accepted (from Idle, or overwriting a Done slot)
  NoChanges,  // 400; nothing to do (no changes and no reboot requested)
};

static inline PostOutcome classifyPost(State state, bool reqid_matches_current,
                                       int change_count, bool reboot_after) {
  if (state != State::Idle && reqid_matches_current) return PostOutcome::Replay;
  if (state == State::Pending) return PostOutcome::Busy;  // reqid differs (match handled above)
  if (change_count <= 0 && !reboot_after) return PostOutcome::NoChanges;
  return PostOutcome::Accept;
}

// The state string a Replay body reports mirrors the batch state (.cpp:640):
// "done" when the matched batch already finished, else "pending".
static inline const char* replayStateName(State state) {
  return state == State::Done ? "done" : "pending";
}

// --------------------------------------------------------------------------
// Drain (.cpp:289-333). One command per tick.
// --------------------------------------------------------------------------

// The drainer waits only BETWEEN commands: never before the first (batch_next
// == 0 runs immediately and fires onConfigBatchStart), never after the last, and
// otherwise until the 25 ms pacing gap elapses. The pacing compare is SIGNED to
// mirror the source verbatim (.cpp:296 `(int32_t)(now - _batch_last_cmd) < 25`),
// matching deadlineReached()'s signedness; for all reachable inputs (elapsed
// 0..25 ms) it is identical to the unsigned form.
static inline bool drainMustWait(int batch_next, int batch_count,
                                 uint32_t now, uint32_t last_cmd_ms) {
  return batch_next > 0 && batch_next < batch_count &&
         (int32_t)(now - last_cmd_ms) < (int32_t)kDrainPacingMs;
}

// all_ok is a sticky AND across command replies; a reply counts as ok iff it
// begins with "OK" (.cpp:314). Once false it stays false.
static inline bool nextAllOk(bool prev_all_ok, bool reply_is_ok) {
  return prev_all_ok && reply_is_ok;
}

// The batch is finished once the post-increment drain index reaches the count
// (.cpp:319-321).
static inline bool drainFinished(int batch_next_after_increment, int batch_count) {
  return batch_next_after_increment >= batch_count;
}

// On finish, a reboot-requested + all-ok batch arms the 30 s fallback deadline;
// a partially-failed batch (all_ok == false) never reboots (.cpp:324-333).
// Returns the reboot_at deadline, or 0 for "no reboot scheduled".
static inline uint32_t finishRebootAt(bool batch_reboot, bool batch_all_ok, uint32_t now) {
  return (batch_reboot && batch_all_ok) ? scheduleAt(now, kRebootFallbackMs) : 0u;
}

// --------------------------------------------------------------------------
// Result read (.cpp:738-786).
// --------------------------------------------------------------------------
enum class ResultOutcome : uint8_t {
  Idle,     // 200 "idle" — no batch; any valid reqid is echoed
  Unknown,  // 404 — a batch exists but the reqid does not match it
  Pending,  // 200 "pending"
  Done,     // 200 "done" (+ per-command results)
};

static inline ResultOutcome classifyResult(State state, bool reqid_matches_current) {
  if (state == State::Idle) return ResultOutcome::Idle;   // no reqid check while idle
  if (!reqid_matches_current) return ResultOutcome::Unknown;
  return state == State::Pending ? ResultOutcome::Pending : ResultOutcome::Done;
}

// The "reboot" flag reported in a Done body (.cpp:767): only a fully-OK,
// reboot-requested batch advertises a pending reboot.
static inline bool doneReportsReboot(bool batch_reboot, bool batch_all_ok) {
  return batch_reboot && batch_all_ok;
}

// The first Done read arms the confirmed (3 s) reboot exactly once (.cpp:777-786):
// the !already_armed guard makes later reads idempotent, so polling cannot push
// the deadline out. When this returns true the caller sets armed = true and
// reboot_at = confirmRebootAt(now).
static inline bool shouldArmConfirmReboot(State state, bool batch_reboot,
                                          bool batch_all_ok, bool already_armed) {
  return state == State::Done && batch_reboot && batch_all_ok && !already_armed;
}
static inline uint32_t confirmRebootAt(uint32_t now) {
  return scheduleAt(now, kRebootConfirmMs);
}

// --------------------------------------------------------------------------
// CLI sequences (/api/cli). The terminal shares this one deferred-command slot
// with config saves rather than owning a second MAX_BATCH array: both drain on
// the loop task, both are single-slot, and a duplicate would cost ~8 KB of
// permanently resident RAM. Sharing also makes a save and a CLI run mutually
// exclusive, which they must be.
//
// Two things differ from a config save:
//  1. results stream. A save's results appear only when the whole batch is
//     Done; a CLI read hands back whatever has executed so far, so a pasted
//     sequence fills the terminal command by command.
//  2. the reboot is not requested by a `reboot` flag on the request but by the
//     word `reboot` appearing in the sequence. It is deferred rather than
//     executed, because Board::reboot() does not return and would take the node
//     down before the client could read a single result.
// --------------------------------------------------------------------------

// Results returned by one read. Bounds the JSON document built on the
// async_tcp task; a longer sequence pages across successive reads.
static const int kCliResultPage = 8;

static inline int cliPageCount(int from, int produced, int page) {
  const int pending = produced - from;
  if (pending <= 0) return 0;
  return pending > page ? page : pending;
}

// "done" means the client has been handed every result, not merely that
// execution finished: the last page may still be unread, and a client that
// stops polling at "done" would lose it.
static inline bool cliReadIsFinal(State state, int from, int page_count, int total) {
  return state == State::Done && from + page_count >= total;
}

// A trailing `reboot` is withheld when any command in the sequence failed,
// exactly as a config save's is. The operator asked for the reboot, but
// rebooting into a half-applied config — over a link they may not get back —
// is the worse failure, and the result body reports the refusal.
static inline bool cliRebootAllowed(bool has_reboot, bool all_ok) {
  return has_reboot && all_ok;
}

// CommonCLI has no single failure convention. Testing only for an "Err" prefix
// let five other shapes through as success — including "Unknown command" and
// "unknown config: x", the two an operator hits most — which coloured them
// green AND let a queued reboot proceed after them.
//
// Every shape below is a literal from CommonCLI.cpp / CommonCLI_Observer.cpp.
// This list is the fragile part of the design: a new failure string added there
// is silently a success here. Which is exactly why it must not be what decides
// whether to reboot — see cliReplyConfirmsWrite.
static inline bool cliReplyIsFailure(const char* r) {
  if (r == NULL || r[0] == 0) return false;    // empty is normalised to "OK"
  static const char* const kPrefixes[] = {
    "Err", "ERR", "err",          // "Err - ", "ERR: ", "Error: "
    "(ERR",                       // "(ERR: clock cannot go backwards)"
    "Unknown command",
    "unknown config",
    "??",                         // "??: <key>" from the get fallthrough
    "Can't find",                 // "Can't find GPS"
  };
  for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); i++) {
    const char* p = kPrefixes[i];
    size_t n = 0;
    while (p[n]) n++;
    bool match = true;
    for (size_t j = 0; j < n; j++) {
      if (r[j] != p[j]) { match = false; break; }
    }
    if (match) return true;
  }
  // "File system erase: Err" reports the failure at the END of the reply.
  for (size_t i = 0; r[i]; i++) {
    if (r[i] == ':' && r[i + 1] == ' ' && r[i + 2] == 'E' && r[i + 3] == 'r' &&
        r[i + 4] == 'r') return true;
  }
  return false;
}

// Whether a command's reply is allowed to gate the deferred reboot.
//
// Only writes are, and only writes have a reply convention worth trusting:
// every setter answers with an "OK" prefix. Diagnostics do not — `memory`
// answers "Free: ...", a getter answers "> value" — so letting them gate would
// mean guessing, and guessing wrong here either strands the operator (a
// harmless `memory` blocks their reboot) or reboots into a config that did not
// apply. The question the gate exists to answer is narrower than "did anything
// fail": it is "did every setting I asked for actually take".
static inline bool cliReplyGatesReboot(const char* cmd) {
  if (cmd == NULL) return false;
  const char* set = "set ";
  const char* pwd = "password ";
  bool is_set = true, is_pwd = true;
  for (int i = 0; i < 4; i++) if (cmd[i] != set[i]) { is_set = false; break; }
  for (int i = 0; i < 9; i++) if (cmd[i] != pwd[i]) { is_pwd = false; break; }
  return is_set || is_pwd;
}

// A write took effect iff its reply starts with "OK" — the one convention every
// setter in CommonCLI actually keeps.
static inline bool cliWriteSucceeded(const char* reply) {
  return reply != NULL && reply[0] == 'O' && reply[1] == 'K';
}

// --------------------------------------------------------------------------
// Reboot fire (.cpp:262-265) and isRebootPending (.cpp:70-74).
// --------------------------------------------------------------------------
static inline bool rebootDue(uint32_t reboot_at, uint32_t now) {
  return reboot_at != 0 && deadlineReached(now, reboot_at);
}

// isRebootPending() reports true only for a config-save reboot in the Done
// state, so the manual /api/reboot path (batch_reboot == false) is deliberately
// NOT reported as pending even though _reboot_at is set.
static inline bool isConfigRebootPending(uint32_t reboot_at, bool batch_reboot, State state) {
  return reboot_at != 0 && batch_reboot && state == State::Done;
}

// --------------------------------------------------------------------------
// Stop gating (.cpp:244-255). Teardown waits indefinitely for in-flight async
// handlers to drain (refs == 0); the STOP_WARN_MS timer only triggers a one-time
// diagnostic — it never forces teardown.
// --------------------------------------------------------------------------
enum class StopAction : uint8_t {
  Finalize,  // refs == 0: finalizeTeardown() may run now
  Warn,      // refs > 0 and the warn deadline passed, not yet warned: log once
  Wait,      // refs > 0: keep the session alive and wait
};

static inline StopAction stopStep(uint32_t handler_refs, bool already_warned,
                                  uint32_t stop_warn_at, uint32_t now) {
  if (handler_refs == 0) return StopAction::Finalize;
  if (!already_warned && stop_warn_at != 0 && deadlineReached(now, stop_warn_at)) {
    return StopAction::Warn;
  }
  return StopAction::Wait;
}

}  // namespace WebConfigBatch

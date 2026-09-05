#pragma once

#include <stdint.h>

// Fork-owned, dependency-free MQTT bridge lifecycle state machine and the
// narrow dependency seam used to drive it deterministically in host tests.
//
// This is the Phase 4 "ownership and teardown test seam" from
// STABILITY_TESTABILITY_HANDOFF.md. It is intentionally pure (no Arduino,
// FreeRTOS, WiFi, or PsychicMqttClient dependencies) so the exact
// start/stop/restart contract can be exercised with a fake clock and a
// recording Ops double, the same way MQTTConnectionPolicy.h and
// MQTTRuntimeBufferLifecycle.h are tested.
//
// Scope boundary (Phase 4 vs Phase 5): this header is the SPEC and the test
// seam. It is not yet wired into MQTTBridge. Phase 5 ("Implement cooperative
// MQTT shutdown") supplies FreeRTOS/PsychicMqttClient-backed Ops and replaces
// the abrupt vTaskDelete in MQTTBridge::end() with this cooperative lifecycle.
// See MQTT_OWNERSHIP.md for the ownership model and the migration plan.
//
// Behavior source (Phase 0 discipline): every transition and invariant encoded
// here is derived from the current MQTTBridge.cpp control flow (begin()/end(),
// mqttTaskLoop(), the volatile handshakes) — not from a hardware soak. Values
// that require on-hardware characterization (the concrete stop timeout, exact
// mbedTLS teardown timing) are called out with "Phase 0 TODO" and left as
// injectable parameters rather than guessed constants.
namespace MQTTLifecycle {

// The lifecycle proposed by the handoff:
//   Stopped -> Starting -> Running -> StopRequested -> Stopping -> Stopped
//
// StopRequested: a stop has been requested and delivered through the ownership
//   channel, but the MQTT task has not yet begun its ordered shutdown.
// Stopping:      the MQTT task is performing its ordered client/service
//   shutdown. A StopBegan signal is optional; a task may ack directly from
//   StopRequested if it does not report the intermediate step.
enum class State : uint8_t {
  Stopped = 0,
  Starting,
  Running,
  StopRequested,
  Stopping,
};

// Events driven either by the owner (loop task) or by the MQTT task reporting
// its own progress. StopTimedOut is synthesized by the Coordinator when a stop
// is not acknowledged within the bounded timeout (the reviewed fallback).
enum class Event : uint8_t {
  StartRequested = 0,  // owner asked the bridge to start
  StartCompleted,      // MQTT task signalled init complete (StartAck)
  StartFailed,         // init failed on the task (partial-init rollback)
  StopRequested,       // owner (or OTA barrier) asked the bridge to stop
  StopBegan,           // MQTT task began its ordered shutdown (optional)
  StopAcknowledged,    // MQTT task signalled ordered shutdown complete (StopAck)
  StopTimedOut,        // bounded timeout expired with no StopAck (fallback)
};

// Side effects a transition asks the caller to perform. Naming WHO does WHAT
// keeps the Phase 5 production wiring and the host fakes on one contract:
//  - create_task:       owner creates/pins the MQTT task.
//  - deliver_stop:      owner delivers the stop request through the channel.
//  - release_resources: owner may now free the queue, runtime buffers, and the
//                       task handle. Fires only after a completed/forced stop
//                       or an init-failure rollback — never mid-run.
//  - ota_release:       the OTA barrier's completion acknowledgment is now
//                       available (a stop reached a terminal state).
struct Effects {
  bool create_task = false;
  bool deliver_stop = false;
  bool release_resources = false;
  bool ota_release = false;
};

struct Result {
  State next;
  Effects effects;
  bool accepted;  // false => the event was a no-op in this state (idempotency)
};

// Unsigned subtraction is the standard millis() idiom and stays correct across
// a single 32-bit rollover (mirrors MQTTConnectionPolicy::elapsedMs).
inline uint32_t elapsedMs(uint32_t now, uint32_t then) { return now - then; }

// Pure transition function. For a rejected/no-op event the result reports the
// unchanged state, no effects, and accepted == false.
inline Result apply(State s, Event e) {
  Result r{s, Effects{}, false};
  switch (s) {
    case State::Stopped:
      // Only a start is meaningful. A stop while already stopped is a no-op
      // (idempotent stop). StartFailed/StopAck cannot occur here.
      if (e == Event::StartRequested) {
        r.next = State::Starting;
        r.effects.create_task = true;
        r.accepted = true;
      }
      break;

    case State::Starting:
      switch (e) {
        case Event::StartCompleted:
          r.next = State::Running;
          r.accepted = true;
          break;
        case Event::StartFailed:
          // Partial-init rollback: release only what the attempt acquired.
          r.next = State::Stopped;
          r.effects.release_resources = true;
          r.accepted = true;
          break;
        case Event::StopRequested:
          // Stop before full initialization: accept it and let the task ack.
          r.next = State::StopRequested;
          r.effects.deliver_stop = true;
          r.accepted = true;
          break;
        default:
          break;
      }
      break;

    case State::Running:
      if (e == Event::StopRequested) {
        r.next = State::StopRequested;
        r.effects.deliver_stop = true;
        r.accepted = true;
      }
      // A duplicate StartRequested/StartCompleted while Running is a no-op.
      break;

    case State::StopRequested:
      switch (e) {
        case Event::StopBegan:
          r.next = State::Stopping;
          r.accepted = true;
          break;
        case Event::StopAcknowledged:
        case Event::StopTimedOut:
          r.next = State::Stopped;
          r.effects.release_resources = true;
          r.effects.ota_release = true;
          r.accepted = true;
          break;
        default:
          // Duplicate StopRequested is a no-op (idempotent stop).
          break;
      }
      break;

    case State::Stopping:
      switch (e) {
        case Event::StopAcknowledged:
        case Event::StopTimedOut:
          r.next = State::Stopped;
          r.effects.release_resources = true;
          r.effects.ota_release = true;
          r.accepted = true;
          break;
        default:
          break;
      }
      break;
  }
  return r;
}

// New connects/publishes/retries/reconfigurations are permitted only while the
// bridge is actively running (or still coming up). Once a stop is requested,
// all new work must cease (handoff: "Cessation of new connects, publishes,
// retries, and reconfigurations").
inline bool acceptsNewWork(State s) {
  return s == State::Starting || s == State::Running;
}

// A late/stale client callback may touch owner-released resources ONLY before
// the owner has freed them. After the terminal Stopped state the queue,
// buffers, and clients may be gone, so a callback arriving then must be a
// no-op. (Callbacks that fire during StopRequested/Stopping run before
// release_resources and are still safe.)
inline bool mayTouchOwnedState(State s) { return s != State::Stopped; }

// A restart (begin()) is safe only from a completed stop.
inline bool mayRestart(State s) { return s == State::Stopped; }

inline bool isStopInProgress(State s) {
  return s == State::StopRequested || s == State::Stopping;
}

inline const char* stateName(State s) {
  switch (s) {
    case State::Stopped:       return "Stopped";
    case State::Starting:      return "Starting";
    case State::Running:       return "Running";
    case State::StopRequested: return "StopRequested";
    case State::Stopping:      return "Stopping";
  }
  return "?";
}

inline const char* eventName(Event e) {
  switch (e) {
    case Event::StartRequested:   return "StartRequested";
    case Event::StartCompleted:   return "StartCompleted";
    case Event::StartFailed:      return "StartFailed";
    case Event::StopRequested:    return "StopRequested";
    case Event::StopBegan:        return "StopBegan";
    case Event::StopAcknowledged: return "StopAcknowledged";
    case Event::StopTimedOut:     return "StopTimedOut";
  }
  return "?";
}

// Narrow dependency seam. These are the only dependencies the lifecycle needs
// to be driven deterministically (handoff: "for only the dependencies needed").
// Phase 5 implements this over FreeRTOS + PsychicMqttClient; host tests
// implement it as a recording double with a settable clock.
//
// Dependencies enumerated by the handoff and where they land:
//  - Clock/timer                 -> nowMs()
//  - Task start/stop/ack/timeout -> startTask()/deliverStop() + the
//                                   onTaskStarted()/onTaskStopped() callbacks
//                                   into Coordinator, and tick() for timeout
//  - Runtime allocator + queue   -> folded into releaseResources() here; the
//                                   allocate/free symmetry itself is already
//                                   covered by MQTTRuntimeBufferLifecycle.h,
//                                   and queue behavior by Phase 6
//  - MQTT client connect/disconnect + delayed callbacks -> modeled by the
//                                   mayTouchOwnedState() guard (a callback
//                                   decides whether it may touch owned state)
//  - OTA coordinator/barrier     -> onStopComplete(clean) + mayBeginFlash()
struct Ops {
  virtual ~Ops() = default;
  virtual uint32_t nowMs() = 0;          // monotonic ms (millis())
  virtual void startTask() = 0;          // create/pin the MQTT task
  virtual void deliverStop() = 0;        // signal stop through the channel
  virtual void releaseResources() = 0;   // free queue/buffers/task (post-stop)
  // Unblock the OTA barrier's waiter. clean == true after a StopAcknowledged;
  // clean == false after a StopTimedOut, so OTA aborts rather than flashing
  // under uncertain ownership (handoff OTA barrier: "MQTT stop times out: OTA
  // aborts safely rather than writing under uncertain ownership").
  virtual void onStopComplete(bool clean) = 0;
};

// Drives the state machine against injected Ops and hosts the bounded stop
// timeout. Idempotent start/stop; safe restart after a completed stop.
class Coordinator {
 public:
  // stop_timeout_ms is the bound on how long a requested stop may run before the
  // reviewed force-kill fallback fires. It is injected (not a constant here) and
  // may be updated per stop via setStopTimeoutMs(): Phase 0 hardware
  // characterization (2026-07-19) showed real mbedTLS/wss teardown scales with
  // the number of connected slots (~5-6 s each, sequential), so the owner sizes
  // it to the current slot count before each stop. See MQTTBridge::end().
  Coordinator(Ops& ops, uint32_t stop_timeout_ms)
      : _ops(ops), _stop_timeout_ms(stop_timeout_ms) {}

  State state() const { return _state; }
  bool stopTimedOut() const { return _stop_timed_out; }

  bool acceptsNewWork() const { return MQTTLifecycle::acceptsNewWork(_state); }
  bool mayTouchOwnedState() const {
    return MQTTLifecycle::mayTouchOwnedState(_state);
  }
  bool isStopInProgress() const {
    return MQTTLifecycle::isStopInProgress(_state);
  }
  // A restart is safe from a completed stop. A stop that reached Stopped via
  // the timeout fallback still allows restart (the bridge is down); only OTA
  // flashing is withheld after a dirty stop.
  bool mayRestart() const { return MQTTLifecycle::mayRestart(_state); }
  // OTA erase/write is permitted only after a CLEAN stop. A timed-out stop
  // leaves ownership uncertain, so flashing stays blocked until a clean
  // start/stop cycle clears the latch.
  bool mayBeginFlash() const {
    return _state == State::Stopped && !_stop_timed_out;
  }

  // Update the stop-timeout bound. Call before requestStop() to size the window
  // to the current slot count (see MQTTBridge::end()); the value is read by
  // tick() against _stop_request_ms, which requestStop() arms afterwards.
  void setStopTimeoutMs(uint32_t ms) { _stop_timeout_ms = ms; }
  uint32_t stopTimeoutMs() const { return _stop_timeout_ms; }

  bool requestStart() { return dispatch(Event::StartRequested); }
  bool requestStop() { return dispatch(Event::StopRequested); }
  bool onTaskStarted() { return dispatch(Event::StartCompleted); }
  bool onTaskStartFailed() { return dispatch(Event::StartFailed); }
  bool onStopBegan() { return dispatch(Event::StopBegan); }
  bool onTaskStopped() { return dispatch(Event::StopAcknowledged); }

  // Call periodically from the owner. Fires the reviewed timeout fallback if a
  // requested stop has not been acknowledged within stop_timeout_ms.
  void tick() {
    if (MQTTLifecycle::isStopInProgress(_state) &&
        elapsedMs(_ops.nowMs(), _stop_request_ms) >= _stop_timeout_ms) {
      dispatch(Event::StopTimedOut);
    }
  }

 private:
  bool dispatch(Event e) {
    const Result r = apply(_state, e);
    if (!r.accepted) return false;

    _state = r.next;
    if (e == Event::StartRequested) {
      _stop_timed_out = false;  // a fresh start clears the dirty-stop latch
    } else if (e == Event::StopRequested) {
      _stop_request_ms = _ops.nowMs();  // arm the timeout window
    } else if (e == Event::StopTimedOut) {
      _stop_timed_out = true;
    }

    if (r.effects.create_task) _ops.startTask();
    if (r.effects.deliver_stop) _ops.deliverStop();
    if (r.effects.release_resources) _ops.releaseResources();
    if (r.effects.ota_release) _ops.onStopComplete(e == Event::StopAcknowledged);
    return true;
  }

  Ops& _ops;
  uint32_t _stop_timeout_ms;
  State _state = State::Stopped;
  uint32_t _stop_request_ms = 0;
  bool _stop_timed_out = false;
};

}  // namespace MQTTLifecycle

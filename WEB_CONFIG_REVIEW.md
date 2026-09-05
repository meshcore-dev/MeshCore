# WebConfig Branch Review

## Scope

This review covers the fork-owned WebConfig and Heltec Tracker additions on the `webconfig` branch, principally commits `d7a7e1b6`, `dfee21a0`, and `639c07a4`, plus the fork-owned MQTT/CLI paths they invoke. Issues inherited unchanged from `meshcore-dev/MeshCore` are intentionally excluded.

No implementation changes are included in this document.

## Executive Summary

The portal builds successfully and has a sound high-level design: HTTP handlers avoid directly running CLI/radio operations, configuration writes are marshalled to the loop task, secrets are represented by placeholders, and the UI is self-contained for offline provisioning.

Before deployment, the most important work is:

1. Secure setup/forced-AP reachability.
2. Correlate each save with its own result.
3. Prevent reboot after partially failed wizard saves.
4. Make persisted Wi-Fi and MQTT settings match live runtime behavior.
5. Remove cross-task preference/statistics races and long loop-task blocking.

## Priority 1: Security and Data Integrity

### 1. Forced AP mode exposes the unauthenticated setup API on the existing LAN

**Severity:** High

**Locations:**

- `src/helpers/esp32/WebConfigServer.cpp:125-134`
- `src/helpers/esp32/WebConfigServer.cpp:346-349`
- `src/helpers/bridges/MQTTBridge.cpp:801-866`
- `src/helpers/bridges/MQTTBridge.cpp:919-926`
- `examples/simple_repeater/MyMesh.cpp:1329-1339`
- `examples/simple_room_server/MyMesh.cpp:974-984`

**Problem:**

`MQTTBridge::end()` deliberately leaves the STA association connected. Forced setup then selects `WIFI_AP_STA`, starts the server on port 80, and disables authentication for all requests while `MODE_SETUP` is active. The server is therefore reachable through both the setup AP and the existing LAN connection.

The comment that setup mode implies physical proximity is not valid in forced-AP mode. Any host on the existing LAN can read or change configuration and reboot the node without the admin password.

**Suggested fix:**

Choose one of these approaches:

- Disconnect and disable STA before entering unauthenticated setup mode, leaving only the SoftAP interface active. If scanning requires STA mode, keep the interface enabled but explicitly disconnect it and prevent auto-reconnect.
- Bind the setup listener only to the SoftAP interface if the ESPAsyncWebServer/network stack supports reliable interface binding.
- Keep authentication enabled for setup-mode requests arriving through STA, while allowing unauthenticated requests only through the SoftAP interface.

Add a hardware test that starts from an associated STA connection, stops the bridge, enters forced AP mode, and confirms that another LAN host cannot access `/api/config` or `/api/reboot` without authentication.

### 2. Default provisioning sends credentials over an open HTTP network

**Severity:** High

**Locations:**

- `src/helpers/esp32/WebConfigServer.cpp:130-134`
- `src/helpers/esp32/WebConfigServer.cpp:346-349`
- `src/helpers/esp32/WebConfigServer.cpp:530-598`

**Problem:**

No reviewed observer environment defines `WEBCONFIG_AP_PASSWORD`, so setup creates an open AP. Setup requests are unauthenticated and use plain HTTP. Wi-Fi passwords, MQTT passwords, and access tokens can be captured by another nearby station. A nearby party can also provision the device before the intended operator.

**Suggested fix:**

- Generate a unique per-device setup password from secure random data during first boot and show it on the display or a physical label.
- Alternatively require a short-lived setup PIN displayed on-device and validated by the API before secrets can be submitted.
- Add an explicit provisioning-session expiry and invalidate the setup credential after successful setup.
- If an intentionally open AP remains supported, document the threat model prominently and avoid describing proximity as authentication.

### 3. Save results are not correlated with the submitted batch

**Severity:** High

**Locations:**

- `webui/index.html:630-680`
- `src/helpers/esp32/WebConfigServer.cpp:547-550`
- `src/helpers/esp32/WebConfigServer.cpp:601-641`

**Problem:**

The server keeps the last completed result readable. The frontend polls `/api/config/result` after nearly every POST failure because the POST may have reached the device even if its response was lost. There is no request or batch identity.

A rejected, lost, or concurrent save can therefore consume a previous or different tab's result, report success, clear `st.dirty`, and overwrite unsaved values with the device configuration.

**Suggested fix:**

- Have the browser generate a random request ID and include it in the POST body.
- Store that ID with the batch and include it in the 202 response and every result response.
- Require the frontend to accept `pending` or `done` only when the ID matches.
- Treat definite HTTP responses such as 400, 409, and 413 as final rejection; only poll after an ambiguous network failure.
- Return the active request ID with 409 so the browser can distinguish its own retry from another client's batch.

Add regression tests for stale `DONE`, two tabs, 409, lost 202, lost result response, and a POST that never reached the server.

### 4. Failed wizard commands still cause partial application and reboot

**Severity:** High

**Locations:**

- `src/helpers/esp32/WebConfigServer.cpp:265-297`
- `src/helpers/esp32/WebConfigServer.cpp:624-637`
- `webui/index.html:950-957`

**Problem:**

Commands are persisted independently. Error replies are recorded, but `_batch_reboot` still schedules a fallback reboot, and reading the result arms the three-second reboot without checking aggregate success. The wizard can display rejected settings while the device reboots with a partially applied configuration.

**Suggested fix:**

- Track aggregate batch success while draining commands.
- Arm automatic reboot only if every required command succeeded.
- If partial persistence cannot be rolled back, return an explicit `partial` state listing applied and rejected keys.
- Keep the portal active after partial failure and present deliberate choices: correct and retry, or reboot with the partial configuration.
- Longer term, validate all values before executing any setter, or stage a complete preference snapshot and commit it atomically.

## Priority 2: Runtime Correctness

### 5. LAN Wi-Fi edits do not perform the reconnect promised by the UI

**Severity:** High

**Locations:**

- `webui/index.html:339-350`
- `webui/index.html:630-641`
- `src/helpers/CommonCLI_Observer.cpp:278-285`

**Problem:**

The Wi-Fi tab says the connection will restart and the page will drop. Normal editor saves do not request reboot, and the underlying setters only persist the SSID/password. The active connection remains on the old network, while subsequent config reads show the new persisted values.

**Suggested fix:**

Use an explicit `Save and reconnect` or `Save and reboot` flow for SSID/password changes. Deliver the result first, then schedule the reconnect/reboot. Do not replace the displayed active network with the persisted network until the transition has begun successfully.

### 6. MQTT publishing controls report success without changing live behavior

**Severity:** High

**Locations:**

- `webui/index.html:303-319`
- `src/helpers/CommonCLI_Observer.cpp:215-238`
- `src/helpers/bridges/MQTTBridge.cpp:637-645`

**Problem:**

Status, packet, raw, RX, and TX options are copied into cached bridge fields during initialization. Their CLI setters persist preferences but do not refresh those fields or restart the bridge. The UI reports success although the running bridge continues using old values.

**Suggested fix:**

- Add one task-safe bridge method that reloads the publishing flags from preferences on the MQTT task, or
- coalesce these changes into one full bridge restart after the batch.

If live application is not desirable, label these controls as restart-required and provide a reboot action instead of claiming immediate success.

### 7. Custom MQTT endpoint edits do not reliably refresh the live slot

**Severity:** High

**Locations:**

- `src/helpers/CommonCLI_Observer.cpp:385-425`
- `examples/simple_repeater/MyMesh.cpp:1354-1367`
- `examples/simple_room_server/MyMesh.cpp:999-1012`
- `src/helpers/bridges/MQTTBridge.cpp:1066-1072`
- `src/helpers/bridges/MQTTBridge.cpp:2076-2095`

**Problem:**

Custom server and port setters persist without requesting slot reconfiguration. Credential changes request reconfiguration, but the custom branch of `applySlotPreset()` reuses the existing slot fields instead of copying the current host, port, username, and password from preferences.

**Suggested fix:**

Create a single `reloadSlotFromPrefs(slot)` operation executed on the MQTT task. It should tear down the slot, copy every custom field and preset-dependent credential from `MQTTPrefs`, validate the complete endpoint, and then reconnect. Every slot-affecting CLI setter should queue that same operation rather than implementing partial restart behavior.

### 8. Wizard review and validation mishandle intentionally cleared values

**Severity:** Medium

**Locations:**

- `webui/index.html:907-929`
- `webui/index.html:931-933`

**Problem:**

Review calculations use `dirtyValue || originalValue`. An intentional empty string is treated as absent, so Review shows the original SSID/password/name/identity/slot while the submitted batch clears it. Final SSID validation can pass using the old SSID even though the effective new value is empty.

**Suggested fix:**

Resolve values by key presence, not truthiness. Add a helper such as `effectiveValue(key)` that returns `st.dirty[key]` when the key exists in `st.dirty`, including `""`, and otherwise returns `st.orig[key]`. Use it for review, validation, and reboot messaging.

### 9. A credential equal to `********` cannot be configured

**Severity:** Low

**Locations:**

- `webui/index.html:422`
- `webui/index.html:527-538`
- `src/helpers/esp32/WebConfigServer.cpp:564`

**Problem:**

The secret sentinel is also a valid possible password/token. The UI either considers it unchanged or the backend silently drops it.

**Suggested fix:**

Track secret-field edit state separately from the displayed placeholder. Prefer an empty password control with an adjacent “stored credential unchanged” indicator. If retaining the sentinel, reject that exact value with a clear validation message rather than silently ignoring it.

## Priority 3: Concurrency, Responsiveness, and Lifecycle

### 10. The preference mutex does not synchronize reads with writes

**Severity:** Medium

**Locations:**

- `src/helpers/esp32/WebConfigServer.cpp:454-523`
- `src/helpers/esp32/WebConfigServer.cpp:265-280`
- `src/helpers/CommonCLI.cpp:1074-1216`
- `src/helpers/CommonCLI_Observer.cpp:198-486`

**Problem:**

`handleConfigGet()` takes `_mux` while reading preferences, but `drainBatch()` invokes CLI setters outside `_mux`. Those setters mutate the same strings and scalar fields. The lock therefore does not protect the data from concurrent async HTTP reads.

**Suggested fix:**

Prefer loop-task ownership: have the HTTP handler request a configuration snapshot, let `tick()` build it on the loop task, and return the immutable snapshot. If retaining direct reads, every writer must take the same mutex, with careful review to avoid holding it through flash writes or callbacks.

### 11. Changing `mqtt.ntp` can block the main loop for up to 30 seconds

**Severity:** Medium

**Locations:**

- `src/helpers/CommonCLI_Observer.cpp:249-272`
- `src/helpers/bridges/MQTTBridge.cpp:3230-3247`
- `src/helpers/esp32/WebConfigServer.cpp:275-280`

**Problem:**

Web batches run from `tick()` on the Arduino loop task. The NTP setter waits for the MQTT task in a polling loop that can last 30 seconds. During that period mesh/radio processing, portal DNS, further batch work, stats, and reboot timers stop progressing.

**Suggested fix:**

Persist and validate hostname syntax synchronously, then queue NTP synchronization without waiting. Represent NTP validation as a separate asynchronous operation/status result. The config batch should finish immediately and the UI can poll NTP validation independently.

### 12. MQTT status snapshots read mutable cross-core state without synchronization

**Severity:** Medium

**Locations:**

- `src/helpers/bridges/MQTTBridge.cpp:295-320`
- `examples/simple_repeater/MyMesh.cpp:1403-1408`
- `examples/simple_room_server/MyMesh.cpp:1048-1053`

**Problem:**

The loop task reads slot state and client counters while the MQTT task and callbacks mutate them. This can yield inconsistent snapshots and formal C++ data races.

**Suggested fix:**

Build a plain-data `SlotStatusSnapshot` array on the MQTT task and publish it atomically or under a shared lock. The web/loop side should read only the copied snapshot and should not call client methods cross-task.

### 13. Fixed-delay server deletion does not prove connections have ended

**Severity:** Medium, hardware/library stress-test required

**Locations:**

- `src/helpers/esp32/WebConfigServer.cpp:188-204`

**Problem:**

Stopping the listener and waiting two seconds does not establish that accepted slow or stalled clients have completed. Deleting the server and route lambdas while a request remains active risks use-after-free or crashes.

**Suggested fix:**

Use connection/request reference counting and finalize deletion only after all accepted requests disconnect, with an upper-bound recovery policy. If the library cannot expose lifecycle state safely, consider retaining one server instance for the firmware lifetime and enabling/disabling routes/listening without deleting captured handler state.

### 14. Repeated starts permanently grow the global default-header list

**Severity:** Medium

**Location:**

- `src/helpers/esp32/WebConfigServer.cpp:177-185`

**Problem:**

`DefaultHeaders::Instance().addHeader()` appends to a process-lifetime list on every server creation. Repeated start/stop cycles consume heap and add duplicate `Cache-Control` headers to every response.

**Suggested fix:**

Register the global header once through a static one-time guard, or avoid global headers and add `Cache-Control: no-store` to each WebConfig response.

### 15. Stats polling can accumulate overlapping requests

**Severity:** Low

**Locations:**

- `webui/index.html:717-747`

**Problem:**

A three-second `setInterval` starts a new request even if the previous fetch is still pending. Degraded Wi-Fi can accumulate requests and pressure both browser and ESP32 memory.

**Suggested fix:**

Schedule the next poll with `setTimeout` only after the current request settles. Add an in-flight guard and use the existing API timeout support.

## Priority 4: Validation, Build, and Maintainability

### 16. Malformed keys can cause out-of-bounds reads and invalid JSON errors

**Severity:** Low

**Locations:**

- `src/helpers/esp32/WebConfigServer.cpp:41-59`
- `src/helpers/esp32/WebConfigServer.cpp:554-562`

**Problem:**

Short attacker-supplied keys are indexed at positions 4 and 5 without first establishing their length. Rejected keys are interpolated into hand-built JSON without escaping quotes or backslashes.

**Suggested fix:**

Check the key length before `memcmp` and indexed access. Construct error responses with ArduinoJson rather than string interpolation.

### 17. Input lengths do not match fixed firmware buffers

**Severity:** Low

**Locations:**

- `webui/index.html` configuration controls
- `src/helpers/CommonCLI.h:106-159`

**Problem:**

Several controls accept more text than the fixed `MQTTPrefs` fields retain. The CLI truncates values while the UI reports success.

**Suggested fix:**

Add `maxlength` values matching each destination buffer minus the NUL terminator, and validate lengths in the backend because client-side constraints are bypassable. Return a clear error instead of silently truncating.

### 18. Generated HTML freshness relies only on timestamps

**Severity:** Low

**Location:**

- `scripts/generate_webconfig_html.py:30-38`

**Problem:**

A generated header with a future timestamp can survive later source edits and embed stale UI code.

**Suggested fix:**

Use an explicit SCons source/target dependency or store the source hash in the generated header and regenerate whenever it differs. Deterministic gzip output can remain unchanged.

### 19. Tracker v1.1 reports itself as Tracker V2

**Severity:** Medium

**Locations:**

- `boards/heltec_tracker_v1_1.json:19,28`
- `variants/heltec_tracker_v2/platformio.ini:61-109`
- `variants/heltec_tracker_v2/HeltecTrackerV2Board.cpp:82-84`

**Problem:**

The v1.1 environment reuses the V2 board implementation, whose manufacturer name is hardcoded as `Heltec Tracker V2`. WebConfig and MQTT metadata therefore report the wrong board.

**Suggested fix:**

Return `Heltec Tracker V1.1` when `HELTEC_TRACKER_V1_1` is defined, and retain the V2 value otherwise. Add a build-time or host-side assertion for both target identities.

### 20. WebConfig operation and security behavior are undocumented

**Severity:** Documentation gap

**Locations:**

- Commands added in `src/helpers/CommonCLI_Observer.cpp:982-994`
- `MQTT_IMPLEMENTATION.md`

**Problem:**

The build targets are documented, but operators cannot discover `start webconfig`, `start webconfig ap`, `stop webconfig`, first-boot AP behavior, authentication, timeout, or the security implications of setup mode.

**Suggested fix:**

Add an operator section to `MQTT_IMPLEMENTATION.md` covering:

- first-boot setup behavior;
- AP name and setup credential;
- LAN versus AP modes;
- exact CLI commands;
- authentication requirements;
- idle and absolute timeout behavior;
- how Wi-Fi changes are applied;
- how to recover through serial if provisioning fails.

### 21. Repeater and room-server integration is duplicated

**Severity:** Optimization

**Locations:**

- `examples/simple_repeater/MyMesh.*`
- `examples/simple_room_server/MyMesh.*`
- Corresponding `UITask.cpp` files

**Problem:**

Lifecycle, restart coalescing, stats construction, and display behavior are implemented twice. Fixes can easily land in only one role.

**Suggested fix:**

Extract shared WebConfig callbacks, lifecycle ownership, and JSON snapshot helpers into a common observer helper. Keep role-specific radio/stat sources as injected callbacks.

### 22. The HTML generator runs for every ESP32 build

**Severity:** Optimization

**Locations:**

- `platformio.ini:57-72`

**Problem:**

The generator runs from `esp32_base` even when WebConfig is not compiled into the target.

**Suggested fix:**

Move the pre-script to MQTT observer environments or have the script inspect build flags and return immediately unless `WITH_MQTT_BRIDGE`/WebConfig is enabled.

### 23. New observer targets enable MQTT debug logging

**Severity:** Optimization

**Locations:**

- `variants/heltec_tracker_v2/platformio.ini:190,226,262,298`

**Problem:**

`MQTT_DEBUG=1` increases serial activity and code/logging overhead in targets that otherwise appear intended for deployment.

**Suggested fix:**

Remove it from production environments or create explicit debug variants. Confirm that no diagnostic output includes credentials or tokens.

## Test Recommendations

No WebConfig-specific automated tests were found. Add focused tests for:

1. Save request/result correlation, including stale and concurrent batches.
2. Partial command failures and reboot gating.
3. Effective-value handling when fields are intentionally cleared.
4. Secret placeholder/edit semantics.
5. CLI parsing and backend length validation.
6. Runtime application of Wi-Fi and cached MQTT settings.
7. Complete custom-slot reconfiguration.
8. NTP updates without loop-task blocking.
9. Config and MQTT status snapshots under concurrent updates.
10. Repeated server start/stop heap behavior and duplicate headers.
11. Forced AP reachability from both SoftAP and STA networks.
12. Setup AP absolute expiry with an idle associated station.
13. Correct board identity for Tracker v1.1 and V2 targets.

## Verification Already Performed

The review ran the following checks successfully:

- `git diff --check`
- Extracted JavaScript with `node --check`
- PlatformIO builds:
  - `heltec_tracker_v1_1_repeater_observer_mqtt`
  - `heltec_tracker_v2_repeater_observer_mqtt`
  - `heltec_tracker_v1_1_room_server_observer_mqtt`
  - `heltec_tracker_v2_room_server_observer_mqtt`

Observed static usage:

- Repeater: 76,960 bytes RAM (23.5%), approximately 47.4% flash.
- Room server: 80,656 bytes RAM (24.6%), approximately 47.3% flash.

Successful compilation does not validate the AP/STA security boundary, async teardown, concurrency, reconnection, or partial-save behavior; those require the targeted host/hardware tests above.

## Suggested Implementation Order

1. Fix forced-AP LAN exposure and define secure provisioning authentication.
2. Introduce request IDs for save/result correlation.
3. Gate reboot on aggregate success and represent partial application explicitly.
4. Correct effective empty-value handling in the wizard.
5. Align Wi-Fi, MQTT flags, and custom-slot runtime behavior with the UI.
6. Remove NTP blocking and marshal config/status snapshots to their owning tasks.
7. Fix server lifecycle/global-header accumulation.
8. Add input-length/backend validation and polling safeguards.
9. Fix Tracker v1.1 identity and add operator documentation.
10. Add automated tests, then run the full hardware matrix.

# Repeater health history

The simple repeater keeps health samples in RAM and answers mesh request
`REQ_TYPE_GET_AVG_MIN_MAX` (`0x04`). A logged-in client needs READ_ONLY access or
higher. This is pull-only: recording history sends no packets. Existing GET_STATUS
and telemetry responses are unchanged.

## Series

| Channel | LPP type | Value recorded each interval |
|---------|----------|------------------------------|
| 200 | `LPP_VOLTAGE` (116) | Battery voltage in V, averaged from periodic reads every 15 seconds |
| 201 | `LPP_ANALOG_INPUT` (2, signed) | Radio noise floor in dBm at the end of the interval |
| 202 | `LPP_PERCENTAGE` (120) | TX airtime percentage over the elapsed interval |
| 203 | `LPP_PERCENTAGE` (120) | RX airtime percentage over the elapsed interval |
| 204 | `LPP_TEMPERATURE` (103) | MCU temperature in degrees C, when supported |

These channels are dedicated to history. Existing self telemetry and GPS use channel 1;
`EnvironmentSensorManager` assigns at most 16 active entries to channels 2–17. Channels
200–204 therefore do not collide with current repeater telemetry. Custom sensor managers
should leave this range free. History channels do not change the channels in `0x03`.

Battery averaging reduces the effect of short voltage dips during transmission. Noise
floor and temperature are point samples. Recording advances on fixed interval boundaries
without accumulating loop delays; the airtime denominator uses a separate actual
measurement timestamp. TX/RX values are unsigned counter deltas in
milliseconds divided by the actual elapsed interval, multiplied by 100. Percentages
use the sensor protocol's integer LPP encoding, so fractional percentages are truncated.
NaN temperatures and the radio driver's uncalibrated noise-floor sentinel (0 dBm)
are skipped. At most five series are returned, below the eight-series reply limit.

## Query and retention

Use the [sensor-compatible `0x04` request](payloads.md#get-minmaxave-sensor-and-repeater-nodes):
start and end are seconds ago, followed by two zero reserved bytes. For example,
`start=3600, end=0` selects samples from the last hour, including age zero and excluding
age 3600. The reply echoes the request timestamp, adds the current RTC time, and then
encodes each series as channel, LPP type, min, max, average. There is no series count.
Empty series are omitted; an empty range returns just the eight-byte header.

Sampling and queries use monotonic uptime, accumulated across `millis()` wraps. The
RTC is used only for the response timestamp. Samples occupy a fixed interval grid
anchored at the first valid sample. Missed intervals are empty (NaN) slots; a gap
at least as long as the ring capacity expires the old ring. Late readings are assigned
to their interval boundary, so their reported age can differ by less than one interval.
Only one timestamp is stored per series.
Unsigned second differences handle seconds-counter wrap; retained samples and query
ranges must span less than one full 32-bit seconds cycle (about 136 years).

The first sample is recorded after one interval. Each series retains the configured
number of interval slots, including missing intervals. History is lost on reboot and
is not written to flash.

## Build flags

| Flag | Default | Description |
|------|---------|-------------|
| `REPEATER_HISTORY_INTERVAL_SECS` | `300` | Positive recording interval in seconds |
| `REPEATER_HISTORY_SLOTS` | `288`, or `0` on STM32/RP2040 | Interval slots per series; nominally 24 hours at five-minute intervals when enabled |

Set these using PlatformIO `build_flags`, for example `-D REPEATER_HISTORY_SLOTS=144`
for nominally 12 hours. `-D REPEATER_HISTORY_SLOTS=0` removes history members,
allocations, sampling, and handling of `0x04` entirely. STM32 and RP2040 builds default
to disabled history to conserve RAM; they can explicitly opt in with a smaller slot count.
There are no history CLI settings.

For both nRF52 and ESP32, sample storage is `5 * slots * 4` bytes (one float per
interval), or 5,760 bytes at the defaults. Comparing enabled and disabled builds shows
168 additional bytes in the enclosing mesh object on both platforms (including padding),
for a total of 5,928 bytes plus allocator overhead for five arrays. Unsupported temperature still reserves
its series storage. Query results use temporary stack space, not retained history RAM.

## Custom sensor sketch compatibility

The old `examples/simple_sensor/TimeSeriesData.h` include forwards to the shared helper.
Deprecated `RTCClock*` overloads remain available for existing custom sketches. They
ignore the RTC and use a shared, wrap-safe `millis()` accumulator; callers should migrate
to explicit monotonic seconds. Call the legacy API regularly enough to observe each
`millis()` wrap (less than about 49 days between calls), and use one clock API consistently
for each series. The shared legacy clock is linked only when these overloads are used.

The integer literal `0` is ambiguous between the seconds and pointer overloads. Use a
`uint32_t` variable (or `uint32_t(0)`) for uptime zero, and `nullptr` for a legacy null
clock argument.

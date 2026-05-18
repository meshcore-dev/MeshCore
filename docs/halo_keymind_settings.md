# Halo and Keymind Branch Settings

This file covers only CLI settings added by the Halo or Keymind branches. Use
`docs/cli_commands.md` for the general MeshCore CLI.

## Quick Start


```text
set retry.preset rooftop
set direct.retry.heard on
set flood.retry.advert off
set flood.retry.bridge off
set flood.retry.prefixes none
set flood.retry.ignore none
```

Then verify:

```text
get retry.preset
get direct.retry.heard
get flood.retry.advert
get flood.retry.prefixes
get flood.retry.ignore
```

Use prefixes from the analyzer or neighbors list or `get recent.repeater` after the repeater has been online for a few hours.  

## Common Examples

Disable retrying advert packets:

```text
set flood.retry.advert off
get flood.retry.advert
```

Ignore a repeater as a successful flood retry echo:  
Use this if you have a car repeater and a house repeater; have the house ignore the car.  

```text
set flood.retry.ignore 71CE82,C7618C
get flood.retry.ignore
```

Only accept specific downstream relays as flood retry success:  
You're in a hole and need to hit a mountain top repeater to get out; keep trying till one you see one of these send out your packet.  

```text
set flood.retry.prefixes A58296,860CCA,425E5C
get flood.retry.prefixes
```

Bridge two groups of repeaters:  

```text
set flood.retry.bridge on
set flood.retry.bucket 1 71CE82,C7618C
set flood.retry.bucket 2 BEEBB0,425E5C
get flood.retry.bucket.1
get flood.retry.bucket.2
```

Return to simple non-bridge flood retry:

```text
set flood.retry.bridge off
set flood.retry.prefixes none
set flood.retry.ignore none
```

## Added Settings

| Setting | What it does | How to use | Example |
| --- | --- | --- | --- |
| `recent.repeater` | Shows or seeds the recent repeater prefix/SNR table used by direct retry and bridge freshness checks. | `get recent.repeater`, `get recent.repeater <page>`, `set recent.repeater <prefix> <snr_db>` | `set recent.repeater A1B2C3 -8.5` |
| `radio.fem.rxgain` | Controls the external LoRa FEM receive-path LNA where the board supports it. | `get radio.fem.rxgain`, `set radio.fem.rxgain on/off` | `set radio.fem.rxgain on` |

## Recent Repeater Table

Direct retry uses the recent repeater table when `direct.retry.heard` is `on`.
Bridge buckets also use this table: a configured bucket prefix is active only
when it was heard within the last hour.

Show learned rows:

```text
get recent.repeater
get recent.repeater 2
```

Seed or correct a prefix:

```text
set recent.repeater A1B2C3 8.5
```

Rows are sorted by prefix width, then SNR. A full direct retry failure lowers
the matching row by `0.25 dB`.

## Direct Retry Settings

Direct retry applies to direct-routed packets. A queued resend is canceled when the next-hop echo is heard.

| Setting | What it does | How to use | Example |
| --- | --- | --- | --- |
| `retry.preset` | Applies shared direct and flood retry defaults. Values: `infra`, `rooftop`, `mobile` or `0`, `1`, `2`. | `get retry.preset`, `set retry.preset <value>` | `set retry.preset rooftop` |
| `direct.retry.heard` | Uses the recent repeater table as the direct retry eligibility gate. | `get direct.retry.heard`, `set direct.retry.heard on/off` | `set direct.retry.heard on` |
| `direct.retry.margin` | SNR margin in dB above the SF-specific receive floor. | `get direct.retry.margin`, `set direct.retry.margin <0-40>` | `set direct.retry.margin 5` |
| `direct.retry.count` | Maximum direct retry attempts after initial TX. | `get direct.retry.count`, `set direct.retry.count <1-15>` | `set direct.retry.count 15` |
| `direct.retry.base` | Base wait in milliseconds before retry. | `get direct.retry.base`, `set direct.retry.base <10-5000>` | `set direct.retry.base 175` |
| `direct.retry.step` | Milliseconds added per retry attempt. | `get direct.retry.step`, `set direct.retry.step <0-5000>` | `set direct.retry.step 100` |
| `direct.retry.cr` | Adaptive coding-rate thresholds for direct retry packets. Uses `CR4`, `CR5`, `CR7`, or `CR8`; `CR6` is never selected. | `get direct.retry.cr`, `set direct.retry.cr <cr4_min>,<cr5_min>,<cr7_min>,<cr8_max>`, `set direct.retry.cr off` | `set direct.retry.cr 10.0,7.5,2.5,0` |

The default adaptive coding-rate profile is `10.0,7.5,2.5,2.5`.
SNR `10.0 dB` and up uses `CR4`, `7.5 dB` and up uses `CR5`,
`2.5 dB` and down uses `CR8`, and the middle band uses `CR7`. If no
recent repeater table entry is available, retry packets use `CR5`. Use
`set direct.retry.cr off` to disable adaptive coding-rate overrides. If
adaptive selection chooses `CR4`, retries after the third attempt use
`CR5`.

Preset details:

| Preset | Base | Count | Step | SNR gate |
| --- | ---: | ---: | ---: | --- |
| `infra` | `275 ms` | `4` | `150 ms` | SF floor + `15 dB` |
| `rooftop` | `175 ms` | `15` | `100 ms` | SF floor + `5 dB` |
| `mobile` | `175 ms` | `15` | `50 ms` | SF floor |

Example for a quiet fixed repeater:

```text
set retry.preset rooftop
set direct.retry.heard on
set direct.retry.margin 5
```

Example for a moving or weak-link node:

```text
set retry.preset mobile
set direct.retry.margin 0
```

## Flood And Advert Settings

Flood retry applies to flood-routed packets. A queued retry is canceled when a qualifying downstream echo is heard.

| Setting | What it does | How to use | Example |
| --- | --- | --- | --- |
| `flood.retry.count` | Maximum flood retry attempts after initial TX. `0` disables flood retry. | `get flood.retry.count`, `set flood.retry.count <0-15>` | `set flood.retry.count 7` |
| `flood.retry.path` | Maximum path hash count eligible for flood retry, or `off` to disable the gate. | `get flood.retry.path`, `set flood.retry.path <0-63/off>` | `set flood.retry.path 1` |
| `flood.retry.advert` | Allows or blocks retry for node advert packets (`type=4`). Default is `off`. | `get flood.retry.advert`, `set flood.retry.advert on/off` | `set flood.retry.advert off` |
| `flood.retry.prefixes` | Target prefixes. If set, only matching downstream echoes cancel a retry. | `get flood.retry.prefixes`, `set flood.retry.prefixes <prefixes/none/off>` | `set flood.retry.prefixes BEEBB0,425E5C` |
| `flood.retry.ignore` | Ignored prefixes. In non-bridge retry, ignored last-hop echoes do not cancel retry. | `get flood.retry.ignore`, `set flood.retry.ignore <prefixes/none/off>` | `set flood.retry.ignore 71CE82,C7618C` |
| `flood.retry.bridge` | Enables bucket-based bridge retry logic. | `get flood.retry.bridge`, `set flood.retry.bridge on/off` | `set flood.retry.bridge on` |
| `flood.retry.bucket.<n>` | Shows one bridge bucket. Buckets are numbered `1`-`6`. | `get flood.retry.bucket.<n>` | `get flood.retry.bucket.1` |
| `flood.retry.bucket` | Sets bridge bucket prefixes. | `set flood.retry.bucket <1-6> <prefixes/none/off>` | `set flood.retry.bucket 1 71CE82,C7618C` |

The shared retry preset sets these flood defaults:

| Preset | Retry count | Path gate |
| --- | ---: | ---: |
| `infra` | `1` | `1` |
| `rooftop` | `3` | `2` |
| `mobile` | `3` | `1` |

Example for path-gated retry:

```text
set retry.preset rooftop
set flood.retry.path 1
set flood.retry.advert off
set flood.retry.ignore 71CE82,C7618C
```

## North South Buckets

Buckets describe groups of repeaters on different sides of this relay. Bucket
numbers do not have built-in meanings; this example uses bucket `1` for North
and bucket `2` for South.

```text
              North bucket 1
        +-----------------------+
        | A1B2C3       D4E5F6   |
        | North A      North B  |
        +-----------+-----------+
                    |
                    v
              +-----------+
              | This node |
              +-----------+
                    ^
                    |
        +-----------+-----------+
        | 71CE82       C7618C   |
        | South A      South B  |
        +-----------------------+
              South bucket 2
```

Configure the buckets:

```text
set flood.retry.bridge on
set flood.retry.bucket 1 A1B2C3,D4E5F6
set flood.retry.bucket 2 71CE82,C7618C
set flood.retry.ignore none
```

Packet heard from the North:

```text
     heard source
         |
         v
  +--------------+        retry targets
  | North bucket | -----> South bucket
  | bucket 1     | -----> Other fresh/unbucketed relays
  +--------------+
```

Packet heard from the South:

```text
     heard source
         |
         v
  +--------------+        retry targets
  | South bucket | -----> North bucket
  | bucket 2     | -----> Other fresh/unbucketed relays
  +--------------+
```

Packet heard from an unbucketed or pathless source:

```text
     heard source
         |
         v
  +--------------+        retry targets
  | Other bucket | -----> North bucket
  | implicit     | -----> South bucket
  +--------------+
```

Bridge retry stays eligible until every target bucket has been heard or
`flood.retry.count` is exhausted. A configured bucket is a target only when at
least one of its prefixes is fresh in `recent.repeater`. Prefixes in
`flood.retry.ignore` never count as bucket hits.

## Troubleshooting

If advert packets are still retrying:

```text
get flood.retry.advert
set flood.retry.advert off
```

If ignored prefixes still appear in `flood retry good` logs:

```text
get flood.retry.ignore
set flood.retry.ignore <prefix>
```

The ignored prefix must match the last hop shown as `heard=<prefix>`. For example, this log needs `C7618C` in the ignore list:

```text
flood retry good (... path=7773D0>C7618C, heard=C7618C ...)
```

If retries are too aggressive:

```text
set flood.retry.count 1
set flood.retry.path 1
set direct.retry.count 4
```

If retries are too sparse:

```text
set flood.retry.count 7
set flood.retry.path 2
```

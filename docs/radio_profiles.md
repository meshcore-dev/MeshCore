# Radio profiles

Companion, repeater, room server and sensor firmware can alternate two LoRa profiles on one transceiver. They share airtime, packet storage, identity and duplicate suppression. Other backends retain their existing primary commands.

Frequencies are MHz; bandwidths are kHz. The driver validates each tuple against the fitted chip. An optional final preamble field accepts `auto`/`0`, or 8â€“65528 symbols within the radio's airtime limits.

| Command | Action |
| --- | --- |
| `get radio` | Read saved primary settings. |
| `set radio freq,bw,sf,cr[,preamble]` | Save primary settings; reboot to apply. |
| `get radio2` | Read saved secondary settings. |
| `set radio2 freq,bw,sf,cr,mode[,preamble]` | Save and enable the secondary. |
| `set radio2 off` | Disable it and cancel its temporary activity and schedules. |
| `set tempradio freq,bw,sf,cr,minutes[,preamble]` | Temporarily replace the primary. |
| `set tempradio2 freq,bw,sf,cr,mode,minutes[,preamble]` | Temporarily replace the secondary. |
| `get tempradio` / `get tempradio2` | Read pending or active temporary settings. |
| `set tempradio off` / `set tempradio2 off` | Restore saved settings and cancel that temporary schedule. |
| `get radio2.status` / `get radio2.timing` | Read switching counters, pending work and effective timing. |
| `get radio2.cross` / `set radio2.cross auto\|on\|off` | Read or save crossover policy. |

Modes are `rx`, `rxtx` and `off`; `rx&tx` aliases `rxtx`. Bare `tempradio ...` remains supported. Temporary durations are 1â€“10080 minutes.

Crossover defaults to `off`. `cross auto` permits crossover when both profiles are permanent or both are temporary. `on` always permits it; `off` keeps traffic on its originating profile. An RX-only profile never transmits. Replies follow receive affinity and crossover policy; queued packets from replaced sessions are discarded.

```text
set radio2 915,125,8,5,rxtx
set tempradio2 916,250,7,5,rx,10,40
set tempradio2 off
```

## Scheduled changes

Each command family has **one RAM-only slot**. Setting it again replaces its unstarted event.

| Command | Action |
| --- | --- |
| `set radioat freq,bw,sf,cr,start[,preamble]` | Apply and save primary settings. |
| `set tempradioat freq,bw,sf,cr,start,end[,preamble]` | Temporarily replace the primary. |
| `set radio2at freq,bw,sf,cr,mode,start[,preamble]` | Apply and save secondary settings. |
| `set tempradio2at freq,bw,sf,cr,mode,start,end[,preamble]` | Temporarily replace the secondary. |

Times are UTC Unix seconds or `+N` whole minutes from acceptance. A valid clock is required. Endpoints must be in the future, within 24.8 days, with `end` after `start`.

```text
set radioat 915,125,8,5,+5
set tempradio2at 916,125,8,5,rx,+10,+20,40
get tempradio2at
del tempradio2at
```

Use `get family` and `del family` to inspect or cancel; optional selectors `1` and `all` are accepted. Cancel an active temporary session before scheduling another on that profile. `radioat2` and `tempradioat2` are aliases.

Overdue events apply in start order; equal starts apply permanent settings first. Immediate setters cancel already-due unstarted events on that profile and retain future slots. Monotonic deadlines prevent clock rollback or delayed replies from extending temporary sessions.

## Switching and persistence

Live changes wait at least two seconds and for queued transmissions to drain. Remote changes also require a successful transmission of their exact CLI response; this confirms local transmission, not delivery. Failed responses cancel the change (`reply_failed=1` in status). Expiry retires the old session before new transmissions and restores the radio when safe.

SX1262 and LR1110 switches reuse acknowledged RX settings and keep the oscillator warm. TX, CAD, reset, sleep and errors revoke cached state. T-Deck display transfers release SPI between bounded strips and service the radio; its framebuffer uses 150 KiB of PSRAM, with direct rendering as the allocation-failure fallback. The qualified T-Deck SPI setting is 8.888888 MHz.

Scanning and pending timers keep the MCU awake. One transceiver cannot receive both profiles simultaneously; acquisition still depends on peer preambles, traffic and application delays.

Permanent settings use existing preferences JSON. Missing fields default to secondary off, crossover off and automatic timing. Temporary settings and schedules disappear on reboot. Scheduled permanent settings are saved only after application.

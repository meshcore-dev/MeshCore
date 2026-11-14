# MCOtxt compatibility delivery

The companion can decode MCOtxt v1 for an app that does not implement the codec.
An app with its own decoder continues to receive the original messages. There
are no CLI commands, persistent settings, or encoder. Companion compatibility
is selected separately for each app session. Local displays decode received
MCOtxt independently of the connected app's capabilities.

## Negotiation

An app declares `mctxt` as a complete, case-sensitive item in a `cap=` token of
the existing `CMD_APP_START` application name (after its eight-byte header):

```text
ExampleApp;cap=mctxt
ExampleApp;cap=other,mctxt,another
```

The `cap=` field is optional. When present, its value is split on commas and
each complete item is checked for `mctxt`; the parser does not require the literal
field `cap=mctxt`. An absent or empty list means that the app has not declared
MCOtxt decoding support. Empty items and unknown capabilities are ignored.
The name itself, `mctxt2`, and `MCTXT` do not match. The bounded parser reads the
entire supplied app-name field, without a separate truncated copy.

This port does not add framed-packet support (`FR01`/`frmfrg1`) or negotiate it.
Splitting decoded text into existing companion messages is independent of that
separate transport feature.

## Supported transports

| Received form | App declares `mctxt` | App does not declare `mctxt` |
|---|---|---|
| Text containing `mct:` + canonical Base91 | Original frame | Decoded text frame(s), replacing the encoded text |
| Channel GROUP_DATA type `0x0120`, subtype/version `0x31` | Original binary frame only | Original binary frame first, followed by decoded channel text frame(s) |
| Other text, CLI data, other binary types, malformed/unsupported containers | Original frame | Original frame |

Text containers can appear inside surrounding prose, including quotes and
parentheses. They must start at a word boundary, not in the middle of a Latin,
Cyrillic, or other supported-script word. The scanner consumes the longest
complete valid container, retaining surrounding text and punctuation; punctuation
cannot simply be stripped because it is also part of the Base91 alphabet.
Candidates must round-trip to the same canonical Base91 spelling. Counted
compressed strings must also have zero padding after their significant bits.
These checks prevent adjacent punctuation from changing the last decoded byte
while still appearing to be a complete container.

The decoder supports generation-0 EN/RU/FR/DE/IT/UK/BE models and the v1 stream
controls, including raw UTF-8. `generated/model_*.h` are protocol tables: changing
symbol order changes wire meaning. Do not edit them by hand.

## Queue and delivery lifecycle

`addToOfflineQueue()` is unchanged apart from resetting the delivery cursor when
overflow evicts the head. One queue entry holds the original **companion frame**
with its encoded MCOtxt payload, not derived text parts. This is the existing
received-message representation, not an encrypted radio packet or a new storage
format. The existing 176-byte frame admission/truncation rules are unchanged.

`MCOtxtDelivery::next()` runs only when the app requests
`CMD_SYNC_NEXT_MESSAGE`. It renders one response and reports whether the raw
queue head can be consumed. A binary source is returned unchanged on the first
request; subsequent requests render its text. Derived parts are never enqueued,
so one long message cannot occupy several queue slots. On each request the
decoder reuses the bounded shared workspace; no decoded copy is retained per
queue entry.

After the final part, the raw source is removed. An unsupported binary source
is removed on the request after its raw delivery, and that same request can
continue with the next queue entry. It must not produce an early
`RESP_CODE_NO_MORE_MESSAGES` while another entry remains.

`APP_START` resets the cursor and re-evaluates capabilities. If a previous app
disconnected partway through delivery, a new app sees the retained source in
its own representation, starting from the beginning. Parts already received
before the reconnect can consequently repeat. This is not an exactly-once
protocol, and the upstream queue remains RAM-only.

## Text formatting and metadata

One part has no numbering. Longer text is split within both the 176-byte
companion-frame budget and the 160-byte text budget, accounting for headers,
the sender prefix, leading/trailing continuation markers, and part number:

```text
Bob: @[Ann] first part of the reply... [1/3]
Bob: ...continuation... [2/3]
Bob: ...last part [3/3]
```

Every part except the last ends its text with `...` before the part number.
Every part except the first starts its text with `...`. A single unsplit message
has neither added ellipses nor numbering.

The reply author comes from the container's reply field, not from heuristics on
the text. The mention is inserted once before splitting, so it stays in the
first part; an identical existing leading mention is not added twice. A reply
inside quoted prose is also promoted to the beginning of the first part. If
several embedded containers have distinct reply anchors, the source is left
raw rather than merging incompatible reply relationships.

Sender and reply-author names use fixed 32-byte arrays in `DecodedMessage`,
matching the current `NodePrefs::node_name` and `ContactInfo::name` fields.
`kMaxNameBytes` is 31 bytes excluding NUL; raise it if the firmware's name limit
changes. Longer names are clipped to the longest complete UTF-8 prefix that
fits. The decoder validates the entire field before clipping; compressed names
are temporarily decoded in the shared workspace, then copied to the fixed
metadata field. A name that cannot be fully decoded within that workspace falls
back to raw. Metadata owns its names and remains valid when text storage is
reused. The sender is repeated on each numbered binary-text part.

Reply mentions and conflict checks use these bounded names. If a complete
bounded mention cannot fit in the first part together with the sender and part
marks, the original is preserved. Text is never clipped to hide decode overflow.

For channel text, the original outer `sender: ` prefix is retained. Direct and
signed-room messages keep their contact/author prefixes without adding a sender
name to their bodies. Binary compatibility text uses the container's sender,
falling back to the envelope's name or `?` if neither exists.

All text parts preserve the source channel/contact and packed path metadata.
Text transports retain the original packet timestamp. Binary transports use
their container timestamp; an inherited timestamp is captured once when binary
delivery begins and reused for all parts. Generated messages use the current
app's legacy or V3 layout; capable apps retain the original bytes/layout.

Splits use UTF-8 character boundaries, preferring a nearby space or newline.
Whitespace at a selected split boundary is omitted. The splitter supports up
to 20 parts; the shared decode scratch defaults to 1280 bytes. No heap
allocation or filesystem write is needed. The shared scratch assumes the
existing single companion loop; concurrent callers would need separate buffers
or synchronization.

Insufficient workspace, malformed UTF-8, embedded NUL that cannot be represented
in companion C-string text, and unknown compressed formats fall back to the raw
source. A truncated decode is never published as a successful complete message.

## Local display previews

After the unchanged raw frame is queued, `MyMesh` adapts the text passed to its
synchronous UI listener. Base91 MCOtxt, including containers embedded in prose,
is decoded for channel, direct, and signed-room text. CLI data is passed through.
Binary MCOtxt is still delivered to the binary listener unchanged; a valid
container additionally produces one ordinary channel-text preview with its
sender and optional reply mention. Unsupported/malformed binary creates no text
preview. This does not add text frames to the companion queue.

The existing `ui-orig` and `ui-new` message previews therefore show decoded text
even when the connected app declares `mctxt`. Their existing buffer sizes are
retained: a long message shows its initial preview, clipped on a UTF-8 character
boundary. BLE part numbers and continuation markers are not added to the local
preview. Unsupported glyphs retain the existing display/font behavior.
`ui-tiny` retains its status/count-only behavior, as it has no ordinary message
preview to adapt.

Decoded callback text borrows the shared scratch buffer. Listeners must copy it
before returning; the existing UI implementations do so. Queue-count listeners
are also updated when an exhausted binary source is removed without a text
response, so an empty queue clears the unread display state.

## Code map and tests

- `examples/companion_radio/MCOtxtDelivery.*`: session capability, one-entry cursor,
  companion layouts, binary-first policy, generated text frames.
- `src/helpers/mcotxt/`: pure C++ stream decoder, protocol models, container
  parser, embedded-text scanner, reply adaptation, UTF-8-safe part planner.
- `src/helpers/mcotxt/MCOtxtDisplay.*`: local text/binary preview adaptation and
  UTF-8-safe copying, independent of companion session capabilities.
- `src/helpers/Base91.*`: canonical basE91, based on Joachim Henke's public-domain
  algorithm.
- `src/helpers/MessageLimits.h`: shared `MAX_FRAME_SIZE` and `MAX_TEXT_LEN`
  definitions, used by `BaseSerialInterface`, `BaseChatMesh`, and `MCOtxtDelivery`.
  Delivery buffers, inline-token/Base91 buffers, and text budgets follow these
  limits without numeric copies.
- `MyMesh.*`: app-start/offline-head lifecycle hooks and display-listener
  adaptation after raw enqueueing. `Frame` and radio protocol layouts are unchanged.
- `platformio.ini` and `build_as_lib.py`: include the pure decoder sources for
  the normal project build, native tests, and `BUILD_EXAMPLE` library builds.

Run the focused native suites with the project's normal PlatformIO setup:

```sh
pio test -e native -f test_mcotxt -f test_mcotxt_delivery
```

Queue lifecycle checks use the actual `MyMesh.cpp` enqueue/drain/SYNC bodies
with small native hardware doubles. Run them with an already installed compiler
and Google Test sources (a developer shell is required for MSVC):

```sh
python tools/test_mcotxt_queue.py --gtest /path/to/googletest --asan
```

The unit-test CI workflow also runs `python tools/test_mcotxt_queue.py --asan`
after the PlatformIO tests. It reuses Google Test from
`.pio/libdeps/native/googletest`; a build error, test failure, or AddressSanitizer
error fails the CI step.

The runner never installs dependencies. It covers overflow/eviction of a partial
head, eviction behind a direct-message head, an all-direct full queue, skipping
an exhausted malformed binary, listener count updates, and a new app taking the
retained raw head. This is separate from compiling all of `MyMesh` for a board.
Polling an already-empty queue is also checked: it must not reset the UI screen.

The decoder suite covers models, bit counts, controls, strict parsing, and text
splitting. Delivery tests cover capability parsing, both companion layouts,
signed-room prefixes, inline containers, reply placement, raw preservation,
multipart/reconnect/eviction cursor handling, malformed/future formats, UTF-8,
bounded mutation/random-input cases, and local display adaptation/copying.
Name regressions cover the 31-byte boundary, clipping raw/compressed/UTF-8 names,
validation beyond the clipped prefix, multiple inline replies, metadata ownership,
full-frame bounds, and preservation of whole bounded reply mentions. Punctuation
regressions cover adjacent Base91 punctuation across token endings, canonical
encoding, and counted-stream padding, including the app and display paths.
`golden.h` contains fixed counted
streams produced by the independent Dart `MCOtxtFrame` encoder with RU/EN.

The decoder, transport, part planner, and unchanged model tables were ported from
south-edition commit `62cf56c694aed48841a7a37e75304f0c98bfa587`, removing fork-only
feature guards. The companion adapter and focused MCOtxt-only scanner are kept
separate from South's UI, CLI, other codecs, and persistent-queue extensions.
Before release, also check the chosen board's complete firmware build, available
flash/RAM/stack, and real BLE/USB queue draining with both kinds of app. Native
tests and standalone ARM compilation do not establish those hardware properties.

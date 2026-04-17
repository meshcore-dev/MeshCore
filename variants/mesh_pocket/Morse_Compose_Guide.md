# Morse Compose — Meshpocket User Guide

Morse Compose lets you type and send messages on any configured channel using the Meshpocket's single button. No keyboard needed — just press and release in the rhythm of Morse code.

## Getting In and Out

**Enter**: Double-click the button from the home screen. A channel picker appears — click to cycle between channels, then double-click to select. The Morse compose screen opens on the chosen channel.

**Exit**: Hold the button for **9 seconds**, then release. The display shows `[EXIT]` when the threshold is reached. You can also exit by long-pressing from the channel picker.

## How Pressing Works

Every button press is either a **dot** or a **dash**, determined by how long you hold:

| Press duration | Result |
|---|---|
| Under 500 ms | Dot (·) |
| 500 ms – 3 s | Dash (—) |

A quick tap (under half a second) is a dot. A deliberate half-second hold is a dash. The threshold is generous to avoid accidental dashes.

## How Letters Form

You don't need to press "confirm" after each letter — the screen detects letter boundaries automatically from silence gaps:

| Gap (after last release) | What happens |
|---|---|
| 1 second | Current dots/dashes are decoded into a letter |
| 3.5 seconds | A space is inserted between words |

So the flow is: press dot/dash patterns → pause for about 1 second → letter appears → continue with the next letter. Pause for 3.5 seconds and a space is added.

### Example: Sending "hi there"

1. Press: · · · · (four quick taps) → pause 1 second → **h** appears
2. Press: · · (two quick taps) → pause 1 second → **i** appears
3. **Wait ~3.5 seconds** → space inserted automatically → buffer shows "hi "
4. Press: — (one half-second press) → pause → **t** appears
5. Press: · · · · → pause → **h** appears
6. Press: · → pause → **e** appears
7. Press: · — · → pause → **r** appears
8. Press: · → pause → **e** appears
9. Buffer now shows "hi there"
10. Hold for ~8 seconds → display shows `[SEND]` → release → message sent!

## Sending, Correcting, and Exiting

There are two ways to send, backspace, and exit:

### Method 1: Hold durations (easier)

Just hold the button and release at the right moment. The display shows which action is armed:

| Hold duration | Display shows | What happens on release |
|---|---|---|
| 3 – 7 s | `[BKSP]` | **Backspace** — deletes the last character |
| 7 – 9 s | `[SEND]` | **Send** — sends the message on the selected channel |
| 9 s+ | `[EXIT]` | **Exit** — returns to the home screen |

### Method 2: Prosigns (advanced)

Two special Morse patterns also work as alternatives:

| Prosign | Pattern | What it does |
|---|---|---|
| **WW** | · — — · — — | **Sends** the message (two W's without a letter gap) |
| **HH** | · · · · · · · · | **Backspace** (8 rapid dots within the 1-second letter gap) |

## Morse Code Reference

### Letters

| Letter | Code | | Letter | Code |
|---|---|---|---|---|
| A | · — | | N | — · |
| B | — · · · | | O | — — — |
| C | — · — · | | P | · — — · |
| D | — · · | | Q | — — · — |
| E | · | | R | · — · |
| F | · · — · | | S | · · · |
| G | — — · | | T | — |
| H | · · · · | | U | · · — |
| I | · · | | V | · · · — |
| J | · — — — | | W | · — — |
| K | — · — | | X | — · · — |
| L | · — · · | | Y | — · — — |
| M | — — | | Z | — — · · |

### Numbers

| Number | Code | | Number | Code |
|---|---|---|---|---|
| 0 | — — — — — | | 5 | · · · · · |
| 1 | · — — — — | | 6 | — · · · · |
| 2 | · · — — — | | 7 | — — · · · |
| 3 | · · · — — | | 8 | — — — · · |
| 4 | · · · · — | | 9 | — — — — · |

### Punctuation

| Character | Code |
|---|---|
| . (full stop) | · — · — · — |
| , (comma) | — — · · — — |
| ? (question mark) | · · — — · · |

## Screen Layout

The Morse screen shows four sections:

- **Header**: "MORSE > channelname" showing which channel you're composing on. When a hold action is armed, `[BKSP]`, `[SEND]`, or `[EXIT]` appears on the right.
- **IN**: The last 2 incoming messages on the selected channel only (messages from other channels are filtered out).
- **OUT**: Your composed message so far, with a cursor.
- **KEY**: Shows "ready" during normal use. During a hold, shows the armed action.

Sent messages also appear in the MeshCore companion app's channel history if BLE is connected.

## Tips

- **Spaces are automatic** — just pause for about 3.5 seconds after finishing a word and a space appears
- Full stop is its own Morse character (· — · — · —) — a space is automatically inserted after it via the same word-gap pause
- All output is **lowercase**
- The maximum message length is 133 characters
- If you enter a wrong dot/dash pattern, it won't match any character and will be silently dropped — just start the letter again after the gap
- A dot is a quick tap (under half a second), a dash is a deliberate hold (half a second or longer)
- The display doesn't update during active keying to avoid blocking button presses — your letters appear when the 1-second letter gap commits them
- M (— —) requires **two separate presses**, each held for about half a second, with a brief release between them
- **Hold durations are the easiest way to send, backspace, and exit** — just hold and watch the display for `[BKSP]`, `[SEND]`, or `[EXIT]`, then release
- The WW prosign and HH prosign still work as alternatives for advanced users
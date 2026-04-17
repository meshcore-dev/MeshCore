# Morse Compose — Meshpocket User Guide

Morse Compose lets you type and send messages on the Public channel using the Meshpocket's single button. No keyboard needed — just press and release in the rhythm of Morse code.

## Getting In and Out

**Enter**: Double-click the button from the home screen.

**Exit**: Hold the button for **9 seconds**, then release. The display shows `[EXIT]` when the threshold is reached.

## How Pressing Works

Every button press is either a **dot** or a **dash**, determined by how long you hold:

| Press duration | Result |
|---|---|
| Under 500 ms | Dot (·) |
| 500 ms or longer | Dash (—) |

A quick tap (under half a second) is a dot. A deliberate half-second hold is a dash. The threshold is generous to avoid accidental dashes.

## How Letters Form

You don't need to press "confirm" after each letter — the screen detects letter boundaries automatically from silence gaps:

| Gap (after last release) | What happens |
|---|---|
| 1 second | Current dots/dashes are decoded into a letter |
| 3.5 seconds | A space is inserted between words |

So the flow is: press dot/dash patterns → pause for about 1 second → letter appears → continue with the next letter. Pause for 3.5 seconds and a space is added.

### Example: Sending "HI THERE"

1. Press: · · · · (four quick taps) → pause 1 second → **H** appears
2. Press: · · (two quick taps) → pause 1 second → **I** appears
3. **Wait ~3.5 seconds** → space inserted automatically → buffer shows "HI "
4. Press: — (one half-second press) → pause → **T** appears
5. Press: · · · · → pause → **H** appears
6. Press: · → pause → **E** appears
7. Press: · — · → pause → **R** appears
8. Press: · → pause → **E** appears
9. Buffer now shows "HI THERE"
10. Hold for ~8 seconds → display shows `[SEND]` → release → message sent!

## Sending, Correcting, and Exiting

There are two ways to send, backspace, and exit:

### Method 1: Hold durations (easier)

Just hold the button and release at the right moment. The display shows which action is armed:

| Hold duration | Display shows | What happens on release |
|---|---|---|
| 3 – 7 s | `[BKSP]` | **Backspace** — deletes the last character |
| 7 – 9 s | `[SEND]` | **Send** — sends the message on the Public channel |
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

- **Header**: "MORSE" on the left, exit hint on the right
- **IN**: The last 2 incoming messages
- **OUT**: Your composed message so far, with a cursor
- **KEY**: The dots and dashes you've entered for the current letter (before it decodes), plus a character count

A hint at the bottom shows `Hold 3s=bksp 7s=send 9s=exit` as a reminder.

## Tips

- **Spaces are automatic** — just pause for about 3.5 seconds after finishing a word and a space appears
- Full stop is its own Morse character (· — · — · —) — a space is automatically inserted after it via the same word-gap pause
- All output is **uppercase** — Morse code doesn't distinguish case
- The maximum message length is 133 characters
- If you enter a wrong dot/dash pattern, it won't match any character and will be silently dropped — just start the letter again after the gap
- A dot is a quick tap (under half a second), a dash is a deliberate hold (half a second or longer)
- The display doesn't update during active keying to avoid blocking button presses — your letters appear when the 1-second letter gap commits them
- M (— —) requires **two separate presses**, each held for about half a second, with a brief release between them
- **Hold durations are the easiest way to send, backspace, and exit** — just hold and watch the display for `[BKSP]`, `[SEND]`, or `[EXIT]`, then release
- The WW prosign and HH prosign still work as alternatives for advanced users
# Morse Compose — Meshpocket User Guide

Morse Compose lets you type and send messages on the Public channel using the Meshpocket's single button. No keyboard needed — just press and release in the rhythm of Morse code.

## Getting In and Out

**Enter**: Triple-click the button from the home screen.

**Exit**: Hold the button for **5 seconds**, then release. The screen will show "Release=exit" when the hold threshold is reached.

## How Pressing Works

Every button press is either a **dot** or a **dash**, determined by how long you hold:

| Press duration | Result |
|---|---|
| Under 240 ms | Dot (·) |
| 240 ms or longer | Dash (—) |

The timing is based on 10 words per minute, where one dot unit = 120 ms. A dash threshold is 2× the dot unit.

## How Letters Form

You don't need to press "confirm" after each letter — the screen detects letter boundaries automatically from silence gaps:

| Gap (after last release) | What happens |
|---|---|
| 360 ms (3 dot units) | Current dots/dashes are decoded into a letter |
| 840 ms (7 dot units) | A space is inserted between words |

So the flow is: press dot/dash patterns → pause briefly → letter appears → continue with the next letter. Pause a bit longer and a space is added.

### Example: Sending "HI THERE"

1. Press: · · · · (four quick taps) → pause 360ms → **H** appears
2. Press: · · (two quick taps) → pause 360ms → **I** appears
3. **Wait ~1 second** (840ms) → space inserted automatically → buffer shows "HI "
4. Press: — (one long press) → pause → **T** appears
5. Press: · · · · → pause → **H** appears
6. Press: · → pause → **E** appears
7. Press: · — · → pause → **R** appears
8. Press: · → pause → **E** appears
9. Buffer now shows "HI THERE"
10. Press: · — — · — — (WW prosign) → message sent!

## Sending and Correcting

Two special Morse patterns (called prosigns) control the message:

| Prosign | Pattern | What it does |
|---|---|---|
| **WW** | · — — · — — | **Sends** the message on the Public channel and clears the buffer |
| **HH** | · · · · · · · · | **Backspace** — deletes the last character |

WW is 6 elements (dot dash dash dot dash dash — W twice without pausing). This pattern was chosen over the traditional AR prosign because "AR" appears frequently in English words, making accidental sends too easy.

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
- **IN**: The last 2 incoming messages from any channel
- **OUT**: Your composed message so far, with a cursor
- **KEY**: The dots and dashes you've entered for the current letter (before it decodes), plus a character count

## Tips

- **Spaces are automatic** — just pause for about 1 second (840 ms) after finishing a word and a space appears. You don't need to enter any special pattern for spaces
- Full stop is its own Morse character (· — · — · —) — a space is automatically inserted after it via the same word-gap pause
- All output is **uppercase** — Morse code doesn't distinguish case
- The maximum message length is 133 characters
- If you make a mistake mid-letter (wrong dot/dash pattern), it won't match anything and will be silently dropped — just start the letter again after the gap
- Practice the rhythm: a dash should feel about 3× longer than a dot
- The WW prosign (send) is just W (· — —) typed twice without pausing: · — — · — —
- HH (backspace) is just 8 rapid dots — hard to confuse with anything else
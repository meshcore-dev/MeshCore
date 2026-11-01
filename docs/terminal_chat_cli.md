# Terminal Chat CLI

Below are the commands you can enter into the Terminal Chat clients:

```
set freq {frequency}
```
Set the LoRa frequency. Example:  set freq 915.8

```
set tx {tx-power-dbm}
```
Sets LoRa transmit power in dBm.

```
set name {name}
```
Sets your advertisement name.

```
set lat {latitude}
```
Sets your advertisement map latitude. (decimal degrees)

```
set lon {longitude}
```
Sets your advertisement map longitude. (decimal degrees)

```
set af {air-time-factor}
```
Sets the transmit air-time-factor.

> **Note:** `set dutycycle` is *not* available in Terminal Chat. Every other
> firmware — Repeater, Room Server, Sensor and Companion Radio — routes its
> `set`/`get` commands through the shared radio prefs handler and does support
> it (see [CLI Commands](./cli_commands.md)). Terminal Chat is the sole
> exception: it has its own inline `set` handler covering only the six options
> listed above.


```
time {epoch-secs}
```
Set the device clock using UNIX epoch seconds. Example:  time 1738242833


```
advert
```
Sends an advertisement packet

```
clock
```
Displays current time per device's clock.


```
ver
```
Shows the device version and firmware build date.

```
card
```
Displays *your* 'business card', for others to manually _import_

```
import {card}
```
Imports the given card to your contacts.

```
list {n}
```
List all contacts by most recent. (optional {n}, is the last n by advertisement date)

```
to
```
Shows the name of current recipient contact. (for subsequent 'send' commands)

```
to {name-prefix}
```
Sets the recipient to the _first_ matching contact (in 'list') by the name prefix. (ie. you don't have to type whole name)

```
send {text}
```
Sends the text message (as DM) to current recipient.

```
reset path
```
Resets the path to current recipient, for new path discovery.

```
public {text}
```
Sends the text message to the built-in 'public' group channel

```
help
```
Lists the available commands.

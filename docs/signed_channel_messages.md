# Signed channel messages (proposal)

Status: proposal, open for review. Values marked "proposed" are placeholders
for the project to set.

## Problem

A plain channel message carries no key. The sender name is just a text prefix,
so anyone can post under any name and a receiving client cannot tell a genuine
message from an impersonation. This also makes name/text spam filtering a losing
game, because the only stable identifier left (the repeater a packet arrived
through) is shared with legitimate traffic.

## Approach

Let a sender sign the message with their own key, and carry the signature in a
separate companion packet rather than inside the text. The text message is
unchanged and full length; clients that don't understand the companion packet
ignore it, so nothing breaks for anyone not opted in. The cost is one extra
packet per signed message.

Signing uses the node's existing Ed25519 identity key (the same key adverts are
signed with), so no new key material and no private-key handling outside the
device.

## Companion packet

A GRP_DATA group-channel datagram on the same channel.

    data_type   0xFFFF   (proposed; a reserved value would be better for a standard)

Payload (79 bytes):

    offset  size  field
    0       3     magic   ASCII "MC1", identifies a signature packet, version 1
    3       8     keyid   first 8 bytes of the signer's Ed25519 public key
    11      4     ts      message timestamp, little-endian (correlates to the text)
    15      64    sig     Ed25519 signature over the canonical input below

## Canonical signed input

The bytes that are signed and verified:

    domain        ASCII domain tag (proposed: "meshcore channel sig v1")
    channel_hash  1 byte, sha256(channel_name)[0]
    ts            4 bytes, little-endian, the message timestamp
    text          the message text, UTF-8

Concatenated in that order. Signed with pure Ed25519 (no pre-hash), matching
advert signing. The channel_hash byte binds a signature to its channel so it
cannot be replayed onto another.

## Verifying

1. On a signature packet, read keyid, ts, sig.
2. Find the channel text message on the same channel with the same ts.
3. Resolve the signer's full public key from keyid (e.g. from a known advert).
4. Rebuild the canonical input from the received text, channel name and ts.
5. Ed25519-verify sig against the public key.

A valid signature proves only that the holder of that key signed that text on
that channel. It does not by itself prove the key belongs to the displayed name.

## Binding a name to a key

To turn a valid signature into anti-impersonation, a client pins the key it
first sees for a name (trust on first use) and warns when a later signed message
under that name presents a different key. Pinning policy is a client concern and
is not part of the wire format.

## Spam

Because a signed message carries a key, a client can offer a verified-only view
on a flooded channel, and can filter by key instead of by name. A key is not
cheap to rotate and is unique to its holder, so muting a key sticks and never
silences anyone else, unlike name, text or repeater-origin filters.

## Compatibility

Clients and firmware that do not implement this ignore an unknown group-channel
datagram and display the plain text as today. Signing is opt-in per message.

#!/usr/bin/env python3
"""Exercise default-reject and opt-in one-key private DMs.

This test changes the two radios' contact lists and transmits one LoRa DM.
It leaves the recipient's offline message queue untouched. Use two nearby
Companion USB radios on the same radio profile, with no other client attached.
"""

from __future__ import annotations

import argparse
import json
import struct
import time

import serial


class FrameTimeout(TimeoutError):
    pass


def encode_host_frame(payload: bytes) -> bytes:
    if not 1 <= len(payload) <= 176:
        raise ValueError("Companion frame length is outside 1..176")
    return b"<" + struct.pack("<H", len(payload)) + payload


class DeviceFrameReader:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def read_frame(self, port: serial.Serial, seconds: float) -> bytes:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            marker = self.buffer.find(b">")
            if marker < 0:
                self.buffer.clear()
            else:
                del self.buffer[:marker]
                if len(self.buffer) >= 3:
                    length = self.buffer[1] | self.buffer[2] << 8
                    if not 1 <= length <= 176:
                        del self.buffer[0]
                        continue
                    if len(self.buffer) >= 3 + length:
                        payload = bytes(self.buffer[3:3 + length])
                        del self.buffer[:3 + length]
                        return payload
            data = port.read(max(1, min(getattr(port, "in_waiting", 0), 512)))
            if data:
                self.buffer.extend(data)
        raise FrameTimeout("no complete Companion response frame")


def validate_app_start_response(frame: bytes) -> dict:
    if len(frame) < 58 or frame[0] != 5 or frame[1] != 1:
        raise RuntimeError("invalid Companion APP_START response")
    frequency_khz, bandwidth_hz = struct.unpack_from("<II", frame, 48)
    return {
        "frequency_khz": frequency_khz,
        "bandwidth_hz": bandwidth_hz,
        "spreading_factor": frame[56],
        "coding_rate": frame[57],
    }


class Link:
    def __init__(self, port: str) -> None:
        self.port = serial.Serial(port, 115200, timeout=0.1, write_timeout=2)
        self.reader = DeviceFrameReader()
        self.pushes: list[bytes] = []

    def close(self) -> None:
        self.port.close()

    def read(self, seconds: float) -> bytes | None:
        try:
            frame = self.reader.read_frame(self.port, seconds)
        except FrameTimeout:
            return None
        if frame[0] >= 0x80:
            self.pushes.append(frame)
        return frame

    def request(self, payload: bytes, expected: tuple[int, ...], seconds: float = 5) -> bytes:
        self.port.write(encode_host_frame(payload))
        self.port.flush()
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            frame = self.read(min(0.5, until - time.monotonic()))
            if frame is None or frame[0] >= 0x80:
                continue
            if frame[0] not in expected:
                raise RuntimeError(
                    f"unexpected Companion response {frame[0]}"
                    + (f", error {frame[1]}" if frame[0] == 1 and len(frame) > 1 else "")
                )
            return frame
        raise TimeoutError(f"Companion request {payload[0]} timed out")

    def start(self) -> tuple[bytes, dict]:
        # Some USB Companions need a fresh CDC open after DFU activation.
        for attempt in range(3):
            try:
                frame = self.request(bytes([1]) + bytes(7) + b"OneKeyDM-HIL",
                                     (5,), seconds=5)
                break
            except TimeoutError:
                if attempt == 2:
                    raise
                self.port.close()
                time.sleep(1)
                self.port.open()
                self.reader = DeviceFrameReader()
        return frame[4:36], validate_app_start_response(frame)

    def contact(self, key: bytes) -> bytes | None:
        frame = self.request(bytes([30]) + key, (1, 3))
        return frame if frame[0] == 3 else None

    def cli(self, command: str) -> str:
        frame = self.request(bytes([66]) + command.encode(), (29,))
        return frame[1:].decode(errors="replace").strip()


def run(sender_port: str, recipient_port: str, reset_contact: bool,
        zero_hop: bool, invalid_signature_first: bool,
        auto_accept: bool = False) -> dict:
    sender = Link(sender_port)
    recipient = Link(recipient_port)
    original_setting = None
    try:
        sender_key, sender_info = sender.start()
        recipient_key, recipient_info = recipient.start()
        radio_fields = ("frequency_khz", "bandwidth_hz", "spreading_factor", "coding_rate")
        if any(sender_info[field] != recipient_info[field] for field in radio_fields):
            raise RuntimeError("radios do not use the same frequency, bandwidth, SF, and CR")

        original_setting = recipient.cli("get dm.one_key").removeprefix("> ")
        if original_setting not in ("on", "off"):
            raise RuntimeError(f"unknown one-key DM setting: {original_setting}")
        requested_setting = "on" if auto_accept else "off"
        if original_setting != requested_setting:
            reply = recipient.cli(f"set dm.one_key {requested_setting}")
            if f"dm.one_key is now {requested_setting}" not in reply:
                raise RuntimeError(f"could not select one-key DM policy: {reply}")

        if recipient.contact(sender_key) is not None:
            if not reset_contact:
                raise RuntimeError("recipient already knows sender; pass --reset-contact to remove it")
            recipient.request(bytes([15]) + sender_key, (0,))
        if recipient.contact(sender_key) is not None:
            raise RuntimeError("recipient still knows sender")

        contact = sender.contact(recipient_key)
        if contact is None:
            advert = recipient.request(bytes([17]), (11,))[1:]
            sender.request(bytes([18]) + advert, (0,))
            for _ in range(10):
                time.sleep(0.25)
                contact = sender.contact(recipient_key)
                if contact is not None:
                    break
        if contact is None:
            raise RuntimeError("sender could not import recipient's signed advert")

        if zero_hop:
            # Contact response and CMD_ADD_UPDATE_CONTACT share the same body.
            update = bytearray(contact)
            update[0] = 9
            update[35] = 0  # zero path hashes
            update[36:100] = bytes(64)
            sender.request(bytes(update), (0,))
            contact = sender.contact(recipient_key)
        if contact is None:
            raise RuntimeError("sender contact vanished")

        if invalid_signature_first:
            # CMD_SEND_ANON_REQ prepends the four-byte tag itself. This has a
            # valid ECDH envelope but an invalid Ed25519 identity signature.
            bogus = b"DMK1forged\0" + bytes(64)
            sender.request(bytes([0x39]) + recipient_key + bogus, (6,), seconds=20)
            time.sleep(2)
            if recipient.contact(sender_key) is not None:
                raise AssertionError("invalid introduction created a contact")
            if any(frame[0] == 0x8A and frame[1:33] == sender_key
                   for frame in recipient.pushes):
                raise AssertionError("invalid introduction created a pending advert")

        timestamp = int(time.time())
        text = f"one-key DM HIL {timestamp}".encode()
        message = bytes([2, 0, 0]) + struct.pack("<I", timestamp) + recipient_key[:6] + text
        sent = sender.request(message, (6,), seconds=20)
        expected_ack = sent[2:6]
        timeout_ms = struct.unpack("<I", sent[6:10])[0]
        # In the default-off case, observe the entire reported send window:
        # the ordinary DM is queued after the introduction and may arrive
        # later than the pending advert and refusal.
        until = time.monotonic() + (timeout_ms / 1000 + 5 if not auto_accept
                                    else min(timeout_ms / 1000 + 5, 120))
        while time.monotonic() < until:
            for link in (sender, recipient):
                link.read(0.1)
            if auto_accept:
                confirmed = any(frame[0] == 0x82 and frame[1:5] == expected_ack
                                for frame in sender.pushes)
                waiting = any(frame[0] == 0x83 for frame in recipient.pushes)
                if confirmed and waiting:
                    break

        advertised = next((frame for frame in recipient.pushes
                           if frame[0] == 0x8A and frame[1:33] == sender_key), None)
        rejected = any(frame[0] == 0x91 and frame[1:33] == recipient_key
                       for frame in sender.pushes)
        if not auto_accept:
            if advertised is None or not rejected:
                raise AssertionError("default-off policy did not advertise and refuse the introduction")
            if recipient.contact(sender_key) is not None:
                raise AssertionError("default-off policy saved an unknown sender")
            if any(frame[0] == 0x83 for frame in recipient.pushes):
                raise AssertionError("default-off policy delivered the one-sided DM")
            if any(frame[0] == 0x82 and frame[1:5] == expected_ack
                   for frame in sender.pushes):
                raise AssertionError("default-off policy acknowledged the one-sided DM")

            # The synthetic PUSH_CODE_NEW_ADVERT has the ordinary contact
            # response body, so the app can accept it through its existing
            # CMD_ADD_UPDATE_CONTACT path.
            recipient.request(bytes([9]) + advertised[1:], (0,))
            if recipient.contact(sender_key) is None:
                raise AssertionError("manual acceptance did not add the sender")
            # No retry: the already-transmitted DM must emerge from the
            # receiver's bounded pending queue after the contact is added.
            until = time.monotonic() + min(timeout_ms / 1000 + 5, 120)
            while time.monotonic() < until:
                for link in (sender, recipient):
                    link.read(0.1)
                confirmed = any(frame[0] == 0x82 and frame[1:5] == expected_ack
                                for frame in sender.pushes)
                waiting = any(frame[0] == 0x83 for frame in recipient.pushes)
                if confirmed and waiting:
                    break

        confirmed = any(
            frame[0] == 0x82 and frame[1:5] == expected_ack for frame in sender.pushes
        )
        waiting = any(frame[0] == 0x83 for frame in recipient.pushes)
        learned = recipient.contact(sender_key) is not None
        result = {
            "sender_prefix": sender_key[:6].hex(),
            "recipient_prefix": recipient_key[:6].hex(),
            "route": "direct" if sent[1] == 0 else "flood",
            "ack_confirmed": confirmed,
            "recipient_message_waiting": waiting,
            "recipient_learned_sender": learned,
            "mode": "auto_accept" if auto_accept else "buffered_accept_after_refusal",
            "pending_advert_received": advertised is not None,
            "sender_notified_of_refusal": rejected,
            "timeout_ms": timeout_ms,
        }
        if invalid_signature_first:
            result["invalid_signature_rejected"] = True
        if not (confirmed and waiting and learned):
            raise AssertionError(json.dumps(result, sort_keys=True))
        return result
    finally:
        try:
            if original_setting in ("on", "off"):
                recipient.cli(f"set dm.one_key {original_setting}")
        finally:
            sender.close()
            recipient.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sender", required=True, help="sender Companion USB serial path")
    parser.add_argument("--recipient", required=True, help="recipient Companion USB serial path")
    parser.add_argument("--reset-contact", action="store_true",
                        help="remove the sender from recipient contacts before the test")
    parser.add_argument("--zero-hop", action="store_true",
                        help="set a direct zero-hop path; use only when both radios are nearby")
    parser.add_argument("--invalid-signature-first", action="store_true",
                        help="verify an encrypted introduction with a bad signature is rejected")
    parser.add_argument("--auto-accept", action="store_true",
                        help="temporarily enable automatic acceptance of verified introductions")
    args = parser.parse_args()
    print(json.dumps(run(args.sender, args.recipient, args.reset_contact,
                         args.zero_hop, args.invalid_signature_first,
                         args.auto_accept),
                     sort_keys=True))

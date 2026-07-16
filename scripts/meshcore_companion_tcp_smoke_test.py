#!/usr/bin/env python3

import argparse
import socket
import struct
import sys


def recv_exact(sock, size):
    buf = bytearray()
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise RuntimeError("connection closed while waiting for data")
        buf.extend(chunk)
    return bytes(buf)


def send_frame(sock, payload):
    sock.sendall(b"<" + struct.pack("<H", len(payload)) + payload)


def recv_frame(sock, timeout):
    sock.settimeout(timeout)
    header = recv_exact(sock, 3)
    if header[0:1] != b">":
        raise RuntimeError(f"unexpected frame header {header!r}")
    payload_len = struct.unpack("<H", header[1:3])[0]
    payload = recv_exact(sock, payload_len)
    return payload


def read_device_info(payload):
    if len(payload) < 82 or payload[0] != 0x0D:
        return None
    manufacturer = payload[20:60].split(b"\x00", 1)[0].decode("utf-8", "replace")
    version = payload[60:80].split(b"\x00", 1)[0].decode("utf-8", "replace")
    return manufacturer, version


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        send_frame(sock, bytes([0x01] + [0x00] * 7))
        start_reply = recv_frame(sock, args.timeout)
        if not start_reply or start_reply[0] != 0x05:
            raise RuntimeError(f"expected APP_START response 0x05, got {start_reply[:1]!r}")

        send_frame(sock, bytes([0x16, 0x03]))
        device_reply = recv_frame(sock, args.timeout)
        if not device_reply or device_reply[0] != 0x0D:
            raise RuntimeError(f"expected DEVICE_QUERY response 0x0d, got {device_reply[:1]!r}")

        decoded = read_device_info(device_reply)
        if decoded:
            manufacturer, version = decoded
            print(f"manufacturer={manufacturer}")
            print(f"firmware={version}")
        else:
            print("DEVICE_QUERY response decoded partially")

    print("PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)

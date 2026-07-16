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


def assert_socket_open(sock, label):
    sock.settimeout(0.1)
    try:
        peek = sock.recv(1, socket.MSG_PEEK)
    except socket.timeout:
        return
    if peek == b"":
        raise RuntimeError(f"{label} was closed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    client_a = socket.create_connection((args.host, args.port), timeout=args.timeout)
    client_b = socket.create_connection((args.host, args.port), timeout=args.timeout)

    try:
        send_frame(client_a, bytes([0x01] + [0x00] * 7))
        recv_frame(client_a, args.timeout)
        send_frame(client_b, bytes([0x01] + [0x00] * 7))
        recv_frame(client_b, args.timeout)

        send_frame(client_a, bytes([0x16, 0x03]))
        reply_a = recv_frame(client_a, args.timeout)
        if not reply_a or reply_a[0] != 0x0D:
            raise RuntimeError(f"client A expected DEVICE_QUERY response 0x0d, got {reply_a[:1]!r}")

        assert_socket_open(client_a, "client A before client B command")

        send_frame(client_b, bytes([0x16, 0x03]))
        reply_b = recv_frame(client_b, args.timeout)
        if not reply_b or reply_b[0] != 0x0D:
            raise RuntimeError(f"client B expected DEVICE_QUERY response 0x0d, got {reply_b[:1]!r}")

        assert_socket_open(client_a, "client A after client B command")
        assert_socket_open(client_b, "client B after client B command")

        send_frame(client_a, bytes([0x16, 0x03]))
        reply_a_again = recv_frame(client_a, args.timeout)
        if not reply_a_again or reply_a_again[0] != 0x0D:
            raise RuntimeError(f"client A second DEVICE_QUERY failed, got {reply_a_again[:1]!r}")

        print("PASS")
        return 0
    finally:
        client_a.close()
        client_b.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)

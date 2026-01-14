import argparse
import json
import sys
import threading
import time

try:
    import serial  # pyserial
except ModuleNotFoundError:  # guide user to install dependency
    print("pyserial not found. Install with: python -m pip install pyserial", file=sys.stderr)
    sys.exit(1)


def send_json(ser, obj):
    line = json.dumps(obj)
    ser.write((line + "\n").encode("utf-8"))
    ser.flush()


def reader(ser):
    try:
        while True:
            line = ser.readline()
            if not line:
                break
            try:
                text = line.decode("utf-8", errors="replace").rstrip()
            except Exception:
                text = str(line)
            ts = time.strftime("%H:%M:%S")
            print(f"[{ts}] {text}")
    except KeyboardInterrupt:
        return


def main():
    parser = argparse.ArgumentParser(description="USB chatbot serial helper")
    parser.add_argument("command", choices=["get-config", "set-config", "send", "set-time", "reboot"], help="Command to send")
    parser.add_argument("--port", default="COM9", help="Serial port (default: COM9)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--node", help="Node name for set-config")
    parser.add_argument("--channel", help="Channel name for set-config")
    parser.add_argument("--key", help="Channel hex key for set-config (32 or 64 hex chars)")
    parser.add_argument("--text", help="Text to send for send command")
    parser.add_argument("--timestamp", type=int, help="Epoch seconds for set-time; default now")

    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        print(f"Failed to open {args.port}: {exc}")
        sys.exit(1)

    t = threading.Thread(target=reader, args=(ser,), daemon=True)
    t.start()

    if args.command == "get-config":
        send_json(ser, {"cmd": "get_config"})
    elif args.command == "set-config":
        payload = {"cmd": "set_config"}
        if args.node:
            payload["node_name"] = args.node
        if args.channel:
            payload["channel_name"] = args.channel
        if args.key:
            payload["channel_key_hex"] = args.key
        send_json(ser, payload)
    elif args.command == "send":
        text = args.text or ""
        send_json(ser, {"cmd": "send", "text": text})
    elif args.command == "set-time":
        ts = args.timestamp or int(time.time())
        send_json(ser, {"cmd": "set_time", "timestamp": ts})
    elif args.command == "reboot":
        send_json(ser, {"cmd": "reboot"})

    try:
        while t.is_alive():
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()

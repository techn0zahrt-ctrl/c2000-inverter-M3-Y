#!/usr/bin/env python3
"""
can_logger.py — CAN debug message receiver for the c2000-inverter firmware.

Listens for CAN frames on ID 0x7FF (the CanLogger debug channel) and
reassembles the ASCII text sent in 8-byte chunks, printing it to stdout.

Usage:
    python can_logger.py [--interface IFACE] [--channel CHANNEL] [--bitrate RATE]

Defaults:
    --interface  pcan
    --channel    PCAN_USBBUS1
    --bitrate    500000

Requirements:
    pip install python-can

Examples:
    # PCAN USB on Windows/Linux
    python can_logger.py

    # SocketCAN on Linux (e.g. Raspberry Pi)
    python can_logger.py --interface socketcan --channel can0

    # PEAK PCAN USB with explicit channel
    python can_logger.py --interface pcan --channel PCAN_USBBUS1 --bitrate 500000
"""

import argparse
import sys
import can

# CAN ID used by CanLogger in the firmware (must match canlogger.cpp)
CAN_LOG_ID = 0x7FF


def parse_args():
    parser = argparse.ArgumentParser(
        description="Receive and display c2000-inverter CAN debug messages"
    )
    parser.add_argument(
        "--interface", default="pcan",
        help="python-can interface type (default: pcan)"
    )
    parser.add_argument(
        "--channel", default="PCAN_USBBUS1",
        help="CAN channel/device (default: PCAN_USBBUS1)"
    )
    parser.add_argument(
        "--bitrate", type=int, default=500000,
        help="CAN bus bitrate in bps (default: 500000)"
    )
    return parser.parse_args()


def main():
    args = parse_args()

    print(
        f"Connecting: interface={args.interface} "
        f"channel={args.channel} "
        f"bitrate={args.bitrate}",
        flush=True
    )

    try:
        bus = can.interface.Bus(
            interface=args.interface,
            channel=args.channel,
            bitrate=args.bitrate,
        )
    except Exception as e:
        print(f"ERROR: Could not open CAN bus: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Listening for debug messages on ID 0x{CAN_LOG_ID:03X} ...\n", flush=True)

    # Accumulate bytes across frames until a null terminator is received.
    # The firmware sends each message as one or more 8-byte frames, null-padded.
    buf = bytearray()

    try:
        for msg in bus:
            if msg.arbitration_id != CAN_LOG_ID:
                continue

            for byte in msg.data:
                if byte == 0:
                    # Null byte = end of message: flush accumulated text
                    if buf:
                        try:
                            text = buf.decode("ascii", errors="replace")
                        except Exception:
                            text = repr(bytes(buf))
                        sys.stdout.write(text)
                        sys.stdout.flush()
                        buf = bytearray()
                else:
                    buf.append(byte)

    except KeyboardInterrupt:
        # Flush any partial message on exit
        if buf:
            sys.stdout.write(buf.decode("ascii", errors="replace"))
            sys.stdout.write("\n")
        print("\nStopped.", flush=True)
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()

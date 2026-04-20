#!/usr/bin/env python3
"""
can_logger.py — CAN debug message receiver for the c2000-inverter firmware.

Control protocol (ID 0x7FE):
  Byte 0 = command: 0x01 = start logging, 0x00 = stop logging
  Byte 1 = interval in 100 ms units (1=100ms, 5=500ms, 20=2s)

Log channel (ID 0x7FF):
  ASCII text in 8-byte null-padded frames. A null byte signals
  end-of-message. Frames are reassembled and printed to stdout.

On startup:     sends start command with requested interval.
Every 5 s:      resends start command as keepalive (firmware auto-stops after 30 s).
On Ctrl+C:      sends stop command then exits.
--no-keepalive: send start once and leave keepalive to the user.

Usage:
    python can_logger.py [options]

Options:
    --interface IFACE       python-can interface (default: pcan)
    --channel   CHANNEL     CAN channel/device  (default: PCAN_USBBUS1)
    --bitrate   RATE        bus bitrate in bps  (default: 500000)
    --interval  MS          log interval in ms  (default: 2000)
    --no-keepalive          send start once, no periodic keepalive

Requirements:
    pip install python-can

Examples:
    python can_logger.py
    python can_logger.py --interval 500
    python can_logger.py --interface socketcan --channel can0
    python can_logger.py --no-keepalive
"""

import argparse
import sys
import threading
import can

CAN_LOG_ID  = 0x7FF   # firmware → PC  (log text)
CAN_CTRL_ID = 0x7FE   # PC → firmware  (start/stop)

CMD_START = 0x01
CMD_STOP  = 0x00

KEEPALIVE_INTERVAL_S = 5   # resend start every 5 s


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
    parser.add_argument(
        "--interval", type=int, default=2000,
        help="log interval in ms (default: 2000)"
    )
    parser.add_argument(
        "--no-keepalive", action="store_true",
        help="send start once, no periodic keepalive (logging stops after 30 s)"
    )
    return parser.parse_args()


def interval_units(ms):
    """Convert ms to firmware 100 ms units, clamped to [1, 255]."""
    units = max(1, min(255, ms // 100))
    return units


def send_control(bus, cmd, units):
    """Send a start (0x01) or stop (0x00) control frame."""
    data = bytes([cmd, units, 0, 0, 0, 0, 0, 0])
    msg = can.Message(
        arbitration_id=CAN_CTRL_ID,
        data=data,
        is_extended_id=False,
    )
    try:
        bus.send(msg)
    except Exception as e:
        print(f"WARNING: control send failed: {e}", file=sys.stderr)


def keepalive_worker(bus, units, stop_event):
    """Resend start command every KEEPALIVE_INTERVAL_S seconds."""
    while not stop_event.wait(KEEPALIVE_INTERVAL_S):
        send_control(bus, CMD_START, units)


def main():
    args = parse_args()
    units = interval_units(args.interval)

    print(
        f"Connecting: interface={args.interface} "
        f"channel={args.channel} "
        f"bitrate={args.bitrate}",
        flush=True,
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

    # Send initial start command
    send_control(bus, CMD_START, units)
    print(
        f"Logging started: interval={args.interval} ms "
        f"({'no keepalive' if args.no_keepalive else f'keepalive every {KEEPALIVE_INTERVAL_S} s'})",
        flush=True,
    )
    print(f"Listening for debug messages on ID 0x{CAN_LOG_ID:03X} ...\n", flush=True)

    # Start keepalive thread unless suppressed
    stop_event = threading.Event()
    ka_thread = None
    if not args.no_keepalive:
        ka_thread = threading.Thread(
            target=keepalive_worker,
            args=(bus, units, stop_event),
            daemon=True,
        )
        ka_thread.start()

    # Accumulate bytes across frames until a null terminator is received.
    buf = bytearray()

    try:
        for msg in bus:
            if msg.arbitration_id != CAN_LOG_ID:
                continue

            for byte in msg.data:
                if byte == 0:
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
        # Flush any partial message
        if buf:
            sys.stdout.write(buf.decode("ascii", errors="replace"))
            sys.stdout.write("\n")
        print("\nStopping...", flush=True)
    finally:
        stop_event.set()
        send_control(bus, CMD_STOP, units)
        bus.shutdown()
        print("Stopped.", flush=True)


if __name__ == "__main__":
    main()

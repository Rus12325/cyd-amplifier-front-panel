#!/usr/bin/env python3
"""Read the panel's serial log, with a normal reset.

The CH340 on this board has the usual auto-reset circuit: DTR goes to IO0 and
RTS to EN.  A pulse on DTR therefore drops the ESP32 into the ROM bootloader,
where it says nothing at all - which is why watching the port can look dead.
Parking both lines and pulsing RTS alone is a plain reset, and that is what this
script does.

  python tools/serial_log.py --seconds 20
  python tools/serial_log.py --no-reset            # just listen, no reboot
  python tools/serial_log.py --port COM7 --seconds 5
"""
import argparse
import pathlib
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is missing:  python -m pip install pyserial")
    sys.exit(2)

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from flash_cyd import pick  # noqa: E402  - same folder, same port search


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="use this port instead of searching for the CH340")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=15.0,
                    help="how long to listen (default: 15)")
    ap.add_argument("--wait", type=float, default=5.0,
                    help="how long to wait for a free port (default: 5)")
    ap.add_argument("--no-reset", action="store_true",
                    help="do not reset the board first, just listen")
    args = ap.parse_args()

    # The board's own text is ASCII, but a yanked cable can leave half a byte on
    # the line; keep that from killing the run on a cp1251 console.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    device = pick(args.port, args.wait)
    if device is None:
        return 2

    port = serial.Serial()
    port.port = device
    port.baudrate = args.baud
    port.timeout = 0.2
    port.dtr = False          # DTR -> IO0: never touch it, or the ROM loader runs
    port.rts = False
    try:
        port.open()
    except Exception as exc:
        print("cannot open %s: %s" % (device, exc))
        return 1

    print("--- %s (%s) ---" % (device, "reset" if not args.no_reset else "listen only"))
    if not args.no_reset:
        port.setRTS(True)
        time.sleep(0.15)
        port.setRTS(False)

    deadline = time.time() + args.seconds
    try:
        while time.time() < deadline:
            chunk = port.readline()
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

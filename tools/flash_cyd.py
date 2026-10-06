"""Flash the CYD without hunting for the port number.

    python tools/flash_cyd.py --list              what is connected
    python tools/flash_cyd.py                     upload env cyd
    python tools/flash_cyd.py --env cyd_bt        upload env cyd_bt
    python tools/flash_cyd.py --wait 120          wait up to 2 min for the port

The port number of this board is not stable (COM3, COM5, COM7 ... all seen), and
a program that was killed while it held the port can keep it locked until the
board is replugged.  This script finds the CH340, proves the port can really be
opened, and only then calls PlatformIO - so the failure is reported here, with
the fix, instead of inside esptool.
"""
import argparse
import pathlib
import subprocess
import sys
import time

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:
    print("pyserial is missing:  python -m pip install pyserial")
    sys.exit(2)

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"

# 1A86:7523 CH340 (the CYD's USB-serial), 303A Espressif native USB.
KNOWN_VIDS = {0x1A86: "CH340", 0x303A: "Espressif USB"}


def candidates():
    """(device, description, free) for every port that could be the board."""
    found = []
    for port in sorted(list_ports.comports(), key=lambda p: p.device):
        if port.vid not in KNOWN_VIDS:
            continue
        free = True
        try:
            with serial.Serial(port.device, 115200, timeout=0.2):
                pass
        except Exception:
            free = False
        found.append((port.device, port.description or KNOWN_VIDS[port.vid], free))
    return found


def list_ports_cmd():
    found = candidates()
    if not found:
        print("no CH340 / Espressif serial port found - is the board plugged in?")
        return 1
    for device, desc, free in found:
        print("  %-6s %-28s %s" % (device, desc, "free" if free else "BUSY"))
    return 0


def pick(explicit, wait_s):
    """Device to flash, waiting up to wait_s seconds for it to be usable."""
    deadline = time.time() + wait_s
    while True:
        found = candidates()
        if explicit:
            found = [f for f in found if f[0].lower() == explicit.lower()]
            if not found:
                print("port %s is not present (or is not a serial device)" % explicit)
                return None
        free = [f for f in found if f[2]]
        if free:
            if len(free) > 1:
                print("several candidates, using the first: %s"
                      % ", ".join(f[0] for f in free))
            return free[0][0]

        busy = ", ".join(f[0] for f in found) or "none"
        if time.time() >= deadline:
            print("port(s) %s are busy and --wait ran out." % busy)
            print("A handle can stay stuck after a program was killed: unplug and")
            print("replug the board (or run tools/restart_cyd_port.ps1 as admin)")
            print("and start this script again.")
            return None
        print("waiting for %s to be free ..." % busy)
        time.sleep(2)


def pick_fast(explicit, seconds):
    """Poll every 100 ms and grab the port the instant it can be opened.

    A board that is replugged is grabbed again within a fraction of a second by
    whatever supervises it, so waiting two seconds between attempts loses the
    race every time.
    """
    deadline = time.time() + seconds
    while time.time() < deadline:
        for device, _desc, free in candidates():
            if free and (not explicit or device.lower() == explicit.lower()):
                return device
        time.sleep(0.1)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", default="cyd",
                    choices=["cyd", "cyd_bt", "cyd_bt_static"],
                    help="PlatformIO environment (default: cyd)")
    ap.add_argument("--port", help="use this port instead of searching")
    ap.add_argument("--wait", type=float, default=0.0,
                    help="seconds to keep retrying while the port is busy")
    ap.add_argument("--watch", type=float, default=0.0, metavar="SECONDS",
                    help="poll fast and flash the moment the port appears, so a "
                         "replug is not won by whatever grabbed it before")
    ap.add_argument("--list", action="store_true", help="list candidates and exit")
    args = ap.parse_args()

    if args.list:
        return list_ports_cmd()

    if args.watch:
        print("watching for the board: unplug and replug its USB cable now "
              "(%.0f s)" % args.watch)
        port = pick_fast(args.port, args.watch)
        if not port:
            print("the port never showed up in %.0f s" % args.watch)
            return 1
        print("caught %s - flashing immediately" % port)
    else:
        port = pick(args.port, args.wait)
        if not port:
            return 1

    cmd = ["pio", "run", "-e", args.env, "-t", "upload", "--upload-port", port]
    print("flashing %s on %s\n  %s\n" % (args.env, port, " ".join(cmd)))
    return subprocess.call(cmd, cwd=str(FIRMWARE))


if __name__ == "__main__":
    sys.exit(main())

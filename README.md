# CYD Amplifier Front Panel

Turns an **ESP32-2432S028 ("CYD")** into the front panel of a 5.1 amplifier.
The panel switches the 24 V rail, holds mute, shows a live audio spectrum,
serves the touch UI, keeps its settings in NVS and can test its own wiring.

The audio path stays **fully analogue** — the panel never touches the signal:

```
PC (3 analogue outputs) ──► amplifier board ──► speakers
                                  ▲
                                  │ K1  24 V rail, K2  mute
                              CYD panel (control + display only)
```

It controls, it does not amplify.

## Layout

```
├── firmware/            PlatformIO project - the actual panel firmware
│   ├── README.md        build environments, flashing, module map
│   ├── platformio.ini   envs: cyd / cyd_enc / cyd_bt
│   ├── include/         config.h (pins, thresholds), spectrum.h
│   └── src/             amp, audio, bt_sink, encoder, hid, io, ota,
│                        settings, spectrum, touch, ui, wire_test
├── docs/
│   ├── wiring.md        full wiring reference incl. power and mute logic
│   └── wiring-netlist.html
├── tools/
│   ├── wiring_diagram.py    regenerates the netlist from the wiring tables
│   ├── check_spectrum.py    verifies the spectrum maths off-device
│   ├── flash_cyd.py         finds the CH340 port, checks it opens, then calls PlatformIO
│   ├── serial_log.py        resets via RTS without disturbing DTR
│   └── restart_cyd_port.ps1 release a stuck COM port (needs admin)
├── wiring_cyd_amp.html      interactive guide with a power/mute simulator
├── render.py, front_*.py, scale_*.py, step_to_stl.py, validate_step.py
│                            FreeCAD/STEP tooling behind the panel drawings
└── compare_rear.html, view_section.html, cross_section_cyd.svg
```

## Firmware builds

```bash
cd firmware
pio run -e cyd      -t upload    # 5.1 panel: PC source, 24 V rail, mute, PC volume over BT-HID
pio run -e cyd_enc  -t upload    # adds a physical encoder on GPIO 22/35
pio run -e cyd_bt   -t upload    # adds A2DP sink - the CYD receives Bluetooth itself
pio device monitor -b 115200
```

See [`firmware/README.md`](firmware/README.md) for the module breakdown, the
self-test entry point and the settings model.

## Flashing notes

The COM port on this board is not stable, and a crashed program can keep the
port claimed until the cable is re-plugged. `tools/flash_cyd.py` exists for
that: it locates the CH340, verifies the port actually opens and only then
invokes PlatformIO.

```bash
python tools/flash_cyd.py --list              # what is attached, and is it free
python tools/flash_cyd.py --env cyd
python tools/flash_cyd.py --env cyd_bt --wait 120
```

Reading the log needs the same care: DTR on this board is wired to IO0, so a
monitor that raises it drops the board into the ROM bootloader and prints
nothing. `tools/serial_log.py` leaves DTR/RTS alone and resets through RTS
only.

```bash
python tools/serial_log.py --seconds 20
```

## Documentation

- [`docs/wiring.md`](docs/wiring.md) — the authoritative wiring reference:
  board, amplifiers, 24 V distribution, relay and mute behaviour, calibration.
- `wiring_cyd_amp.html` — the same content as an interactive guide with a
  power and mute simulator.
- `compare_rear.html`, `view_section.html` — rendered rear-panel and
  cross-section views used while fitting the panel to the case.

## Requirements

- PlatformIO (CLI or IDE extension)
- Python 3 for the tooling
- FreeCAD only if you want to regenerate the STEP/STL geometry

## Hardware summary

| Item | Count | Note |
| --- | --- | --- |
| Amplifier boards | 3 × stereo | front / rear / centre + sub |
| Power supply | 1 × 24 V / 4 A | 96 W total |
| Source | PC, 3 analogue outputs | green, black, orange |
| Panel | CYD ESP32-2432S028 | screen, touch, Wi-Fi/BT |
| Relays | 2 × 5 V | K1 = 24 V rail, K2 = mute |

> Mains and 24 V wiring must be done with the power off. Switch only
> low-voltage loads from the panel's GPIOs — never put mains on an ESP32 pin.

## License

No license file is included. Contact the author before redistributing.

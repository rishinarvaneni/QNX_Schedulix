# Hardware preflight (Raspberry Pi 4 / QNX 8.0)

Verifies the software stack for CAN / UART / GPIO bring-up **before** hardware
is purchased. Runs on the Windows or Linux dev host today; probes the real
target once a Pi is flashed.

## Run

```bash
python hw/preflight.py --host         # library + toolchain + port inventory
python hw/preflight.py --self-test    # CAN virtual bus tx/rx, UART framing
python hw/preflight.py --all          # everything above (default)
python hw/preflight.py --target pi@192.168.1.50   # probe live Pi / QNX
```

Exit code `0` = ready, `1` = gaps. `--self-test` is the useful gate: it proves
python-can and pyserial actually move bytes without any hardware attached.

## Install

```bash
pip install -r hw/requirements-host.txt
```

`RPi.GPIO`, `gpiozero`, `spidev` are Linux-only by design and stay
uninstalled on the Windows host. That is expected, not a failure.

## Platform split

The Pi is not one OS, so the access path differs by target:

| Target | CAN | UART | GPIO |
| --- | --- | --- | --- |
| QNX 8.0 on Pi 4 | `can-mcp2515` DDK driver or `dev-can-linux` over PCI USB adapter -> `/dev/can0` | `devc-*` driver (mini-UART) -> `/dev/ser1` | `gpio-bcm2711` utility |
| Raspberry Pi OS (Linux) | SocketCAN + `mcp251x` -> `can0` | `/dev/ttyS0` or USB adapter | RPi.GPIO / gpiozero |
| Windows dev host | enumeration only | USB-to-UART adapter | n/a |

Python (`python-can`, `pyserial`) drives the **Linux** and dev-host legs. The
QNX target talks to `/dev/can*` and `/dev/ser*` from C, so no Python stack is
needed on target. Confirm which OS runs the workloads before wiring the
injector.

## Wiring (MCP2515 CAN HAT, standard)

| Pi pin | Signal | MCP2515 |
| --- | --- | --- |
| 2 | 5V | transceiver VCC |
| 1 | 3V3 | MCP2515 VCC |
| 19 / 21 / 23 | MOSI / MISO / SCLK | SI / SO / SCK |
| 24 | CE0 | CS |
| 22 | GPIO25 | INT |
| 6 / 9 | GND | GND |

Two 120 ohm terminators, one at each end of the bus. Only fits `mcp2515`, not
`mcp2515fd` (CAN FD) or `mcp25625`. Note the crystal is 8 MHz on some modules
and 16 MHz on others; the QNX driver command takes `-c 16000000` or `-c 8000000`
to match, and a mismatch means no frames.

GPIO marker: any header pin with a jumper to ground, driven via
`gpio-bcm2711 set <n> op pn dh` / `dl`, observed on a logic analyzer.
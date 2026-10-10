#!/usr/bin/env python3
"""
Schedulix hardware preflight.

Verifies the software stack needed to drive CAN / UART / GPIO on a
Raspberry Pi 4 (QNX 8.0 target, Linux dev host) before hardware is bought.

Modes:
    python hw/preflight.py --host        tool/library inventory (dev host)
    python hw/preflight.py --target      check for a live QNX/Pi target
    python hw/preflight.py --self-test   end-to-end loopback tests that need
                                         no hardware (virtual CAN, serial
                                         loopback via pty on Linux)

Exit code 0 = ready, 1 = gaps found.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib
import importlib.util
import io
import logging
import os
import platform
import shutil
import subprocess
import sys

OK = "PASS"
FAIL = "FAIL"
WARN = "WARN"
INFO = "INFO"

_RESULTS: list[tuple[str, str, str, str]] = []

# python-can probes every vendor DLL on import and logs a wall of noise about
# the ones that are absent. Those messages are expected on a dev host.
logging.getLogger("can").setLevel(logging.ERROR)


@contextlib.contextmanager
def quiet_can_probe():
    """Silence python-can's vendor backend discovery chatter."""
    logger = logging.getLogger("can")
    buf = io.StringIO()
    handler = logging.StreamHandler(buf)
    prev_level, prev_prop = logger.level, logger.propagate
    logger.addHandler(handler)
    logger.setLevel(logging.CRITICAL)
    logger.propagate = False
    try:
        yield
    finally:
        logger.removeHandler(handler)
        logger.setLevel(prev_level)
        logger.propagate = prev_prop


def record(check: str, status: str, detail: str = "") -> None:
    _RESULTS.append((check, status, detail, ""))
    print(f"[{status:4}] {check}" + (f"  {detail}" if detail else ""))


def module_present(name: str) -> bool:
    try:
        return importlib.util.find_spec(name) is not None
    except (ImportError, ValueError):
        return False


def which(*names: str) -> str | None:
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    return None


# --------------------------------------------------------------------------
# host checks
# --------------------------------------------------------------------------

PY_LIBS = [
    ("serial", "pyserial", "UART via USB-to-UART adapter"),
    ("can", "python-can", "CAN frame injection (SocketCAN / serial / virtual)"),
    ("RPi", "RPi.GPIO", "GPIO markers on Raspberry Pi (Linux only)"),
    ("gpiozero", "gpiozero", "GPIO markers, higher-level API"),
    ("spidev", "spidev", "SPI access for MCP2515 CAN HAT (Linux only)"),
    ("numpy", "numpy", "trace post-processing"),
    ("matplotlib", "matplotlib", "trace plots"),
]


def check_python_libs() -> None:
    print("\n--- Python libraries ---")
    for mod, pkg, why in PY_LIBS:
        if module_present(mod):
            try:
                m = importlib.import_module(mod)
                ver = getattr(m, "__version__", "?")
                record(f"{pkg:<12} import {mod:<9}", OK, f"{ver}  ({why})")
            except Exception as exc:  # noqa: BLE001
                record(f"{pkg:<12} import {mod:<9}", FAIL, f"found but broken: {exc}")
        else:
            record(f"{pkg:<12} import {mod:<9}", FAIL, f"not installed  ({why})")


HOST_TOOLS = [
    (("git",), "git", "branch / trace version control"),
    (("cmake", "cmake.exe"), "cmake", "Qt build system"),
    (("ninja", "ninja.exe"), "ninja", "Qt build backend"),
    (("qmake6", "qmake", "qmake6.exe"), "qmake6", "Qt 6 toolchain probe"),
    (("qnx", "qnxsdp-env"), "qnx-sdp", "QNX cross-compiler env"),
    (("slog2info",), "slog2info", "QNX target logging"),
]


def check_tools() -> None:
    print("\n--- Host tools ---")
    for names, label, why in HOST_TOOLS:
        p = which(*names)
        if p:
            record(f"{label:<12}", OK, f"{p}  ({why})")
        else:
            record(f"{label:<12}", WARN, f"not on PATH  ({why})")

    qt_root = os.environ.get("QTDIR") or "C:/Qt/6.8.3/mingw_64"
    if os.path.isdir(qt_root):
        record("qt install", OK, qt_root)
    else:
        record("qt install", WARN, f"set QTDIR; looked for {qt_root}")


def check_host_ports() -> None:
    print("\n--- Host serial / CAN devices ---")
    if not module_present("serial"):
        record("serial ports", FAIL, "pyserial missing, cannot enumerate")
        return
    try:
        from serial.tools import list_ports  # noqa: PLC0415

        ports = list(list_ports.comports())
        if ports:
            for p in ports:
                record(f"serial {p.device}", OK, p.description)
        else:
            record("serial ports", WARN, "none found - adapter not plugged in")
    except Exception as exc:  # noqa: BLE001
        record("serial ports", FAIL, str(exc))

    if not module_present("can"):
        record("can interfaces", FAIL, "python-can missing")
        return
    try:
        import can  # noqa: PLC0415

        with quiet_can_probe():
            cfgs = can.detect_available_configs()
        backends = sorted({c.get("interface", "?") for c in cfgs})
        record("can backends", OK, ", ".join(backends) or "none")
        if platform.system() != "Linux":
            record("can physical", WARN,
                   f"physical CAN needs SocketCAN; {platform.system()} host "
                   "is enumeration-only")
    except Exception as exc:  # noqa: BLE001
        record("can interfaces", FAIL, str(exc))


# --------------------------------------------------------------------------
# self-tests that need no hardware
# --------------------------------------------------------------------------

def selftest_can() -> None:
    print("\n--- Self-test: python-can virtual bus ---")
    try:
        import can  # noqa: PLC0415
    except ImportError:
        record("virtual CAN tx/rx", FAIL, "python-can missing")
        return
    bus = None
    try:
        bus = can.Bus(interface="virtual", channel="preflight", receive_own_messages=True)
        msg = can.Message(arbitration_id=0x123, data=b"\x01\x02\x03", is_extended_id=False)
        bus.send(msg, timeout=1.0)
        got = bus.recv(timeout=1.0)
        if got and got.arbitration_id == 0x123 and got.data == b"\x01\x02\x03":
            record("virtual CAN tx/rx", OK, "0x123 payload round-tripped")
        else:
            record("virtual CAN tx/rx", FAIL, f"unexpected frame: {got}")
    except Exception as exc:  # noqa: BLE001
        record("virtual CAN tx/rx", FAIL, str(exc))
    finally:
        if bus is not None:
            with contextlib.suppress(Exception):
                bus.shutdown()


def selftest_serial() -> None:
    print("\n--- Self-test: UART framing ---")
    try:
        import serial  # noqa: PLC0415
    except ImportError:
        record("serial framing", FAIL, "pyserial missing")
        return
    try:
        s = serial.serial_for_url("loop://", baudrate=115200, timeout=1)
        s.write(b"CAN?\x0a")
        echoed = s.read(4)
        s.close()
        record("serial framing", OK if echoed == b"CAN?" else FAIL,
               f"loop:// echo -> {echoed!r}")
    except Exception as exc:  # noqa: BLE001
        record("serial framing", FAIL, str(exc))


# --------------------------------------------------------------------------
# target checks
# --------------------------------------------------------------------------

def check_target(host: str | None) -> None:
    print("\n--- Target (Raspberry Pi / QNX) ---")
    exe = which("ssh", "ssh.exe")
    if not host or not exe:
        record("ssh to target", WARN, "pass --target <user@host> once hardware is up")
        return
    cmd = [exe, "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", host,
           "uname -a; echo ---; ls /dev/can* 2>&1; echo ---; ls /dev/ser* /dev/ttyS* 2>&1"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    except subprocess.TimeoutExpired:
        record("ssh to target", FAIL, "timeout")
        return
    if out.returncode != 0:
        record("ssh to target", FAIL, out.stderr.strip()[:200])
        return
    record("ssh to target", OK, host)
    print(out.stdout)
    if "/dev/can" in out.stdout:
        record("CAN device nodes", OK, "found /dev/can*")
    else:
        record("CAN device nodes", FAIL,
               "no /dev/can* - run can-mcp2515 or dev-can-linux on target")
    if any(k in out.stdout for k in ("/dev/ser", "/dev/ttyS")):
        record("serial device nodes", OK, "found")
    else:
        record("serial device nodes", FAIL, "no serial nodes - start devc-* driver")


def summary() -> int:
    fails = [r for r in _RESULTS if r[1] == FAIL]
    warns = [r for r in _RESULTS if r[1] == WARN]
    print("\n" + "=" * 68)
    print(f"{len(_RESULTS) - len(fails) - len(warns)} pass  "
          f"{len(warns)} warn  {len(fails)} fail")
    if fails:
        print("\nBlocking:")
        for c, _, d, _ in fails:
            print(f"  - {c}: {d}")
    print("=" * 68)
    return 1 if fails else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", action="store_true", help="inventory dev host")
    ap.add_argument("--target", metavar="USER@HOST", help="probe a live Pi/QNX target")
    ap.add_argument("--self-test", action="store_true", help="no-hardware loopback tests")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()

    print(f"python {platform.python_version()} on "
          f"{platform.platform()} ({platform.machine()})")
    if not (args.host or args.target or args.self_test or args.all):
        args.all = True

    if args.host or args.all:
        check_python_libs()
        check_tools()
        check_host_ports()
    if args.self_test or args.all:
        selftest_can()
        selftest_serial()
    if args.target or args.all:
        check_target(args.target)

    return summary()


if __name__ == "__main__":
    sys.exit(main())
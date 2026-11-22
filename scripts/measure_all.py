#!/usr/bin/env python3
"""Run the board measurements of every prebuilt system and save the UART logs.

    python3 scripts/measure_all.py [--systems v1 v2 ...] [--modes 2 3 5] [--out build/campaign]

For each system in prebuilt/ and each ML-DSA level the script loads the
bitstream, the PS initialisation and the ELF over JTAG (sw/run_on_board.tcl),
reads the summary from the USB-UART and saves it as <out>/<log name>; v1 and
v2 also run the single-permutation test. Requires xsdb (source scripts/env.sh)
and pyserial.

Cables: with two micro-USB cables (PROG J17 and UART J14) the whole campaign
runs unattended. With one cable the script waits until the cable is on the
port it needs (it watches the USB devices) and prints which port to use.
"""
import argparse
import glob
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[1]
# system: (XSA name, ELF backend, log tag)
SYSTEMS = {
    "v1":      ("keccak_f_system",                 "v1",         "v1"),
    "v2":      ("keccak_f_system_maxi",            "v2",         "v2"),
    "v3":      ("keccak_multiblock",               "v3",         "v3"),
    "ntt":     ("keccak_multiblock_ntt8",          "v3-ntt",     "v3_ntt8"),
    "ntt64":   ("keccak_multiblock_ntt8w64",       "v3-ntt64",   "v3_ntt8w64"),
    "ntt64bi": ("keccak_multiblock_ntt8w64bi",     "v3-ntt64bi", "v3_ntt64bi"),
    "final":   ("keccak_multiblock_ntt8w64bi_hp2", "v3-ntt64bi", "v3_ntt64bi_hp2"),
}
FTDI_VENDOR = "0403"  # Digilent USB-JTAG on PROG (J17)


def uart_port():
    ports = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return ports[0] if ports else None


def jtag_present():
    for vendor in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        if Path(vendor).read_text().strip() == FTDI_VENDOR:
            return True
    return False


def wait_for(check, message):
    if check():
        return
    print(f">>> {message}", flush=True)
    while not check():
        time.sleep(0.5)
    time.sleep(2)  # let the device settle after enumeration


def program(xsa, elf, work):
    work.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(xsa) as archive:
        bits = [n for n in archive.namelist() if n.endswith(".bit")]
        if len(bits) != 1:
            raise SystemExit(f"{xsa}: expected one .bit, found {bits}")
        (work / "system.bit").write_bytes(archive.read(bits[0]))
        (work / "ps7_init.tcl").write_bytes(archive.read("ps7_init.tcl"))
    subprocess.run([shutil.which("xsdb") or "xsdb", str(ROOT / "sw/run_on_board.tcl"),
                    str(elf), str(work / "system.bit"), str(work / "ps7_init.tcl")],
                   check=True, stdout=subprocess.DEVNULL)


def capture(marker, copies, seconds):
    """Read the UART until `marker` has appeared `copies` times (the summary
    repeats every two seconds, so the last copy is always complete)."""
    port = uart_port()
    text = ""
    deadline = time.time() + seconds
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.reset_input_buffer()
        while time.time() < deadline and text.count(marker) < copies:
            text += s.read(4096).decode(errors="replace")
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--systems", nargs="+", choices=list(SYSTEMS), default=list(SYSTEMS))
    ap.add_argument("--modes", nargs="+", type=int, choices=[2, 3, 5], default=[2, 3, 5])
    ap.add_argument("--prebuilt", type=Path, default=ROOT / "prebuilt")
    ap.add_argument("--out", type=Path, default=ROOT / "build/campaign")
    ap.add_argument("--no-keccak-test", action="store_true")
    ap.add_argument("--timeout", type=float, default=400, help="seconds per run")
    a = ap.parse_args()

    runs = []
    for system in a.systems:
        xsa_name, backend, tag = SYSTEMS[system]
        folder = a.prebuilt / system
        xsa = folder / f"{xsa_name}.xsa"
        for mode in a.modes:
            runs.append((system, xsa, folder / f"mldsa_mode{mode}_{backend}.elf",
                         f"mldsa_mode{mode}_{tag}_1000msg_uart.txt", "END", "PASS (code 0)"))
        if system in ("v1", "v2") and not a.no_keccak_test:
            runs.append((system, xsa, folder / f"keccak_test_{system}.elf",
                         f"keccak_test_{system}_uart.txt", "(lane 0 after the timing loops",
                         "PASS: 1001 states"))
    missing = [str(p) for _, xsa, elf, *_ in runs for p in (xsa, elf) if not p.is_file()]
    if missing:
        raise SystemExit("Missing prebuilt files:\n  " + "\n  ".join(sorted(set(missing))))

    a.out.mkdir(parents=True, exist_ok=True)
    two_cables = jtag_present() and uart_port() is not None
    print(f"{len(runs)} runs, {'two cables: unattended' if two_cables else 'one cable: move it when asked'}",
          flush=True)
    results = []
    for i, (system, xsa, elf, log, marker, ok) in enumerate(runs, 1):
        print(f"[{i}/{len(runs)}] {system}: {elf.name}", flush=True)
        wait_for(jtag_present, "Cable on PROG (J17) to program the board")
        program(xsa, elf, ROOT / "build/program/campaign")
        wait_for(lambda: uart_port() is not None, "Cable on UART (J14) to read the result")
        text = capture(marker, 3, a.timeout)
        (a.out / log).write_text(text)
        passed = ok in text and text.count(marker) >= 2
        results.append((log, passed))
        print(f"[{i}/{len(runs)}] {'PASS' if passed else 'FAIL'} -> {a.out / log}", flush=True)
    failed = [log for log, passed in results if not passed]
    print(f"DONE: {len(results) - len(failed)}/{len(results)} passed" +
          (f"; failed: {' '.join(failed)}" if failed else ""), flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

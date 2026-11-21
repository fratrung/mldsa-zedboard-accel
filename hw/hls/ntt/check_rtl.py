"""Verify that an exported or integrated NTT contains the co-simulated Verilog."""
import argparse
from pathlib import Path


def check_rtl(solution, target, top):
    simulation = solution / "sim/verilog"
    synthesis = solution / "syn/verilog"
    files = sorted(p for p in synthesis.glob(f"{top}*") if p.suffix in (".dat", ".v"))
    groups = (simulation / f"{top}_transform_stage_Pipeline_groups.v").read_text()
    for lane in range(8):
        if f"{top}_butterfly_{lane}u_s grp_butterfly_{lane}u_s_fu_" not in groups:
            raise SystemExit(f"Missing instantiated butterfly {lane} in simulation")
    if not any(p.name == f"{top}.v" for p in files):
        raise SystemExit("Missing synthesis top")
    for path in files:
        expected = path.read_bytes()
        for directory in (simulation, target):
            candidate = directory / path.name
            # IP Integrator puts the ROM contents under the BD instance, while
            # its Verilog source is shared between instances in ipshared/.
            if directory == target and path.suffix == ".dat" and not candidate.is_file():
                shared = next((p for p in target.parents if p.name == "ipshared"), None)
                if shared:
                    matches = list(shared.parent.glob(f"ip/*/hdl/verilog/{path.name}"))
                    if len(matches) == 1:
                        candidate = matches[0]
            if not candidate.is_file() or candidate.read_bytes() != expected:
                raise SystemExit(f"NTT RTL mismatch: {candidate}")
    print(f"Verified {len(files)} NTT files and eight butterfly instances in {target}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("solution", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("--top", choices=("ntt_forward", "ntt_transform"), default="ntt_forward")
    args = parser.parse_args()
    check_rtl(args.solution, args.target, args.top)

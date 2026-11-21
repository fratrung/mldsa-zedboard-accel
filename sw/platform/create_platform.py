"""Create (or update) and build the standalone Vitis platform for the Cortex-A9.

    vitis -s sw/platform/create_platform.py [path/to/system.xsa]

The default XSA is the final system, build/xsa/keccak_multiblock_ntt8w64bi_hp2.xsa.
Every design in this repository places the Keccak control registers at
0x40000000 in a cell named keccak_f1600_0, so one platform serves all the
ELFs built by sw/mldsa/build.py. The board support package ends up in
build/vitis/zedboard_platform/export/zedboard_platform/sw/standalone_ps7_cortexa9_0,
which is the default --bsp of build.py.
"""
import sys
from pathlib import Path

import vitis

try:
    ROOT = Path(__file__).resolve().parents[2]
except NameError:  # some vitis -s versions do not define __file__
    ROOT = Path.cwd()
WORKSPACE = ROOT / "build/vitis"
NAME = "zedboard_platform"
DOMAIN = "standalone_ps7_cortexa9_0"

xsa = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/xsa/keccak_multiblock_ntt8w64bi_hp2.xsa"
xsa = xsa.resolve()
if not xsa.is_file():
    raise SystemExit(f"XSA not found: {xsa}")
WORKSPACE.mkdir(parents=True, exist_ok=True)

client = vitis.create_client()
client.set_workspace(path=str(WORKSPACE))
if (WORKSPACE / NAME).is_dir():
    platform = client.get_component(name=NAME)
    platform.update_hw(hw_design=str(xsa))
else:
    platform = client.create_platform_component(
        name=NAME, hw_design=str(xsa), os="standalone", cpu="ps7_cortexa9_0",
        domain_name=DOMAIN, compiler="gcc")
platform.build()
bsp = WORKSPACE / NAME / "export" / NAME / "sw" / DOMAIN
print(f"BSP: {bsp}")
vitis.dispose()

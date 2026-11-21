# Building and running every system

All commands run from the repository root. Generated files go to `build/`
(ignored by git) and can be deleted at any time with `make clean`.

## 0. Environment

```sh
git submodule update --init                 # pq-crystals/dilithium and Digilent board files
pip install -r requirements.txt             # pyserial, for the UART reader
export XILINX_ROOT=/path/to/Xilinx/2026.1   # directory that contains Vivado/ and Vitis/
source scripts/env.sh                       # Vivado, Vitis, Vitis HLS, xsdb, arm-none-eabi-gcc
```

The scripts need AMD **2026.1**: `hw/vivado/keccak_f_system_bd.tcl` was written
by `write_bd_tcl` and refuses other versions. The ZedBoard board files are
taken from the submodule, so nothing has to be installed in Vivado.

## 1. HLS IPs

Each script is run with `vitis-run --mode hls --tcl <script>` from an empty
working directory (the Makefile targets do this under `build/hls/`). The HLS
projects are created at fixed paths under `build/`, where the Vivado script
looks for them.

| Make target | Script | Top | Steps | IP directory | Used by |
|---|---|---|---|---|---|
| `hls-keccak` | `hw/hls/keccak/export_multiblock.tcl` | `keccak_multiblock` | synthesis, export | `build/mldsa/hls-v3/multiblock` | v3, all NTT systems |
| `verify-keccak` | `hw/hls/keccak/verify_multiblock.tcl` | `keccak_multiblock` | C sim, synthesis, C/RTL co-sim (15 calls) | `build/mldsa/hls-verify-v3/multiblock` | verification only |
| `hls-ntt` | `hw/hls/ntt/verify_bidir.tcl` | `ntt_transform` | C sim (1003 polynomials per direction), synthesis, co-sim, export, RTL check | `build/ntt/eight_lane_bidir_hls` | `ntt64bi`, `final` |
| `hls-v1` | `hw/hls/keccak/export_v1.tcl` | `keccak_f1600` (v1) | C sim (1001 states), synthesis, co-sim, export | `build/mldsa/hls-v1/keccak_f1600` | v1 |
| `hls-v2` | `hw/hls/keccak/export_v2.tcl` | `keccak_f1600` (v2) | C sim (1001 states), synthesis, co-sim, export | `build/mldsa/hls-v2/keccak_f1600` | v2 |
| `hls-ntt32` | `hw/hls/ntt/w32/verify_ntt.tcl` | `ntt_forward` (32-bit) | C sim, synthesis, co-sim, export, RTL check | `build/ntt/eight_lane_hls` | `ntt` |
| `hls-ntt64` | `hw/hls/ntt/w64/verify_ntt.tcl` | `ntt_forward` (64-bit) | C sim, synthesis, co-sim, export, RTL check | `build/ntt/eight_lane_w64_hls` | `ntt64` |

The testbenches compare the HLS code with the functions of the reference
implementation (`KeccakF1600_StatePermute`, `keccak_squeezeblocks`, `ntt`,
`invntt_tomont`) coefficient by coefficient or lane by lane. The Keccak IPs
are synthesised for a 10 ns target, the NTT IPs for 20 ns; every system then
runs at 50 MHz. [`check_rtl.py`](../hw/hls/ntt/check_rtl.py) verifies that the
exported NTT package, and later the copy imported by Vivado, contain exactly
the co-simulated Verilog and ROM files.

## 2. Vivado systems

```sh
make bitstream VARIANT=<variant>
# = cd build/vivado/<variant>_logs && vivado -mode batch -source ../../../hw/vivado/build_system.tcl -tclargs <variant>
```

| Variant | HLS IPs needed | Project (`build/vivado/`) | XSA (`build/xsa/`) | ELF backend |
|---|---|---|---|---|
| `v1` | `hls-v1` | `keccak_v1` | `keccak_f_system.xsa` | `v1` |
| `v2` | `hls-v2` | `keccak_v2` | `keccak_f_system_maxi.xsa` | `v2` |
| `v3` | `hls-keccak` | `keccak_v3` | `keccak_multiblock.xsa` | `v3` |
| `ntt` | `hls-keccak`, `hls-ntt32` | `keccak_v3_ntt8` | `keccak_multiblock_ntt8.xsa` | `v3-ntt` |
| `ntt64` | `hls-keccak`, `hls-ntt64` | `keccak_v3_ntt8w64` | `keccak_multiblock_ntt8w64.xsa` | `v3-ntt64` |
| `ntt64bi` | `hls-keccak`, `hls-ntt` | `keccak_v3_ntt8w64bi` | `keccak_multiblock_ntt8w64bi.xsa` | `v3-ntt64bi` |
| `final` | `hls-keccak`, `hls-ntt` | `keccak_v3_ntt8w64bi_hp2` | `keccak_multiblock_ntt8w64bi_hp2.xsa` | `v3-ntt64bi` |

The script ([`build_system.tcl`](../hw/vivado/build_system.tcl)):

1. creates the project for the XC7Z020-1CLG484 with the ZedBoard board part;
2. builds the block design: for v1 from the ZedBoard preset, for v2 and later
   by sourcing [`keccak_f_system_bd.tcl`](../hw/vivado/keccak_f_system_bd.tcl)
   (PS7 with the preset, FCLK_CLK0 = 50 MHz, Keccak control on GP0 at
   `0x40000000`, Keccak master on HP0); the NTT variants add the NTT control
   at `0x40010000` and connect its master to HP0 (shared) or, for `final`,
   to HP2 through its own SmartConnect;
3. runs synthesis and implementation with the strategy
   `Congestion_SpreadLogic_high` up to the bitstream;
4. checks the NTT RTL inside the project, requires positive setup and hold
   slack, writes utilization reports in the `impl_1` run directory and exports
   the XSA with the bitstream.

A run takes 10–25 minutes and about 4 GB of RAM.

## 3. Vitis platform (BSP)

```sh
make platform                       # from the final XSA
make platform XSA=build/xsa/<other>.xsa
# = vitis -s sw/platform/create_platform.py <xsa>
```

The script creates the platform component `zedboard_platform` (standalone OS,
domain `standalone_ps7_cortexa9_0`) in the workspace `build/vitis` and builds
it. All designs place the Keccak IP in a cell called `keccak_f1600_0` at
`0x40000000`, so the BSP of one XSA serves every ELF. The register offsets of
each IP are taken from the drivers generated by HLS. The linker script is
[`sw/platform/lscript.ld`](../sw/platform/lscript.ld); `build.py` reserves
256 KiB of stack for the ML-DSA matrices.

## 4. ELFs

```sh
python3 sw/mldsa/build.py board <backend>... --mode {2,3,5}   # Cortex-A9 ELF
python3 sw/mldsa/build.py host  <backend>... --mode {2,3,5}   # run on the PC with the HLS model
```

Backends: `sw`, `v1`, `v2`, `v3`, `v3-ntt`, `v3-ntt64`, `v3-ntt64bi` (see
[benchmark.md](benchmark.md#backends)). The ELF is written to
`build/mldsa/board/mode<N>/<backend>/mldsa.elf`. Requirements per backend:
`v1`/`v2` read their register header from `build/xsa/keccak_f_system.xsa` /
`keccak_f_system_maxi.xsa`; `v3*` use the multiblock driver header (BSP or
`build/mldsa/hls-v3`); the NTT backends use the header of their NTT HLS
project. Options: `--bsp`, `--ldscript`, `--v3-header` to use files exported
elsewhere.

Other programs:

```sh
python3 sw/mldsa/build.py board v1 v2 --keccak-test   # build/mldsa/keccak-test/v{1,2}/keccak_test.elf
python3 sw/mldsa/build.py board v3 --squeeze-profile  # build/mldsa/squeeze-profile/mldsa.elf
```

## 5. Running on the board

Set the boot jumpers JP7–JP11 to GND (JTAG boot), power the board and connect
the micro-USB cable to **PROG (J17)**.

```sh
make program MODE=2                       # final system, ML-DSA-44
# or explicitly:
mkdir -p build/program/x
unzip -p build/xsa/<system>.xsa '*.bit' > build/program/x/system.bit
unzip -o build/xsa/<system>.xsa ps7_init.tcl -d build/program/x
xsdb sw/run_on_board.tcl <elf> build/program/x/system.bit build/program/x/ps7_init.tcl
```

`run_on_board.tcl` resets the system, programs the PL, runs `ps7_init` and
`ps7_post_config`, downloads the ELF and starts Cortex-A9 #0. Then move the
cable to **UART (J14)** (or use a second cable) and read the summary:

```sh
make uart MODE=2
# = python3 scripts/uart_read.py --seconds 400 --until END --out build/logs/mldsa_mode2_uart.txt
```

The summary appears about one minute after the start and is printed every two
seconds until the board is reset. Use the ELF of each level with the
bitstream of its backend. The first line of a capture may contain leftover
bytes of a previous run kept by the USB-UART bridge; the block that ends with
`END` is complete. If the port cannot be opened, add your user to the
`dialout` group.

The single-permutation test prints `PASS: 1001 states, all equal to the
original` and the time per permutation. The squeeze profile does not use the
UART:

```sh
xsdb sw/keccak_test/squeeze_profile/read_jtag.tcl build/mldsa/squeeze-profile/mldsa.elf <bit> <ps7_init.tcl>
```

## 6. Verification status of the scripts

- **HLS:** all seven scripts of section 1 were run from an empty `build/`
  directory with AMD 2026.1. C simulation and C/RTL co-simulation pass, and
  the co-simulated latencies equal those of the IPs used for the measurements
  (v1 67/376 cycles, v2 102/110, v3 401 for five SHAKE128 blocks with all 15
  transactions identical, NTT 882 / 626 / 628).
- **Host:** `make host-all` passes for all 21 combinations of level and backend.
- **Platform and ELFs:** `make platform` (from the final XSA) and `make elf`
  run without errors. The three rebuilt ELFs load exactly the same code and
  data as the measured ones; the only difference is the 20-byte GNU build ID,
  a hash that also covers the debug information and therefore the build path.
- **Vivado:** the measured systems `v3`, `ntt`, `ntt64`, `ntt64bi` and `final`
  were built by the same flow that `build_system.tcl` contains (same block
  design script, connections, strategy and checks). The v1 and v2 systems were
  originally created in the Vivado IDE; the v1/v2 branches of
  `build_system.tcl` have not yet been run end to end.
- **Board:** the measurements in [results/](../results) come from the
  bitstreams and ELFs built during the project; the final XSA and ELFs are
  published as release files with their SHA-256 sums.

# Keccak-f[1600] and NTT accelerators for ML-DSA on the ZedBoard

Hardware acceleration of **ML-DSA** (FIPS 204, the NIST post-quantum signature
standard) on the Zynq-7000 SoC of a Digilent ZedBoard. The unmodified
[pq-crystals reference implementation](https://github.com/pq-crystals/dilithium)
runs bare metal on the ARM Cortex-A9; two accelerators written in C++ with
**Vitis HLS** run in the programmable logic:

- **Keccak-f[1600]** (the permutation behind SHAKE128/SHAKE256), with a
  *multiblock* command that produces up to five SHAKE output blocks per start;
- the **NTT** of ML-DSA, forward and inverse, with eight butterfly units and a
  64-bit AXI master.

Only the bodies of `KeccakF1600_StatePermute`, `keccak_squeezeblocks`, `ntt`
and `invntt_tomont` are redirected to the hardware (two small patches applied
at build time). Every accelerated run produces **byte-for-byte the same
signatures** as the software, which the benchmark checks on 1000 messages.

## Results

Final system: Keccak-f v3 on port HP0 + forward/inverse NTT on port HP2,
PL clock 50 MHz, CPU 650 MHz. Mean time of one signature and one verification
over 1000 messages, all columns from the same run of the same ELF and bitstream
([logs](results/uart)):

| Level | Software sign / verify (µs) | Keccak only | + forward NTT | + forward and inverse NTT | Speed-up sign / verify |
|---|---:|---:|---:|---:|---:|
| ML-DSA-44 | 8 890 / 2 691 | 6 271 / 1 396 | 5 457 / 1 175 | **4 209 / 1 068** | **2.11× / 2.52×** |
| ML-DSA-65 | 14 876 / 4 458 | 10 592 / 2 153 | 9 387 / 1 856 | **7 204 / 1 696** | **2.06× / 2.63×** |
| ML-DSA-87 | 18 193 / 7 526 | 12 136 / 3 377 | 10 812 / 2 976 | **8 556 / 2 765** | **2.13× / 2.72×** |

The Keccak accelerator went through three interfaces before the NTT was added:

| Keccak-f version | Interface | One permutation (ns) | Five SHAKE128 blocks (µs) | ML-DSA-44 sign / verify (µs) |
|---|---|---:|---:|---:|
| Software (Cortex-A9, `-O3`) | – | 16 806 | 86.0 | 8 898 / 2 695 |
| v1 | AXI4-Lite, the CPU moves the 25 lanes | 38 280 | 193.6 | 13 233 / 4 824 |
| v2 | AXI4 master, state in DDR, one permutation per start | 4 947 | 26.6 | 6 510 / 1 516 |
| v3 | AXI4 master, up to five squeeze blocks per start | – | 18.1 | 6 274 / 1 399 |

One NTT on the FPGA costs 21.8–21.9 µs in both directions, copies and cache
maintenance included, against 45.9–46.5 µs (forward) and 48.4–48.5 µs (inverse)
on the Cortex-A9.

| System (50 MHz) | LUT | FF | BRAM | DSP | WNS / WHS (ns) |
|---|---:|---:|---:|---:|---:|
| Keccak v3 only | 13 675 | 10 577 | 1 | 0 | +5.270 / +0.038 |
| Final (Keccak v3 + NTT on HP2) | 21 746 (41 %) | 19 606 (18 %) | 18 | 66 | +3.396 / +0.015 |

Results for every intermediate system (32/64-bit NTT master, shared HP0
port), the methodology and the meaning of each UART line are in
[docs/benchmark.md](docs/benchmark.md) and [results/](results).

## Architecture

```
                 Zynq-7020 PS                                      PL, 50 MHz
 ┌────────────────────────────────────────┐        ┌───────────────────────────────────────┐
 │ Cortex-A9 #0, 650 MHz, bare metal      │ M_GP0  │ axi_smc ─┬─► keccak_f1600_0  0x4000_0000│
 │  ML-DSA reference + benchmark          ├───────►│          └─► ntt_transform_0 0x4001_0000│
 │  driver: sw/mldsa/board.c              │        │                                       │
 │                                        │ S_HP0  │ keccak_f1600_0 m_axi ─► axi_smc_1      │
 │ DDR controller ◄───────────────────────┤◄───────┤                                       │
 │                                        │ S_HP2  │ ntt_transform_0 m_axi ─► axi_smc_2     │
 │                ◄───────────────────────┤◄───────┤                                       │
 └────────────────────────────────────────┘        └───────────────────────────────────────┘
```

The CPU writes buffer addresses and parameters over AXI4-Lite (GP0), starts
the IP and polls `ap_done`. The IPs move data to and from DDR with burst
transfers on their own HP ports; the CPU flushes the data cache before the
start and invalidates it after `done`. Register maps and data formats:
[docs/benchmark.md](docs/benchmark.md#accelerator-interfaces).

## Repository layout

```
hw/hls/keccak/      Keccak-f HLS source (v2/v3 tops), v1 in v1_axilite/, testbenches, HLS scripts
hw/hls/ntt/         NTT HLS source (forward + inverse), testbench, RTL check; w32/ and w64/ are the forward-only versions
hw/vivado/          build_system.tcl (all systems), block design script, Digilent board files (submodule)
sw/mldsa/           ML-DSA benchmark, accelerator driver, patches for fips202.c and ntt.c, build script, host model
sw/keccak_test/     single-permutation test of v1/v2, SHAKE squeeze profiling (read over JTAG)
sw/platform/        Vitis platform (BSP) script and linker script
sw/run_on_board.tcl JTAG loader (bitstream + PS init + ELF)
scripts/            tool environment, USB-UART reader
results/            UART logs of every measurement, HLS and Vivado reports
third_party/        pq-crystals/dilithium (submodule, commit d35ba3fe54, unmodified)
```

## Requirements

- **Board:** Digilent ZedBoard (Zynq XC7Z020-1CLG484; tested on Rev. F), boot
  jumpers JP7–JP11 on GND (JTAG). One micro-USB cable is enough: PROG (J17) to
  program, UART (J14) to read the results.
- **AMD tools 2026.1:** Vivado, Vitis and Vitis HLS (the free edition covers the
  XC7Z020), with the cable drivers installed. The block design script checks the
  version and refuses other releases.
- **Host:** Linux, Python 3 with `pyserial` (`pip install -r requirements.txt`),
  a C/C++ compiler, `patch`, `unzip`.

Clone with the submodules:

```sh
git clone --recursive <repository-url>
cd mldsa-zedboard-accel
# or, in an existing clone: git submodule update --init
```

## Quick start

### 1. Functional test on the PC (no AMD tools)

The benchmark is compiled for the host with a register-level model of the HLS
code in place of the hardware. It signs and verifies 1000 messages with every
path and compares the signatures with the software ones:

```sh
make host          # final configuration, ML-DSA-44, -65 and -87
make host-all      # every backend (sw, v1, v2, v3, NTT variants)
```

Each run ends with `ML-DSA mode N, backend 3, 1000 messages: PASS (code 0)`.

### 2. Run the prebuilt release on the board

The release contains the final XSA (bitstream + PS initialisation) and the three
ELFs (one per security level). Only `xsdb` (part of Vivado/Vitis or Vivado Lab
Edition) and `pyserial` are needed. Download the release files into `dist/`:

```sh
source scripts/env.sh        # after: export XILINX_ROOT=/path/to/Xilinx/2026.1
(cd dist && sha256sum -c SHA256SUMS)
# cable on PROG (J17), board on
make program XSA=dist/keccak_multiblock_ntt8w64bi_hp2.xsa ELF=dist/mldsa_mode2_v3-ntt64bi.elf
# move the cable to UART (J14); the summary arrives after about one minute
make uart MODE=2
```

The program prints its summary every two seconds, so the reader can be
started after moving the cable. A run takes about one minute per level.

### 3. Build everything from source

```sh
export XILINX_ROOT=/path/to/Xilinx/2026.1
source scripts/env.sh
make hls          # Keccak v3 IP and bidirectional NTT IP (C sim, synthesis, co-sim, export)
make bitstream    # final system: block design, implementation, timing check, XSA (~15 min)
make platform     # standalone BSP for the Cortex-A9 from the XSA
make elf          # build/mldsa/board/mode{2,3,5}/v3-ntt64bi/mldsa.elf
make program MODE=2 && make uart MODE=2
```

The Vivado script writes the XSA only if setup and hold timing are met and if
the NTT RTL inside Vivado is identical to the co-simulated one. Every other
system of the tables above (v1, v2, v3, 32/64-bit NTT, shared HP0) is built
with the same scripts: see [docs/build.md](docs/build.md).

## Limitations

- Research prototype for performance evaluation. The accelerators and the
  driver are not designed against side-channel or fault attacks.
- The benchmark uses fixed, public seeds and signing randomness so that every
  backend performs the same work; its keys must not be used for anything else.
- One process owns the accelerators (no locking, polling instead of interrupts).

## License

MIT, see [LICENSE](LICENSE). Third-party code keeps its own license: the
ML-DSA reference implementation (`third_party/dilithium`, public domain /
Apache 2.0) and the Digilent board files (`hw/vivado/boards/digilent`).
The Keccak-f permutation in `hw/hls/keccak/keccak.cpp` is copied from the
public-domain code in the reference implementation, as stated in its header.
`sw/platform/lscript.ld` is the AMD standalone linker script (MIT).

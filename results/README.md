# Measured results

Raw outputs of the measurements on the ZedBoard (PL 50 MHz, CPU 650 MHz,
ELFs compiled with `-O3`), copied unchanged except for the host name removed
from the Vivado report headers. The tables in the [README](../README.md) and in
[docs/benchmark.md](../docs/benchmark.md) are taken from these files.

## `uart/`: UART captures

ML-DSA benchmark, 1000 messages per run, one file per level (`mode2/3/5` =
ML-DSA-44/65/87). Every run reports `PASS (code 0)`.

| Files | System (XSA) | ELF backend |
|---|---|---|
| `mldsa_mode{2,3,5}_v1_1000msg_uart.txt` | v1, `keccak_f_system.xsa` | `v1` |
| `mldsa_mode{2,3,5}_v2_1000msg_uart.txt` | v2, `keccak_f_system_maxi.xsa` | `v2` |
| `mldsa_mode{2,3,5}_v3_1000msg_uart.txt` | v3, `keccak_multiblock.xsa` | `v3` |
| `mldsa_mode{2,3,5}_v3_ntt8_1000msg_uart.txt` | v3 + forward NTT 32-bit, `keccak_multiblock_ntt8.xsa` | `v3-ntt` |
| `mldsa_mode{2,3,5}_v3_ntt8w64_1000msg_uart.txt` | v3 + forward NTT 64-bit, `keccak_multiblock_ntt8w64.xsa` | `v3-ntt64` |
| `mldsa_mode{2,3,5}_v3_ntt64bi_1000msg_uart.txt` | v3 + bidirectional NTT, shared HP0, `keccak_multiblock_ntt8w64bi.xsa` | `v3-ntt64bi` |
| `mldsa_mode{2,3,5}_v3_ntt64bi_hp2_1000msg_uart.txt` | **final**, NTT on HP2, `keccak_multiblock_ntt8w64bi_hp2.xsa` | `v3-ntt64bi` |
| `keccak_test_v1_uart.txt`, `keccak_test_v2_uart.txt` | v1 / v2, single-permutation test on 1001 states | `--keccak-test` |

The program prints its summary repeatedly; a capture contains several
identical copies. In some files the first line holds leftover bytes of the
previous run kept by the USB-UART bridge: the block from the title line to
`END` is complete.

## `squeeze_profile/`: phases of a five-block SHAKE128 call

Same profiling ELF (`build.py board v3 --squeeze-profile`), results read over
JTAG with `read_jtag.tcl`. `A1.log` and `A2.log`: v3-only bitstream, before
and after `B.log`: v3 + bidirectional NTT with shared HP0. `C_hp2.log`: final
system. Columns: total, state copy + flush, command and rate registers,
output flush, `nblocks` register, start → done, invalidate + copies (ns, mean
of 1000 calls, five batches).

## `implementation/`: Vivado reports

For each system: the design timing summary after routing
(`*_timing_summary.txt`) and the utilization after placement
(`*_utilization_placed.rpt`). The v1 report is not available because the same
project was later upgraded to v2.

## `hls/`: Vitis HLS reports

Synthesis and C/RTL co-simulation of the Keccak multiblock IP and of the
bidirectional NTT IP. `keccak_multiblock_cosim_transactions.rpt` lists the
latency of each of the 15 co-simulated calls (transactions 0–5 SHAKE128,
6–11 SHAKE256, 12 single permutation).

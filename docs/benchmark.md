# Benchmark methodology and accelerator interfaces

## What the benchmark measures

[`sw/mldsa/benchmark.c`](../sw/mldsa/benchmark.c) is a bare-metal program for
Cortex-A9 #0. It uses the public API of the reference implementation
(`crypto_sign_keypair`, `crypto_sign_signature`, `crypto_sign_verify`), compiled
directly from the submodule. One ELF corresponds to one ML-DSA level
(`--mode 2/3/5` = ML-DSA-44/65/87) and one backend.

1. **Deterministic inputs.** `randombytes` is replaced by a counter generator,
   so the key pair, the context and the signing randomness are fixed and public.
   Message `i` (0 ≤ i < 1000) is the text `ZedBoard ML-DSA benchmark` followed
   by the 4 little-endian bytes of `i`. Every signature restarts from the same
   signing randomness, so every backend runs the same number of rejection-loop
   iterations (on average 4.26, 5.25 and 3.83 for ML-DSA-44, 65 and 87, in line
   with FIPS 204). The averages over 1000 messages are therefore comparable
   across backends; a single message would not be representative.
2. **Software reference.** All 1000 messages are signed, then all 1000
   signatures are verified, with `fips202.c` and `ntt.c` unchanged. Each block is
   one timed interval; the printed value is the interval divided by 1000.
3. **Accelerated path(s).** The same two timed blocks are repeated with the
   hardware enabled. The NTT ELFs run three accelerated paths on the same
   bitstream: Keccak only, Keccak + forward NTT, Keccak + forward and inverse NTT.
4. **Checks.** Every accelerated signature must have the expected length, be
   identical byte for byte to the software one and be accepted; the first
   signature must be rejected after one byte of its message is changed.
5. **Microbenchmarks.** Five SHAKE128 output blocks (840 bytes) are produced
   1000 times from the same state, in software and with the selected backend,
   and the output buffer and all 25 lanes of the final state are compared. The
   NTT ELFs also transform 128 polynomials in each direction on the ARM and on
   the FPGA, compare all coefficients, and time the three phases of an FPGA call.

Times come from the ARM global timer (`XTime_GetTime`, 325 MHz, half the CPU
clock). They include transfers, cache flush/invalidate and polling. Key
generation, signature comparison, the negative check and UART output are
outside the timed intervals. The summary is printed again every two seconds
and ends with `END`, so it can be read after moving the USB cable from J17 to
J14.

The PL runs at 50 MHz and the CPU at 650 MHz in every system. The first
bitstream at 100 MHz could not be routed (congestion caused by the two
Keccak rounds per cycle); all systems use the implementation strategy
`Congestion_SpreadLogic_high`.

## Backends

| `build.py` backend | Path during signing and verification | Bitstream (XSA) |
|---|---|---|
| `sw` | Everything on the Cortex-A9 | any |
| `v1` | Every permutation through the AXI4-Lite IP: 50 writes + 50 reads of 32 bits | `keccak_f_system.xsa` |
| `v2` | Every permutation through the AXI4-master IP, state in an aligned DDR buffer | `keccak_f_system_maxi.xsa` |
| `v3` | Whole-block squeezes with the multiblock command, other permutations with the single command | `keccak_multiblock.xsa` |
| `v3-ntt` | v3 + forward NTT, 32-bit master, shared HP0 | `keccak_multiblock_ntt8.xsa` |
| `v3-ntt64` | v3 + forward NTT, 64-bit master, shared HP0 | `keccak_multiblock_ntt8w64.xsa` |
| `v3-ntt64bi` | v3 + forward and inverse NTT, 64-bit master | `keccak_multiblock_ntt8w64bi.xsa` (shared HP0) or `keccak_multiblock_ntt8w64bi_hp2.xsa` (final) |

For the hardware backends `build.py` applies
[`fips202.patch`](../sw/mldsa/fips202.patch) (and, for the NTT backends,
[`ntt.patch`](../sw/mldsa/ntt.patch)) to copies of the reference files in the
build directory. The patches add an early `if (flag) { hw_...(); return; }` at
the top of `KeccakF1600_StatePermute`, `keccak_squeezeblocks`, `ntt` and
`invntt_tomont`; the rest of ML-DSA is compiled unchanged from the submodule.
The driver is [`sw/mldsa/board.c`](../sw/mldsa/board.c).

## UART output

Example: final system, ML-DSA-44 ([log](../results/uart/mldsa_mode2_v3_ntt64bi_hp2_1000msg_uart.txt)).

```
Keccak v3 + bidirectional NTT (64-bit)
ML-DSA mode 2, backend 3, 1000 messages: PASS (code 0)
SHAKE128 5 blocks (ns/call, 1000 calls): SW 86158, selected 18069
SW mean sign 8890 us, verify 2691 us
NTT forward (ns/call, 128 calls): SW 45937, FPGA 21878
Keccak only mean sign 6271 us, verify 1396 us
NTT phases (ns/call): copy+flush 4401, start-to-done 14404, invalidate+copy 3124
NTT inverse (ns/call, 128 calls): SW 48531, FPGA 21905
Inverse phases (ns/call): copy+flush 4397, start-to-done 14404, invalidate+copy 3124
Keccak + forward mean sign 5457 us, verify 1175 us
selected mean sign 4209 us, verify 1068 us
END
```

| Line | Meaning |
|---|---|
| `... PASS (code 0)` | All checks passed; otherwise `FAIL` and an error code (below) |
| `SHAKE128 5 blocks` | Mean time to produce 840 bytes, software and selected backend |
| `SW mean sign/verify` | Software reference, mean over 1000 messages |
| `Keccak only` | Keccak accelerator, NTT in software (NTT ELFs) |
| `Keccak + forward` | Keccak + forward NTT in hardware, inverse in software |
| `selected` | All accelerators of the backend enabled |
| `NTT forward/inverse` | One transform on the ARM and on the FPGA (copies and cache included) |
| `NTT/Inverse phases` | FPGA call split into copy + flush, start → done, invalidate + copy |

Error codes: 1 key generation, 2 signature generation or length, 3 signature
differs from the software one or is rejected, 4 tampered message accepted,
5 SHAKE squeeze differs from the reference, 6 forward NTT differs,
7 inverse NTT differs.

## Accelerator interfaces

All IPs are generated by Vitis HLS: control and arguments on an AXI4-Lite
slave (`s_axi_control`), data on a 64-bit AXI4 master (`m_axi_gmem`, burst
transfers). The control register at offset `0x00` has `ap_start` in bit 0 and
`ap_done` in bit 1. The driver writes the arguments, sets `ap_start` and polls
`ap_done`; the interrupt output is not connected. The masters do not go
through the CPU caches, so the driver keeps dedicated 32-byte aligned DDR
buffers, flushes them before the start and invalidates them after `done`.

### Keccak-f v3 (`keccak_multiblock`, control at `0x40000000`)

| Offset | Argument | Meaning |
|---|---|---|
| `0x10`/`0x14` | `state` | DDR address of the 25 64-bit lanes (read at the start, written back at the end) |
| `0x1c`/`0x20` | `out` | DDR address of the output buffer (squeeze only) |
| `0x28` | `nblocks` | Number of output blocks; each one costs one permutation |
| `0x30` | `rate_lanes` | 21 for SHAKE128 (168 bytes), 17 for SHAKE256 (136 bytes) |
| `0x38` | `command` | 0 = one permutation in place, 1 = squeeze |

After permutation `b + 1`, lane `i` of the state is written to
`out[b * rate_lanes + i]`, little endian, as `store64` does in the reference.
The state is read once and written back once per start, so the fixed costs of
a call are paid once for up to five blocks. The driver output buffer holds
five SHAKE128 blocks (840 bytes, the `poly_uniform` request); longer requests
are split into several starts that keep the state in DDR. Co-simulation:
107 cycles for one permutation, 401 cycles for five SHAKE128 blocks
([report](../results/hls/keccak_multiblock_cosim_transactions.rpt)).

### NTT (`ntt_transform`, control at `0x40010000`)

| Offset | Argument | Meaning |
|---|---|---|
| `0x10`/`0x14` | `coefficients` | DDR address of one polynomial (1 KiB), transformed in place |
| `0x1c` | `inverse` | bit 0: 0 = `ntt`, 1 = `invntt_tomont` |

The 256 coefficients travel as 128 64-bit words: word `i` holds coefficient
`2i` in bits 0–31 and `2i+1` in bits 32–63, which is the memory layout of
`int32_t[256]`. Eight butterfly units (7 DSP each) process one group of eight
pairs every two cycles; the polynomial lives in two arrays of eight banks
(ping-pong between stages). The inverse runs the stages in the opposite order
and applies the factor 41978 (R²/256 mod q) while writing back, as
`invntt_tomont`. Co-simulation: 628 cycles per transform in both directions,
344 of them for the eight stages ([co-simulation](../results/hls/ntt_transform_cosim.rpt), [synthesis](../results/hls/ntt_transform_csynth.rpt)).

### Keccak-f v1 and v2 (`keccak_f1600`)

v1 exposes the state in the AXI4-Lite register file: lane `i` at
`0x100 + 8i` (low word) and `0x104 + 8i` (high word). v2 takes the DDR
address of the state at `0x10`/`0x14` and moves it with two bursts of 25
64-bit words. Both perform one permutation per start.

## Where the time goes

Counted on the PC on the same 1000 messages with an instrumented copy of the
reference (not part of this repository):

| Per operation | ML-DSA-44 | ML-DSA-65 | ML-DSA-87 |
|---|---:|---:|---:|
| Keccak-f permutations, sign / verify | 201 / 99 | 325 / 174 | 454 / 311 |
| Forward NTTs, sign / verify | 33 / 9 | 49 / 12 | 54 / 16 |
| Inverse NTTs, sign / verify | 47 / 4 | 83 / 6 | 86 / 8 |

With about 17 µs per permutation in software, Keccak is roughly 40 % of the
signing time and 60–70 % of the verification time (estimate), which bounds the
speed-up of Keccak acceleration alone (Amdahl). The measured gains of the NTT
match, within 3 %, the number of transforms times the saving per transform.

Phases of a five-block SHAKE128 call on the FPGA (ns, mean of 1000 calls,
[profiling logs](../results/squeeze_profile)):

| Phase | v3 only | v3 + NTT, shared HP0 | Final (NTT on HP2) |
|---|---:|---:|---:|
| state copy + flush | 953 | 952–953 | 953 |
| command + rate registers | 778–780 | 779–782 | 776–778 |
| output buffer flush | 1 633–1 634 | 1 633–1 634 | 1 633 |
| `nblocks` register | 425–426 | 425–426 | 425 |
| start → done | 10 725–10 726 | 13 025–13 027 | 10 724–10 746 |
| invalidate + copies | 3 883–3 887 | 3 857–3 863 | 3 866–3 888 |

The ARM work is the same on every bitstream. With the NTT master sharing the
SmartConnect in front of HP0, start → done grows by 2.3 µs although the NTT is
idle; giving the NTT its own SmartConnect and port HP2 removes the delay. In
the final system start → done (10.7 µs) still exceeds the 401 co-simulated
cycles (8.0 µs): real DDR latency and polling. The profiling ELF
(`build.py board v3 --squeeze-profile`) stores its results in memory;
[`read_jtag.tcl`](../sw/keccak_test/squeeze_profile/read_jtag.tcl) loads it
and reads them over JTAG.

## Intermediate systems

Mean over 1000 messages (µs), sign / verify, columns of a row from the same
run ([logs](../results/uart)). "+ NTT" is the selected path of the ELF.

| Level | System | Software | Keccak only | Keccak + forward | Keccak + forward + inverse |
|---|---|---:|---:|---:|---:|
| ML-DSA-44 | forward NTT, 32-bit, shared HP0 | 8 865 / 2 685 | 6 346 / 1 442 | 5 724 / 1 273 | – |
| | forward NTT, 64-bit, shared HP0 | 8 865 / 2 686 | 6 346 / 1 441 | 5 544 / 1 224 | – |
| | bidirectional NTT, shared HP0 | 8 891 / 2 689 | 6 372 / 1 445 | 5 583 / 1 231 | 4 368 / 1 128 |
| | **final, NTT on HP2** | 8 890 / 2 691 | 6 271 / 1 396 | 5 457 / 1 175 | **4 209 / 1 068** |
| ML-DSA-65 | forward NTT, 32-bit, shared HP0 | 14 853 / 4 451 | 10 724 / 2 230 | 9 819 / 2 006 | – |
| | forward NTT, 64-bit, shared HP0 | 14 855 / 4 451 | 10 725 / 2 229 | 9 556 / 1 941 | – |
| | bidirectional NTT, shared HP0 | 14 876 / 4 458 | 10 752 / 2 238 | 9 582 / 1 948 | 7 454 / 1 793 |
| | **final, NTT on HP2** | 14 876 / 4 458 | 10 592 / 2 153 | 9 387 / 1 856 | **7 204 / 1 696** |
| ML-DSA-87 | forward NTT, 32-bit, shared HP0 | 18 206 / 7 521 | 12 371 / 3 526 | 11 364 / 3 223 | – |
| | forward NTT, 64-bit, shared HP0 | 18 205 / 7 522 | 12 370 / 3 525 | 11 070 / 3 135 | – |
| | bidirectional NTT, shared HP0 | 18 191 / 7 520 | 12 357 / 3 523 | 11 069 / 3 137 | 8 868 / 2 930 |
| | **final, NTT on HP2** | 18 193 / 7 526 | 12 136 / 3 377 | 10 812 / 2 976 | **8 556 / 2 765** |

One forward NTT on the FPGA: 28.0 µs with the 32-bit master (start → done
20.2 µs), 22.6 µs with the 64-bit master (14.7 µs), 21.9 µs in the final
system (14.4 µs).

Keccak-only systems, sign / verify (µs), software column from the v3 run:

| Level | Software | v1 | v2 | v3 |
|---|---:|---:|---:|---:|
| ML-DSA-44 | 8 898 / 2 695 | 13 233 / 4 824 | 6 510 / 1 516 | 6 274 / 1 399 |
| ML-DSA-65 | 14 876 / 4 458 | 21 872 / 8 193 | 11 037 / 2 393 | 10 589 / 2 153 |
| ML-DSA-87 | 18 219 / 7 527 | 28 003 / 14 217 | 12 793 / 3 823 | 12 176 / 3 384 |

Implementation of each system at 50 MHz ([reports](../results/implementation)):

| System | LUT | FF | BRAM | DSP | WNS / WHS (ns) |
|---|---:|---:|---:|---:|---:|
| v1 | 8 593 | 5 386 | 2 | 0 | +4.859 / +0.044 |
| v2 | 11 851 | 8 068 | 1 | 0 | +2.302 / +0.024 |
| v3 | 13 675 | 10 577 | 1 | 0 | +5.270 / +0.038 |
| v3 + forward NTT, 32-bit | 22 376 | 22 314 | 17.5 | 56 | +1.533 / +0.024 |
| v3 + forward NTT, 64-bit | 22 402 | 22 500 | 18 | 56 | +2.321 / +0.016 |
| v3 + bidirectional NTT, shared HP0 | 23 354 | 23 060 | 18 | 66 | +1.871 / +0.007 |
| **final, NTT on HP2** | **21 746** | **19 606** | **18** | **66** | **+3.396 / +0.015** |

The v1 figures come from the original implementation report, which was
overwritten when the same project was upgraded to v2.

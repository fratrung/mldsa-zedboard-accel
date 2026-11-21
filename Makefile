# Reproduce the final system: Keccak-f v3 (HP0) + forward/inverse NTT (HP2), 50 MHz.
#
#   make host                 functional test on the PC (no AMD tools needed)
#   source scripts/env.sh     then, for every other target:
#   make hls                  HLS IPs: Keccak multiblock and bidirectional NTT
#   make bitstream            Vivado: block design, bitstream, XSA
#   make platform             Vitis: standalone BSP from the XSA
#   make elf                  ML-DSA-44/65/87 benchmark ELFs for the Cortex-A9
#   make program MODE=2       load bitstream + ELF over JTAG (cable on J17)
#   make uart MODE=2          read the result from the USB-UART (cable on J14)
#
# Other systems: make bitstream VARIANT=v3 (see hw/vivado/build_system.tcl).
# Prebuilt files: make program XSA=dist/<file>.xsa ELF=dist/<file>.elf

MODE    ?= 2
MODES   ?= 2 3 5
VARIANT ?= final
XSA     ?= build/xsa/keccak_multiblock_ntt8w64bi_hp2.xsa
ELF     ?= build/mldsa/board/mode$(MODE)/v3-ntt64bi/mldsa.elf
PROGRAM := build/program/$(notdir $(basename $(XSA)))
PYTHON  ?= python3

.PHONY: all host host-all hls hls-keccak hls-ntt hls-v1 hls-v2 hls-ntt32 hls-ntt64 verify-keccak bitstream platform elf program uart clean

all: hls bitstream platform elf

host:
	for m in $(MODES); do $(PYTHON) sw/mldsa/build.py host v3-ntt64bi --mode $$m || exit 1; done

host-all:
	for m in $(MODES); do $(PYTHON) sw/mldsa/build.py host sw v1 v2 v3 v3-ntt v3-ntt64 v3-ntt64bi --mode $$m || exit 1; done

hls: hls-keccak hls-ntt

hls-keccak:
	mkdir -p build/hls/keccak_multiblock
	cd build/hls/keccak_multiblock && vitis-run --mode hls --tcl ../../../hw/hls/keccak/export_multiblock.tcl

verify-keccak:
	mkdir -p build/hls/verify_multiblock
	cd build/hls/verify_multiblock && vitis-run --mode hls --tcl ../../../hw/hls/keccak/verify_multiblock.tcl

hls-ntt:
	mkdir -p build/hls/ntt_bidir
	cd build/hls/ntt_bidir && vitis-run --mode hls --tcl ../../../hw/hls/ntt/verify_bidir.tcl

# IPs of the earlier systems (VARIANT=v1, v2, ntt, ntt64)
hls-v1:
	mkdir -p build/hls/keccak_v1
	cd build/hls/keccak_v1 && vitis-run --mode hls --tcl ../../../hw/hls/keccak/export_v1.tcl

hls-v2:
	mkdir -p build/hls/keccak_v2
	cd build/hls/keccak_v2 && vitis-run --mode hls --tcl ../../../hw/hls/keccak/export_v2.tcl

hls-ntt32:
	mkdir -p build/hls/ntt_w32
	cd build/hls/ntt_w32 && vitis-run --mode hls --tcl ../../../hw/hls/ntt/w32/verify_ntt.tcl

hls-ntt64:
	mkdir -p build/hls/ntt_w64
	cd build/hls/ntt_w64 && vitis-run --mode hls --tcl ../../../hw/hls/ntt/w64/verify_ntt.tcl

bitstream:
	mkdir -p build/vivado/$(VARIANT)_logs
	cd build/vivado/$(VARIANT)_logs && vivado -mode batch -source ../../../hw/vivado/build_system.tcl -tclargs $(VARIANT)

platform:
	vitis -s sw/platform/create_platform.py $(abspath $(XSA))

elf:
	for m in $(MODES); do $(PYTHON) sw/mldsa/build.py board v3-ntt64bi --mode $$m || exit 1; done

program:
	mkdir -p $(PROGRAM)
	unzip -p $(XSA) '*.bit' > $(PROGRAM)/system.bit
	unzip -o -q $(XSA) ps7_init.tcl -d $(PROGRAM)
	xsdb sw/run_on_board.tcl $(ELF) $(PROGRAM)/system.bit $(PROGRAM)/ps7_init.tcl

uart:
	mkdir -p build/logs
	$(PYTHON) scripts/uart_read.py --seconds 400 --until END --out build/logs/mldsa_mode$(MODE)_uart.txt

clean:
	rm -rf build

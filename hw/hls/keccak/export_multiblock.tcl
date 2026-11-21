# Keccak-f[1600] v3 (multiblock squeeze): synthesis and export as a Vivado IP.
# This is the IP used by every v3 bitstream, including the final one. The
# C simulation and C/RTL co-simulation of the same top are in
# verify_multiblock.tcl.
#   (cd build/hls && vitis-run --mode hls --tcl ../../hw/hls/keccak/export_multiblock.tcl)
# IP:     build/mldsa/hls-v3/multiblock/solution/impl/ip
# Driver: .../impl/ip/drivers/keccak_multiblock_v1_0/src/xkeccak_multiblock_hw.h
set root [file normalize [file join [file dirname [info script]] ../../..]]
open_project [file join $root build mldsa hls-v3 multiblock]
set_top keccak_multiblock
add_files [file join $root hw hls keccak keccak.cpp]
open_solution -flow_target vivado solution
set_part xc7z020clg484-1
create_clock -period 10
csynth_design
export_design -format ip_catalog
exit

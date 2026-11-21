# Keccak-f[1600] v2 (AXI4 master: one permutation per start, state in DDR).
# C simulation, synthesis, C/RTL co-simulation and export as a Vivado IP.
#   (cd build/hls && vitis-run --mode hls --tcl ../../hw/hls/keccak/export_v2.tcl)
# IP: build/mldsa/hls-v2/keccak_f1600/solution/impl/ip
set src [file dirname [file normalize [info script]]]
set root [file normalize [file join $src ../../..]]
open_project [file join $root build mldsa hls-v2 keccak_f1600]
set_top keccak_f1600
add_files [file join $src keccak.cpp]
add_files -tb [file join $src keccak_tb.cpp] -cflags "-I$src"
add_files -tb [file join $src tb_ref.c] -cflags "-I$src"
open_solution -reset -flow_target vivado solution
set_part xc7z020clg484-1
create_clock -period 10
csim_design
csynth_design
cosim_design -rtl verilog -tool xsim
export_design -format ip_catalog
exit

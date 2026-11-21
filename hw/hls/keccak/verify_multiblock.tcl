set src [file dirname [file normalize [info script]]]
set root [file normalize [file join $src ../../..]]
open_project [file join $root build mldsa hls-verify-v3 multiblock]
set_top keccak_multiblock
add_files [file join $src keccak.cpp]
add_files -tb [file join $src multiblock_tb.cpp] -cflags "-I$src"
add_files -tb [file join $src tb_ref.c] -cflags "-I$src"
open_solution -flow_target vivado solution
set_part xc7z020clg484-1
create_clock -period 10
csim_design
csynth_design
cosim_design -rtl verilog -tool xsim
exit

# Measured version with a 64-bit AXI master (bitstream keccak_multiblock_ntt8w64).
set src [file dirname [file normalize [info script]]]
set root [file normalize [file join $src ../../../..]]
set ref [file join $root third_party dilithium ref]
set project [file join $root build ntt eight_lane_w64_hls]
open_project $project
set_top ntt_forward
add_files [file join $src ntt_forward.cpp]
add_files -tb [file join $src ntt_tb.cpp] -cflags "-I$src"
foreach f {ntt.c reduce.c} {
    add_files -tb [file join $ref $f] -cflags "-I$ref"
}
open_solution -reset -flow_target vivado solution
set_part xc7z020clg484-1
create_clock -period 20
csim_design
csynth_design
cosim_design -rtl verilog -tool xsim
export_design -format ip_catalog
exec python3 [file join $src .. check_rtl.py] [file join $project solution] [file join $project solution impl ip hdl verilog]
exit

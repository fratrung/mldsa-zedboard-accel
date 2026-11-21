# Build one ZedBoard system (block design, bitstream, XSA) in batch mode.
#
#   mkdir -p build/vivado/<variant>_logs && cd build/vivado/<variant>_logs
#   vivado -mode batch -source ../../../hw/vivado/build_system.tcl -tclargs <variant>
#
# Variants (all at FCLK_CLK0 = 50 MHz, strategy Congestion_SpreadLogic_high):
#   v1           Keccak-f v1, AXI4-Lite only, control on GP0          -> keccak_f_system.xsa
#   v2           Keccak-f v2, AXI4 master on HP0                      -> keccak_f_system_maxi.xsa
#   v3           Keccak-f v3 multiblock on HP0                        -> keccak_multiblock.xsa
#   ntt          v3 + forward NTT, 32-bit master, shared HP0          -> keccak_multiblock_ntt8.xsa
#   ntt64        v3 + forward NTT, 64-bit master, shared HP0          -> keccak_multiblock_ntt8w64.xsa
#   ntt64bi      v3 + forward/inverse NTT, 64-bit, shared HP0         -> keccak_multiblock_ntt8w64bi.xsa
#   final        v3 on HP0 + forward/inverse NTT, 64-bit, on HP2      -> keccak_multiblock_ntt8w64bi_hp2.xsa
#                (alias: ntt64bi_hp2)
#
# Required HLS IPs (see README.md):
#   v1: hw/hls/keccak/export_v1.tcl   v2: hw/hls/keccak/export_v2.tcl
#   v3 and all NTT variants: hw/hls/keccak/export_multiblock.tcl, plus
#   ntt: hw/hls/ntt/w32/verify_ntt.tcl   ntt64: hw/hls/ntt/w64/verify_ntt.tcl
#   ntt64bi, final: hw/hls/ntt/verify_bidir.tcl
#
# Address map: Keccak control at 0x40000000 (64 KiB), NTT control at
# 0x40010000 (64 KiB), both on M_AXI_GP0. The AXI masters see the DDR at
# 0x00000000 (512 MiB). The XSA is written only if setup and hold timing are
# met and, for NTT variants, if the NTT RTL inside Vivado is the co-simulated one.
set root [file normalize [file join [file dirname [info script]] ../..]]
set variant [expr {[llength $argv] ? [lindex $argv 0] : ""}]
if {$variant eq "ntt64bi_hp2"} {set variant final}
#            project                  XSA                              NTT HLS project       HLS IP of Keccak
array set names {
    v1      {keccak_v1               keccak_f_system                  {}                    hls-v1/keccak_f1600}
    v2      {keccak_v2               keccak_f_system_maxi             {}                    hls-v2/keccak_f1600}
    v3      {keccak_v3               keccak_multiblock                {}                    hls-v3/multiblock}
    ntt     {keccak_v3_ntt8          keccak_multiblock_ntt8           eight_lane_hls        hls-v3/multiblock}
    ntt64   {keccak_v3_ntt8w64       keccak_multiblock_ntt8w64        eight_lane_w64_hls    hls-v3/multiblock}
    ntt64bi {keccak_v3_ntt8w64bi     keccak_multiblock_ntt8w64bi      eight_lane_bidir_hls  hls-v3/multiblock}
    final   {keccak_v3_ntt8w64bi_hp2 keccak_multiblock_ntt8w64bi_hp2  eight_lane_bidir_hls  hls-v3/multiblock}
}
if {![info exists names($variant)]} {
    error "Expected one argument: [lsort [array names names]] (or ntt64bi_hp2)"
}
lassign $names($variant) project_name xsa_name ntt_hls keccak_hls
set with_ntt [expr {$ntt_hls ne ""}]
set project_dir [file join $root build vivado $project_name]
set repo [file join $root build mldsa $keccak_hls solution impl ip]
if {![file isdirectory $repo]} {error "Missing Keccak HLS IP: $repo (run the HLS step first)"}

set_param board.repoPaths [list [file join $root hw vivado boards digilent new board_files]]
create_project $project_name $project_dir -part xc7z020clg484-1
set_property board_part digilentinc.com:zedboard:part0:1.1 [current_project]
set repos [list $repo]
if {$with_ntt} {
    set top [expr {$variant in {ntt64bi final} ? "ntt_transform" : "ntt_forward"}]
    set cell ${top}_0
    set ntt_solution [file join $root build ntt $ntt_hls solution]
    set rtl_check [file join $root hw hls ntt check_rtl.py]
    puts [exec python3 $rtl_check $ntt_solution [file join $ntt_solution impl ip hdl verilog] --top $top]
    lappend repos [file join $ntt_solution impl ip]
}
set_property ip_repo_paths $repos [current_project]
update_ip_catalog

if {$variant eq "v1"} {
    # v1 has no AXI master: ZedBoard preset, the IP on GP0 through a
    # SmartConnect, clock and reset from the PS. Same cell names as below.
    set ::keccak_vlnv xilinx.com:hls:keccak_f1600:1.0
    create_bd_design keccak_f_system
    set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0]
    apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
        -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} $ps
    set_property -dict [list CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {50} CONFIG.PCW_USE_M_AXI_GP0 {1}] $ps
    create_bd_cell -type ip -vlnv $::keccak_vlnv keccak_f1600_0
    create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 axi_smc
    set_property CONFIG.NUM_SI {1} [get_bd_cells axi_smc]
    create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_ps7_0_50M
    connect_bd_intf_net [get_bd_intf_pins processing_system7_0/M_AXI_GP0] [get_bd_intf_pins axi_smc/S00_AXI]
    connect_bd_intf_net [get_bd_intf_pins axi_smc/M00_AXI] [get_bd_intf_pins keccak_f1600_0/s_axi_control]
    connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
        [get_bd_pins processing_system7_0/M_AXI_GP0_ACLK] [get_bd_pins axi_smc/aclk] \
        [get_bd_pins rst_ps7_0_50M/slowest_sync_clk] [get_bd_pins keccak_f1600_0/ap_clk]
    connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] [get_bd_pins rst_ps7_0_50M/ext_reset_in]
    connect_bd_net [get_bd_pins rst_ps7_0_50M/peripheral_aresetn] \
        [get_bd_pins axi_smc/aresetn] [get_bd_pins keccak_f1600_0/ap_rst_n]
    assign_bd_address -offset 0x40000000 -range 64K \
        -target_address_space [get_bd_addr_spaces processing_system7_0/Data] \
        [get_bd_addr_segs keccak_f1600_0/s_axi_control/Reg]
    validate_bd_design
    save_bd_design
} else {
    # v2 and later: the block design saved with write_bd_tcl (PS7 with the
    # ZedBoard preset, GP0 -> axi_smc -> IP control, IP master -> axi_smc_1 -> HP0).
    set ::keccak_vlnv [expr {$variant eq "v2" ? "xilinx.com:hls:keccak_f1600:1.0"
                                             : "xilinx.com:hls:keccak_multiblock:1.0"}]
    source [file join $root hw vivado keccak_f_system_bd.tcl]
}
if {[get_property VLNV [get_bd_cells keccak_f1600_0]] ne $::keccak_vlnv} {
    error "Keccak IP $::keccak_vlnv missing from the block design"
}
set bd [get_files keccak_f_system.bd]
if {$with_ntt} {
    create_bd_cell -type ip -vlnv xilinx.com:hls:${top}:1.0 $cell
    set_property CONFIG.NUM_MI 2 [get_bd_cells axi_smc]
    connect_bd_intf_net [get_bd_intf_pins axi_smc/M01_AXI] [get_bd_intf_pins $cell/s_axi_control]
    if {$variant eq "final"} {
        # The NTT gets its own SmartConnect and port S_AXI_HP2, so Keccak keeps
        # HP0 for itself as in v3. HP0/HP1 and HP2/HP3 enter the DDR controller
        # on different ports.
        set_property CONFIG.PCW_USE_S_AXI_HP2 {1} [get_bd_cells processing_system7_0]
        create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 axi_smc_2
        set_property CONFIG.NUM_SI {1} [get_bd_cells axi_smc_2]
        connect_bd_intf_net [get_bd_intf_pins $cell/m_axi_gmem] [get_bd_intf_pins axi_smc_2/S00_AXI]
        connect_bd_intf_net [get_bd_intf_pins axi_smc_2/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_HP2]
        connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins axi_smc_2/aclk]
        connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins processing_system7_0/S_AXI_HP2_ACLK]
        connect_bd_net [get_bd_pins rst_ps7_0_50M/peripheral_aresetn] [get_bd_pins axi_smc_2/aresetn]
        set ntt_ddr [get_bd_addr_segs processing_system7_0/S_AXI_HP2/HP2_DDR_LOWOCM]
    } else {
        set_property CONFIG.NUM_SI 2 [get_bd_cells axi_smc_1]
        connect_bd_intf_net [get_bd_intf_pins $cell/m_axi_gmem] [get_bd_intf_pins axi_smc_1/S01_AXI]
        set ntt_ddr [get_bd_addr_segs processing_system7_0/S_AXI_HP0/HP0_DDR_LOWOCM]
    }
    connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins $cell/ap_clk]
    connect_bd_net [get_bd_pins rst_ps7_0_50M/peripheral_aresetn] [get_bd_pins $cell/ap_rst_n]
    assign_bd_address -offset 0x40010000 -range 64K -target_address_space [get_bd_addr_spaces processing_system7_0/Data] [get_bd_addr_segs $cell/s_axi_control/Reg]
    assign_bd_address -offset 0 -range 512M -target_address_space [get_bd_addr_spaces $cell/Data_m_axi_gmem] $ntt_ddr
    validate_bd_design
    save_bd_design
}
set wrapper [make_wrapper -files $bd -top]
add_files -norecurse $wrapper
set_property top keccak_f_system_wrapper [current_fileset]
set_property strategy Congestion_SpreadLogic_high [get_runs impl_1]
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Implementation incomplete: [get_property STATUS [get_runs impl_1]]"
}
open_run impl_1
if {$with_ntt} {
    set gen [file join $project_dir ${project_name}.gen sources_1 bd keccak_f_system ipshared]
    set ntt_top [glob -directory $gen */hdl/verilog/${top}.v]
    if {[llength $ntt_top] != 1} {error "Ambiguous NTT RTL in the Vivado project"}
    puts [exec python3 $rtl_check $ntt_solution [file dirname [lindex $ntt_top 0]] --top $top]
}
foreach delay {max min} {
    set worst [get_timing_paths -delay_type $delay -max_paths 1]
    if {![llength $worst] || [get_property SLACK $worst] < 0} {
        error "Timing constraints ($delay delay) not met"
    }
}
set impl [get_property DIRECTORY [get_runs impl_1]]
report_utilization -file [file join $impl utilization_routed.rpt]
report_utilization -hierarchical -file [file join $impl utilization_hier_routed.rpt]
file mkdir [file join $root build xsa]
write_hw_platform -fixed -include_bit -force [file join $root build xsa ${xsa_name}.xsa]
close_project

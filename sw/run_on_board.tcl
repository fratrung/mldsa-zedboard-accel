# =============================================================================
# run_on_board.tcl - load bitstream + ELF on the ZedBoard over JTAG (xsdb)
#
# Usage:
#   xsdb sw/run_on_board.tcl <app.elf> <bitstream.bit> <ps7_init.tcl>
#
# Requirements: board powered on, USB cable on PROG (J17), boot-mode jumpers
# JP7-JP11 on GND (JTAG mode), cable drivers installed.
#
# Sequence (the same one the Vitis IDE runs on "Run"):
#   1. reset of the whole system
#   2. PL configuration with the bitstream
#   3. ps7_init: PS clocks, DDR and MIO (done by the FSBL in a normal boot;
#      over JTAG it is done here)
#   4. ps7_post_config: enable the PS<->PL level shifters
#   5. download of the ELF into DDR and start on Cortex-A9 #0
# =============================================================================
if {[llength $argv] != 3} {
    puts "Usage: xsdb run_on_board.tcl <app.elf> <bitstream.bit> <ps7_init.tcl>"
    exit 1
}
lassign $argv elf bit ps7init

connect
puts "JTAG targets:"
puts [targets]

targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000

fpga -file $bit

targets -set -nocase -filter {name =~ "APU*"}
source $ps7init
ps7_init
ps7_post_config

targets -set -nocase -filter {name =~ "*A9*#0"}
dow $elf
con
puts "INFO: program started on Cortex-A9 #0"
disconnect
exit

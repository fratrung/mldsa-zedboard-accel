# xsdb read_jtag.tcl app.elf system.bit ps7_init.tcl
if {[llength $argv] != 3} {error "Expected ELF, bitstream and ps7_init.tcl"}
lassign $argv elf bit init
set symbols [exec nm -n $elf]
foreach name {profile_done profile_result} {
    set pattern [format {^([0-9a-fA-F]+) [A-Za-z] %s$} $name]
    if {![regexp -line $pattern $symbols -> hex]} {
        error "Missing ELF symbol $name"
    }
    set addr($name) [scan $hex %x]
}
connect
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000
fpga -file $bit
targets -set -nocase -filter {name =~ "APU*"}
source $init
ps7_init
ps7_post_config
targets -set -nocase -filter {name =~ "*A9*#0"}
dow $elf
bpadd -addr $addr(profile_done) -type hw
con -block -timeout 30
set data [mrd -value $addr(profile_result) 37]
if {[llength $data] != 37} {error "Incomplete result"}
puts "bitstream $bit"
puts "ELF [exec sha256sum $elf]"
puts "software_ns [lindex $data 1]"
puts "batch total_ns state_copy_flush_ns command_rate_ns out_flush_ns nblocks_ns start_done_ns invalidate_copy_ns"
for {set b 0} {$b < 5} {incr b} {
    set first [expr {2 + 7*$b}]
    puts "[expr {$b+1}] [join [lrange $data $first [expr {$first+6}]] { }]"
}
if {[lindex $data 0] != 1} {error "Reference comparison failed"}
puts "PASS"
disconnect
exit

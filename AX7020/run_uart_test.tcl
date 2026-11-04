set root [file normalize [file dirname [info script]]]
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000
fpga -file "$root/design_1_wrapper.bit"
targets -set -nocase -filter {name =~ "*A9*#0"}
source "$root/ps7_init.tcl"
ps7_init
ps7_post_config
dow "$root/tinycnn_app.elf"
con
puts "PROGRAM_RUN_OK"
after 5000
targets -set -nocase -filter {name =~ "APU*"}
configparams force-mem-access 1
set remaining [expr [mrd -value 0x02000044]]
if {$remaining == 0} {
    puts "CALIBRATION_READY: show gestures now."
} else {
    puts "CALIBRATION_NOT_READY: remaining=$remaining. Keep ROI empty; check camera input."
}
disconnect
exit

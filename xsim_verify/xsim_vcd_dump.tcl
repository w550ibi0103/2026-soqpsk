open_vcd dut_full_dump.vcd
set sigs [get_objects -r /tb_xsim_verify_sps8_lut/dut/*]
puts "found [llength $sigs] objects"
log_vcd $sigs
run all
close_vcd
quit

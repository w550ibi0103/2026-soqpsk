## One-off: csim_design only, using verify_tmp/ with VERIFY_FIXED_SPS_SEL=1
## (temporarily flipped from its usual 0), to get an SPS=8 golden CSV to
## diff against xsim_verify/output_waveform_xsim.csv. hls_prj_verify/ is
## deleted after use per the established verify_tmp/ workflow.
open_project -reset hls_prj_verify
add_files "verify_tmp/src/top.cpp" -cflags "-I./verify_tmp/src"
add_files "verify_tmp/src/top.h"
add_files -tb "verify_tmp/tb/tb_top.cpp" -cflags "-I./verify_tmp/src -Wno-unknown-pragmas" -csimflags "-Wno-unknown-pragmas"

set_top tfm_modulator

open_solution -reset "solution1" -flow_target vivado
set_part {xczu9eg-ffvb1156-2-e}
create_clock -period 6.25 -name default

csim_design
exit

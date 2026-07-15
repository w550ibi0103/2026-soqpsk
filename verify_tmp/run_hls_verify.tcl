############################################################
## Throwaway cosim-verification project. sps_sel is pinned to a
## compile-time constant here (see verify_tmp/src/top.h) purely so
## Vitis HLS cosim's ap_ctrl_none restriction is satisfied. This does
## NOT reflect the real IP (hls_prj/), which keeps sps_sel as a
## runtime s_axilite register. Delete verify_tmp/ and hls_prj_verify/
## once done confirming RTL == C model.
############################################################
open_project -reset hls_prj_verify
add_files "verify_tmp/src/top.cpp" -cflags "-I./verify_tmp/src"
add_files "verify_tmp/src/top.h"
add_files -tb "verify_tmp/tb/tb_top.cpp" -cflags "-I./verify_tmp/src -Wno-unknown-pragmas" -csimflags "-Wno-unknown-pragmas"

set_top tfm_modulator

open_solution -reset "solution1" -flow_target vivado
set_part {xczu9eg-ffvb1156-2-e}
create_clock -period 10 -name default

csim_design
csynth_design
cosim_design

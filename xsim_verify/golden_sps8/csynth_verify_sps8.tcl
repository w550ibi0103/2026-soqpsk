## Synthesize verify_tmp/ (sps_sel pinned to compile-time constant 1 = SPS=8,
## HW_DEBUG_MODE off) to get RTL with NO s_axi_CTRL bundle at all -- so there
## is no AXI4-Lite write and no possibility of the sps_sel race found in the
## real hls_prj/ interface. Run from repo root.
open_project -reset hls_prj_verify
add_files "verify_tmp/src/top.cpp" -cflags "-I./verify_tmp/src"
add_files "verify_tmp/src/top.h"
add_files -tb "verify_tmp/tb/tb_top.cpp" -cflags "-I./verify_tmp/src -Wno-unknown-pragmas -DTEST_SPS_SEL=1" -csimflags "-Wno-unknown-pragmas"

set_top tfm_modulator

open_solution -reset "solution1" -flow_target vivado
set_part {xczu9eg-ffvb1156-2-e}
create_clock -period 6.25 -name default

csim_design
csynth_design
exit

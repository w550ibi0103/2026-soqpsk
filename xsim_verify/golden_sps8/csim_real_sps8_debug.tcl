## Throwaway: csim_design using the REAL src/top.cpp (not verify_tmp), with
## -DTEST_SPS_SEL=1 (SPS=8) and HW_DEBUG_MODE's stdout Alpha log intact, to
## get a precise per-bit Alpha reference for diagnosing the xsim divergence.
## Does not touch the official hls_prj/ project (separate project name).
## Run from the repo root: vitis_hls -f xsim_verify/golden_sps8/csim_real_sps8_debug.tcl
open_project -reset hls_prj_debug_sps8
add_files "src/top.cpp" -cflags "-I./src"
add_files "src/top.h"
add_files -tb "tb/tb_top.cpp" -cflags "-I./src -Wno-unknown-pragmas -DTEST_SPS_SEL=1" -csimflags "-Wno-unknown-pragmas"

set_top tfm_modulator

open_solution -reset "solution1" -flow_target vivado
set_part {xczu9eg-ffvb1156-2-e}
create_clock -period 6.25 -name default

csim_design
exit

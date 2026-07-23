############################################################
## Quick re-check: does cosim_design still reject the real
## ap_ctrl_none + s_axilite(sps_sel) interface? (expected: fails fast
## with "non-self-synchronizing top I/O sps_sel" - see
## memory project_cosim_ap_ctrl_none_limitation.md)
############################################################
open_project hls_prj
set_top tfm_modulator
open_solution "solution1" -flow_target vivado
cosim_design

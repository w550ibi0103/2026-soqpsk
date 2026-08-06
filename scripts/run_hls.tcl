############################################################
## This file is generated automatically by Vitis HLS.
## Please DO NOT edit it.
## Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
############################################################
# 1. Project Setup
# -reset: Overwrites existing project directory to ensure a clean build
open_project -reset hls_prj

# 2. Add Source Files and Testbench
# Ensure paths are relative to the location where this script is executed
# -I means "Include Directory"
add_files "src/top.cpp" -cflags "-I./src"
add_files "src/top.h"
add_files -tb "tb/tb_top.cpp" -cflags "-I./src -Wno-unknown-pragmas" -csimflags "-Wno-unknown-pragmas"

# 3. Set Top-Level Function
# This must match the function name in your C++ source code
set_top tfm_modulator

# 4. Solution Configuration
# -flow_target: Set to 'vivado' for standalone RTL IP generation
open_solution -reset "solution1" -flow_target vivado
set_part {xczu9eg-ffvb1156-2-e}
# 160 MSPS I/Q output rate required (customer spec: 20 Mbit/s bit_in @ SPS=8;
# one I/Q pair is written every ap_clk cycle regardless of SPS, so ap_clk itself
# must be >= 160MHz). Confirmed via a dedicated 6.25ns-target csynth run
# (2026-07-23): estimated critical path 4.552ns, achieved II=1, Trip Count=inf.
create_clock -period 6.25 -name default

# 5. Implementation Flow
# Source optimization directives if the file exists
if {[file exists "./hls_prj/solution1/directives.tcl"]} {
    source "./hls_prj/solution1/directives.tcl"
}

# Run C Simulation (Functional verification)
csim_design

# Run C Synthesis (Transform C++ to RTL/Verilog)
csynth_design

# Run C/RTL Co-simulation (Verify RTL behavior against C testbench)
# NOTE: cosim_design is intentionally left disabled here. This IP is ap_ctrl_none
# (free-running) AND exposes sps_sel as a runtime s_axilite register; Vitis HLS 2023.2's
# cosim engine rejects that combination ("non-self-synchronizing top I/O sps_sel") because
# there is no ap_start to anchor an AXI4-Lite register write against. Confirmed via a
# throwaway verify_tmp/ build with sps_sel pinned to a compile-time constant (2026-07-15).
#cosim_design

# Export the design as a Vivado IP Catalog package (.zip)
export_design -format ip_catalog -description "SOQPSK TFM Modulator IP" -vendor "user" -library "hls" -display_name "tfm_modulator"
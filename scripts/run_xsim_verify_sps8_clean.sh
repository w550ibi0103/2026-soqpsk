#!/bin/bash
# Cleanest SPS=8 RTL vs C check: tb_xsim_verify_sps8.sv against the
# verify_tmp/ RTL (sps_sel hardwired to 1/SPS=8 at synth time, no
# s_axi_CTRL bundle, so no AXI4-Lite write and no possibility of the
# sps_sel race found in tb_xsim_top.sv). Diff against
# xsim_verify/golden_sps8_output_waveform.csv.
set -e

VIVADO_BIN="/c/Xilinx/Vivado/2023.2/bin"
PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RTL_DIR="$PROJ_ROOT/hls_prj_verify/solution1/syn/verilog"
XSIM_DIR="$PROJ_ROOT/xsim_verify"

cd "$XSIM_DIR"
rm -rf xsim.dir output_waveform_xsim_verify_sps8.csv wave_verify_sps8.wdb

RTL_FILES=$(find "$RTL_DIR" -maxdepth 1 -name "*.v" | sort)

echo ">> xvlog: compiling $(echo "$RTL_FILES" | wc -l) RTL files + testbench"
"$VIVADO_BIN/xvlog.bat" --sv $RTL_FILES tb_xsim_verify_sps8.sv

echo ">> xelab: elaborating tb_xsim_verify_sps8"
"$VIVADO_BIN/xelab.bat" -debug typical tb_xsim_verify_sps8 -s tb_xsim_verify_sps8_snap

echo ">> xsim: running simulation"
"$VIVADO_BIN/xsim.bat" tb_xsim_verify_sps8_snap -wdb wave_verify_sps8.wdb -tclbatch xsim_run.tcl

echo ">> Done. CSV: $XSIM_DIR/output_waveform_xsim_verify_sps8.csv"

#!/bin/bash
# Clean control test: tb_xsim_top_sps16_default.sv (no AXI4-Lite write, sps_sel
# stays at its reset default = SPS16), diffed against the existing golden
# hls_prj/solution1/csim/build/output_waveform.csv. See xsim_verify/tb_xsim_top_sps16_default.sv
# for why this sidesteps the sps_sel write race found in tb_xsim_top.sv.
set -e

VIVADO_BIN="/c/Xilinx/Vivado/2023.2/bin"
PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RTL_DIR="$PROJ_ROOT/hls_prj/solution1/syn/verilog"
XSIM_DIR="$PROJ_ROOT/xsim_verify"

cd "$XSIM_DIR"
rm -rf xsim.dir output_waveform_xsim_sps16.csv wave_sps16.wdb

RTL_FILES=$(find "$RTL_DIR" -maxdepth 1 -name "*.v" | sort)

echo ">> xvlog: compiling $(echo "$RTL_FILES" | wc -l) RTL files + testbench"
"$VIVADO_BIN/xvlog.bat" --sv $RTL_FILES tb_xsim_top_sps16_default.sv

echo ">> xelab: elaborating tb_xsim_top_sps16_default"
"$VIVADO_BIN/xelab.bat" -debug typical tb_xsim_top_sps16_default -s tb_xsim_sps16_snap

echo ">> xsim: running simulation"
"$VIVADO_BIN/xsim.bat" tb_xsim_sps16_snap -wdb wave_sps16.wdb -tclbatch xsim_run.tcl

echo ">> Done. CSV: $XSIM_DIR/output_waveform_xsim_sps16.csv, waveform: $XSIM_DIR/wave_sps16.wdb"

#!/bin/bash
# Standalone RTL simulation of the exported tfm_modulator RTL, bypassing
# Vitis HLS's own cosim entirely (see README: cosim rejects ap_ctrl_none +
# s_axilite). Compiles hls_prj/solution1's generated Verilog + a hand-rolled
# SystemVerilog testbench (xsim_verify/tb_xsim_top.sv) with Vivado's
# standalone xvlog/xelab/xsim tools, runs it, and produces:
#   - xsim_verify/output_waveform_xsim.csv (compare against a csim run's
#     hls_prj/solution1/csim/build/output_waveform.csv)
#   - xsim_verify/xsim.dir/tb_xsim_snap/... + xsim_verify/wave.wdb (waveform,
#     open in Vivado: `xsim_verify/xsim_open_wave.sh` or see README)
set -e

VIVADO_BIN="/c/Xilinx/Vivado/2023.2/bin"
PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RTL_DIR="$PROJ_ROOT/hls_prj/solution1/syn/verilog"
XSIM_DIR="$PROJ_ROOT/xsim_verify"

cd "$XSIM_DIR"
rm -rf xsim.dir output_waveform_xsim.csv wave.wdb

RTL_FILES=$(find "$RTL_DIR" -maxdepth 1 -name "*.v" | sort)

echo ">> xvlog: compiling $(echo "$RTL_FILES" | wc -l) RTL files + testbench"
"$VIVADO_BIN/xvlog.bat" --sv $RTL_FILES tb_xsim_top.sv

echo ">> xelab: elaborating tb_xsim_top"
"$VIVADO_BIN/xelab.bat" -debug typical tb_xsim_top -s tb_xsim_snap

echo ">> xsim: running simulation"
"$VIVADO_BIN/xsim.bat" tb_xsim_snap -wdb wave.wdb -tclbatch xsim_run.tcl

echo ">> Done. CSV: $XSIM_DIR/output_waveform_xsim.csv, waveform: $XSIM_DIR/wave.wdb"

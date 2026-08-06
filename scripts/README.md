# scripts/ 用途索引

## 正式建置

- **`run_hls.tcl`** — 唯一真正在用的建置腳本。`open_project -reset hls_prj` → `csim_design` → `csynth_design` → `export_design`(打包成 Vivado IP Catalog zip)。target `xczu9eg-ffvb1156-2-e`, `create_clock -period 6.25`(160MHz, 對應客戶規格)。用法見根目錄 `README.md`「開發環境還原步驟」。跑完會停在互動式 `vitis_hls>` 提示字元(設計如此,方便接著手動下指令,不是漏寫 `exit`)——如果要在背景/非互動環境跑,改用 `echo "exit" | vitis_hls -f scripts/run_hls.tcl`。

## 產生原始碼裡的資料表(`.inc` 檔)

- **`gen_g_coeffs.ps1`** — 產生 `src/g_coeffs_sps{16,8,4}.inc`(FIR 脈衝濾波器 g(t) 係數表),公式跟參數照 Python 參考模型(`soqpsk-tg.py`)的 Block 4。改了 SPS 清單或濾波器參數要重跑這支。
- **`gen_sincos_lut.ps1`** — 產生 `src/sin_lut.inc`/`cos_lut.inc`(256 筆線性內插用的 sin/cos 查表,取代 CORDIC)。

## 診斷/一次性腳本

- **`check_cosim_reject.tcl`** — 很小的診斷腳本,只是重新確認「Vitis HLS 2023.2 的 `cosim_design` 會拒絕 `ap_ctrl_none` + `s_axilite(sps_sel)` 這個組合」這件事(找不到時間基準點驅動 AXI-Lite write)。不產生任何交付物,純粹是重跑一次確認限制還在。

## RTL 對 golden 的 XSIM 驗證(繞過 `cosim_design`, 見 Note.md)

這幾支各自編譯+跑一次手寫的 SystemVerilog testbench(在 `xsim_verify/`),直接拿 Vitis HLS 匯出的 Verilog 去對 golden CSV 逐點比對。完整的 testbench↔golden 對照表見 `xsim_verify/README.md`。

- **`run_xsim_verify.sh`** — 跑 `tb_xsim_top.sv`(對 `hls_prj/solution1` 的真實介面, reset 放開後透過 AXI4-Lite 寫 `sps_sel`)。這支最早踩到「sps_sel AXI4-Lite write race」的問題,發散不是 bug,是已知限制的展示。
- **`run_xsim_verify_sps16.sh`** — 跑 `tb_xsim_top_sps16_default.sv`(同樣對 `hls_prj/solution1`,但完全不寫 AXI4-Lite, `sps_sel` 停在 reset 預設值 0=SPS16),避開上面那個 race,乾淨對照組。
- **`run_xsim_verify_sps8_clean.sh`** — 跑 `tb_xsim_verify_sps8.sv`,對象是 `hls_prj_verify/solution1`(`verify_tmp/src/top.cpp` 編譯出來的 RTL, `sps_sel` 編譯期釘死成 1=SPS8, 完全沒有 `s_axi_CTRL`, 不可能有 race)。`hls_prj_verify/` 由 `xsim_verify/golden_sps8/csim_sps8_only.tcl`/`csynth_verify_sps8.tcl` 產生(要從 repo 根目錄執行)。

# xsim_verify/ 索引:每支 testbench 測什麼、對哪個 golden

繞過 Vitis HLS `cosim_design` 的限制(`tfm_modulator` 是 `ap_ctrl_none` + `sps_sel`
這個 `s_axilite` port 的組合,`cosim_design` 會直接拒絕,見 Note.md),直接用 Vivado 的
`xvlog`/`xelab`/`xsim` 編譯匯出的 RTL + 手寫的 SystemVerilog testbench,對 golden CSV
逐點比對。`.gitignore` 排除大部分模擬產物(CSV/`.wdb`/`.log`/`.jou`/`.pb`/`xsim.dir/`),
但以下這幾類**有**進版控:`.sv`/`.tcl` 原始碼;LUT ROM 記憶體初始檔(`.dat`,由
`csynth_design` 產生 -- `$readmemh` 用相對路徑讀取,`csynth_design` 重新產生 RTL 後記得
手動把 `hls_prj/.../syn/verilog/*.dat` 覆蓋過來,不然 xsim 會撿到這裡殘留的舊檔,見
Note.md 第 49 節踩過的坑);以及少數留作參考用的歷史產物(如 `dut_full_dump.vcd`,見下方
「其他檔案」說明)。

## Testbench 對照表

| Testbench | 測的 RTL | 驅動腳本 | 對比的 golden | 狀態 |
|---|---|---|---|---|
| `tb_xsim_top_sps16_default_coldstart.sv` | `hls_prj/solution1`(**真實設計**, cold_start 已套用, 無 debug port) | 手動編譯(無 `.sh`) | `hls_prj/solution1/csim/build/output_waveform.csv` | **目前現行**——2026-08-06, 1024 樣本逐位元吻合(Note.md 第 45 節), 之後 Q1.15/512-entry LUT 改動(Note.md 第 49 節)也是用這支重新驗證的 |
| `tb_xsim_top_sps16_default.sv` | `hls_prj/solution1`(真實設計, CORDIC 版, 舊介面含 debug port) | `scripts/run_xsim_verify_sps16.sh` | `hls_prj/solution1/csim/build/output_waveform.csv` | 歷史——2026-07-23, 被上面的 `_coldstart` 版取代 |
| `tb_xsim_top.sv` | `hls_prj/solution1`(真實設計, CORDIC 版, 舊介面含 debug port) | `scripts/run_xsim_verify.sh` | `hls_prj/solution1/csim/build/output_waveform.csv` | 歷史——用來展示 `sps_sel` AXI4-Lite write race(Note.md 第 24 節), 預期會發散, 不是「應該要過」的測試 |

(`tb_xsim_verify_sps8.sv`/`tb_xsim_verify_sps8_lut.sv`,對 `verify_tmp/` 編出來的 `hls_prj_verify` 那兩支,連同 `verify_tmp/` 本身已於 2026-08-06 刪除——存在理由已經隨 cold_start 正式驗證完成而失效,詳見 Note.md.)

## 其他檔案

- **`xsim_run.tcl`** — 所有 testbench 共用的 **xsim(Vivado 模擬器)批次指令**(`log_wave -recursive *` + `run all` + `quit`),是 xsim 自己的控制腳本,不是 Vitis HLS 的專案建置腳本(那是根目錄 `scripts/run_hls.tcl` 那一類 `.tcl`)。每支 `.sh` 驅動腳本最後都靠 `xsim.bat ... -tclbatch xsim_run.tcl` 呼叫它。
- **`current_phase_xsim.csv`、`debug_phase_xsim.csv`、`debug_pulse_xsim.csv`、`phase_diff.csv`、`golden_sps8_debugphase_output_waveform.csv`** — CORDIC 時代(2026-07-23)的內部訊號探針,搭配 `tb_xsim_top.sv` 那次跑的,LUT 上線後已經沒有現實意義,純參考。
- **`*.log`/`*.jou`/`*.pb`/`xsim.dir/`** — 工具執行紀錄跟編譯資料庫,每次跑都會重新產生,git-ignored,不需要理會內容。

## 之後新增驗證時

新的 testbench/golden 對照,請直接加進上面那張表,並在 `Note.md` 記錄結果——不要讓這個資料夾再變回一堆看不出對應關係的檔案。

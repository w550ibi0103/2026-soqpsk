# xsim_verify/ 索引:每支 testbench 測什麼、對哪個 golden

繞過 Vitis HLS `cosim_design` 的限制(見 `verify_tmp/README.md`),直接用 Vivado 的
`xvlog`/`xelab`/`xsim` 編譯匯出的 RTL + 手寫的 SystemVerilog testbench,對 golden CSV
逐點比對。`.gitignore` 只 track 這裡的 `.sv`/`.tcl`(原始碼),所有 CSV/`.wdb`/`.log`/
`.jou`/`.pb`/`xsim.dir/` 都是可重新產生的模擬產物,沒有進 git。

## Testbench 對照表

| Testbench | 測的 RTL | 驅動腳本 | 對比的 golden | 狀態 |
|---|---|---|---|---|
| `tb_xsim_top_sps16_default_coldstart.sv` | `hls_prj/solution1`(**真實設計**, cold_start 已套用, 無 debug port) | 手動編譯(無 `.sh`) | `hls_prj/solution1/csim/build/output_waveform.csv` | **目前現行**——2026-08-06, 1024 樣本逐位元吻合(Note.md 第 45 節) |
| `tb_xsim_verify_sps8_lut.sv` | `hls_prj_verify`(`verify_tmp/` 編出來的 RTL, LUT 版, 含診斷用 debug stream) | 手動編譯(無 `.sh`) | `golden_sps8_lut_output_waveform.csv`、`golden_sps8_lut_debug_signals.csv`、`golden_debug_alpha_stream.csv`、`golden_debug_idle_stream.csv` | **cold_start 的原始驗證**——2026-07-29, 512 樣本逐位元吻合(Note.md 第 36 節), 是 cold_start 修法最早被證實有效的地方 |
| `tb_xsim_verify_sps8.sv` | `hls_prj_verify`(`verify_tmp/` 編出來的 RTL, CORDIC 版, SPS8 釘死) | `scripts/run_xsim_verify_sps8_clean.sh` | `golden_sps8_output_waveform.csv`(`golden_sps8_widened_*` 是加寬精度實驗的變體) | 歷史——2026-07-23, CORDIC 時代, 已被 LUT 版取代 |
| `tb_xsim_top_sps16_default.sv` | `hls_prj/solution1`(真實設計, CORDIC 版, 舊介面含 debug port) | `scripts/run_xsim_verify_sps16.sh` | `hls_prj/solution1/csim/build/output_waveform.csv` | 歷史——2026-07-23, 被上面的 `_coldstart` 版取代 |
| `tb_xsim_top.sv` | `hls_prj/solution1`(真實設計, CORDIC 版, 舊介面含 debug port) | `scripts/run_xsim_verify.sh` | `hls_prj/solution1/csim/build/output_waveform.csv` | 歷史——用來展示 `sps_sel` AXI4-Lite write race(Note.md 第 24 節), 預期會發散, 不是「應該要過」的測試 |

**`hls_prj_verify/` 是什麼**:`verify_tmp/src/top.cpp` 編出來的 build 產物(git-ignored),由
`golden_sps8/csim_sps8_only.tcl`/`csynth_verify_sps8.tcl` 產生,細節見
`verify_tmp/README.md`。

## 其他檔案

- **`xsim_run.tcl`** — 所有 testbench 共用的 xsim batch 指令(`log_wave -recursive *` + `run all` + `quit`)。
- **`golden_sps8/`** — 產生 `hls_prj_verify`/SPS8 golden 用的三支 tcl(`csim_sps8_only.tcl`、`csynth_verify_sps8.tcl`、`csim_real_sps8_debug.tcl`),用途見 `scripts/README.md`。
- **`dut_full_dump.vcd` + `xsim_vcd_dump.tcl` + `alpha_stream_from_vcd.csv`** — 2026-07-28 追 `shift_reg`/`alpha` 內部訊號時的 VCD dump-and-parse 繞路方法(Note.md 第 33 節),後來直接加了專用的 `debug_alpha_stream` port 取代,這批檔案留著純參考。
- **`golden_expected_nonzero_pulses.csv`** — 第 33 節第一次猜訊號名稱失敗留下的產物,已知是錯的猜測,純參考不代表正確結果。
- **`current_phase_xsim.csv`、`debug_phase_xsim.csv`、`debug_pulse_xsim.csv`、`phase_diff.csv`、`golden_sps8_debugphase_output_waveform.csv`** — CORDIC 時代(2026-07-23)的內部訊號探針,搭配 `tb_xsim_top.sv`/`tb_xsim_verify_sps8.sv` 那次跑的,LUT 上線後已經沒有現實意義,純參考。
- **`*.log`/`*.jou`/`*.pb`/`xsim.dir/`** — 工具執行紀錄跟編譯資料庫,每次跑都會重新產生,git-ignored,不需要理會內容。

## 之後新增驗證時

新的 testbench/golden 對照,請直接加進上面那張表,並在 `Note.md` 記錄結果——不要讓這個資料夾再變回一堆看不出對應關係的檔案。

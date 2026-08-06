# verify_tmp/ 是什麼、驗證過什麼

## 用途

`src/top.cpp`/`top.h`/`tb/` 的完整平行副本,**不是**要交付的東西,是拿來做實驗、不去動真正的 `src/`。

存在的直接原因:Vitis HLS 2023.2 的 `cosim_design` 不接受 `ap_ctrl_none`(free-running)+ `sps_sel` 這個 `s_axilite` port 混用的組合(`WARNING: [COSIM] found non-self-synchronizing top I/O sps_sel` ——沒有 `ap_start` 可以當作驅動 AXI4-Lite write 的時間基準點),所以沒辦法用 HLS 自己內建的 cosim 機制驗證 RTL 對 C model。`verify_tmp/top.h` 把 `sps_sel` 直接在編譯期釘死成常數、整個拿掉 `s_axi_CTRL` bundle,拿到一份沒有這個限制的 RTL,才能用手寫的 XSIM testbench(見 `xsim_verify/README.md`)去對 golden 做逐位元比對。

**手動同步**:這是一份手動維護的分支,不會自動跟著 `src/` 改動。要在這裡做實驗時,先手動把 `src/top.cpp`/`top.h` 的相關改動搬過來(或反過來,要正式套用某個已驗證的修法時,從這裡搬回 `src/`)。

## 驗證過什麼(依時間順序, 詳細過程見 `Note.md`)

1. **加寬中間精度的否證實驗**(Note.md 第 25 節):懷疑 128-tap FIR 加總或 phase accumulator 有中間溢位,加寬位元寬度重跑,發散位置/數值逐位元不變,排除溢位假設。
2. **CORDIC 發散問題根因定位**(第 26 節):用 hierarchical reference 直接讀 RTL 內部的 `current_phase` 暫存器,確認問題出在 `hls::cos`/`hls::sin`(CORDIC)本身,不是前面的運算。
3. **256-entry LUT 取代 CORDIC**(第 27/28 節):改用線性內插查表,csim/csynth 都過,`Estimated Fmax=225.77MHz`;用「自我一致性掃描」(拿 RTL 內部真實輸入去餵獨立重算的 LUT)證實 LUT 電路本身是乾淨的確定性函數,沒有殘留狀態。
4. **真正的根因:reset 放開瞬間 byte0 被誤判成 idle**(第 29/33/34 節):新增 `debug_alpha_stream`/`debug_idle_stream`/`debug_current_bit_stream` 這幾個診斷用的 axis port(只在 `verify_tmp/` 有,不在 `src/`),直接證實 `bit_in` 的 AXI4-Stream regslice 在 reset 放開瞬間還沒追趕上,`BYTE_LOOP` 第一次 `read_nb()` 幾乎必然撲空,byte0 被誤判成 idle,後面資料永久位移一個 byte。
5. **cold_start 修法驗證成功**(第 36 節):加入 `cold_start` 這個 state,在還沒收到過第一筆真實資料前每個 cycle 都重試 `read_nb()`。驗證結果:csim 一致、csynth `II=1`/`Depth=15`/`Fmax=225.77MHz` 跟修改前完全相同(零時序代價)、RTL 對 golden **512 個樣本逐位元完全吻合**。這個修法後來(2026-08-06)正式搬進 `src/top.cpp`,並在真實的 `hls_prj/solution1` 上重新跑過 csim/csynth/RTL-vs-golden/真實 Vivado P&R 四層驗證(Note.md 第 45/47 節),結論一致。

## 跟 `hls_prj_verify/` 的關係

`hls_prj_verify/`(repo 根目錄, git-ignored)是 Vitis HLS 拿 `verify_tmp/src/top.cpp` 當輸入合成出來的 build 產物,由 `xsim_verify/golden_sps8/csim_sps8_only.tcl`/`csynth_verify_sps8.tcl` 產生(要從 repo 根目錄執行)。`verify_tmp/` 本身只有原始碼(9 個檔案,`.tcl`/`src/`/`tb/`),不含任何合成產物。

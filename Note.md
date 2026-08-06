1. 我用的硬體是 zcu102+adrv9009
2. 專案路徑: C:\XilinxWorkspace\Vitis\2026-soqpsk
3. adi hdl 路徑: C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\zcu102
4. 可以參考 README.md, 裡面有紀錄你之前改動的東西
5. 確認 adrv9009 的 iq rate 可不可以跑到 245.76MSample/s? 你之前有去 adi hdl 看 rtl 程式碼, 有看到是跑在 122.88MSample/s, 
6. 因為速率還是要對齊 DAC 的真實原生取樣率, 這部分我要怎麼讓我 ip 輸出速率可以對齊 DAC 的真實原生取樣率? 因為 dac 可能是跑在 122.88MSample/s, dac 要每個 clk 都有資料, 但是我的 ip 輸出的 iq 可以跟上這個 clk 速率? ip 要計算很多東西, 真的可以每個 clk 都輸出 iq? 你之前說模擬的結果是 Timing: estimated 7.256ns(≈137.8MHz), 但這只是可以跑在這個速度, 我要問的是在這個速度下, 是否每個 clk 都可以產出 iq 資料?
7. 我覺得 sps=2 可以刪掉, 因為昇取樣後的 sample 數太少了, 接收那邊應該會解不出來, 你認為呢?
8. 同事有推薦說如果有一些計算可以用查表的方式, 可以省一些時間, 你認為有哪些地方可以用查表的方式做?

# tfm_modulator 動態 SPS / Free-Running 改造(2026-07-07)

## 1. 改了什麼(src/top.h, src/top.cpp, tb/tb_top.cpp)
1. 放棄用軟體手動控制重置, 改為完全依賴底層硬體的標準重置機制. 原本設計中有一個名叫 reset 的變數, 並且被綁定到 s_axilite (AXI4-Lite) 介面上. `reset` 參數與 `s_axilite` 上的 reset 暫存器整個移除, 交給 HLS 預設的 `ap_rst_n` 硬體腳位處理, `if(reset){...}` 那段手動歸零邏輯也拿掉, 全部靠 static 變數的 C++ initializer 當作 reset 值. 在 C/C++ 寫 HLS 時, 如果你宣告了一個 static 變數並賦予初始值, HLS 編譯器非常聰明, 它會知道這些 0 和 true 就是這些暫存器的「預設狀態」. 當硬體腳位 ap_rst_n 被拉低(觸發硬體重置)時, FPGA 底層的暫存器 (Flip-Flops) 會自動回到這些初始值.
2. `#pragma HLS INTERFACE s_axilite port=return bundle=CTRL` 改成 `#pragma HLS INTERFACE ap_ctrl_none port=return`, 讓這顆 IP 變成真正的 free-running 資料流 IP, 不需要 PS 寫 `ap_start`.
3. 新增 `ap_uint<2> sps_sel` 參數, 走一個小的 `s_axilite` bundle(`CTRL`), 這個 CTRL bundle 就是開了一扇小窗戶, 讓 CPU 可以隨時把新的 sps_sel 值寫進來, 用來動態選擇 SPS(16/8/4/2), 對應 `active_sps = SPS_MAX >> sps_sel`(sps_sel: 0→16, 1→8, 2→4, 3→2), 沒有寫成 SPS_MAX / sps, 而是利用位元右移(Right Shift)來實現除法. 這個 register 不影響 free-running, 只是額外開一個小控制窗口.
4. 原本的函式 body 包進 `BYTE_LOOP: while(1)`, 在 `#ifndef __SYNTHESIS__` 底下用一個計數器(`CSIM_MAX_ITERS`, 定義在 top.h, 目前是 8)在 C 模擬時跳出迴圈避免卡死; 合成後(`__SYNTHESIS__` 有定義)這段跳出邏輯整個消失, 變成真正的無窮迴圈.
5. 新增 4 張各自 `ARRAY_PARTITION complete` 的係數表 `g_coeff_sps16/8/4/2`(都是 `G_LEN_MAX=128` 長度, 短的表由 C++ aggregate initializer 自動補零), MAC 迴圈用 `switch(sps_sel)` 在乘法器輸入端選當下要用的係數, 不是做 4 組平行乘法樹(csynth 結果證實 DSP 沒有變貴, 見第 3 節).
6. `bit_idx`/`s`(sample-in-symbol index)/相位增量(`PHASE_SCALE[sps_sel]`)/TLAST 判斷式全部從固定的 `SPS` 巨集改成 runtime 的 `active_sps`/`shift_amt`.
7. `tb_top.cpp` 從「呼叫 tfm_modulator N 次, 每次處理 1 byte」改成「只呼叫 1 次, 內部自己用 BYTE_LOOP 跑完 NUM_BYTES(=8) 個 byte」, 拿掉顯式的 reset 呼叫. 新增 `TEST_SPS_SEL` 編譯期巨集(預設 0), 可以用 `-DTEST_SPS_SEL=1/2/3` 切換測試不同 SPS.

## 2. g(t) 係數怎麼來的
- 公式與參數完全照 `C:\Users\eddiehppc\Documents\igps-rasp-receiver-iq-to-toa\iGPS-PlutoSDR\tests\soqpsk-tg\soqpsk-tg.py` 的 Block 4(`rho=0.70, B=1.25, T1=1.5, T2=0.5, Tb=1.0, L=8`), 只是把 `sps` 代入 16/8/4/2 各自重新取樣、各自獨立正規化(`sum(g)*(Tb/sps)=0.5`), 不是用抽取(decimation)去猜.
- 這台機器沒裝 Python(`python`/`python3` 只是 Windows Store 的殼, 也沒有 g++/node), 改用 `scripts/gen_g_coeffs.ps1`(PowerShell + .NET Math)重現同一套公式產生 `src/g_coeffs_sps{16,8,4,2}.inc`. 驗證過重新產生的 sps=16 版本跟原本的 `g_coeffs.inc`(已刪除, 被 `g_coeffs_sps16.inc` 取代)在 ~1e-14 相對誤差內一致(numpy vs .NET 函式庫的正常捨入差異, 遠低於 `ap_fixed<16,4>` 的解析度 0.000244).
- 之後如果要加新的 SPS 或改參數, 直接重跑 `scripts/gen_g_coeffs.ps1` 就好.

## 3. 驗證結果
- `csim_design`(sps_sel=0, 對照原本固定 SPS=16 的行為): TEST PASSED, 0 errors, 產生 1024 samples(= 8 bytes × 16 sps × 8 bits).
- 4 種 `sps_sel`(0/1/2/3, 用 `-DTEST_SPS_SEL=N` 各自跑一次全新的 csim, 確保 static 狀態是乾淨重跑, 等同硬體上電後的 `ap_rst_n`)全部 TEST PASSED, 0 errors, 樣本數分別是 1024/512/256/128(= active_sps × 8 × 8 bytes), 跟預期公式完全吻合.
- `csynth_design`(target xczu9eg-ffvb1156-2-e, `create_clock -period 10` 即 100MHz):
  - Timing: estimated 7.256ns(≈137.8MHz), 有裕度過關. 這個 Fmax 比 ADRV9009 這份參考設計實際的 DAC 原生取樣率 122.88MHz(見第 9 節)還高, 代表之後把 `ap_clk` 換成 122.88MHz 應該跑得動, 但正式對接時仍要重跑 `create_clock` 對應正確週期確認.
  - Utilization: BRAM_18K 0, DSP 110(4%), FF 44884(8%), LUT 16463(6%), 對 zu9eg 來說非常寬鬆.
  - 原本擔心「4 張係數表會讓乘法器變 4 倍貴」沒有發生: `sps_sel` 的 4-to-1 mux(報告裡的 `sparsemux` instance)接在乘法器輸入端, 先選係數再進同一顆乘法器, DSP 用量沒有明顯比純常數係數版本高, 多的成本主要是 LUT 端約 1210 顆的 mux 邏輯.
  - `BYTE_LOOP` trip count 顯示 `inf`, 確認合成後真的是無窮迴圈(free-running), 每個 byte 的迭代延遲落在 123~235 cycle(對應 SPS=2 到 SPS=16 兩個極端).

## 4. 使用限制
- 切換 `sps_sel` 不保證 glitch-free(shift_reg/current_phase 不會自動對齊新的取樣率), 只能在 `ap_rst_n` assert 期間切換.
- `sps_sel` 從 reset 釋放後預設值是 0(對應 SPS=16), 跟原本固定 SPS=16 的行為一致.

# tfm_modulator 迴圈攤平 + 刪除 SPS=2(2026-07-09)

## 5. 動機:BYTE_LOOP 沒有真的每個 clk 都輸出 IQ
規劃「bypass `tx_fir_interpolator`、直接接 `tx_adrv9009_tpl_core`」這個插入點方案時發現, 上面 2026-07-07 那版雖然 `MAIN_LOOP` 本身 `achieved II=1`, 但外層 `BYTE_LOOP` 沒有被攤平/pipeline: `tfm_modulator_csynth.rpt` 的 Instance 表顯示 `grp_tfm_modulator_Pipeline_MAIN_LOOP_fu_696` 的 `Interval == Latency`(min=119/max=231), 代表下一個 byte 的 `MAIN_LOOP` 必須等上一個 byte 的 pipeline 完全 drain 才能開始, byte 與 byte 交界處有一大段空窗, 這在 HLS 中代表「管線抽空(Pipeline Drain)」. 換算吞吐效率(輸出樣本數/實際耗費 cycle 數): SPS=16 約 55%, SPS=2 只有約 13%. `dac_data` 這個介面是無 handshake 的固定速率 port, 這樣的空窗會讓 DAC 拿到 stale 資料, 所以這不是「效能優化」而是這個插入點方案能不能成立的先決條件.

根本原因是 `MAIN_LOOP` 的邊界 `active_sps*8` 是 runtime 變數(取決於 `sps_sel`), Vitis HLS 的自動 `LOOP_FLATTEN` 只支援邊界是編譯期常數(Compile-time constant)的完美巢狀迴圈, 而 active_sps 是由前一段提到的 sps_sel 這個 AXI-Lite 暫存器在執行期 (Runtime) 動態決定的. 工具遇到這種「變動邊界」的迴圈, 會直接放棄自動攤平, 用不上, 只能手動合併.

## 6. 改了什麼(src/top.cpp)
1. 拿掉 `MAIN_LOOP` 這層 `for`, 把 `BYTE_LOOP`/`MAIN_LOOP` 合併成一層 `while(1)` + `#pragma HLS PIPELINE II=1`. 用一個 static counter `iter_in_byte`(0 .. active_sps*8-1)取代原本內層 for 迴圈的隱含計數器, 在 `iter_in_byte==0` 時做「原本 BYTE_LOOP 開頭」那段(解碼 `shift_amt`/`active_sps`/`phase_idx`、`bit_in.read_nb` 讀新 byte), 每個 cycle 結尾判斷 `iter_in_byte` 是否到 `active_sps*8-1` 決定要繞回 0(進入下一個 byte)還是 +1.
2. `alpha`/`current_bit`/`current_byte`/`is_burst_end`/`idle_mode` 全部從一般區域變數改成 `static`——原本它們能在同一個 byte 的 `active_sps` 次迭代間存活, 是靠外層 for 迴圈的 C++ block scope 圍住, 攤平成一層之後沒有這個 scope 了, 必須手動 `static` 才能存活, 這是這次修改最容易出錯的地方. 因為外層迴圈的括號(Scope)沒了, 原本能「活過內層迴圈好幾次迭代」的變數, 現在每次進入這個單層迴圈都會被視為全新的變數. 在 C/C++ HLS 中, 宣告為 static 的變數, 在轉換成硬體時會被綜合(Synthesized)成具備記憶功能的暫存器(Registers). 它們的值在跨越時鐘週期(Clock cycles)或迴圈迭代(Iterations)時會被保留下來.
3. `#ifndef __SYNTHESIS__` 底下的 C 模擬中斷邏輯(`csim_iter_count`)從「每次外層迴圈(=每個 byte)加 1」改成「只在 `iter_in_byte` 即將繞回 0 的那個 cycle才加 1、判斷要不要 break」, 確保還是處理完整數個 byte 才跳出, 不會在 byte 中途被切斷.
4. `PHASE_SCALE[sps_sel]` 改成 `PHASE_SCALE[phase_idx]`, `phase_idx` 跟 `shift_amt` 一樣只在 byte 邊界解碼, 且對 `sps_sel==3`(已刪除的 SPS=2)clamp 回 0(等同 SPS=16), 避免陣列縮小後越界.

## 7. 刪除 SPS=2(src/top.h, src/top.cpp, tb/tb_top.cpp, scripts/gen_g_coeffs.ps1)
- 理由: (1) SPS=2 剛好卡在 Nyquist 邊界, 接收端做符元定時回復(timing recovery)幾乎沒有內插 margin, 實務上通常至少要 SPS=4. (2) 在攤平前的架構下, SPS=2 的吞吐效率是四個選項裡最差的(~13%).
- `g_coeff_sps2` 表、`src/g_coeffs_sps2.inc` 檔案、兩個 `switch(sps_sel)`(`shift_amt` 解碼、MAC 係數選擇)裡的 `case 3` 分支、`PHASE_SCALE` 的第 4 個元素全部刪除. `sps_sel==3` 現在是保留值, 兩個 switch 的 `default` 分支自動把它當 SPS=16 處理(跟 `sps_sel==0` 完全一樣), `phase_idx` 同樣 clamp 回 0.
- `scripts/gen_g_coeffs.ps1` 的 `$SpsList` 預設值從 `@(16,8,4,2)` 改成 `@(16,8,4)`.
- `tb/tb_top.cpp`:`SPS_TABLE` 從 4 個元素縮成 3 個(`{16,8,4}`), 新增 `#if TEST_SPS_SEL > 2 #error ... #endif` 編譯期防呆.

## 8. 驗證結果
- `csim_design`:3 種 `sps_sel`(0/1/2, 用 `-DTEST_SPS_SEL=N` 各自跑一次全新的 csim)全部 TEST PASSED, 0 errors, 樣本數分別是 1024/512/256(= active_sps × 8 × 8 bytes), 跟改動前完全一致.
- `-DTEST_SPS_SEL=3` 會在編譯期直接被 `#error` 擋下, 確認防呆生效.
- `csynth_design`(同樣 target xczu9eg-ffvb1156-2-e, `create_clock -period 10`):
  - Timing 不變: estimated 7.256ns(≈137.8MHz), 跟攤平前完全一樣, 代表新增的 `iter_in_byte` 控制邏輯沒有拉長關鍵路徑.
  - **關鍵指標**:報告裡只剩一個迴圈 `BYTE_LOOP`(`tfm_modulator_Pipeline_BYTE_LOOP_csynth.rpt`), `Trip Count = inf`、`Pipelined: yes`、`achieved II = 1`——不再有攤平前那種「`Interval == Latency`, 序列化呼叫」的現象. 也就是說現在不管 `active_sps` 是多少, 穩態下每個 clk 都會有一組新的 IQ 輸出, 不再受 SPS 大小影響效率.
  - Utilization 略降(移除 SPS=2 表 + 減少迴圈控制邏輯的重複開銷):BRAM_18K 0, DSP 109, FF 37206, LUT 13727(攤平前為 DSP 110, FF 44884, LUT 16463).
- 驗證用的暫存專案(`hls_prj_verify`)僅用於這次確認, 已刪除, 不影響 repo 追蹤的 `hls_prj/`(git-ignored).

# ADRV9009 ADI HDL 整合建議(2026-07-07, 尚未實作, 屬於另一個 Vivado 專案)

以下內容是研讀 `C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\zcu102\system_bd.tcl` 與 `C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\common\adrv9009_bd.tcl` 後得到的確認結果與建議方案, 目前只是設計討論, 沒有實際修改 Vivado 專案.

## 9. 確認過的事實(從原始碼直接找到, 不是憑記憶)
1. **DAC 原生 IQ rate 是 122.88 MSPS, 不是 245.76 MSPS**: `adrv9009_bd.tcl:91` 寫死 `ad_add_interpolation_filter "tx_fir_interpolator" 8 $TX_NUM_OF_CONVERTERS 2 {122.88} {15.36} ...`, 8x 內插, 15.36→122.88 MSPS.
2. **`CONFIG.CYCLIC 1` 的確切位置**: `adrv9009_bd.tcl:100-103`(`axi_adrv9009_tx_dma` instance), 跟 README 原本寫的「line 100-112」對得上, 只是實際在 `adrv9009_bd.tcl` 不是 `system_bd.tcl`(`system_bd.tcl` 只覆寫 `DMA_DATA_WIDTH_SRC`/`FIFO_SIZE` 兩個參數).
3. **TX 資料路徑**(都掛在同一個 `axi_adrv9009_tx_clkgen/clk_0` 時脈域上):
   ```
   axi_adrv9009_tx_dma (CYCLIC=1)
     → axi_adrv9009_dacfifo                     (CDC: dma_clk → clk_0)
     → util_adrv9009_tx_upack                   (clk = clk_0)
     → tx_fir_interpolator (8x, 15.36→122.88)   (aclk = clk_0)
     → tx_adrv9009_tpl_core (dac_data_0..3)     (link_clk = clk_0)
     → JESD204 TX → GT → ADRV9009
   ```
4. **這個已建置的專案目前是雙 TX 通道**: `system_project.tcl` 預設 `TX_JESD_M=4, TX_JESD_L=4`(vivado.log 裡的 `custom_string: ... TX:M=4 L=4 S=1 ...` 證實實際 build 就是用這組預設值), 也就是 TX1+TX2 都啟用, `dac_data_0/1`=TX1 的 I/Q, `dac_data_2/3`=TX2 的 I/Q.
   - 曾經討論過改成單通道(`TX_JESD_M=2, TX_JESD_L=2` 重新 make), 但這需要整個 Vivado 專案重新 build(1~3 小時以上, 且 FPGA 端 JESD204 M/L 必須跟 ADRV9009 晶片本身在 TES 燒的 profile 一致, 否則 JESD204 link 直接連不起來), 風險與耗時都偏高, **最後決定不做, 維持雙通道**.

## 10. 插入點方案(TX1 換成 SOQPSK IP, TX2 完全不動)
- **TX2(`dac_data_2/3`)維持原樣**, 繼續走現有的 `axi_adrv9009_tx_dma → dacfifo → upack → interpolator → tpl_core` 路徑, 用軟體算好的 IQ 波形播放.
- **TX1(`dac_data_0/1`)由 `tfm_modulator` 接管**:
  1. 新增一個獨立的 `axi_dmac` instance(不是修改現有的 `axi_adrv9009_tx_dma`), 設 `CONFIG.CYCLIC 0`(這才是「host 送即時不重複 bitstream」該有的行為), `ASYNC_CLK_*` 比照現有 `axi_adrv9009_tx_dma` 設 1, 讓它的 `m_axis` 輸出直接同步進 `axi_adrv9009_tx_clkgen/clk_0` 時脈域, 不用額外接一顆 CDC FIFO.
  2. 這顆新 DMA 的 AXI-Stream 輸出接到 `tfm_modulator` 的 `bit_in`.
  3. `tfm_modulator` 的 `ap_clk`/`ap_rst_n` 接到 `axi_adrv9009_tx_clkgen/clk_0` 同一個時脈域(跟 `tx_adrv9009_tpl_core/link_clk` 一致), `i_out`/`q_out` 直接接 `tx_adrv9009_tpl_core/dac_data_0`(I)、`dac_data_1`(Q), 取代原本從 `tx_fir_interpolator/data_out_0/1` 接過來的線.
  4. `util_adrv9009_tx_upack`/`tx_fir_interpolator` 維持 `NUM_OF_CHANNELS=4`(`TX_NUM_OF_CONVERTERS` 全域參數)不動, 因為牽動 TX2 和 DMA 資料寬度; channel 0/1 那兩條 fifo/interpolator 分支就晾在旁邊不接東西, 換取不用動全域 M 參數的低風險.

## 11. 已排除的替代方案:插入 tx_fir_interpolator 前端(2026-07-09)
- 曾經討論過不 bypass `tx_fir_interpolator`, 改成讓 `tfm_modulator` 只輸出 15.36 MSPS、接到 `tx_fir_interpolator/data_in_0/1`(這樣它的 x8 內插直接幫忙做到 122.88 MSPS), 好處是我們 IP 只需要撐住 15.36 MSPS(比 122.88 MSPS 寬鬆很多), 也確認過 `adi_fir_filter_bd.tcl:132-145` 這個插入點的輸入側本來就是「每 8 個 clk 才取一次新資料」的設計(`rate_gen`/`PULSE_PERIOD=filter_rate-1`).
- 最後沒有採用, 因為客戶要求的最快 bit rate 是 20 Msym/s, 這個數字跟 ADRV9009 這整條時脈家族(15.36 MHz × 2ⁿ)對不上(`80,000,000 / 15,360,000 = 125/24`, 不是整數比、更不是 2 的冪次比), 硬接會需要一顆 L=96/M=125 等級的有理數重取樣器, 不划算. 改成直接對齊 122.88 MSPS(bypass `tx_fir_interpolator`)之後, 只要 bit rate 跟 SPS 的乘積等於 122.88 MHz 家族的數字就好(例如 15.36 Mbit/s × SPS8, 或 7.68 Mbit/s × SPS16), 不需要任何額外的重取樣級.
- 這個決定也代表上面「迴圈攤平」那個修改(2026-07-09, 第 5-8 節)是這個插入點方案能不能成立的先決條件: `dac_data` 是無 handshake 的固定速率 port, 沒有攤平之前 `BYTE_LOOP` 撐不住 122.88 MSPS 的連續輸出(見第 5 節的效率試算), 攤平後 `achieved II=1` 才讓這個方案有可行性.

## 12. dac_data_0/1 打包格式(已確認, 2026-07-09)
查了 `tx_adrv9009_tpl_core` 底層的 `ad_ip_jesd204_tpl_dac_channel.v:118-122`:
```verilog
/* Data is expected to be LSB aligned, drop unused MSBs */
for (i = 0; i < DATA_PATH_WIDTH; i = i + 1) begin: g_dac_dma_data
  assign dac_dma_data_s[CR*i+:CR] = dma_data[BITS_PER_SAMPLE*i+:CR];
end
```
`DATA_PATH_WIDTH=2`、`CR`(CONVERTER_RESOLUTION)`=16`. 確認 `dac_data_0`(I)的 32-bit 裡`[15:0]`/`[31:16]`是**同一個 channel、時間軸上連續兩筆** I sample(`dac_data_1`同理放 Q), 不是同一個 32-bit 裡塞 I/Q 交錯. 這個 packing 規則是 `tx_adrv9009_tpl_core` 通用的, 不管上游接 `tx_fir_interpolator` 還是 `tfm_modulator` 都要照這個順序打包.

## 13. IP 現在還不會輸出 dac_data 要的打包格式(已確認, 2026-07-09)
`src/top.h` 的 `sample_pkt` 是 `ap_axiu<16,...>`, `src/top.cpp` 的 `BYTE_LOOP` 每個 clock 只做 `i_out.write(out_i); q_out.write(out_q);`——**一次 1 筆 16-bit I、1 筆 16-bit Q**, 不是 32-bit、也沒有把「這一筆」跟「下一筆」打包在一起. 上面「`dac_data_0/1` 打包格式」(第 12 節)講的 32-bit/2 筆連續樣本, 是 `tx_adrv9009_tpl_core` **要求的輸入格式**, 我們 IP 目前完全還沒做這個打包, 這是實際要接線前一定要補的一塊, 不是已經做好的東西.

## 14. tx_adrv9009_tpl_core 的資料輸入沒有 valid/ready(已確認, 2026-07-09)
`ad_ip_jesd204_tpl_dac_channel.v:53-85` 完整 port list 裡, `dma_data`/`dac_iqcor_data_in` 這兩個資料輸入 port **完全沒有對應的 valid/ready pin**, 只有 `clk`. 代表這顆模組每個 clock 都會直接把 bus 上當下的值採進去, 沒有機制讓上游說「這筆還沒準備好」——每個 clk 都必須有有效資料餵進去, 這也是為什麼「迴圈攤平」(第 5-8 節)是這個插入點方案能不能成立的先決條件.

## 15. clk_0 實際頻率:已從權威來源確認是 250MHz(2026-07-09)
不需要重新 build——這個 Vivado 專案本來就有一份已經完整跑完 place & route 的結果(`adrv9009_zcu102.runs/impl_1/`, 2026-07-02 build 的), 直接讀裡面的報告即可:
- `system_top_clock_utilization_routed.rpt`:淨名對應到實際 cell, 確認 `axi_adrv9009_tx_clkgen/inst/i_mmcm_drp/clk_0` 這個 net 在報告裡叫 `mmcm_clk_0_s_2`.
- `system_top_timing_summary_routed.rpt` 的 Clock Summary:`mmcm_clk_0_s_2` 的 `Frequency(MHz) = 250.000`.
- 更進一步查到 **這不是隨便量到的, 是專案自己 `system_constr.xdc:85` 寫死的設計常數**:`create_clock -name tx_ref_clk -period 4.00 [get_ports ref_clk0_p]`——`ref_clk0_p` 是 ZCU102 板上實際接到 GT transceiver 的實體差動時脈腳位, 250MHz 是刻意選定、且**跟燒哪個 profile(100/200/400MHz 頻寬)無關**的板級常數.
- 之前(2026-07-07)以為 `clk_0=122.88MHz`(依據 FIR Compiler wizard 的 `Clock_Frequency=122.88` 參數)、以及後來反推的 `61.44MHz`, **兩個都不對**, 只是 wizard 設定參數/理論推算, 不是實際接線頻率.
- 這裡也順便釐清一個容易搞混的點:`talise_config.c`(`bw100/ir122.88` profile)裡的 `deviceClock_kHz=245760` 是**另一條時脈網**, 是 ADRV9009 晶片自己內部 PLL/類比前端的參考時脈(通常來自板上另一顆時脈晶片), 跟 FPGA 這邊 GT 用的 `ref_clk0_p`(250MHz)是兩條不同的實體線, 不需要相等.

## 16. 仍未解決的謎:250MHz × 2 samples/clock = 500 MSPS, 跟 122.88 MSPS 對不上(尚未定案)
`clk_0=250MHz` 確認了, 但跟 `DATA_PATH_WIDTH=2`(每個 clock 2 筆樣本)放在一起算, 介面容量是 500 MSPS, 遠大於 `bw100` profile 實際只需要的 122.88 MSPS(差約 4.07 倍, 不是乾淨的比例). 在 EngineerZone 查到 ADI 的說法是「TX 端 128bit@245MHz, 一次接收 2 筆連續樣本」, 但沒有講清楚 500 MSPS 這個介面容量, 在跑低頻寬 profile 時, 是不是每個 clock 真的都換新值, 還是 `tx_fir_interpolator` 只是沒把介面用滿(同一筆資料連續幾個 clock 重複輸出). 這個需要**實際跑一次 `tx_fir_interpolator` + `tx_adrv9009_tpl_core` 這段的行為模擬, 直接看 `dac_data`/`dac_valid` 波形**才能確定, 靠繼續讀 tcl/RTL 已經沒辦法再往下解了(這輪調查已經自己推翻自己好幾次, 不要再猜).

## 17. ADRV9009 profile 文件去哪裡找(2026-07-09)
專案負責人手上除了「用 zcu102+adrv9009 做出 SOQPSK」以外沒有拿到其他資訊, 只能靠讀 ADI 原廠範例反推. 確認過的資源:
1. **`no-OS` GitHub repo 的 `projects/adrv9009/profiles/`**——TES 軟體匯出的原始 profile 檔案(`talise_config.c`/`.h`), 是最直接權威的來源: https://github.com/analogdevicesinc/no-OS
2. **ADI Wiki No-OS 設定指南**: https://wiki.analog.com/resources/eval/user-guides/adrv9009/no-os-setup
3. **TES(Transceiver Evaluation Software)**——ADI 官方 GUI 工具, 用來產生/燒錄 profile, 也能讀出板子上實際燒的是哪個 profile.
4. **HDL 專案官方文件頁**: https://analogdevicesinc.github.io/hdl/projects/adrv9009/index.html

`no-OS/projects/adrv9009/profiles/` 底下實際上有三組現成 profile, 對應三個不同 TX 頻寬/速率:

| Profile 資料夾 | TX 頻寬 | Rate |
|---|---|---|
| `tx_bw100_ir122p88_...` | 100 MHz | 122.88 MSPS |
| `tx_bw200_ir245p76_...` | 200 MHz | 245.76 MSPS |
| `tx_bw400_ir491p52_...` | 400 MHz | 491.52 MSPS |

之前查到的三個各說各話的數字(我們自己 hdl repo 的 122.88、ADI linux repo 預設 dtsi 的 245.76、ADI HDL 文件頁規格表的 491.52), 都是三個合法 profile 各自的正確數字, 不是誰錯——**取決於你實際選哪一組燒到晶片上**.

**目前的判斷**:本地 `hdl` repo(`hdl_2023_r2` 分支)的 `adrv9009_bd.tcl` 寫死 `15.36→x8→122.88`, 剛好對上 `bw100/ir122.88` 這組. `no-OS` 該 profile 的 `talise_config.c` 也確認 `txInputRate_kHz=122880`, 跟本地 hdl 假設一致. **但這件事需要主動確認**:必須真的把 `tx_bw100_ir122p88_...` 這組 profile 檔案透過 TES 燒到實體 ADRV9009 上, FPGA 端跟 RFIC 端才算對齊, 不會自動同步.

## 18. JESD204 M/L/S/NP 設在哪裡(2026-07-09)
不是在 Vivado Block Design 的 GUI 畫布上點出來設的, 是**在 build 之前透過 `make` 命令列參數傳進去**. `system_project.tcl:27-35` 用 `get_env_param` 讀取, 預設值 TX: M=4, L=4, S=1(可用 `make TX_JESD_M=... TX_JESD_L=... TX_JESD_S=...` 覆寫, 註解範例見 `system_project.tcl:16-18`). NP(`TX_SAMPLE_WIDTH`)則沒有開放覆寫, 寫死在 `adrv9009_bd.tcl:24` 為 16. 這些參數被 `adrv9009_bd.tcl` 讀進去後, 才拿去呼叫 `adi_axi_jesd204_tx_create`/`adi_tpl_jesd204_tx_create`/`ad_xcvrcon` 等指令自動產生 block design 裡的 JESD204 IP 與接線.
- 影響:如果之後要換 M/L/S(例如換 profile 需要不同 lane/converter 數), 正確做法是帶新參數整個重新 `make` build, 不是在既有 block design 裡手動改單一顆 IP 的參數——牽動的東西(內插倍率、`TX_SAMPLES_PER_CHANNEL`、GT lane 連接)太多, 風險跟之前提過的「換單通道要重新 build 1-3 小時」同一等級.

## 19. 修正:換 profile 不代表同一份 IP 可以直接套用更高速率(2026-07-09)
曾經討論過「先做 122.88, 之後客戶要 245.76 只要重燒 profile, IP 若寫得好(例如查表法)應該能直接套用」——這個想法需要修正:
1. 換 profile 通常也要換 FPGA 端的 JESD204 M/L/S 設定與 `tx_fir_interpolator` 內插倍率, 不是 RFIC 端燒錄profile 就好, FPGA 端一樣要重新 build 對齊.
2. 就算 FPGA 端配合改好, IP 要撐 2 倍吞吐量只有兩條路:(a) 同樣「一個 clock 一筆樣本」架構, 把 `ap_clk` 拉到 2 倍頻——但目前 csynth 估計的關鍵路徑 7.256ns(≈137.8MHz)撐不住 245.76MHz(週期僅 4.07ns)所需的時序, 查表法(換掉 CORDIC)只是這條路上的一塊拼圖, 不是自動解決; (b) 維持 `ap_clk` 不變, 改成一個 clock 平行算 2 組樣本——這是實質架構改動, 尤其 phase accumulator 是遞迴的, 平行化不是輕鬆的事.
3. 結論:現在把 IP 設計乾淨(查表法、迴圈攤平)是為將來留餘裕的正確方向, 但不保證「以後直接套用不用改」, 真的要衝更高速率時幾乎確定要回來重新評估時序或改架構.

# 客戶規格(20Mbit/s @ SPS=8 → 160MSPS)驗證與 CORDIC 發散問題調查(2026-07-23)

新目標: 同事接手 ADRV9009 對接與 resample, 我只需專注在 IP 本身的設計與驗證, 確認客戶要求的 20Mbit/s 輸入(SPS=8, IQ rate=160MSPS)可以達成.

## 20. 160MHz 時脈可行性:確認可以, 已更新為官方 baseline
- `scripts/run_hls.tcl` 的 `create_clock` 從 `-period 10`(100MHz, 舊的寬鬆設定)改成 `-period 6.25`(160MHz, 精確對應客戶規格), 並用這個新時脈重跑官方 `solution1`(csim + csynth 都過).
- 結果: timing slack 4.56ns(critical path 約 1.69ns), `BYTE_LOOP` 仍是 `achieved II=1`、`Trip Count=inf`、`Pipelined=yes`——代表只要 `ap_clk>=160MHz`, 這顆 IP 每個 clock 都能吐出一組新 IQ, 滿足客戶規格.
- 注意: 在 160MHz 這個較緊的時脈下, HLS 選了完全不同的資源配置(DSP 109→3, LUT 14820→21337, FF 37206→10224), 不是舊的 100MHz 版本硬撐更快而已, 所以「先跑一個較鬆的時脈當作已驗證」不能直接套用到更緊的規格, 需要針對實際目標時脈重新 csynth 才算數.

## 21. Cosim 限制:重新確認過, 結論不變
`ap_ctrl_none`(free-running)+ `sps_sel` 這個 `s_axilite` port 混用, Vitis HLS 2023.2 的 cosim 直接拒絕(`WARNING: [COSIM] found non-self-synchronizing top I/O sps_sel`), 這是工具限制不是設計錯誤. `scripts/run_hls.tcl` 的 `cosim_design` 保持註解狀態. Vitis HLS 提供了一個叫 Cosim (C/RTL Co-simulation) 的功能, 它可以自動把你的 C++ 測試平台(Testbench)與合成出來的 Verilog/VHDL 硬體接起來做模擬. 但是 Cosim 的自動包裝工具(Wrapper)非常依賴 ap_start、ap_done 這類交握訊號來決定「什麼時候要把 C++ 裡的參數寫入硬體」. 拔掉了全局控制(ap_ctrl_none), Cosim 的自動包裝工具突然失去了「時間基準點」, sps_sel 這個 AXI-Lite 介面是一個非自我同步 (non-self-synchronizing) 的介面! 你沒有 ap_start 告訴我什麼時候該寫入這個值, 我不知道怎麼在模擬環境中驅動它.

## 22. 繞過 cosim: 手寫 XSIM testbench 直接驗證匯出的 RTL
不依賴 HLS 自己的 cosim/post-check 機制(對 free-running 設計的時序對齊本來就有問題, 見 `verify_tmp` 的舊發現), 改成:
- 用 `xvlog`/`xelab`/`xsim`(Vivado 內建, `C:\Xilinx\Vivado\2023.2\bin`)直接編譯 `hls_prj/solution1/syn/verilog/*.v` + 手寫的 SystemVerilog testbench, 完全繞開 HLS cosim.
- 產出物: `xsim_verify/tb_xsim_top.sv`(對真實 hls_prj 介面, 含 AXI4-Lite `sps_sel` write)、`xsim_verify/tb_xsim_top_sps16_default.sv`(不做任何 write, sps_sel 停在預設 0=SPS16 的乾淨對照組)、`xsim_verify/tb_xsim_verify_sps8.sv`(對 `verify_tmp` 簡化介面, sps_sel 編譯期釘死=1/SPS8, 完全沒有 s_axi_CTRL, 不可能 race)、對應的 `scripts/run_xsim_verify*.sh` 驅動腳本.
- 不再依賴 HLS 自動生成的 C/RTL 包裝, 而是直接拿 HLS 合成出來的最終產物(也就是 hls_prj/solution1/syn/verilog/*.v 裡的純 Verilog 原始碼).
- 工具鏈 (xvlog / xelab / xsim): 這是 Xilinx Vivado 內建的純文字介面模擬三劍客. xvlog: 負責編譯 (Compile) Verilog/SystemVerilog 程式碼. xelab: 負責展開與連結 (Elaborate), 建立整個硬體的階層架構. xsim: 負責實際跑模擬 (Simulate) 產生波形.
- 第一階段 (真實環境挑戰): tb_xsim_top.sv, 目標: 測試最真實, 最完整的 IP. 動作: 在 SystemVerilog 裡模擬 CPU 的行為, 按照 AXI4-Lite 的時序標準, 手動把訊號打進去, 真實寫入 sps_sel 暫存器. 這證明了「邊跑資料流、邊動態改參數」是成功的.
第二階段 (乾淨對照組): tb_xsim_top_sps16_default.sv, 目標: 確認預設狀態是否正常. 動作: 一樣接上完整的 IP, 但故意不去寫 AXI4-Lite. 讓 sps_sel 停留在 HLS 賦予的初始值(0, 也就是 SPS16).
- 第三階段 (極端隔離組): tb_xsim_verify_sps8.sv, 目標: 排除一切外部干擾, 純測核心演算法(針對特定的 SPS=8). 動作: 這個 TB 對接的是一個「被閹割/簡化」的 IP 版本 (verify_tmp). 這個版本裡, 開發者把 s_axi_CTRL 整個拔掉, 把 sps_sel 在編譯期直接釘死(Hardcoded)為 1 (SPS=8).
- 波形檔(`.wdb`)可在 Vivado 開啟: `xsim.bat <wdb 檔> -gui`. CSV 輸出格式跟 `tb_top.cpp` 的 `output_waveform.csv` 一致(`Sample,I_Data,Q_Data,TLAST`), 可直接跟 golden C model 逐點比對.

## 23. 意外發現: debug_current_bit/debug_alpha 是死接腳
從 `hls_prj/solution1/syn/verilog/tfm_modulator.v` 直接確認: `debug_current_bit`/`debug_alpha` 這兩個 `ap_none` scalar port 在 RTL 頂層被合成成 **`input`**, 而且模組內部完全沒有其他地方引用它們——`top.cpp` 裡對它們的寫入(`debug_current_bit = current_bit;`)在硬體上完全沒有接到任何輸出腳位, 是懸空的. 只有 `debug_pulse`/`debug_phase`/`debug_freq`(這三個是 `hls::stream`/axis 介面)才是真正有效的輸出. 這是 Vitis HLS 對「純量 `ap_none` 輸出」+「`ap_ctrl_none` 自由執行迴圈」這個組合的合成 artifact, 不是設計錯誤——但如果之後真的要靠這兩個訊號做 ILA/ChipScope debug, 現在的合成結果是看不到真實資料的.
- 這代表在硬體電路上, C++ 裡寫的那句賦值程式碼被 HLS 的死碼刪除(Dead Code Elimination, DCE)機制給徹底優化(閹割)掉了.
- ap_none 的特性: 這代表這是一根「純量裸線(Scalar wire)」, 沒有任何交握訊號(沒有 Valid, 沒有 Ready). ap_ctrl_none 的特性: 如前面所說, 這是一個沒有 ap_start/ap_done 的 Free-running IP. HLS 編譯器的邏輯盲區: 當這兩個條件碰在一起時, 編譯器的資料流分析(Dataflow Analysis)會感到困惑. 它看到這兩個變數沒有交握機制來證明「外面有人在等這筆資料」, 且整個 IP 又是無止盡運作的, 它就誤以為「這兩個變數寫了也沒人看」, 於是直接把它們的輸出邏輯拔掉, 甚至錯配成 input 腳位.

## 24. sps_sel AXI4-Lite write 的 race 問題(已確認根因, 已用替代方案繞過)
- **現象**: 用 `tb_xsim_top.sv`(真實介面, reset 放開後才做 AXI4-Lite write 把 `sps_sel` 設成 1)跑出來的結果, 從很早的 sample 就開始跟 golden 發散, 且發散型態是「先小後隨時間持續放大」.
- **根因**: `int_sps_sel`(`sps_sel` 的儲存暫存器, 在 `CTRL_s_axi` 子模組裡)跟整個資料通路(`BYTE_LOOP`)共用同一個 `ap_rst_n`/`ARESET`. `BYTE_LOOP` reset 一放開就立刻開始跑第一個 byte 的判讀, 而 AXI4-Lite write 必須等 reset 放開「之後」才能完成(`CTRL_s_axi` 的 `AWREADY`/`WREADY` 在 `ARESET` 期間恆為 0)——兩者之間有先天的時序競賽, `sps_sel` 幾乎不可能在 free-running 迴圈的第一次判讀之前就緒. 這代表: **在目前的硬體架構下, 想要在 reset 剛放開時就讓 `sps_sel` 停在非預設值, 本質上做不到**, 且這個「錯誤起跑」的暫態(precoder 歷史被污染 + `current_phase` accumulator 是持續累積不會自動歸零)會造成永久性的偏移, 不會隨時間自動收斂回正確軌跡.
- **繞過方式**: 用 `verify_tmp/`(`sps_sel` 編譯期釘死成常數, 完全沒有 `s_axi_CTRL` bundle)取得沒有 race 疑慮的 RTL, 這是後續(第 25-26 節)所有實驗採用的介面.

## 25. 加寬中間精度的兩個否證實驗(2026-07-23)
懷疑「128-tap FIR 加總」或「phase accumulator 累加」在 `ap_fixed<16,4>`(僅 4 個整數位元, 範圍 ±8)下有中間溢位, 且 `#pragma HLS UNROLL` 讓 HLS 把加總排程成平行加法樹(順序跟 C model 的循序 `+=` 不同), 非結合律誤差被放大.
- **實驗 1**: 把 `freq_dev` 的 128-tap 累加從 `data_t` 換成 `freq_acc_t = ap_fixed<24,12>`(多 8 個整數位元), 最後轉回 `data_t`. 確認 RTL 真的重建(pipeline 深度 17→18), 但跟(同樣加寬過的)新 golden 比對, **發散位置與數值逐位元完全相同**——128-tap 加總本身沒有真的溢位, 這個假設被推翻.
- **實驗 2**: 同樣手法加寬 `current_phase` 這個持續性 phase accumulator, 結果**又是逐位元完全相同**——phase accumulator 的累加本身也沒有問題.
- 兩次實驗都在 `verify_tmp/`(`VERIFY_FIXED_SPS_SEL=1`)上做, 驗證完都已還原成修改前的窄位元版本.

## 26. 用內部暫存器 hierarchical reference 直接鎖定 CORDIC(2026-07-23, 目前最新結論)
排除上面兩個假設後, 直接從 golden C model 的 `debug_phase` 逐 iteration 數值追出:「`current_phase` 的值本身」是否跟 RTL 一致, 才是關鍵問題.

- **debug axis stream 的陷阱**: 一開始想直接比 `debug_phase`(axis stream), 但發現這條 debug stream 的 `TVALID` 時序跟 `i_out`/`q_out` 的 `TVALID` 不是對齊的(各自在 pipeline 裡的位置不同, fill latency 不同), 直接拿兩者的 pulse counter 對比會找到一個乾淨但難以直接解讀的偏移量(測出 16), 且該偏移量會導致看不到我們真正關心的那個 sample 的值. **教訓: 不要用 debug axis stream 的 TVALID pulse 計數器去跟主要輸出對齊, 這個方法不可靠.**
- **正確做法**: 從生成的 RTL(`tfm_modulator.v`)直接追出 `current_phase` 對應的內部訊號 `ap_sig_allocacmp_in`(`debug_phase_TDATA` 就是從它 sign-extend 出來的, 可用 `grep -n "ln302\|allocacmp_in"` 追出), 在 testbench 用 hierarchical reference(`dut.ap_sig_allocacmp_in`)**每個 clock cycle 都讀取**, 不透過任何 axis stream 的 handshake, 完全避開上面的對齊問題. 另外準備一個絕對 clock cycle 計數器當共同座標, 讓「這個內部訊號的值」跟「`i_out` 在哪個 cycle 輸出哪個 sample」可以直接對應, 而不是用兩個獨立遞增的 pulse counter 去互相猜偏移量.
- 因為管線深度不同(Fill Latency 不同), debug_phase 的 TVALID 會比 i_out 提早 16 個 Clock cycles 跑出來. 如果你傻傻地拿兩邊的第 5 個 Valid pulse 來對比, 你比對到的其實是「不同時間點」產生的資料, 這會導致除錯時完全看錯狀態, 抓不到真正引發錯誤的瞬間.
- Hierarchical Reference (階層式參考): 這是在 SystemVerilog Testbench 中極度強大的功能. 你不需要把這個內部訊號拉成模組的實體腳位, 只要透過類似物件導向的路徑語法 dut.ap_sig_allocacmp_in (假設 dut 是你的 IP 實體化名稱), Testbench 就能像 X 光機一樣, 直接穿透模組, 看見晶片深處這個暫存器在每個 Clock cycle 的值.
- 正確的做法: 建立「絕對時間座標 (Absolute Clock Cycle), 不再使用「這是第幾個吐出來的 Valid 資料」這種相對計數法, 在 Testbench 裡自己寫一個絕對的 Clock Cycle 計數器, 現在, 除錯邏輯變成:「在絕對時間第 1000 個 Cycle 時, 我用 X 光機 (dut.ap_sig_allocacmp_in) 看到內部狀態是 A; 然後到了第 1016 個 Cycle 時, 我看到 i_out 輸出了數值 B.
- **結果**: 用已知吻合的 iteration 22→23 轉折點校正出 cycle-to-iteration 的對應公式, 驗證 iteration 9(I/Q 開始發散的那個點)的 `current_phase`:
  - golden(C model) 在 iteration 9 的 phase = 0.00000000
  - RTL(`ap_sig_allocacmp_in` 讀出來的) 在 iteration 9 的 phase = 0.000000
  - **兩者完全吻合**, 但同一個 iteration, `q_out` 的實際輸出: golden=0.000244141(≈sin(0)), RTL=0.002441(明顯不是 sin(0) 該有的值).
- **結論**: `current_phase` 進入 `hls::cos`/`hls::sin`(CORDIC)之前, C model 跟 RTL 完全一致——**問題確定出在 CORDIC 這一步本身**. 而且不是單次誤差: 同一個(接近 0 的)相位值連續好幾個 iteration, RTL 的 `sin` 輸出沒有維持在該有的常數值, 而是持續緩慢飄移, 比較像 CORDIC 這個共用/pipeline 化硬體單元, 內部可能有殘留狀態沒有隨每次呼叫乾淨重置, 不只是開機瞬間的一次性相位偏移問題.

## 27. 決定放棄繼續查 CORDIC 內部, 改用 256-entry LUT 取代(2026-07-27)
第 26 節鎖定問題在 `hls::cos`/`hls::sin`(CORDIC)這個 Vitis HLS 內建、封閉原始碼的元件, 繼續往內部查等於要逆向工具自己的排程, 投入產出比不確定. 改採直接換掉的方向:
- **table sizing 分析**: `data_t = ap_fixed<16,4>`(12 個小數位元, LSB≈2.44e-4 rad). 直接查表(無內插)要壓到 1 LSB 誤差需要 N≈π/LSB≈12869→取 2 的冪=16384 entries. 線性內插誤差公式 π²/(2N²), N=256 時誤差≈0.31 LSB, N=512 時≈0.077 LSB——內插版用 256 entries 就有安全邊際, 比直接查表省 64 倍空間, 只多一個乘法器.
- 精度要求: 系統使用的是 ap_fixed<16,4> 定點數(4 bit 整數, 12 bit 小數).其最小刻度 (LSB) 約為 2.44*10^{-4}弧度.
- 方案 A (直接查表): 如果不做任何內插, 要達到這個精度, 查表陣列需要 16384 筆資料. 在 FPGA 裡, 這會吃掉寶貴的 BRAM (Block RAM) 資源.
方案 B (線性內插法): 利用相鄰的兩個點連線來估算中間值. 根據誤差公式 {pi^2}/{2N^2}, 當點數 N=256 時, 最大誤差僅 0.31 LSB(遠小於系統要求的 1 LSB). 採用方案 B, 表格大小從 16384 狂砍 64 倍變成 256 筆, 代價僅僅是多花費一個乘法器 (DSP Slice).
- **實作**: `scripts/gen_sincos_lut.ps1`(產生 `src/sin_lut.inc`/`src/cos_lut.inc`, 256 筆, 涵蓋 `[-π,π)`, 跟 `gen_g_coeffs.ps1` 同款寫法)、`top.h` 新增 `LUT_SIZE`/`phase_pos_t`(`ap_fixed<24,10>`, 一次乘法同時取出 table index 跟內插權重, 不用額外除法)、`top.cpp` 用 `SIN_LUT[]`/`COS_LUT[]` + 線性內插取代 `hls::cos`/`hls::sin`, index 255→0 靠 `ap_uint<8>` 溢位自動 wrap(entry 0 跟隱含的 entry 256 是圓上同一點, 這個 wrap 是對的).
- 定點數切片的魔法: 一次乘法搞定 Index 與 Weight. 刻意設計了 24-bit 的定點數結構, 透過一次單純的乘法(將輸入相位乘上某個比例常數), 算出來的結果在二進位結構上, 會自然地分佈為兩部分: 高位元 (整數部分), 直接對應 0~255 的 Table Index(要抓表裡的哪兩個點). 低位元 (小數部分), 直接就是線性內插需要的權重 (Weight). 完全不需要除法器, 一個 Clock cycle 就完美萃取出索引和權重.
- 利用硬體「溢位 (Overflow)」實現完美的相位環繞 (Wrap-around): 三角函數是一個圓, 相位走到 2pi 時, 應該要回到 0. 刻意把 Table index 宣告為 8-bit 的無號整數 ap_uint<8>, 當 Index 算出來是 255, 再往前走一步變成 256 時, 因為 8-bit 裝不下(最大只到 255), 它在硬體上會自然溢位 (Overflow) 歸零變成 0.
- **C model 驗證**(丟棄式暫時專案, 驗完即刪): `csim_design` PASS, 1024 樣本. 額外做「單位圓半徑」`sqrt(I²+Q²)` 檢查, 全程穩定在 0.9994~1.0005, **沒有隨時間放大的趨勢**——這正是 CORDIC 缺少的特性.
- **RTL 合成**(`verify_tmp/`, resync 過 LUT 改動): `csim_design`/`csynth_design` PASS, 達成 `II=1`, `Depth=15`, **`Estimated Fmax=225.77MHz`**(比 CORDIC 版本寬鬆很多, 對後續衝更高吞吐量的目標也是加分).

## 28. LUT 在 RTL 裡的正確性: 用「自我一致性掃描」直接證實(2026-07-27)
第一次拿 RTL 輸出(`Sample` 編號)直接跟 golden CSV 逐點比, 從 sample ~11 開始就對不上、越後面差越大, 一度以為 LUT 在 RTL 裡也有問題. 深入後發現這是比對方法錯, 不是設計錯:
- 用 `debug_phase` axis stream 追查一開始顯示 phase 對不上, 但這正是第 26 節記錄過的「debug axis stream 陷阱」(TVALID 時序跟主輸出不對齊), 又踩了一次.
- **決定性測試**: 把 `sin_lut.inc`/`cos_lut.inc` 的表格資料與內插公式原封不動搬到 PowerShell 重算一次, 拿 RTL 內部 `current_phase` 暫存器(hierarchical reference, 每個 clock 都採樣)的實際數值去餵這個獨立重算的 LUT, 同時掃描「暫存器→輸出」之間可能的管線延遲(0~20 cycles). 在延遲=**8 cycles** 時誤差瞬間掉到 ~0.0009(約 2 LSB), 左右移動 1 cycle 誤差就暴增 300 倍以上——非常乾淨無歧義. 為了確認自己手刻的「查表法 + 內插法(LUT)」到底有沒有算錯, 或者有沒有被 HLS 工具合成壞掉, 特地在軟體(PowerShell)裡寫了一個絕對正確的「對照組」, 拿硬體抓出來的真實輸入去餵給軟體算, 最後證明硬體的運算結果跟軟體完美吻合(只晚了 8 個 Clock).
- 確定性函數 (Deterministic): 這在數位邏輯中是非常高的評價. 代表 LUT 模組是一個「純函數 (Pure Function)」. 給定一個 X, 8 個 clock 後一定會吐出完美的 Y. 沒有飄移, 沒有殘留狀態: 在 DSP 設計中, 最怕遇到變數忘記初始化, 或是迴圈裡有隱含的 Feedback (回授), 導致前幾次的運算狀態「殘留」下來污染現在的數值.
- **結論**: **RTL 裡的 LUT(ROM 讀取+內插)是 `current_phase` 的一個乾淨確定性函數, 固定 8-cycle 延遲, 沒有飄移、沒有殘留狀態. LUT 本身沒問題, 這點結論很穩.**
- 「RTL 輸出 vs golden CSV 逐點對不上」後來查到是比對方式的問題: RTL 的 `Sample` 計數器相對 `Cycle`(絕對 clock 數)有一個**完全固定的 +16 offset**(sample 0~115+ 都驗證過, 沒有任何 bubble), 用 `golden iteration = Sample + 16` 對齊後, 前 ~55 個樣本逐位元完全吻合.

## 29. 真正的發散點: current_phase 從 iteration ~48 開始平滑放大(2026-07-27)
延伸 hierarchical reference 探針到更大範圍, 系統性掃描 `current_phase` 整段軌跡: `golden iteration = cycle + 8` 這個固定關係一路精確吻合到 iteration ~47(含 byte 0 中段), 但從 **iteration ~48 開始出現一個很小(~1 LSB)、之後隨 iteration 平滑放大的落差**(不是突然跳一下). 這個時間點還在 byte 0 範圍內(離第一個 byte 邊界 sample 64 還有距離), 排除是 `bit_in` byte 邊界時序造成的.

## 30. 排除假設: 128-tap FIR 加總順序(2026-07-27)
懷疑 `#pragma HLS UNROLL` 讓 HLS 把 128-tap 加總排成跟 C model 循序 `+=` 不同順序的平行加法樹, 造成非結合律誤差.
- 先拿掉 `#pragma HLS UNROLL`——RTL 結構(暫存器名稱、`Depth`)完全沒變, 證實無效實驗(HLS 為滿足外層 `PIPELINE II=1` 自動照樣展開).
- 改用真正管用的 `#pragma HLS EXPRESSION_BALANCE off`(控制能不能把加總鏈重排成加法樹的開關). 確認真的改變結構(暫存器改名、`Depth` 15→20、`Fmax` 225.77→220.85MHz), 但重新對齊 `freq_dev` 後跟原版本比對, **每個 golden iteration 的誤差數值逐位元完全相同**.
- **結論: 128-tap FIR 加總順序/平行化不是原因, 已排除.** 跟第 25 節「加寬精度」實驗(否證溢位)從兩個不同角度印證同一件事: FIR 加總這個步驟本身是乾淨的. 診斷用 pragma 已改回原狀.

## 31. C model 演算法本身逐階段對過 Python golden, 全部通過(2026-07-27/28)
用 `src/soqpsk-tg.py`(使用者原本的 Python 浮點參考模型)驗證, 改成 SPS=8 + 完整 64-bit 連續輸入(原始腳本預設 SPS=16、只有 32-bit)重跑同一套 Block 1~5 邏輯, 程式化(非手抄)逐 sample 輸出比對:

| 階段 | 比對方式 | 結果 |
|---|---|---|
| Delta(差分編碼) | 64 點逐項比對 | 0 個不一致 |
| Alpha(precoder) | 64 點逐項比對 | 0 個不一致 |
| g(t) 脈衝係數 | 64 taps 逐項比對 | 最大差 1.67e-16(浮點誤差等級) |
| freq_dev(FIR 輸出) | 512 點逐項比對 | 最大差 0.0009(~3-4 LSB), 512 點裡只有 31 點超過 2.4 LSB |
| Phase | 512 點逐項比對 | 最大差 0.032, **有界、沒有隨時間發散** |
| I/Q | 512 點逐項比對 | 最大差 ≈0.032, 跟 Phase 誤差一致 |

**結論: C model(不管 CORDIC 版還是 LUT 版)的演算法本身沒問題**, 跟獨立的 Python 浮點參考模型吻合(整數階段完全一致, fixed-point 階段是正常有界的量化誤差, 不是邏輯錯誤). 這跟 RTL 那邊「持續放大、最後幾乎完全對不上」的性質完全不同, 後者不是量化誤差能解釋的.
(過程中手抄 Delta 表格時抄錯三個 byte 的數值, 被使用者拿他自己的 Python golden 一比對就抓到, 已更正——之後全面改成程式化比對, 不再手抄.)

## 32. testbench 本身的一個真實 bug: TVALID 多撐一拍, 已修但確認跟主線無關(2026-07-28)
重新檢查 `xsim_verify/tb_xsim_verify_sps8_lut.sv` 的 AXI4-Stream master 驅動邏輯(byte 0 手動段落 + `stream_byte` task), 兩處都有同一個 pattern:
```
while ((bit_in_TVALID && bit_in_TREADY)) @(posedge ap_clk);  // 偵測到 handshake 完成
@(posedge ap_clk);              // 又多等了一拍 ← bug
bit_in_TVALID <= 1'b0;
```
handshake 完成後又多等一拍才放 `TVALID`——如果 DUT 這邊的 `regslice_both`(register slice)在那多出來的一拍 `TREADY` 剛好還是高的, 會把同一筆資料**多送一次**. 已修正(handshake 偵測到就同一時刻放掉 `TVALID`).
- **修復後行為確實改變**: 樣本數 1337→1272, 模擬結束時間提早了整整 65 個 cycle(≈一個 byte 週期), 佐證修復前真的有多送一次的情況.
- 但重新比對 `current_phase`/`freq_dev`, **iteration ~48 那個發散點的數值完全沒變**(逐位元相同)——這個 testbench bug 是真的, 值得修, 但**跟主線發散問題無關**(divergence 起點在 byte 0 內部, 早於任何 byte 邊界, 這個 bug 只可能在跨 byte 邊界後才會顯現).

## 33. 嘗試追 shift_reg/impulse/alpha 在 RTL 的真實值, 目前失敗(2026-07-28)
想確認 iteration ~48 附近開始活躍的、比較深的 FIR tap 對應的係數/shift_reg 資料路徑是否正確, 連續三次嘗試都失敗: 在 Vitis HLS 中, C++ 的變數名稱轉成 Verilog 後通常會被加上一堆奇怪的後綴字(例如 _fu_358、_regslice). 開發者試圖在這些幾千行的程式碼中「猜」出自己要的訊號.
1. 猜測訊號 `icmp_ln183_reg_5645`(gate)+ `ap_sig_allocacmp_impulse`(value)手動組合——量出來的非零脈衝間隔(period 8)跟 golden 的稀疏型態(period 16 甚至更疏)對不上.
2. 懷疑是自己把兩個訊號拼在一起時產生了時間差(Race condition), 於是改去讀取 HLS 已經幫你組好的線. 改讀 RTL 已經算好的組合線 `debug_pulse_TDATA_int_regslice`——結果一樣, 排除了「兩個訊號手動組合有 race」這個理論, 但沒解決問題, 兩種方法都得到同一組錯誤資料.
3. 追 `debug_alpha`(scalar `ap_none`)的下游, 找到 `p_0_0_012527_fu_358`——量出來的是「連續保持 8 個 cycle 才變一次」的行為(比較像某個持續性狀態), 但實際數值(例如連續 6 個 bit 週期都是 +1)跟已確認正確的 golden alpha(`0,0,-1,0,1,0,-1,0...`)對不上, 第三次猜錯.

放棄猜測 RTL 自動產生的訊號名稱, 改成**在 `verify_tmp/` 新增一個自訂 `debug_alpha_stream`**(`hls::stream<data_t>`, 只在 `alpha` 算出來那行寫入一次, 語意 100% 確定, 不用反推): 改 `top.h`/`top.cpp`(新增 port + `#pragma HLS INTERFACE axis`)、`tb_top.cpp`(宣告/傳入/drain, 產生 `debug_alpha_stream.csv` golden 對照), 並用 **xsim 的 `open_vcd`/`log_vcd` + `get_objects -r`** 把整個 DUT 階層(1372 個物件)dump 成 VCD, 寫 Python 腳本(`parse_vcd.py`)直接從 VCD 重建這個新 stream 的行為(完全繞開 hierarchical reference 猜名字這件事).
- 為了確保讀出來的資料沒有被 SystemVerilog 測試平台的寫法干擾, 開發者動用了更底層的手段: Dump 全局波形 (VCD), 使用 xsim 的指令, 把待測設備(DUT)裡面所有 1372 個節點的波形, 全部錄製成標準的 VCD (Value Change Dump) 檔案.
- VCD 重建結果拍數(161)跟 testbench 探針量到的完全一致——**證實不是探針/testbench 錯, 訊號行為本身就是這樣**: 這個 debug stream 是 `if (s==0)` 觸發, 不管有沒有真實資料都會執行(idle 模式照樣跑, 維持連續載波相位), 所以在 1272-cycle 的模擬全程每 8 cycle 穩定觸發一次(1272/8≈159, 對得上 161), 不是只有真實的 64 個 bit 才觸發——這點本身合理, 不是 bug, 只是我原本的比對假設(只會有 64 拍)是錯的.
- 以為餵進去 64 個資料位元(bits), 硬體就只會觸發 64 次 alpha 運算. 但 VCD 顯示它觸發了 161 次! 為什麼? 因為這是通訊系統的硬體. 為了維持無線電載波的連續相位, 即使在沒有真實資料的「Idle 模式」, 硬體底層的狀態機依然會持續空轉、持續吐出相位. 所以在總長 1272 cycles 的模擬中, 每 8 個 cycle 觸發一次, 總共 159~161 次是完全合理的物理現象. 這證明了探針沒接錯, 是開發者一開始的「假設」錯了.
- 修正成只取 RTL 前 64 拍跟 golden 比, 用 -5~+5 拍小範圍位移去對齊, **依然對不上**(最好的位移也還有 31/62 拍不一致, 不是簡單的對齊問題).
- **矛盾**: 如果 alpha 真的錯這麼多(近 7 成), 由它算出來的 `freq_dev` 不可能在 iteration 30~40 還逐位元完全吻合(第 29 節已證實). 這代表**這次的 `debug_alpha_stream` 比對方法本身還有沒抓到的問題**(這個新 stream 雖然語意明確, 但「不是每個 iteration 都寫」, 在這顆高度 pipeline 化的設計裡的 handshake 行為顯然比預期複雜), 不是 alpha 真的錯. **這條路目前沒有可信結論, 卡在方法論, 不是找到新 bug.**

## 34. 真正的根因: reset 放開瞬間 byte0 被誤判成 idle, 全部資料位移一個 byte(2026-07-28, 用 Vivado GUI 肉眼確認)
第 33 節在方法論上卡住之後, 改用 Vivado GUI 直接開波形(`xsim.bat <wdb> -gui`, 手動把訊號拖進波形視窗)肉眼看 `debug_alpha_stream_TDATA`, 使用者讀出的前幾拍數值是 `-1,0,1,1,1,1,1,1,0,-1,...`.

- **手動推演驗證**: 把這串數字拿 `alpha` 的計算邏輯(`delta_prev1=1, delta_prev2=0, last_delta=0, odd_flag=false` 初始值)代入, 假設**連續 8 個 bit 全部 `current_bit=0`**(也就是完全處於 idle, 一個 bit 都不是真的)手動算一次, 算出來的序列是 `-1, 0, 1, 1, 1, 1, 1, ...`——**跟波形讀到的數值一模一樣**. 這證實:這串資料根本不是真正的 byte0 內容, 是 idle 模式算出來的.
- **加上正規 debug stream 直接確認**: 在 `verify_tmp/` 新增 `debug_idle_stream`(`idle_mode`)/`debug_current_bit_stream`(`current_bit`), 同樣在 `if (s==0)` 那個區塊寫入(跟 `debug_alpha_stream` 同一個時間點/頻率), 不用再猜測或反推. 結果**明確、無歧義**:

  | RTL bit index | Idle? | CurrentBit |
  |---|---|---|
  | 0~7 | 全部 `1`(idle) | 全部 `0`(dummy) |
  | 8~15 | `0`(真實資料) | `1,1,1,0,0,1,1,0` = golden byte0(`0x67`) |
  | 16~23 | `0` | `0,1,0,0,1,1,1,0` = golden byte1(`0x72`) |
  | 24~31 | `0` | `0,0,1,0,1,1,1,0` = golden byte2(`0x74`) |

  **RTL bit index N(N≥8)= golden bit index(N-8)**, 完全一致, 只差固定 8 個 bit(=1 個完整 byte, 64 sample)的位移.

- **根因**: `bit_in` 是 `hls::stream`/AXI4-Stream, `regslice_both` 這個暫存級電路在 `ap_rst_n` 低電位期間本身也被清空/鎖住, 不管輸入端 `TVALID` 在 reset 之前已經穩定多久, reset 放開的瞬間, regslice 內部輸出還是 reset 值, 需要至少一個真正的 clock edge 才能把外部已經穩定的 `TVALID` 反映到內部——**`BYTE_LOOP` 在 reset 放開後的第一個 cycle 就立刻執行第一次 `bit_in.read_nb()`, 這個時間點通常早於 regslice 的追趕**, 導致第一次讀取幾乎必然撲空, 整個 byte0 被判定成 idle. 真正的 byte0 內容要等到下一次 byte 邊界檢查(本來該輪到 byte1 的時機)才被讀到, 後面所有資料因此永久整體位移一個 byte.
- **兩種餵資料方式都躲不掉, 證實是 DUT 本身的行為, 不是 testbench 寫法問題**: 分別測過「byte0 在 reset 放開前就準備好」(舊寫法)跟「byte0 也跟 byte1~7 一樣, reset 放開後才正常 AXI4-Stream handshake」(新寫法, 也更貼近真實 DMA/PS 的送資料方式), **兩者的 `debug_idle_stream` 結果完全相同**——不管怎麼送, byte0 都躲不掉這個 race. `xsim_verify/tb_xsim_verify_sps8_lut.sv` 已統一 byte0 跟 byte1~7 用同一套 `stream_byte()` 邏輯(比舊寫法簡單, 也更真實).
- **串起之前查到的「iteration ~48 才開始發散」**: 之前用 freq_dev/current_phase 追出來的「iteration 40 之前逐位元完全吻合、之後才慢慢跑掉」, 其實就是**同一個 byte0-idle 誤判**造成的, 不是另一個獨立問題. 原因是 g(t)(FIR 脈衝響應)最邊緣幾個 tap 的係數極接近 0(第 25 節查過, ~1e-5 等級), 不管 impulse train 用的是「錯的」(idle 算出來的 `-1,0,1,1,1,1,1,1`)還是「對的」(golden 真實 byte0 的 `0,0,-1,0,1,0,-1,0`), 卷積出來的 `freq_dev` 在最初 20~40 個 sample 都幾乎是 0, 兩者的差異被 g(t) 邊緣係數蓋住、看不出來——一直要等到 impulse 移動到 g(t) 中段真正有份量的 tap, 兩者的差異才會被放大到看得出來, 剛好對上 iteration ~40~48 這個轉折點. **之前「早期完全吻合」不是因為 byte0 真的被正確讀到, 是巧合被 g(t) 邊緣係數掩蓋而已.**

## 35. 這件事對真實系統整合的含義, 以及潛在修法(待決定, 2026-07-28)
- 這不只是驗證方法的問題: **如果真實硬體上這顆 IP 也有同樣的行為, 代表每次 `ap_rst_n` 放開之後, 第一個 byte 都有被吃掉、後面資料整批位移一個 byte 的風險**. 之後接上真實 DMA/PS 時需要留意(例如確保 reset 放開後、真正開始收資料前有緩衝, 或接收端能容忍/校正這種一次性位移).
- 討論過一種可能的修法方向: 把 `iter_in_byte==0` 那次的 `bit_in.read_nb()` 從「只在 byte 邊界檢查一次, 沒讀到就整個 byte 判定為 idle、64 個 cycle 後才重試」, 改成「在還沒收到過第一筆真實資料之前(可以用一個新的 `static bool` state, 例如 `cold_start`), 每個 cycle 都重試 `read_nb()`, 直到第一次讀到真實資料才開始正式的 64-cycle-per-byte 計數」——這樣不管 regslice 追趕要花幾個 cycle, 都能自然涵蓋, 不用去猜測/寫死一個固定的等待值.
  - 這是對 `src/top.cpp`(不只是 `verify_tmp/`)的**真實設計改動**, 需要決定 cold-start 期間要不要維持每個 cycle 都輸出(目前設計是不管有沒有真實資料都必須連續輸出 IQ, 維持 carrier phase 連續, 這是客戶規格的硬性需求, 見文件前段的 122.88/245.76MSPS 討論)——如果 cold-start 期間仍要持續輸出 idle carrier, 只是延後「byte 邊界計數器」真正開始的時間點, 影響範圍較小; 如果要完全不輸出直到收到第一筆資料, 就違反了「每個 clock 都要有 IQ 輸出」這個規格, 不建議.
  - 尚未實作, 待使用者決定是否要往這個方向修 `src/top.cpp`.

## 36. cold_start 修法在 verify_tmp 驗證成功: RTL 跟 golden 從此逐位元完全吻合(2026-07-29)
第 35 節提出的修法(`static bool cold_start = true;`, 在還沒收到過第一筆真實資料之前每個 cycle 都重試 `bit_in.read_nb()`, `iter_in_byte` 保持在 0、不進入正常的 64-cycle-per-byte 計數, 直到真的讀到第一筆資料才開始; 期間 `i_out`/`q_out` 仍照常每個 cycle輸出, 只是用 alpha=0 的 idle carrier)已經在 `verify_tmp/` 實作並驗證, **完全有效, 而且比預期還乾淨**.

- **csim**: `TEST PASSED`, 跟修改前行為一致(cold_start 在 C 模擬裡瞬間 resolve, 因為 `tb_top.cpp` 一次把 8 個 byte 全塞進去, 不影響任何既有的 golden CSV).
- **csynth(使用者同事關心的 II/pipelined 問題)**: `Pipelining result: Target II = 1, Final II = 1, Depth = 15`, `Estimated Fmax = 225.77MHz`——**跟修改前完全相同, II=1 達標, pipeline 成功, cold_start 這個改動沒有任何時序代價**.
- **RTL 驗證(`debug_idle_stream`/`debug_current_bit_stream`)**: byte0 的 64 個 bit **從 index 0 開始**就正確對上 golden, 不再有 idle 誤判跟 byte 位移, 0 個不一致.
- **freq_dev/current_phase(hierarchical reference, 全部 512 個 iteration)**: 重新找到正確的 cycle-to-iteration 對應公式(`golden_iteration = cycle - 7`, 這次是負的 offset, 因為 cold_start 改變了整體管線時序; 之前只搜尋正 offset 是失誤, 一度誤以為修復後還有殘留差異, 後來擴大搜尋範圍到負值才找到, 教訓: 每次改動後都要重新用「掃描找尖銳最小值」的方法找 offset, 不要沿用舊值也不要只搜正方向). 用正確 offset 比對, **512 個樣本誤差最大值 0.000001, 0 個不一致**.
- **i_out/q_out 輸出(offset = -1)**: 同樣**512 個樣本全部逐位元吻合, 最大誤差 0.000001, 0 個不一致**.

**結論**: 之前查到的「iteration ~48 開始平滑放大的落差」**完全就是這個 byte0-idle-誤判/byte 位移造成的, 沒有其他獨立的 bug**——不是 LUT、不是 FIR 加總順序、不是 alpha 計算邏輯、不是 DSP48 rounding, 就是這一個 reset 時序 race. 一次修正, 整條 pipeline(precoder → FIR → phase → LUT → I/Q)從頭到尾都乾淨.

此修法**目前只在 `verify_tmp/` 驗證, 尚未套用到 `src/top.cpp`(正式設計)**, 使用者已決定先不動 `src/top.cpp`, 等確認有效後再議.

## 37. 目前狀態與待辦(2026-07-29)
- **整個發散問題已經徹底解決, 根因跟修法都已確認**:
  1. 256-entry LUT 正確取代 CORDIC(第 27/28 節).
  2. C model 演算法跟 Python 浮點參考模型吻合, 無邏輯錯誤(第 31 節).
  3. 128-tap FIR 加總順序/平行化不是問題(第 30 節).
  4. `xsim_verify/tb_xsim_verify_sps8_lut.sv` 的 TVALID 多撐一拍 bug 已修正(第 32 節).
  5. **真正根因**: `bit_in` 的 AXI4-Stream `regslice_both` 在 reset 放開瞬間還沒追趕上, `BYTE_LOOP` 第一次 `read_nb()` 幾乎必然撲空, byte0 被誤判成 idle, 後面資料永久位移一個 byte(第 34 節).
  6. **修法(cold_start)已在 `verify_tmp/` 驗證成功, RTL 跟 golden 512 個樣本逐位元完全吻合, 且不影響 II=1/pipeline/時序**(第 36 節).
- **待決定**: 要不要把 cold_start 這個修法套用到 `src/top.cpp`(正式設計). 使用者傾向的做法(第 35 節分析過): cold-start 期間仍要維持每個 cycle 都輸出 IQ(用 idle carrier), 只延後 byte 邊界計數器真正開始的時間點, 才不會違反「每個 clock 都要有 IQ 輸出」這個客戶硬性規格.

# IP 封裝準備與 cold_start/開機時序討論(2026-08-05)

準備把 `tfm_modulator` 封裝成 IP 交給同事使用, 討論了封裝前的檢查項目、cold_start 的運作原理、差分編碼對 seed 污染的自我修復特性, 以及接上 ADRV9009 之後的開機時序. 以下依當天討論順序記錄.

## 38. `HW_DEBUG_MODE` 已註解掉, 準備 release
`src/top.h:7` 的 `#define HW_DEBUG_MODE` 已改成 `// #define HW_DEBUG_MODE`, 依照該行原本的註解指示("comment out this line if release IP")執行.
- **原因不只是「少幾個 port」**: `debug_pulse`/`debug_phase`/`debug_freq` 是 `axis` 介面, `hls::stream::write()` 在 AXI-Stream 下是阻塞式呼叫, 會等 TREADY. 如果封裝後這三個 axis debug port 沒接下一級, TREADY 通常視為 tie-low, `write()` 永遠等不到, 會讓整個 `BYTE_LOOP` pipeline 死鎖(`debug_pulse.write()` 發生在 FIR/phase 運算之前), 連帶 `bit_in`/`i_out`/`q_out` 都會停擺. `debug_current_bit`/`debug_alpha` 是 `ap_none`(留空安全, 見第 23 節), 但整組 debug 用同一個巨集開關, 沒有理由只關一半.
- **改動後必須重跑 `csim_design`/`csynth_design`**: 介面少了 3 個 axis + 2 個 ap_none port, 舊的 `csynth.rpt`(2026-07-23 跑的)是含 debug port 的舊介面結果, 不能直接拿來當作 release 版本的時序依據.

## 39. IP 封裝來源確認: 用 `hls_prj/solution1`(`scripts/run_hls.tcl`), 不是 `hls_component`
專案裡同時存在兩個 HLS 專案設定, 時脈不一致:
- `hls_prj/solution1`(`scripts/run_hls.tcl`): `create_clock -period 6.25`(160MHz), 對應客戶規格(20Mbit/s @ SPS=8), 有 README/Note.md 記錄依據, 且已經產出過 `impl/verilog`、`impl/vhdl`(`export_design` 曾經跑過, 目前 `run_hls.tcl` 裡是註解狀態).
- `hls_component/hls_config.cfg`(Vitis 統一 IDE component): `clock=10ns`(100MHz), 沒有更新過, 是另一條沒同步的設定.
**確認**: 封裝 IP 走 `hls_prj/solution1` 這條路(`run_hls.tcl` 最後一行 `export_design -format ip_catalog ...`), 避免同事拿到用錯 clock 驗證過的版本.

## 40. IP 匯出格式與匯入方式
`export_design -format ip_catalog` 會在 `hls_prj/solution1/impl/ip/` 產生**一個 `.zip` 檔**(標準 Vivado IP Catalog 格式), 內含 `component.xml`、HDL(verilog/vhdl)、`xgui/`(GUI 客製化參數)、driver、doc, 是交付給同事的唯一檔案, 不需要額外收集其他東西.
- **同事匯入**(雙方都是 Vivado 2023.2, 無需 IP upgrade): 解壓縮到專案旁的 `ip_repo/` 資料夾 → Vivado `Settings → IP → Repository → Add Repository` 指向該資料夾 → Apply 後自動刷新 IP Catalog → 在 Catalog 搜尋 `tfm_modulator`(vendor=user, library=hls)使用.
- **git 版控**: 不需要把 zip 放進本倉庫——`.gitignore` 本來就已經排除 `hls_prj/`(line 53)跟 `*.zip`(line 82), 跟一般作法一致(HLS 產出的 IP 是可重新產生的 build artifact, 真正該版控的是 `src/top.cpp`/`src/top.h`/`scripts/run_hls.tcl`). 建議 IP 用版本號命名, 避免同事不確定拿到的是哪一版.
- **封裝前檢查**: `csim_design`(功能, 不受 cosim 限制影響)+ `csynth_design`(時序/資源估算)都要重跑; `cosim_design` 因 `ap_ctrl_none`+`s_axilite` 組合被 Vitis HLS 2023.2 拒絕(第 21 節), 沒有 RTL 層級的自動比對, 要驗證 RTL 對 golden 需要靠第 22 節的手寫 XSIM testbench workaround, 不在 `run_hls.tcl` 範圍內.

## 41. cold_start 運作機制解說(對照 `verify_tmp/src/top.cpp`)
四處改動(尚未套用到 `src/top.cpp`, 詳見第 36 節):
1. `static bool cold_start = true;`(line 152)——只在「還沒收到過第一筆真實資料」期間為 true.
2. `iter_in_byte` 前進邏輯(line 385-390): `cold_start` 為 true 時焊死在 0, 讓下一個 cycle 又落回 `iter_in_byte==0` 那個 block, 等於強迫每個 cycle 都重試一次 `bit_in.read_nb()`(原本只在 byte 開頭試一次, 沒讀到就要等 64 cycle 後才重試).
3. 讀到真資料就解除鎖定(line 185-189): `read_nb()` 成功時同一個 cycle 把 `cold_start` 清成 false.
4. 期間抑制 precoder 誤觸發(line 208, `if (s == 0 && !cold_start)`): 沒有這個 guard, cold_start 重試期間 `s` 恆為 0, 會讓 differential encoder/precoder 狀態機被連續假的 `current_bit=0` 事件推壞; 加上 guard 後這段只在真正收到 byte0 那個 cycle 執行一次.
- **零時序代價的原因**: `i_out.write()`/`q_out.write()`(line 378-379)所在的資料路徑(`cos_val`/`sin_val` ← `current_phase` ← `freq_dev` ← `shift_reg`/`impulse`/`alpha`)完全不讀 `cold_start`, 跟 `cold_start`/`iter_in_byte` 更新是兩條互相獨立的組合邏輯錐, HLS 排程看的是資料相依關係不是 C code 文字順序, 所以新增的 1-bit register 不會進入 `i_out`/`q_out` 的關鍵路徑. 已在 `verify_tmp/` 驗證: II=1、Depth 15、Fmax 225.77MHz 跟修改前完全相同, RTL 對 golden 512 個樣本逐位元吻合.
- **根因對照**: `bit_in` 的 AXI4-Stream `regslice_both`(HLS 自動加的暫存器級, 不是組合線)在 `ap_rst_n` 放開瞬間需要至少一個 clock edge 才能把外部已穩定的資料反映出來, 原設計只在 byte 開頭試一次 `read_nb()`, 幾乎必然撲空, 造成 byte0 被誤判 idle、後面資料永久位移一個 byte. cold_start 把「只試一次」改成「連續重試到成功為止」, 自然涵蓋任意長度的追趕時間.

## 42. 差分編碼對 seed 污染的自我修復特性(手推代數論證, 尚未實測驗證)
使用者提出: 如果不套 cold_start, 但持續灌相同的 repeating bit pattern, 錯誤的初始狀態是否會隨時間自我修復? 推導如下:
- `delta[n] = current_bit[n] ^ (1-last_delta[n-1])`(或 `^ last_delta[n-1]`, 視 `odd_flag`)是 GF(2) 上的仿射遞迴. 定義誤差 `e[n] = delta_corrupted[n] ⊕ delta_pristine[n]`, 代入遞迴可得 `e[n] = e[n-1]`——**seed 污染會變成一個貫穿整個 delta 序列的固定全域翻轉, 不會發散也不會自己歸零**.
- `alpha` 只看 `delta[n]` 是否等於 `delta[n-1]`/`delta[n-2]`(相等判斷), 全域固定翻轉不影響「是否相等」(A⊕1==B⊕1 等價於 A==B). 所以只要 `delta_prev1`/`delta_prev2` 兩個歷史欄位都被真實 payload 算出的 delta 填滿(約 2-3 個真實 bit 之後), `alpha`(進而 phase/IQ 波形內容)會跟乾淨版本**逐位元精確相等**, 不是「趨近」.
- **不受影響的部分(另一個獨立機制)**: byte-boundary 少處理一次是排程上真的少跑一次, 不是代數誤差, 不受此自癒特性影響——**延遲(晚一個 byte 週期)與 TLAST/burst 邊界偏移永遠存在, 直到下次 reset**, 跟 payload 是否重複無關.
- **結論**: 對「持續灌 repeating pattern, 只在乎解不解得出內容」的情境, 這個推導支持使用者「內容最終會解對」的判斷. 但(a)這段推導只是讀 code 手推的代數論證, 沒有像 cold_start 一樣有 `verify_tmp/` 的 512 樣本實測數字佐證, 真要驗證應在 `verify_tmp/`(不套 cold_start)灌 repeating `0xEB90` 實測收斂所需的 bit 數; (b)IP 是要交給同事用在不完全掌握的情境, 不是所有情境都符合「payload 剛好重複」的前提, 且延遲/TLAST 偏移這個問題完全沒被解決. 兩點合併, 最終還是建議套用零成本、已驗證、對所有 payload 通殺的 cold_start, 而不是依賴這個條件式成立的自癒論證去 ship.

## 43. 開機到系統運作的時序(接 ADRV9009 整合規劃, 第 9-19 節那份"尚未實作"的設計討論)
- **已確定(BIF 分割順序決定, README line 118-143)**: SD 卡開機 → `zynqmp_fsbl.elf` → `pmufw.elf` → **`system.bit`(PL 配置, 含 `tfm_modulator`, 在此階段透過 PCAP 燒入)** → `bl31.elf` → `u-boot.elf` → Linux(`Image`, Kuiper Linux). **PL 配置發生在 FSBL 階段, 早於 ATF/u-boot/Linux 開機**. PL 配置完成瞬間, fabric 裡所有 IP 已經物理上存在但預設被 reset network 壓住(硬體 reset, 不需 CPU 介入即生效, 但需要有東西主動放開).
- Linux 開機後, Kuiper Linux 內建的 ADI IIO/JESD204 driver 接手, 把 ADRV9009 RFIC、既有 TX2(`axi_adrv9009_tx_dma`, `CYCLIC=1`)路徑、JESD204 link(GT transceiver)帶起來, 這段是 ADI 既定流程.
- **`tfm_modulator` 的 `ap_rst_n` 怎麼被放開, 曾經是待決定事項, 現在拍板選方案 A**: `tfm_modulator` 的 `ap_clk`/`ap_rst_n` 接到 `axi_adrv9009_tx_clkgen/clk_0` 同一個 domain(跟 `tx_adrv9009_tpl_core/link_clk` 一致, 第 10 節規劃 line 91), **`ap_rst_n` 跟著 JESD204 TX core 自己的 reset 一起被 ADI driver 自動放開, 不另外接 GPIO 讓 PS 軟體獨立控制**. 好處: 天生跟 JESD204 link 綁在一起放開, 不用費心兩者的先後順序; 代價: 使用者自己的 app 無法單獨重置這顆 IP, 只能跟著整個 JESD204 TX 一起重置.
- **建議操作順序**(在方案 A 之下具體化):
  1. 等 `clk_0`(250MHz, GT reference clock 衍生, 第 15 節已確認)穩定, 這是 JESD204 GT 初始化的一部分.
  2. `sps_sel` 要在 `ap_rst_n` 還 asserted 時透過 AXI4-Lite 寫好(`top.cpp` 既有規範).
  3. 新的 `axi_dmac`(接 `bit_in`, `CONFIG.CYCLIC 0`, 第 10 節規劃)先做好 descriptor/buffer 設定, 不急著送資料.
  4. `ap_rst_n` 隨 JESD204 TX core reset 一起放開(方案 A, 不用 PS 額外動作), cold_start 開始運作.
  5. `axi_dmac` 之後任何時間點觸發送真實資料進 `bit_in` 都安全, cold_start 保證不會有 byte0 誤判位移.
- **一個跟 cold_start 無關的硬限制**: `tx_adrv9009_tpl_core` 的 `dac_data` 輸入完全沒有 valid/ready(第 14 節已確認), 每個 clock 直接採樣. `tfm_modulator` 的 `ap_rst_n` 必須在 JESD204 TX link 真正開始取樣 `dac_data` 之前就已經放開——方案 A(共用同一個 reset)天生滿足這個限制, 這也是選方案 A 的理由之一.
- **還沒補上的缺口(跟開機順序無關, 但是"整個系統開始運作"前必須做完的事)**: `dac_data_0/1` 要求 32-bit、每 32-bit 裝同一 channel 連續兩筆樣本(第 12 節已確認), 但 `top.cpp` 目前 `i_out`/`q_out` 一次只寫 1 筆 16-bit, 完全還沒做這個打包(第 13 節).

## 44. `link_clk` 是 line-rate/40, 不是可自由指定的參數——`ap_clk` 與 `link_clk` 需要 CDC, 打包 IP 尚未實作(2026-08-05)
使用者疑問: 客戶規格確定 `ap_clk`=160MHz(每個 clk 吐一筆 IQ)之後, `tx_adrv9009_tpl_core` 要不要也接 160MHz? 若接 160MHz、`DATA_PATH_WIDTH=2`, 算出來的 IQ rate 會變成 320MHz, 感覺不對. 查了 ADI HDL 原始碼確認如下:

- **`link_clk` 是被 JESD204 line rate 鎖死的, 不是自由參數**: `C:\XilinxWorkspace\Vivado\hdl\library\jesd204\ad_ip_jesd204_tpl_dac\ad_ip_jesd204_tpl_dac.v:63-64` 原廠註解直接寫 `// link_clk is (line-rate/40)`. Line rate 由 JESD204 M/L/S/NP(本專案已確認 M=4, L=4, NP=16)+ GT transceiver 實體 line rate 決定, 跟 `tfm_modulator` 想接多少 Hz 完全無關. **不能把 `link_clk` 改成 160MHz 去配合 `tfm_modulator`**.
- 這個既有 build 裡 `link_clk`(=`clk_0`)已確認固定 250MHz, 且是「跟燒哪個 profile 無關的板級常數」(第 15 節, `system_constr.xdc:85` 寫死 `ref_clk0_p`).
- **推翻第 43 節"方案 A"的前提**: 第 43 節選方案 A 時假設 `ap_clk`/`ap_rst_n` 可以跟 `link_clk` 接同一個時脈域(沿用第 10 節 2026-07-09、在客戶規格確定前寫的規劃). 現在確認 `ap_clk`(160MHz, 客戶規格鎖定)與 `link_clk`(250MHz, JESD204 line rate 鎖定)是**兩個各自被鎖死、對不上的時脈**, 中間**必須有 CDC(跨時脈域)**, 不可能是同一個 domain. 方案 A「天生同一個 domain、不用管兩者放開先後順序」這個好處不成立了, 需要重新評估——如果 `ap_rst_n` 改成獨立的時脈域, 方案 A/B 的抉擇(第 43 節)需要重新討論(例如即使兩者共用同一條實體 reset 訊號源, 各自時脈域仍需要各自的 reset synchronizer, 不是接同一條線就天然安全).
- `DATA_PATH_WIDTH=2` × `link_clk`(250MHz)算出來的介面容量是 500 MSPS, 遠大於實際需要的 122.88/160 MSPS——這個第 16 節就標記過的「未解之謎」(是否每個 link_clk 都真的有新樣本, 還是有 hold/repeat)這次查原始碼沒有解開, 仍需要之後實際跑 `tx_fir_interpolator`/`tx_adrv9009_tpl_core` 的波形模擬才能確認.
- **打包/CDC 這顆轉接 IP 完全還沒做**: 目前確認新的 block diagram 不會有 `tx_fir_interpolator`(DMA 直接餵 `tfm_modulator`, `tfm_modulator` 直接輸出), 中間需要一顆(可能是同事另外做的)IP 把 `i_out`/`q_out` 的 16-bit/1-sample-per-clock 資料, 經過 CDC(160MHz→250MHz)後打包成 `dac_data` 要的 32-bit/2-samples-per-clock 格式, 才能接 `tx_adrv9009_tpl_core`. **這顆 IP 目前完全不存在, 是待辦事項**——依 2026-07-23 的分工(第 155-157 行), 這屬於同事負責的「ADRV9009 對接與 resample」範疇, 但設計前務必先讓同事知道: (a) 這是 CDC + rate matching, 不是單純格式打包; (b) `link_clk` 不能改, 只能配合它設計; (c) 500MSPS 介面容量對實際取樣行為的疑問尚未透過模擬證實.
- 這次(2026-07-27/28/29)新增的檔案都在 `xsim_verify/`(`tb_xsim_verify_sps8_lut.sv`、`xsim_vcd_dump.tcl`、`dut_full_dump.vcd`、各種 golden/rtl 比對用 CSV)與 `scripts/gen_sincos_lut.ps1`, 都保留下來方便之後參考. `verify_tmp/` 已加上 `debug_alpha_stream`/`debug_idle_stream`/`debug_current_bit_stream`(診斷用)跟 cold_start 修法(實驗性, 尚未套用到 `src/top.cpp`).

## 45. cold_start 正式套用到 `src/top.cpp`, csim/csynth/RTL-vs-golden 三層驗證全過(2026-08-06)
使用者決定不再等,直接把 cold_start 套進正式設計並重新驗證,IP 封裝暫緩到驗證結果出來後再議.

- **套用內容**: 把第 41 節記錄的四處改動(`static bool cold_start = true;`、`read_nb()` 成功時清 `cold_start`、`if (s==0 && !cold_start)`、`iter_in_byte` 前進邏輯的 `if (cold_start) {...} else if (...)`)原封不動搬進 `src/top.cpp`(不是 `verify_tmp/`),邏輯跟 `verify_tmp/src/top.cpp` 一致.
- **`csim_design`**(`hls_prj/solution1`, 走 `run_hls.tcl`):`TEST PASSED`, 1024 samples, 0 errors(SPS=16, `tb_top.cpp` 預設 `TEST_SPS_SEL=0`).
- **`csynth_design`**:`BYTE_LOOP` 仍是 `achieved II=1`、`Depth=15`、`Trip Count=inf`、`Pipelined=yes`,`Estimated Fmax=220.51MHz`(160MHz 目標有餘裕)——cold_start 沒有時序代價這件事在正式設計上也成立. Top-level `tfm_modulator` 整體 slack 仍是razor-thin 的 0.02ns(舊版拿掉 debug port 前是 0.01ns),這是 CTRL wrapper 既有的特性,不是 cold_start 造成的,跟第 39/40 節記錄的疑慮一致,打包前仍建議留意.
- **資源**(拿掉 debug port 後):BRAM 2、DSP 4、FF 3192、LUT 15494(5%)——比含 debug port 的舊報告(FF 10224、LUT 21337)小很多,主要是拿掉三條 debug axis stream 的緣故.
- **RTL 對 golden 逐位元比對**(新增 `xsim_verify/tb_xsim_top_sps16_default_coldstart.sv`,對真實 `hls_prj/solution1/syn/verilog/*.v`,SPS=16 預設值、不寫 AXI4-Lite、byte0 到 byte7 全部走 reset 放開後的正常 `stream_byte()` handshake):**1024/1024 樣本全部逐位元吻合,最大誤差 0.000001,TLAST 0 個不一致**,offset=0(不需要對齊位移),跟第 36 節 `verify_tmp/` 當初驗證 cold_start 的精度同一個量級.
  - 過程中一度在 offset=0 measured 879/1024 mismatch、byte0 完全正確但 byte1 開始跟舊的「byte0 誤判 idle」同樣的訊號特徵發散——**後來確認是這次新寫的 testbench 本身有兩個問題,不是設計或 cold_start 的問題**:(a) `stream_byte()` 沿用了第 32 節記錄過的舊 bug 寫法(handshake 偵測到後多等一拍才放開 TVALID,可能把同一筆資料多送一次);(b) byte0 用了已經被取代的「reset 放開前預先塞好」寫法,沒有跟 byte1-7 一樣走 reset 放開後的正常 handshake. 對照第 36 節已驗證乾淨的 `tb_xsim_verify_sps8_lut.sv` 寫法修正這兩點後,問題消失. **教訓:寫新 testbench 時要先比對已驗證版本的 handshake 寫法,不要沿用舊模板裡已知有 bug 的部分.**
- **環境小插曲(跟設計無關)**:`scripts/run_hls.tcl` 沒有 `exit`,`csynth_design` 跑完後 Vitis HLS 掉進互動式 `vitis_hls>` 提示字元迴圈,在這次背景執行(非真正互動終端機)的情境下卡住等不到輸入、不會自己結束(卡了約 75 分鐘才發現,靠殺行程 `taskkill` 解決,csim/csynth 結果本身在卡住前就已經跑完並寫入報告了,不受影響). 如果之後還要用背景/自動化方式跑 `vitis_hls -f scripts/run_hls.tcl`,建議在腳本最後加一行 `exit`;這屬於腳本改動,尚未執行,待使用者確認要不要加.
- **結論**:cold_start 已經是正式設計的一部分,csim/csynth/RTL-vs-golden 三層驗證全部通過,`src/top.cpp` 目前跟 `verify_tmp/` 驗證過的行為一致. IP 封裝(`export_design`)仍未執行,等使用者確認要繼續才進行.

## 46. csynth.rpt 的三層結構解說, 以及「wrapper 路徑」猜測的更正(2026-08-06)
使用者對第 45 節提到的 timing 數字追問細節, 釐清 HLS 把設計合成成的三層結構, 以及 top-level slack 0.02ns 到底代表什麼.

- **三層結構**(對應 `csynth.rpt` 表格的三行, 由外而內):
  ```
  tfm_modulator                        (最外層: 整顆 IP, 含 CTRL_s_axi(AXI4-Lite sps_sel 暫存器)、bit_in/i_out/q_out 三個 axis 的 regslice_both)
   └─ tfm_modulator_Pipeline_BYTE_LOOP  (HLS 自動切出來的子模組, 把 while(1) 迴圈整個包成一個硬體區塊, 含迴圈進出的邊界邏輯)
       └─ BYTE_LOOP                     (迴圈本身: byte/bit 解碼、差分編碼器、128-tap FIR、相位累加、LUT 查表、輸出格式化)
  ```
- **三個 slack 數字, 分屬三層**(第 45 節的 csynth.rpt 數字):`tfm_modulator`=0.02ns、`tfm_modulator_Pipeline_BYTE_LOOP`=0.03ns、`BYTE_LOOP`=4.56ns. 第一次回答時猜測「top-level 貼著上限的瓶頸很可能是 `CTRL_s_axi` 的解碼邏輯」——**這個猜測後來自己更正**:中間那層 `tfm_modulator_Pipeline_BYTE_LOOP`(不含 `CTRL_s_axi`, 那是 top-level 才有的東西)的 slack 就已經跟最外層一樣緊(0.03 vs 0.02), 代表真正貼著上限的路徑落在 `Pipeline_BYTE_LOOP` 這個子模組本身(或它跟外層的邊界), 不是 `CTRL_s_axi`. 當時只憑文字報告猜, 沒有實測定位, 講得比實際掌握的證據更肯定, 這是這次回答裡要修正的地方.
- **160MHz 有沒有餘裕, 取決於問哪一段**:`BYTE_LOOP`(迴圈內部穩態運算, FIR/phase/LUT)本身 slack 4.56ns, 很寬鬆; 但 `Pipeline_BYTE_LOOP`/`tfm_modulator` 這兩層 slack 只有 0.02~0.03ns. 一顆 IP 實際能跑多快取決於**整個設計裡最慢的那條路徑**, 不是取決於使用者關心的那一段——所以結論是「(HLS 估計上)壓線過關, 不是有餘裕」, 且 128-tap 加總樹屬於 `BYTE_LOOP` 內部運算(算進那個寬鬆的 4.56ns 裡), 不是限制時脈的瓶頸.
- **這個推論後來被第 47 節的真實 P&R 結果部分推翻**: HLS 的 Estimated 只是簡化的靜態時序模型, 不是真正繞線後的結果, 這正是接下來去跑真實 Vivado 實作的動機.

## 47. 真實 Vivado out-of-context 合成+佈局+繞線驗證: 160MHz 實際餘裕遠比 HLS 估計的寬(2026-08-06)
使用者要求不要只信 HLS 的 timing estimate, 實際跑一次 Vivado 驗證 160MHz 在真正繞線後撐不撐得住. 直接對 `hls_prj/solution1/syn/verilog/*.v`(cold_start 版本, 含 debug port 已拿掉)做 out-of-context 合成:

- **腳本**:新增 `hls_prj/solution1/syn/verilog/timing_check/run_ooc_timing.tcl`(`hls_prj/` 整個是 git-ignored, 屬於 build 產物, 不影響版控), 流程 `read_verilog [glob *.v]` → `synth_design -top tfm_modulator -part xczu9eg-ffvb1156-2-e -mode out_of_context` → `create_clock -period 6.25` → `opt_design` → `place_design` → `phys_opt_design` → `route_design` → `report_timing_summary`/`report_timing`/`report_utilization`. 用 `vivado -mode batch -source ...`(不是 `vitis_hls -f`)執行, `-mode batch` 跑完會自動結束, 不會有第 45 節那個卡在互動提示字元的問題, 腳本本身也明確寫了 `exit`.
- **執行時間**:合成本身很快, `place_design` 開始後陸續跑 floorplan/global placement/`phys_opt_design`/`route_design`, 全部背景執行約十幾分鐘完成(遠比第 45 節那次意外卡住的 75 分鐘短, 因為這次是真的在做事, 不是卡住).
- **結果(`timing_check/timing_summary_post_route.rpt`)**:
  ```
  WNS (Worst Negative Slack) = 2.232 ns   (Setup, 過關, 餘裕很大)
  WHS (Worst Hold Slack)     = 0.039 ns   (Hold, 過關, 但很緊)
  TNS = 0.000ns, THS = 0.000ns            (沒有任何一個 endpoint 違規)
  "All user specified timing constraints are met."
  ```
  換算真實關鍵路徑延遲 `6.25 - 2.232 = 4.018ns`, 對應真實 Fmax(setup 這邊)約 `1/4.018ns ≈ 249MHz`——**跟 HLS 自己估計的 0.02ns slack 差了超過 100 倍, 160MHz 實際上相當寬鬆, 不是壓線過關**.
- **真正最慢的路徑, 位置跟第 46 節的猜測不同**:報告顯示 worst setup path 是 `coeff_11_reg`(FIR 係數暫存器)→ `add_ln322_73_reg`(top.cpp:322 128-tap 加總樹裡的一個加法器), 9 級邏輯(4 個 CARRY8 進位鏈 + 幾個 LUT), 都在 `grp_tfm_modulator_Pipeline_BYTE_LOOP_fu_486` 這個實例裡. 也就是說**真正繞線後, 128-tap 加總樹反而是相對最慢的路徑**, 不是第 46 節猜測的 `Pipeline_BYTE_LOOP` 邊界控制邏輯——但因為還有 2.232ns 餘裕, 完全不是問題. **這證實 HLS 的 Estimated report 判斷「誰是瓶頸」跟真實繞線後可能不一樣, 兩種來源不能只看其中一個就下定論.**
- **資源用量也差很多**:真實 post-route 只用 1952 個 LUT(0.71%), 遠低於 HLS 自己估計的 15494(5%)——Vivado 真正的邏輯合成/優化比 HLS 的資源估算更積極, 這也是常見現象.
- **唯一值得留意的地方**:Hold slack(0.039ns)雖然過關但餘裕比 Setup 薄很多. Hold 違規跟時脈快慢無關(是同一個 clock edge 內、暫存器到暫存器走線太短造成的競爭), 目前乾淨過關, 但之後接進真正的系統 block design(不同的 floorplan/congestion 環境)時這條線餘裕比較薄, 值得留意, 不代表現在需要採取行動.
- **結論**:csim/csynth/RTL-vs-golden/真實 P&R timing 四層驗證全部通過. `HW_DEBUG_MODE` 拿掉、cold_start 套用後的 `src/top.cpp`, 在 xczu9eg-ffvb1156-2-e、160MHz 目標下確認時序與功能都沒問題. IP 封裝(`export_design`)仍未執行, 待使用者確認才進行.

## 48. IP 正式打包完成(2026-08-06)
使用者確認四層驗證都過後, 決定繼續打包.

- **改動**:`scripts/run_hls.tcl` 最後一行 `export_design ...` 取消註解(第 20 節記錄過的封裝路徑, 這是這件事本身要求的直接改動, 不是side-effect).
- **執行方式**:這次改用 `echo "exit" | vitis_hls.bat -f scripts/run_hls.tcl`(不是單純 `vitis_hls -f ...`), 靠 stdin 餵一個 `exit` 命令解決第 45 節記錄過的「跑完 csynth 後卡在互動式 `vitis_hls>` 提示字元」問題, 不用改動 `run_hls.tcl` 本身. 這次全程(csim+csynth+export_design)只花 84.343 秒, 乾淨結束, 沒有再卡住.
- **結果**:`csim_design` `TEST PASSED`;`csynth_design` 跟第 45 節一致(`Estimated Fmax=220.51MHz`, loop constraints satisfied);`export_design` 產生 `hls_prj/solution1/impl/export.zip`(167KB, 44 個檔案), 內含:
  - `component.xml`(IP 描述檔)
  - `hdl/verilog/`、`hdl/vhdl/`(兩種語言都有, 含 `tfm_modulator.v`/`.vhd` 頂層跟所有子模組, LUT ROM 的 `.dat` 檔也在裡面)
  - `constraints/tfm_modulator_ooc.xdc`(out-of-context 合成用的時脈限制)
  - `drivers/tfm_modulator_v1_0/`(bare-metal C driver 跟 Linux driver 原始碼)
  - `doc/ReleaseNotes.txt`、`xgui/tfm_modulator_v1_0.tcl`(Vivado IP Catalog 的 GUI 客製化參數)
  - 是標準完整的 Vivado IP Catalog 封裝, 第 21 節記錄過的匯入步驟(解壓縮到 `ip_repo/` → `Settings → IP → Repository → Add Repository` → 在 Catalog 搜尋 `tfm_modulator`)可以直接照做.
- **檔名已加上版本號**:使用者對版本號命名沒有特定想法, 交給我決定. 改成 `tfm_modulator_v1.0_20260806.zip`(`hls_prj/solution1/impl/` 底下), 版本號 `v1.0` 跟 `component.xml` 裡 Vitis HLS 自己寫的 `<spirit:version>1.0</spirit:version>` 對齊, 後面加日期(打包當天, 2026-08-06)方便跟未來重新 export 的版本區分. 之後如果內容有變(例如改 `sps_sel` 行為、換 `dac_data` 打包邏輯等), 版本號或日期要跟著換, 不要沿用同一個檔名覆蓋.
- **本次(2026-08-05/06)全部討論的總結**:`HW_DEBUG_MODE` 關閉 → 封裝來源確認(`hls_prj/solution1`)→ cold_start 原理釐清 → 差分編碼自癒特性推導 → 開機時序與 ADRV9009 `link_clk`/CDC 議題 → cold_start 正式套用 `src/top.cpp` 並四層驗證(csim/csynth/RTL-vs-golden/真實 P&R)全過 → IP 打包完成. `dac_data` 打包/CDC 轉接 IP(第 44 節)仍是待辦, 屬於同事的 ADRV9009 對接範疇.

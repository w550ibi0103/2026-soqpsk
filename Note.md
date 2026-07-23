1. 我用的硬體是 zcu102+adrv9009
2. 專案路徑: C:\XilinxWorkspace\Vitis\2026-soqpsk
3. adi hdl 路徑: C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\zcu102
4. 可以參考 README.md, 裡面有紀錄你之前改動的東西
5. 確認 adrv9009 的 iq rate 可不可以跑到 245.76MSample/s? 你之前有去 adi hdl 看 rtl 程式碼, 有看到是跑在 122.88MSample/s, 
6. 因為速率還是要對齊 DAC 的真實原生取樣率, 這部分我要怎麼讓我 ip 輸出速率可以對齊 DAC 的真實原生取樣率? 因為 dac 可能是跑在 122.88MSample/s, dac 要每個 clk 都有資料, 但是我的 ip 輸出的 iq 可以跟上這個 clk 速率? ip 要計算很多東西, 真的可以每個 clk 都輸出 iq? 你之前說模擬的結果是 Timing: estimated 7.256ns(≈137.8MHz), 但這只是可以跑在這個速度, 我要問的是在這個速度下, 是否每個 clk 都可以產出 iq 資料?
7. 我覺得 sps=2 可以刪掉, 因為昇取樣後的 sample 數太少了, 接收那邊應該會解不出來, 你認為呢?
8. 同事有推薦說如果有一些計算可以用查表的方式, 可以省一些時間, 你認為有哪些地方可以用查表的方式做?

# tfm_modulator 動態 SPS / Free-Running 改造(2026-07-07)

## 改了什麼(src/top.h, src/top.cpp, tb/tb_top.cpp)
1. `reset` 參數與 `s_axilite` 上的 reset 暫存器整個移除, 交給 HLS 預設的 `ap_rst_n` 硬體腳位處理, `if(reset){...}` 那段手動歸零邏輯也拿掉, 全部靠 static 變數的 C++ initializer 當作 reset 值.
2. `#pragma HLS INTERFACE s_axilite port=return bundle=CTRL` 改成 `#pragma HLS INTERFACE ap_ctrl_none port=return`, 讓這顆 IP 變成真正的 free-running 資料流 IP, 不需要 PS 寫 `ap_start`.
3. 新增 `ap_uint<2> sps_sel` 參數, 走一個小的 `s_axilite` bundle(`CTRL`), 用來動態選擇 SPS(16/8/4/2), 對應 `active_sps = SPS_MAX >> sps_sel`(sps_sel: 0→16, 1→8, 2→4, 3→2). 這個 register 不影響 free-running, 只是額外開一個小控制窗口.
4. 原本的函式 body 包進 `BYTE_LOOP: while(1)`, 在 `#ifndef __SYNTHESIS__` 底下用一個計數器(`CSIM_MAX_ITERS`, 定義在 top.h, 目前是 8)在 C 模擬時跳出迴圈避免卡死; 合成後(`__SYNTHESIS__` 有定義)這段跳出邏輯整個消失, 變成真正的無窮迴圈.
5. 新增 4 張各自 `ARRAY_PARTITION complete` 的係數表 `g_coeff_sps16/8/4/2`(都是 `G_LEN_MAX=128` 長度, 短的表由 C++ aggregate initializer 自動補零), MAC 迴圈用 `switch(sps_sel)` 在乘法器輸入端選當下要用的係數, 不是做 4 組平行乘法樹(csynth 結果證實 DSP 沒有變貴, 見下方).
6. `bit_idx`/`s`(sample-in-symbol index)/相位增量(`PHASE_SCALE[sps_sel]`)/TLAST 判斷式全部從固定的 `SPS` 巨集改成 runtime 的 `active_sps`/`shift_amt`.
7. `tb_top.cpp` 從「呼叫 tfm_modulator N 次, 每次處理 1 byte」改成「只呼叫 1 次, 內部自己用 BYTE_LOOP 跑完 NUM_BYTES(=8) 個 byte」, 拿掉顯式的 reset 呼叫. 新增 `TEST_SPS_SEL` 編譯期巨集(預設 0), 可以用 `-DTEST_SPS_SEL=1/2/3` 切換測試不同 SPS.

## g(t) 係數怎麼來的
- 公式與參數完全照 `C:\Users\eddiehppc\Documents\igps-rasp-receiver-iq-to-toa\iGPS-PlutoSDR\tests\soqpsk-tg\soqpsk-tg.py` 的 Block 4(`rho=0.70, B=1.25, T1=1.5, T2=0.5, Tb=1.0, L=8`), 只是把 `sps` 代入 16/8/4/2 各自重新取樣、各自獨立正規化(`sum(g)*(Tb/sps)=0.5`), 不是用抽取(decimation)去猜.
- 這台機器沒裝 Python(`python`/`python3` 只是 Windows Store 的殼, 也沒有 g++/node), 改用 `scripts/gen_g_coeffs.ps1`(PowerShell + .NET Math)重現同一套公式產生 `src/g_coeffs_sps{16,8,4,2}.inc`. 驗證過重新產生的 sps=16 版本跟原本的 `g_coeffs.inc`(已刪除, 被 `g_coeffs_sps16.inc` 取代)在 ~1e-14 相對誤差內一致(numpy vs .NET 函式庫的正常捨入差異, 遠低於 `ap_fixed<16,4>` 的解析度 0.000244).
- 之後如果要加新的 SPS 或改參數, 直接重跑 `scripts/gen_g_coeffs.ps1` 就好.

## 驗證結果
- `csim_design`(sps_sel=0, 對照原本固定 SPS=16 的行為): TEST PASSED, 0 errors, 產生 1024 samples(= 8 bytes × 16 sps × 8 bits).
- 4 種 `sps_sel`(0/1/2/3, 用 `-DTEST_SPS_SEL=N` 各自跑一次全新的 csim, 確保 static 狀態是乾淨重跑, 等同硬體上電後的 `ap_rst_n`)全部 TEST PASSED, 0 errors, 樣本數分別是 1024/512/256/128(= active_sps × 8 × 8 bytes), 跟預期公式完全吻合.
- `csynth_design`(target xczu9eg-ffvb1156-2-e, `create_clock -period 10` 即 100MHz):
  - Timing: estimated 7.256ns(≈137.8MHz), 有裕度過關. 這個 Fmax 比 ADRV9009 這份參考設計實際的 DAC 原生取樣率 122.88MHz(見下方)還高, 代表之後把 `ap_clk` 換成 122.88MHz 應該跑得動, 但正式對接時仍要重跑 `create_clock` 對應正確週期確認.
  - Utilization: BRAM_18K 0, DSP 110(4%), FF 44884(8%), LUT 16463(6%), 對 zu9eg 來說非常寬鬆.
  - 原本擔心「4 張係數表會讓乘法器變 4 倍貴」沒有發生: `sps_sel` 的 4-to-1 mux(報告裡的 `sparsemux` instance)接在乘法器輸入端, 先選係數再進同一顆乘法器, DSP 用量沒有明顯比純常數係數版本高, 多的成本主要是 LUT 端約 1210 顆的 mux 邏輯.
  - `BYTE_LOOP` trip count 顯示 `inf`, 確認合成後真的是無窮迴圈(free-running), 每個 byte 的迭代延遲落在 123~235 cycle(對應 SPS=2 到 SPS=16 兩個極端).

## 使用限制
- 切換 `sps_sel` 不保證 glitch-free(shift_reg/current_phase 不會自動對齊新的取樣率), 只能在 `ap_rst_n` assert 期間切換.
- `sps_sel` 從 reset 釋放後預設值是 0(對應 SPS=16), 跟原本固定 SPS=16 的行為一致.

# tfm_modulator 迴圈攤平 + 刪除 SPS=2(2026-07-09)

## 動機:BYTE_LOOP 沒有真的每個 clk 都輸出 IQ
規劃「bypass `tx_fir_interpolator`、直接接 `tx_adrv9009_tpl_core`」這個插入點方案時發現, 上面 2026-07-07 那版雖然 `MAIN_LOOP` 本身 `achieved II=1`, 但外層 `BYTE_LOOP` 沒有被攤平/pipeline: `tfm_modulator_csynth.rpt` 的 Instance 表顯示 `grp_tfm_modulator_Pipeline_MAIN_LOOP_fu_696` 的 `Interval == Latency`(min=119/max=231), 代表下一個 byte 的 `MAIN_LOOP` 必須等上一個 byte 的 pipeline 完全 drain 才能開始, byte 與 byte 交界處有一大段空窗. 換算吞吐效率(輸出樣本數/實際耗費 cycle 數): SPS=16 約 55%, SPS=2 只有約 13%. `dac_data` 這個介面是無 handshake 的固定速率 port, 這樣的空窗會讓 DAC 拿到 stale 資料, 所以這不是「效能優化」而是這個插入點方案能不能成立的先決條件.

根本原因是 `MAIN_LOOP` 的邊界 `active_sps*8` 是 runtime 變數(取決於 `sps_sel`), Vitis HLS 的自動 `LOOP_FLATTEN` 只支援邊界是編譯期常數的完美巢狀迴圈, 用不上, 只能手動合併.

## 改了什麼(src/top.cpp)
1. 拿掉 `MAIN_LOOP` 這層 `for`, 把 `BYTE_LOOP`/`MAIN_LOOP` 合併成一層 `while(1)` + `#pragma HLS PIPELINE II=1`. 用一個 static counter `iter_in_byte`(0 .. active_sps*8-1)取代原本內層 for 迴圈的隱含計數器, 在 `iter_in_byte==0` 時做「原本 BYTE_LOOP 開頭」那段(解碼 `shift_amt`/`active_sps`/`phase_idx`、`bit_in.read_nb` 讀新 byte), 每個 cycle 結尾判斷 `iter_in_byte` 是否到 `active_sps*8-1` 決定要繞回 0(進入下一個 byte)還是 +1.
2. `alpha`/`current_bit`/`current_byte`/`is_burst_end`/`idle_mode` 全部從一般區域變數改成 `static`——原本它們能在同一個 byte 的 `active_sps` 次迭代間存活, 是靠外層 for 迴圈的 C++ block scope 圍住, 攤平成一層之後沒有這個 scope 了, 必須手動 `static` 才能存活, 這是這次修改最容易出錯的地方.
3. `#ifndef __SYNTHESIS__` 底下的 C 模擬中斷邏輯(`csim_iter_count`)從「每次外層迴圈(=每個 byte)加 1」改成「只在 `iter_in_byte` 即將繞回 0 的那個 cycle才加 1、判斷要不要 break」, 確保還是處理完整數個 byte 才跳出, 不會在 byte 中途被切斷.
4. `PHASE_SCALE[sps_sel]` 改成 `PHASE_SCALE[phase_idx]`, `phase_idx` 跟 `shift_amt` 一樣只在 byte 邊界解碼, 且對 `sps_sel==3`(已刪除的 SPS=2)clamp 回 0(等同 SPS=16), 避免陣列縮小後越界.

## 刪除 SPS=2(src/top.h, src/top.cpp, tb/tb_top.cpp, scripts/gen_g_coeffs.ps1)
- 理由: (1) SPS=2 剛好卡在 Nyquist 邊界, 接收端做符元定時回復(timing recovery)幾乎沒有內插 margin, 實務上通常至少要 SPS=4. (2) 在攤平前的架構下, SPS=2 的吞吐效率是四個選項裡最差的(~13%).
- `g_coeff_sps2` 表、`src/g_coeffs_sps2.inc` 檔案、兩個 `switch(sps_sel)`(`shift_amt` 解碼、MAC 係數選擇)裡的 `case 3` 分支、`PHASE_SCALE` 的第 4 個元素全部刪除. `sps_sel==3` 現在是保留值, 兩個 switch 的 `default` 分支自動把它當 SPS=16 處理(跟 `sps_sel==0` 完全一樣), `phase_idx` 同樣 clamp 回 0.
- `scripts/gen_g_coeffs.ps1` 的 `$SpsList` 預設值從 `@(16,8,4,2)` 改成 `@(16,8,4)`.
- `tb/tb_top.cpp`:`SPS_TABLE` 從 4 個元素縮成 3 個(`{16,8,4}`), 新增 `#if TEST_SPS_SEL > 2 #error ... #endif` 編譯期防呆.

## 驗證結果
- `csim_design`:3 種 `sps_sel`(0/1/2, 用 `-DTEST_SPS_SEL=N` 各自跑一次全新的 csim)全部 TEST PASSED, 0 errors, 樣本數分別是 1024/512/256(= active_sps × 8 × 8 bytes), 跟改動前完全一致.
- `-DTEST_SPS_SEL=3` 會在編譯期直接被 `#error` 擋下, 確認防呆生效.
- `csynth_design`(同樣 target xczu9eg-ffvb1156-2-e, `create_clock -period 10`):
  - Timing 不變: estimated 7.256ns(≈137.8MHz), 跟攤平前完全一樣, 代表新增的 `iter_in_byte` 控制邏輯沒有拉長關鍵路徑.
  - **關鍵指標**:報告裡只剩一個迴圈 `BYTE_LOOP`(`tfm_modulator_Pipeline_BYTE_LOOP_csynth.rpt`), `Trip Count = inf`、`Pipelined: yes`、`achieved II = 1`——不再有攤平前那種「`Interval == Latency`, 序列化呼叫」的現象. 也就是說現在不管 `active_sps` 是多少, 穩態下每個 clk 都會有一組新的 IQ 輸出, 不再受 SPS 大小影響效率.
  - Utilization 略降(移除 SPS=2 表 + 減少迴圈控制邏輯的重複開銷):BRAM_18K 0, DSP 109, FF 37206, LUT 13727(攤平前為 DSP 110, FF 44884, LUT 16463).
- 驗證用的暫存專案(`hls_prj_verify`)僅用於這次確認, 已刪除, 不影響 repo 追蹤的 `hls_prj/`(git-ignored).

# ADRV9009 ADI HDL 整合建議(2026-07-07, 尚未實作, 屬於另一個 Vivado 專案)

以下內容是研讀 `C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\zcu102\system_bd.tcl` 與 `C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\common\adrv9009_bd.tcl` 後得到的確認結果與建議方案, 目前只是設計討論, 沒有實際修改 Vivado 專案.

## 確認過的事實(從原始碼直接找到, 不是憑記憶)
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

## 插入點方案(TX1 換成 SOQPSK IP, TX2 完全不動)
- **TX2(`dac_data_2/3`)維持原樣**, 繼續走現有的 `axi_adrv9009_tx_dma → dacfifo → upack → interpolator → tpl_core` 路徑, 用軟體算好的 IQ 波形播放.
- **TX1(`dac_data_0/1`)由 `tfm_modulator` 接管**:
  1. 新增一個獨立的 `axi_dmac` instance(不是修改現有的 `axi_adrv9009_tx_dma`), 設 `CONFIG.CYCLIC 0`(這才是「host 送即時不重複 bitstream」該有的行為), `ASYNC_CLK_*` 比照現有 `axi_adrv9009_tx_dma` 設 1, 讓它的 `m_axis` 輸出直接同步進 `axi_adrv9009_tx_clkgen/clk_0` 時脈域, 不用額外接一顆 CDC FIFO.
  2. 這顆新 DMA 的 AXI-Stream 輸出接到 `tfm_modulator` 的 `bit_in`.
  3. `tfm_modulator` 的 `ap_clk`/`ap_rst_n` 接到 `axi_adrv9009_tx_clkgen/clk_0` 同一個時脈域(跟 `tx_adrv9009_tpl_core/link_clk` 一致), `i_out`/`q_out` 直接接 `tx_adrv9009_tpl_core/dac_data_0`(I)、`dac_data_1`(Q), 取代原本從 `tx_fir_interpolator/data_out_0/1` 接過來的線.
  4. `util_adrv9009_tx_upack`/`tx_fir_interpolator` 維持 `NUM_OF_CHANNELS=4`(`TX_NUM_OF_CONVERTERS` 全域參數)不動, 因為牽動 TX2 和 DMA 資料寬度; channel 0/1 那兩條 fifo/interpolator 分支就晾在旁邊不接東西, 換取不用動全域 M 參數的低風險.

## 已排除的替代方案:插入 tx_fir_interpolator 前端(2026-07-09)
- 曾經討論過不 bypass `tx_fir_interpolator`, 改成讓 `tfm_modulator` 只輸出 15.36 MSPS、接到 `tx_fir_interpolator/data_in_0/1`(這樣它的 x8 內插直接幫忙做到 122.88 MSPS), 好處是我們 IP 只需要撐住 15.36 MSPS(比 122.88 MSPS 寬鬆很多), 也確認過 `adi_fir_filter_bd.tcl:132-145` 這個插入點的輸入側本來就是「每 8 個 clk 才取一次新資料」的設計(`rate_gen`/`PULSE_PERIOD=filter_rate-1`).
- 最後沒有採用, 因為客戶要求的最快 bit rate 是 20 Msym/s, 這個數字跟 ADRV9009 這整條時脈家族(15.36 MHz × 2ⁿ)對不上(`80,000,000 / 15,360,000 = 125/24`, 不是整數比、更不是 2 的冪次比), 硬接會需要一顆 L=96/M=125 等級的有理數重取樣器, 不划算. 改成直接對齊 122.88 MSPS(bypass `tx_fir_interpolator`)之後, 只要 bit rate 跟 SPS 的乘積等於 122.88 MHz 家族的數字就好(例如 15.36 Mbit/s × SPS8, 或 7.68 Mbit/s × SPS16), 不需要任何額外的重取樣級.
- 這個決定也代表上面「迴圈攤平」那個修改(2026-07-09)是這個插入點方案能不能成立的先決條件: `dac_data` 是無 handshake 的固定速率 port, 沒有攤平之前 `BYTE_LOOP` 撐不住 122.88 MSPS 的連續輸出(見上面章節的效率試算), 攤平後 `achieved II=1` 才讓這個方案有可行性.

## `dac_data_0/1` 打包格式(已確認, 2026-07-09)
查了 `tx_adrv9009_tpl_core` 底層的 `ad_ip_jesd204_tpl_dac_channel.v:118-122`:
```verilog
/* Data is expected to be LSB aligned, drop unused MSBs */
for (i = 0; i < DATA_PATH_WIDTH; i = i + 1) begin: g_dac_dma_data
  assign dac_dma_data_s[CR*i+:CR] = dma_data[BITS_PER_SAMPLE*i+:CR];
end
```
`DATA_PATH_WIDTH=2`、`CR`(CONVERTER_RESOLUTION)`=16`. 確認 `dac_data_0`(I)的 32-bit 裡`[15:0]`/`[31:16]`是**同一個 channel、時間軸上連續兩筆** I sample(`dac_data_1`同理放 Q), 不是同一個 32-bit 裡塞 I/Q 交錯. 這個 packing 規則是 `tx_adrv9009_tpl_core` 通用的, 不管上游接 `tx_fir_interpolator` 還是 `tfm_modulator` 都要照這個順序打包.

## IP 現在還不會輸出 dac_data 要的打包格式(已確認, 2026-07-09)
`src/top.h` 的 `sample_pkt` 是 `ap_axiu<16,...>`, `src/top.cpp` 的 `BYTE_LOOP` 每個 clock 只做 `i_out.write(out_i); q_out.write(out_q);`——**一次 1 筆 16-bit I、1 筆 16-bit Q**, 不是 32-bit、也沒有把「這一筆」跟「下一筆」打包在一起. 上面「`dac_data_0/1` 打包格式」那段講的 32-bit/2 筆連續樣本, 是 `tx_adrv9009_tpl_core` **要求的輸入格式**, 我們 IP 目前完全還沒做這個打包, 這是實際要接線前一定要補的一塊, 不是已經做好的東西.

## tx_adrv9009_tpl_core 的資料輸入沒有 valid/ready(已確認, 2026-07-09)
`ad_ip_jesd204_tpl_dac_channel.v:53-85` 完整 port list 裡, `dma_data`/`dac_iqcor_data_in` 這兩個資料輸入 port **完全沒有對應的 valid/ready pin**, 只有 `clk`. 代表這顆模組每個 clock 都會直接把 bus 上當下的值採進去, 沒有機制讓上游說「這筆還沒準備好」——每個 clk 都必須有有效資料餵進去, 這也是為什麼「迴圈攤平」(見上面章節)是這個插入點方案能不能成立的先決條件.

## `clk_0` 實際頻率:已從權威來源確認是 250MHz(2026-07-09)
不需要重新 build——這個 Vivado 專案本來就有一份已經完整跑完 place & route 的結果(`adrv9009_zcu102.runs/impl_1/`, 2026-07-02 build 的), 直接讀裡面的報告即可:
- `system_top_clock_utilization_routed.rpt`:淨名對應到實際 cell, 確認 `axi_adrv9009_tx_clkgen/inst/i_mmcm_drp/clk_0` 這個 net 在報告裡叫 `mmcm_clk_0_s_2`.
- `system_top_timing_summary_routed.rpt` 的 Clock Summary:`mmcm_clk_0_s_2` 的 `Frequency(MHz) = 250.000`.
- 更進一步查到 **這不是隨便量到的, 是專案自己 `system_constr.xdc:85` 寫死的設計常數**:`create_clock -name tx_ref_clk -period 4.00 [get_ports ref_clk0_p]`——`ref_clk0_p` 是 ZCU102 板上實際接到 GT transceiver 的實體差動時脈腳位, 250MHz 是刻意選定、且**跟燒哪個 profile(100/200/400MHz 頻寬)無關**的板級常數.
- 之前(2026-07-07)以為 `clk_0=122.88MHz`(依據 FIR Compiler wizard 的 `Clock_Frequency=122.88` 參數)、以及後來反推的 `61.44MHz`, **兩個都不對**, 只是 wizard 設定參數/理論推算, 不是實際接線頻率.
- 這裡也順便釐清一個容易搞混的點:`talise_config.c`(`bw100/ir122.88` profile)裡的 `deviceClock_kHz=245760` 是**另一條時脈網**, 是 ADRV9009 晶片自己內部 PLL/類比前端的參考時脈(通常來自板上另一顆時脈晶片), 跟 FPGA 這邊 GT 用的 `ref_clk0_p`(250MHz)是兩條不同的實體線, 不需要相等.

## 仍未解決的謎:250MHz × 2 samples/clock = 500 MSPS, 跟 122.88 MSPS 對不上(尚未定案)
`clk_0=250MHz` 確認了, 但跟 `DATA_PATH_WIDTH=2`(每個 clock 2 筆樣本)放在一起算, 介面容量是 500 MSPS, 遠大於 `bw100` profile 實際只需要的 122.88 MSPS(差約 4.07 倍, 不是乾淨的比例). 在 EngineerZone 查到 ADI 的說法是「TX 端 128bit@245MHz, 一次接收 2 筆連續樣本」, 但沒有講清楚 500 MSPS 這個介面容量, 在跑低頻寬 profile 時, 是不是每個 clock 真的都換新值, 還是 `tx_fir_interpolator` 只是沒把介面用滿(同一筆資料連續幾個 clock 重複輸出). 這個需要**實際跑一次 `tx_fir_interpolator` + `tx_adrv9009_tpl_core` 這段的行為模擬, 直接看 `dac_data`/`dac_valid` 波形**才能確定, 靠繼續讀 tcl/RTL 已經沒辦法再往下解了(這輪調查已經自己推翻自己好幾次, 不要再猜).

## ADRV9009 profile 文件去哪裡找(2026-07-09)
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

## JESD204 M/L/S/NP 設在哪裡(2026-07-09)
不是在 Vivado Block Design 的 GUI 畫布上點出來設的, 是**在 build 之前透過 `make` 命令列參數傳進去**. `system_project.tcl:27-35` 用 `get_env_param` 讀取, 預設值 TX: M=4, L=4, S=1(可用 `make TX_JESD_M=... TX_JESD_L=... TX_JESD_S=...` 覆寫, 註解範例見 `system_project.tcl:16-18`). NP(`TX_SAMPLE_WIDTH`)則沒有開放覆寫, 寫死在 `adrv9009_bd.tcl:24` 為 16. 這些參數被 `adrv9009_bd.tcl` 讀進去後, 才拿去呼叫 `adi_axi_jesd204_tx_create`/`adi_tpl_jesd204_tx_create`/`ad_xcvrcon` 等指令自動產生 block design 裡的 JESD204 IP 與接線.
- 影響:如果之後要換 M/L/S(例如換 profile 需要不同 lane/converter 數), 正確做法是帶新參數整個重新 `make` build, 不是在既有 block design 裡手動改單一顆 IP 的參數——牽動的東西(內插倍率、`TX_SAMPLES_PER_CHANNEL`、GT lane 連接)太多, 風險跟之前提過的「換單通道要重新 build 1-3 小時」同一等級.

## 修正:換 profile 不代表同一份 IP 可以直接套用更高速率(2026-07-09)
曾經討論過「先做 122.88, 之後客戶要 245.76 只要重燒 profile, IP 若寫得好(例如查表法)應該能直接套用」——這個想法需要修正:
1. 換 profile 通常也要換 FPGA 端的 JESD204 M/L/S 設定與 `tx_fir_interpolator` 內插倍率, 不是 RFIC 端燒錄profile 就好, FPGA 端一樣要重新 build 對齊.
2. 就算 FPGA 端配合改好, IP 要撐 2 倍吞吐量只有兩條路:(a) 同樣「一個 clock 一筆樣本」架構, 把 `ap_clk` 拉到 2 倍頻——但目前 csynth 估計的關鍵路徑 7.256ns(≈137.8MHz)撐不住 245.76MHz(週期僅 4.07ns)所需的時序, 查表法(換掉 CORDIC)只是這條路上的一塊拼圖, 不是自動解決; (b) 維持 `ap_clk` 不變, 改成一個 clock 平行算 2 組樣本——這是實質架構改動, 尤其 phase accumulator 是遞迴的, 平行化不是輕鬆的事.
3. 結論:現在把 IP 設計乾淨(查表法、迴圈攤平)是為將來留餘裕的正確方向, 但不保證「以後直接套用不用改」, 真的要衝更高速率時幾乎確定要回來重新評估時序或改架構.

# 客戶規格(20Mbit/s @ SPS=8 → 160MSPS)驗證與 CORDIC 發散問題調查(2026-07-23)

新目標: 同事接手 ADRV9009 對接與 resample, 我只需專注在 IP 本身的設計與驗證, 確認客戶要求的 20Mbit/s 輸入(SPS=8, IQ rate=160MSPS)可以達成.

## 1. 160MHz 時脈可行性:確認可以, 已更新為官方 baseline
- `scripts/run_hls.tcl` 的 `create_clock` 從 `-period 10`(100MHz, 舊的寬鬆設定)改成 `-period 6.25`(160MHz, 精確對應客戶規格), 並用這個新時脈重跑官方 `solution1`(csim + csynth 都過).
- 結果: timing slack 4.56ns(critical path 約 1.69ns), `BYTE_LOOP` 仍是 `achieved II=1`、`Trip Count=inf`、`Pipelined=yes`——代表只要 `ap_clk>=160MHz`, 這顆 IP 每個 clock 都能吐出一組新 IQ, 滿足客戶規格.
- 注意: 在 160MHz 這個較緊的時脈下, HLS 選了完全不同的資源配置(DSP 109→3, LUT 14820→21337, FF 37206→10224), 不是舊的 100MHz 版本硬撐更快而已, 所以「先跑一個較鬆的時脈當作已驗證」不能直接套用到更緊的規格, 需要針對實際目標時脈重新 csynth 才算數.

## 2. Cosim 限制:重新確認過, 結論不變
`ap_ctrl_none`(free-running)+ `sps_sel` 這個 `s_axilite` port 混用, Vitis HLS 2023.2 的 cosim 直接拒絕(`WARNING: [COSIM] found non-self-synchronizing top I/O sps_sel`), 這是工具限制不是設計錯誤. `scripts/run_hls.tcl` 的 `cosim_design` 保持註解狀態.

## 3. 繞過 cosim: 手寫 XSIM testbench 直接驗證匯出的 RTL
不依賴 HLS 自己的 cosim/post-check 機制(對 free-running 設計的時序對齊本來就有問題, 見 `verify_tmp` 的舊發現), 改成:
- 用 `xvlog`/`xelab`/`xsim`(Vivado 內建, `C:\Xilinx\Vivado\2023.2\bin`)直接編譯 `hls_prj/solution1/syn/verilog/*.v` + 手寫的 SystemVerilog testbench, 完全繞開 HLS cosim.
- 產出物: `xsim_verify/tb_xsim_top.sv`(對真實 hls_prj 介面, 含 AXI4-Lite `sps_sel` write)、`xsim_verify/tb_xsim_top_sps16_default.sv`(不做任何 write, sps_sel 停在預設 0=SPS16 的乾淨對照組)、`xsim_verify/tb_xsim_verify_sps8.sv`(對 `verify_tmp` 簡化介面, sps_sel 編譯期釘死=1/SPS8, 完全沒有 s_axi_CTRL, 不可能 race)、對應的 `scripts/run_xsim_verify*.sh` 驅動腳本.
- 波形檔(`.wdb`)可在 Vivado 開啟: `xsim.bat <wdb 檔> -gui`. CSV 輸出格式跟 `tb_top.cpp` 的 `output_waveform.csv` 一致(`Sample,I_Data,Q_Data,TLAST`), 可直接跟 golden C model 逐點比對.

## 4. 意外發現: debug_current_bit/debug_alpha 是死接腳
從 `hls_prj/solution1/syn/verilog/tfm_modulator.v` 直接確認: `debug_current_bit`/`debug_alpha` 這兩個 `ap_none` scalar port 在 RTL 頂層被合成成 **`input`**, 而且模組內部完全沒有其他地方引用它們——`top.cpp` 裡對它們的寫入(`debug_current_bit = current_bit;`)在硬體上完全沒有接到任何輸出腳位, 是懸空的. 只有 `debug_pulse`/`debug_phase`/`debug_freq`(這三個是 `hls::stream`/axis 介面)才是真正有效的輸出. 這是 Vitis HLS 對「純量 `ap_none` 輸出」+「`ap_ctrl_none` 自由執行迴圈」這個組合的合成 artifact, 不是設計錯誤——但如果之後真的要靠這兩個訊號做 ILA/ChipScope debug, 現在的合成結果是看不到真實資料的.

## 5. sps_sel AXI4-Lite write 的 race 問題(已確認根因, 已用替代方案繞過)
- **現象**: 用 `tb_xsim_top.sv`(真實介面, reset 放開後才做 AXI4-Lite write 把 `sps_sel` 設成 1)跑出來的結果, 從很早的 sample 就開始跟 golden 發散, 且發散型態是「先小後隨時間持續放大」.
- **根因**: `int_sps_sel`(`sps_sel` 的儲存暫存器, 在 `CTRL_s_axi` 子模組裡)跟整個資料通路(`BYTE_LOOP`)共用同一個 `ap_rst_n`/`ARESET`. `BYTE_LOOP` reset 一放開就立刻開始跑第一個 byte 的判讀, 而 AXI4-Lite write 必須等 reset 放開「之後」才能完成(`CTRL_s_axi` 的 `AWREADY`/`WREADY` 在 `ARESET` 期間恆為 0)——兩者之間有先天的時序競賽, `sps_sel` 幾乎不可能在 free-running 迴圈的第一次判讀之前就緒. 這代表: **在目前的硬體架構下, 想要在 reset 剛放開時就讓 `sps_sel` 停在非預設值, 本質上做不到**, 且這個「錯誤起跑」的暫態(precoder 歷史被污染 + `current_phase` accumulator 是持續累積不會自動歸零)會造成永久性的偏移, 不會隨時間自動收斂回正確軌跡.
- **繞過方式**: 用 `verify_tmp/`(`sps_sel` 編譯期釘死成常數, 完全沒有 `s_axi_CTRL` bundle)取得沒有 race 疑慮的 RTL, 這是後續(第 6-8 節)所有實驗採用的介面.

## 6/7. 加寬中間精度的兩個否證實驗(2026-07-23)
懷疑「128-tap FIR 加總」或「phase accumulator 累加」在 `ap_fixed<16,4>`(僅 4 個整數位元, 範圍 ±8)下有中間溢位, 且 `#pragma HLS UNROLL` 讓 HLS 把加總排程成平行加法樹(順序跟 C model 的循序 `+=` 不同), 非結合律誤差被放大.
- **實驗 1**: 把 `freq_dev` 的 128-tap 累加從 `data_t` 換成 `freq_acc_t = ap_fixed<24,12>`(多 8 個整數位元), 最後轉回 `data_t`. 確認 RTL 真的重建(pipeline 深度 17→18), 但跟(同樣加寬過的)新 golden 比對, **發散位置與數值逐位元完全相同**——128-tap 加總本身沒有真的溢位, 這個假設被推翻.
- **實驗 2**: 同樣手法加寬 `current_phase` 這個持續性 phase accumulator, 結果**又是逐位元完全相同**——phase accumulator 的累加本身也沒有問題.
- 兩次實驗都在 `verify_tmp/`(`VERIFY_FIXED_SPS_SEL=1`)上做, 驗證完都已還原成修改前的窄位元版本.

## 8. 用內部暫存器 hierarchical reference 直接鎖定 CORDIC(2026-07-23, 目前最新結論)
排除上面兩個假設後, 直接從 golden C model 的 `debug_phase` 逐 iteration 數值追出:「`current_phase` 的值本身」是否跟 RTL 一致, 才是關鍵問題.

- **debug axis stream 的陷阱**: 一開始想直接比 `debug_phase`(axis stream), 但發現這條 debug stream 的 `TVALID` 時序跟 `i_out`/`q_out` 的 `TVALID` 不是對齊的(各自在 pipeline 裡的位置不同, fill latency 不同), 直接拿兩者的 pulse counter 對比會找到一個乾淨但難以直接解讀的偏移量(測出 16), 且該偏移量會導致看不到我們真正關心的那個 sample 的值. **教訓: 不要用 debug axis stream 的 TVALID pulse 計數器去跟主要輸出對齊, 這個方法不可靠.**
- **正確做法**: 從生成的 RTL(`tfm_modulator.v`)直接追出 `current_phase` 對應的內部訊號 `ap_sig_allocacmp_in`(`debug_phase_TDATA` 就是從它 sign-extend 出來的, 可用 `grep -n "ln302\|allocacmp_in"` 追出), 在 testbench 用 hierarchical reference(`dut.ap_sig_allocacmp_in`)**每個 clock cycle 都讀取**, 不透過任何 axis stream 的 handshake, 完全避開上面的對齊問題. 另外準備一個絕對 clock cycle 計數器當共同座標, 讓「這個內部訊號的值」跟「`i_out` 在哪個 cycle 輸出哪個 sample」可以直接對應, 而不是用兩個獨立遞增的 pulse counter 去互相猜偏移量.
- **結果**: 用已知吻合的 iteration 22→23 轉折點校正出 cycle-to-iteration 的對應公式, 驗證 iteration 9(I/Q 開始發散的那個點)的 `current_phase`:
  - golden(C model) 在 iteration 9 的 phase = 0.00000000
  - RTL(`ap_sig_allocacmp_in` 讀出來的) 在 iteration 9 的 phase = 0.000000
  - **兩者完全吻合**, 但同一個 iteration, `q_out` 的實際輸出: golden=0.000244141(≈sin(0)), RTL=0.002441(明顯不是 sin(0) 該有的值).
- **結論**: `current_phase` 進入 `hls::cos`/`hls::sin`(CORDIC)之前, C model 跟 RTL 完全一致——**問題確定出在 CORDIC 這一步本身**. 而且不是單次誤差: 同一個(接近 0 的)相位值連續好幾個 iteration, RTL 的 `sin` 輸出沒有維持在該有的常數值, 而是持續緩慢飄移, 比較像 CORDIC 這個共用/pipeline 化硬體單元, 內部可能有殘留狀態沒有隨每次呼叫乾淨重置, 不只是開機瞬間的一次性相位偏移問題.

## 目前狀態與待辦
- 客戶規格(160MHz/SPS=8 吞吐量)已確認可達成, `scripts/run_hls.tcl` 已更新為正式 baseline.
- RTL vs C model 的發散根因已鎖定在 `hls::cos`/`hls::sin`(CORDIC), 尚未查到 CORDIC 內部確切的問題點(例如 iteration 數/延遲設定, 或是否為已知的 Vitis HLS 限制).
- 尚待決定: (a) 繼續往 CORDIC 內部查(例如檢查 HLS CORDIC 設定、或改用查表法取代), 或 (b) 評估這個飄移對實際 SOQPSK 解調的影響有多大, 再決定是否值得投入更多時間修.
- 這次調查用到的檔案都在 `xsim_verify/`(testbench、golden CSV、比對用的中間 CSV)與 `scripts/run_xsim_verify*.sh`, 都保留下來(不像 `hls_prj_verify/` 那樣每次用完就刪), 方便之後繼續查.
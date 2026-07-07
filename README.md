1. 我用的硬體是 zcu102+adrv9009
2. 我要做一個 ip, 在 PS 端從 host 送即時不重複的 bitstream 進 dma, PL 端把資料從 dma fifo 搬出來丟進我的 ip, ip 會做幾件事, 你可以讀 top.cpp, 大致上就是轉 differential, 轉成三元碼, 昇取樣與 pulse 形成, concolution, 轉成 phase, 產生 iq
3. 我的 ip 輸入 bit rate 最高會到 20Mbit/s, 如果維持 sps=16, 輸出會變成 320MSample/s, 會超過 adrv9009 的上限吧?
4. adrv9009 的 iq rate 最高是 245.76MSample/s 還是 122.88MSample/s?
5. 我的 ip 是 ap_ctrl_hs 協定, 我的需求是 free-running 的資料流 IP. 純資料流 IP 一般會用 ap_ctrl_none. 所以 top.cpp 要改成 ap_ctrl_none + 外層 while(1) 的 free-running 結構吧?
6. 輸入我的 ip 的 bitstream 是即時不重複的資料流.
7. axi_adrv9009_tx_dma(line 100-112):CONFIG.CYCLIC 1 — 是「不斷重播同一段波形」的元兇, 我的需求是從 host 送 bitstream 進 ip, 不是送固定重複的波形
8. 我的 ip 產生出來的 iq, 已經是有昇取樣過後的 iq samples (sps=16), 要對接到 adi hdl 範例程式, 要從範例程式的哪裡插入我的 ip?
9. 我想要根據我輸入 bit rate 動態調整 sps, 避免 bit rate 太高, sps 也太高, 造成超過 adrv9009 的上限, 有辦法做? 我可以產生不同 sps 的 g_coeffs.inc, 之後再動態決定要用哪一個吧?
10. reset 參數從 s_axilite 改成走 HLS 預設的 ap_rst_n 硬體腳位, 這樣才可以硬體 reset?
11. 因為速率還是要對齊 DAC 的真實原生取樣率, 這部分我要怎麼讓我 ip 輸出速率可以對齊 DAC 的真實原生取樣率?
12. 專案路徑: C:\XilinxWorkspace\Vitis\2026-soqpsk
13. adi hdl 路徑: C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\zcu102
1. 我用的硬體是 zcu102+adrv9009
2. 專案路徑: C:\XilinxWorkspace\Vitis\2026-soqpsk
3. adi hdl 路徑: C:\XilinxWorkspace\Vivado\hdl\projects\adrv9009\zcu102
4. 可以參考 README.md, 裡面有紀錄你之前改動的東西
5. 確認 adrv9009 的 iq rate 可不可以跑到 245.76MSample/s? 你之前有去 adi hdl 看 rtl 程式碼, 有看到是跑在 122.88MSample/s, 
6. 因為速率還是要對齊 DAC 的真實原生取樣率, 這部分我要怎麼讓我 ip 輸出速率可以對齊 DAC 的真實原生取樣率? 因為 dac 可能是跑在 122.88MSample/s, dac 要每個 clk 都有資料, 但是我的 ip 輸出的 iq 可以跟上這個 clk 速率? ip 要計算很多東西, 真的可以每個 clk 都輸出 iq? 你之前說模擬的結果是 Timing: estimated 7.256ns(≈137.8MHz), 但這只是可以跑在這個速度, 我要問的是在這個速度下, 是否每個 clk 都可以產出 iq 資料?
7. 我覺得 sps=2 可以刪掉, 因為昇取樣後的 sample 數太少了, 接收那邊應該會解不出來, 你認為呢?
8. 同事有推薦說如果有一些計算可以用查表的方式, 可以省一些時間, 你認為有哪些地方可以用查表的方式做?
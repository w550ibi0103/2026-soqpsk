## 開發環境建立
# 開發環境還原步驟
1. 從 git clone 程式碼 
2. 打開 Vitis HLS Command Prompt, 使用 cd 指令進入你 clone 下來的根目錄: 2026-soqpsk
3. 從根目錄呼叫 scripts 資料夾裡的腳本: vitis_hls -f scripts/run_hls.tcl
4. 打開 Vitis HLS, 從 hls_prj 資料夾開啟 project

# 腳本指令說明
1. csim_design: 最單純的軟體測試. 工具會把你寫的 top.cpp 和 tb_top.cpp 當作一般的 C++ 程式, 用 GCC 或 Clang 編譯器跑一次. 目的是驗證演算法的「功能」對不對.
2. csynth_design: 工具會開始把你的 C++ 程式碼「翻譯」成硬體描述語言 (Verilog 或 VHDL). 目的是將軟體邏輯轉換為硬體電路設計圖, 工具會進行「排程 (Scheduling)」(決定哪個加法器在哪個 Clock 執行) 與「綁定 (Binding)」(決定這個加法要用 DSP 還是 LUT 來做).
3. cosim_design: 拿步驟 1 寫好的 C++ 測試檔 (Testbench) 產生輸入測資, 餵給步驟 2 產生出來的「硬體電路 (RTL)」, 然後把硬體算出來的結果, 拿去跟 C++ 算出來的結果做比對. 目的是驗證「合成出來的硬體」行為是否和「原本的 C++ 軟體」一模一樣.
4. export_design: 拿著步驟 2 產生出來的硬體設計圖, 實際對應到你指定的 FPGA 晶片內部結構上. 包含了邏輯合成 (Logic Synthesis), 佈局 (Placement - 決定邏輯閘要放在晶片的哪個位置) 以及繞線 (Routing - 把這些邏輯閘用金屬線連起來). 目的是獲得最真實的硬體數據, 並準備產生最終燒錄檔.

## 問題與解法
# 編譯器在跑模擬 (CSIM) 時，找不到你的標頭檔 top.h
1. 點擊工具列的 Project -> Project Settings
2. 在左側選擇 Simulation
3. 在右側選取你的 Testbench 檔案 (tb_top.cpp)
4. 點擊 Edit CFLAGS 按鈕
5. 輸入: -I../src (假設你的 top.h 放在與 tb 並列的 src 資料夾中)
6. 同樣的操作也要在 Synthesis 標籤頁對 top.cpp 做一次 (設定其 CFLAGS)
## 還原開發環境
# 執行腳本
1. 打開 Vitis HLS Command Prompt, 使用 cd 指令進入你 clone 下來的根目錄: 2026-soqpsk
2. 從根目錄呼叫 scripts 資料夾裡的腳本: vitis_hls -f scripts/run_hls.tcl
3. 打開 Vitis HLS, 從 hls_prj 資料夾開啟 project

## 問題與解法
# 編譯器在跑模擬 (CSIM) 時，找不到你的標頭檔 top.h
1. 點擊工具列的 Project -> Project Settings
2. 在左側選擇 Simulation
3. 在右側選取你的 Testbench 檔案 (tb_top.cpp)
4. 點擊 Edit CFLAGS 按鈕
5. 輸入: -I../src (假設你的 top.h 放在與 tb 並列的 src 資料夾中)
6. 同樣的操作也要在 Synthesis 標籤頁對 top.cpp 做一次 (設定其 CFLAGS)
# 編譯器在跑模擬（CSIM）時，找不到你的標頭檔 top.h
1. 點擊工具列的 Project -> Project Settings
2. 在左側選擇 Simulation
3. 在右側選取你的 Testbench 檔案（tb_top.cpp）
4. 點擊 Edit CFLAGS 按鈕
5. 輸入：-I../src (假設你的 top.h 放在與 tb 並列的 src 資料夾中)
6. 同樣的操作也要在 Synthesis 標籤頁對 top.cpp 做一次（設定其 CFLAGS）
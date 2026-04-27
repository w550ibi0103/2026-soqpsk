## 開發
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

# C Simulation 功能介紹
1. Launch Debugger: 編譯你的 C/C++ 程式碼與 Testbench, 但不會直接把程式跑完, 而是會自動切換到 Debug Perspective（除錯介面）. 當你的 C Simulation 結果不如預期、發生當機（例如 Segmentation Fault）, 或是你想確認某個變數在迴圈裡的值是如何變化的時候。你可以利用它來下斷點（Breakpoints）、單步執行（Step Over/Into）並即時監控變數.
2. Build Only: 工具只會執行編譯動作（將程式碼編譯成執行檔 csim.exe）, 不會執行你的 Testbench. 當你剛寫完或大幅修改了一段程式碼, 只想快速檢查「有沒有語法錯誤（Syntax Error）」、「標頭檔有沒有 include 成功」或「資料型態有沒有給錯」時.
3. Clean Build: 在編譯之前, 強制清除之前產生的所有暫存檔（Object files）、快取與舊的執行檔, 然後從零開始重新編譯. 強烈建議在修改了 .h 標頭檔（如 Macro 數值變更）, 或者你明明改了程式碼, 但模擬出來的結果卻還是舊的時候使用. 這可以避免編譯器因為偷懶而吃到舊的 Cache.
4. Optimizing Compile: 使用較高等級的最佳化參數（類似 GCC 的 -O2 或 -O3）來編譯程式碼. 預設情況下, HLS 是帶有除錯資訊在編譯的, 跑起來比較慢. 當你的 Testbench 資料量非常龐大（例如要處理好幾張 4K 圖片、或是跑極長的通訊封包陣列）, 導致普通的 C 模擬要跑好幾分鐘甚至更久時. 勾選這個會稍微增加「編譯時間」, 但能大幅縮短「執行時間」. （注意: 此選項與 Launch Debugger 是互斥的, 兩者不能同時使用）.
5. Enable pre-synthesis control flow viewer: 在 C 模擬後, 幫你畫出一張視覺化的「控制流圖 (Control Flow Graph)」, 讓你提早看看 HLS 是怎麼理解你的 if-else 分支和 for/while 迴圈結構的. 如果你是在 Windows 環境下執行 Vitis HLS, 這個選項就會被工具強制反灰鎖死.

# C Synthesis 功能介紹
1. Vivado IP Flow Target: 這就是最經典的 HLS 開發模式, 將 C/C++ 演算法轉換成標準的硬體矽智財（RTL IP）.
1.1. 整合方式: 你必須打開 Vivado, 建立 Block Design, 把這個 IP 拉進畫布中. 然後手動把時鐘（ap_clk）、重置信號（ap_rst_n）、AXI 總線一根一根連起來, 甚至手動配置 AXI DMA 來搬運資料.
1.2. 控制方式: 通常在 Zynq PS 端寫 Bare-metal（裸機）C code 或自製 Linux Driver, 透過讀寫 AXI4-Lite 暫存器來控制 IP 啟動.

2. Vitis Kernel Flow Target: 這是 Xilinx 較新推廣的異質運算（Heterogeneous Computing）模式, 主要搭配 XRT (Xilinx Runtime) 使用.
2.1. 整合方式: 你不需要打開 Vivado 畫 Block Design. 你只需把 .xo 檔交給 Vitis, 設定好要連結幾個 DDR 記憶體通道, Vitis 的 v++ 編譯器會在背景自動幫你生成 Vivado 專案, 自動把 AXI 總線和記憶體控制器接好.
2.2. 控制方式: 在運行 Embedded Linux 的 Zynq SoC（或資料中心的 Alveo 卡）上, 軟體端會使用 OpenCL 或 XRT API（C++/Python）來呼叫這個 Kernel, 就像呼叫軟體函式庫一樣, 作業系統會自動處理資料搬運與硬體啟動.

# C/RTL Co-simulation 功能介紹
1. Vivado XSIM / ModelSim / Riviera: 預設選 Vivado XSIM 就好, 這是 Xilinx 內建且免費的. ModelSim 或 Riviera 是第三方的商業軟體, 模擬速度通常更快, 但你需要另外購買授權並安裝. 如果你選了第三方軟體, 才需要填寫下方的 Compiled Library Location 告訴工具函式庫在哪裡.
2. Verilog / VHDL: 業界通常以 Verilog 為主流, 直接選 Verilog 即可.
3. Optimizing Compile: 如果你的 Testbench 餵進去的測資非常龐大（例如跑幾萬個週期的訊號取樣點）, 一般的 RTL 模擬會跑非常久. 勾選這個會拉長一開始的「編譯時間」, 但能大幅縮短後續的「模擬執行時間」.
4. Input Arguments: 讓你傳遞參數給 C Testbench 裡的 main(int argc, char *argv[]) 函數. 如果你的 Testbench 是寫成動態讀取外部檔案（例如 csim.exe input_data.txt）, 你就在這格填入 input_data.txt, 如果沒有用到就留空.
5. Dump Trace: 決定是否要記錄硬體的波形變化. 下拉有 none (不記錄)、port (只記錄頂層 I/O 引腳)、all (記錄所有內部訊號).
6. Random Stall: 在模擬過程中, 工具會隨機地把 AXI4-Stream 的 TREADY 或握手訊號拉低, 假裝「下游模組突然很忙，暫時不收資料」或是「上游突然斷流」.
7. Wave Debug: 上面的 Dump Trace 設為 none 就會反灰. 如果你改成 all, 這個選項就會亮起. 勾選後, 可以在檢視波形時, 看到更直觀的 Dataflow 各任務（Tasks）之間的啟動與停止時序圖.
8. Disable Deadlock Detection: HLS 預設會在 Dataflow 電路中插入偵測「死結 (Deadlock)」的額外邏輯（死結就是 Task A 等 Task B 給資料，Task B 又在等 Task A 釋放空間, 導致整個系統卡死）. 平常不要勾（保持偵測開啟）. 只有在 IP 已經驗證非常穩定, 想要省下那麼一點點 FPGA 邏輯資源時, 才在最終合成前勾選並重新跑流程.
9. Channel (PIPO/FIFO) Profiling: 追蹤 Dataflow 任務之間互相傳遞資料的 FIFO 或 PIPO (Ping-Pong Buffer) 的使用深度. 當你發現硬體的吞吐量（Throughput）不如預期, 資料卡在某個階段時勾選. 模擬跑完後, 工具會出具報告, 告訴你哪個 FIFO 容量設得太小導致瓶頸, 你就可以針對性地去修改 #pragma HLS stream depth=N 來加大緩衝區.
10. Dynamic Deadlock Prevention: 在模擬期間自動調整內部 FIFO 的大小, 以防止因為緩衝區不足而導致的死結. 當你遇到 Dataflow 死結, 但不確定 FIFO 該設多大時, 可以勾選讓工具幫你動態嘗試.

## 問題與解法
# 編譯器在跑模擬 (CSIM) 時，找不到你的標頭檔 top.h
1. 點擊工具列的 Project -> Project Settings
2. 在左側選擇 Simulation
3. 在右側選取你的 Testbench 檔案 (tb_top.cpp)
4. 點擊 Edit CFLAGS 按鈕
5. 輸入: -I../src (假設你的 top.h 放在與 tb 並列的 src 資料夾中)
6. 同樣的操作也要在 Synthesis 標籤頁對 top.cpp 做一次 (設定其 CFLAGS)
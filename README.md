# 開發
## 開發環境還原步驟
1. 從 git clone 程式碼 
2. 打開 Vitis HLS Command Prompt, 使用 cd 指令進入你 clone 下來的根目錄: 2026-soqpsk
3. 從根目錄呼叫 scripts 資料夾裡的腳本: vitis_hls -f scripts/run_hls.tcl
4. 打開 Vitis HLS, 從 hls_prj 資料夾開啟 project

## 腳本指令說明
1. csim_design: 最單純的軟體測試. 工具會把你寫的 top.cpp 和 tb_top.cpp 當作一般的 C++ 程式, 用 GCC 或 Clang 編譯器跑一次.
- 目的: 驗證演算法的「功能」對不對.
2. csynth_design: 工具會開始把你的 C++ 程式碼「翻譯」成硬體描述語言 (Verilog 或 VHDL).
- 目的: 將軟體邏輯轉換為硬體電路設計圖, 工具會進行「排程 (Scheduling)」(決定哪個加法器在哪個 Clock 執行) 與「綁定 (Binding)」(決定這個加法要用 DSP 還是 LUT 來做).
3. cosim_design: 拿步驟 1 寫好的 C++ 測試檔 (Testbench) 產生輸入測資, 餵給步驟 2 產生出來的「硬體電路 (RTL)」, 然後把硬體算出來的結果, 拿去跟 C++ 算出來的結果做比對.
- 目的: 驗證「合成出來的硬體」行為是否和「原本的 C++ 軟體」一模一樣.
4. export_design: 拿著步驟 2 產生出來的硬體設計圖, 實際對應到你指定的 FPGA 晶片內部結構上. 包含了邏輯合成 (Logic Synthesis), 佈局 (Placement - 決定邏輯閘要放在晶片的哪個位置) 以及繞線 (Routing - 把這些邏輯閘用金屬線連起來).
- 目的: 獲得最真實的硬體數據, 並準備產生最終燒錄檔.

## C Simulation 功能介紹
1. Launch Debugger: 編譯你的 C/C++ 程式碼與 Testbench, 但不會直接把程式跑完, 而是會自動切換到 Debug Perspective（除錯介面）. 當你的 C Simulation 結果不如預期、發生當機（例如 Segmentation Fault）, 或是你想確認某個變數在迴圈裡的值是如何變化的時候。你可以利用它來下斷點（Breakpoints）、單步執行（Step Over/Into）並即時監控變數.
2. Build Only: 工具只會執行編譯動作（將程式碼編譯成執行檔 csim.exe）, 不會執行你的 Testbench. 當你剛寫完或大幅修改了一段程式碼, 只想快速檢查「有沒有語法錯誤（Syntax Error）」、「標頭檔有沒有 include 成功」或「資料型態有沒有給錯」時.
3. Clean Build: 在編譯之前, 強制清除之前產生的所有暫存檔（Object files）、快取與舊的執行檔, 然後從零開始重新編譯. 強烈建議在修改了 .h 標頭檔（如 Macro 數值變更）, 或者你明明改了程式碼, 但模擬出來的結果卻還是舊的時候使用. 這可以避免編譯器因為偷懶而吃到舊的 Cache.
4. Optimizing Compile: 使用較高等級的最佳化參數（類似 GCC 的 -O2 或 -O3）來編譯程式碼. 預設情況下, HLS 是帶有除錯資訊在編譯的, 跑起來比較慢. 當你的 Testbench 資料量非常龐大（例如要處理好幾張 4K 圖片、或是跑極長的通訊封包陣列）, 導致普通的 C 模擬要跑好幾分鐘甚至更久時. 勾選這個會稍微增加「編譯時間」, 但能大幅縮短「執行時間」. （注意: 此選項與 Launch Debugger 是互斥的, 兩者不能同時使用）.
5. Enable pre-synthesis control flow viewer: 在 C 模擬後, 幫你畫出一張視覺化的「控制流圖 (Control Flow Graph)」, 讓你提早看看 HLS 是怎麼理解你的 if-else 分支和 for/while 迴圈結構的. 如果你是在 Windows 環境下執行 Vitis HLS, 這個選項就會被工具強制反灰鎖死.

## C Synthesis 功能介紹
1. Vivado IP Flow Target: 這就是最經典的 HLS 開發模式, 將 C/C++ 演算法轉換成標準的硬體矽智財（RTL IP）.
- 整合方式: 你必須打開 Vivado, 建立 Block Design, 把這個 IP 拉進畫布中. 然後手動把時鐘（ap_clk）、重置信號（ap_rst_n）、AXI 總線一根一根連起來, 甚至手動配置 AXI DMA 來搬運資料.
- 控制方式: 通常在 Zynq PS 端寫 Bare-metal（裸機）C code 或自製 Linux Driver, 透過讀寫 AXI4-Lite 暫存器來控制 IP 啟動.

2. Vitis Kernel Flow Target: 這是 Xilinx 較新推廣的異質運算（Heterogeneous Computing）模式, 主要搭配 XRT (Xilinx Runtime) 使用.
- 整合方式: 你不需要打開 Vivado 畫 Block Design. 你只需把 .xo 檔交給 Vitis, 設定好要連結幾個 DDR 記憶體通道, Vitis 的 v++ 編譯器會在背景自動幫你生成 Vivado 專案, 自動把 AXI 總線和記憶體控制器接好.
- 控制方式: 在運行 Embedded Linux 的 Zynq SoC（或資料中心的 Alveo 卡）上, 軟體端會使用 OpenCL 或 XRT API（C++/Python）來呼叫這個 Kernel, 就像呼叫軟體函式庫一樣, 作業系統會自動處理資料搬運與硬體啟動.

## C/RTL Co-simulation 功能介紹
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

## top.h
1. void 在 C++ 中代表「空」或「無類型」. 在這裡, 它放在 function（函式）名稱 tfm_modulator 的最前面, 用來表示這個函式執行完畢後, 不會回傳任何數值.
2. void tfm_modulator 則代表它純粹執行內部的硬體邏輯, 資料的輸入與輸出都是透過參數（如 stream）來處理, 不需要傳統的 return 數值.
3. :: 叫做「範圍解析運算子」, hls::stream 的意思就是: 「請使用在 hls 命名空間中所定義的 stream（串流）資料型態」, 這樣可以避免跟其他庫（例如標準庫 std）中可能同名的東西發生衝突.
4. &i_out 中的 & 符號代表「引用（Reference）」, 在 C++ 的函式參數中, 加上 & 意味著「參數傳遞時, 使用的是同一個記憶體位置（或硬體線路）, 而不是複製一份新的資料」.
5. #ifndef __TOP_H__ (If Not Defined), 意思是: 「如果現在編譯器還沒有定義 __TOP_H__ 這個巨集（Macro）的話, 就繼續往下看」. #define __TOP_H__ 意思是: 「馬上定義 __TOP_H__ 這個巨集」. #endif 意思是: 「到這裡, 整個流程結束, 關閉 #ifndef 的判斷.」
6. 第一次引入這個檔案時, __TOP_H__ 還沒被定義, 所以會進入區塊, 定義它, 並讀取裡面的程式碼. 第二次如果有其他檔案又引入了同一個標頭檔, 編譯器看到 #ifndef __TOP_H__ 時, 發現「剛剛已經定義過了」, 就會直接跳過到 #endif, 裡面的程式碼就不會被重複讀取.
7. ap_axiu<32, 0, 0, 0>: 四個參數的用途. TDATA: 設定實際傳輸資料的位元寬度. TUSER: 設定使用者自定義的旁帶訊號寬度, 例如設定為 1 來標記「一張影像的第一個像素（Start of Frame, SOF）」, 讓硬體知道新畫面來了. TID: 設定資料流的 ID 寬度, 當你的硬體設計需要把多個不同的資料流（例如兩條獨立的音訊線）「合併（Multiplex）」到同一條實體線上傳輸時, 接收端需要靠 TID 來分辨這筆資料是誰的. TDEST: 設定資料流的目的地寬度, 當你的資料要經過 AXI Router（路由網路）, 需要被分發到不同的硬體模組時, 這就像是信封上的「收件區號」, 決定資料該往哪裡送.

## top.cpp
1. << 符號: 搭配 std::cout（C++ 的標準輸出）一起使用時, 你可以把 << 想像成一個「資料流動的箭頭」. 運作邏輯: 它會把右邊的資料（字串、變數、計算結果）, 順著箭頭的方向「推」進左邊的 std::cout 裡, 最後顯示在你的電腦螢幕（終端機）上.
2. __SYNTHESIS__ 是 Xilinx Vivado/Vitis HLS 編譯器內部預設定義好的一個巨集（Macro）. 這段語法的核心目的是: 「區分現在是在跑『軟體模擬』還是『硬體合成』」.
3. 在軟體模擬階段（C Simulation）: 你在電腦上寫好 C++, 需要印出（std::cout）變數數值來檢查邏輯對不對. 此時編譯器不會定義 __SYNTHESIS__, 所以 #ifndef（If Not Defined）條件成立, 這段印出數值的程式碼會被執行.
4. 在硬體合成階段（C Synthesis）: HLS 工具準備把你的 C++ 轉成硬體電路（Verilog/VHDL）. 但是，硬體晶片（FPGA）是沒有「螢幕」可以印出字串的! 如果硬體合成工具看到 std::cout, 它會不知道該怎麼把它轉成電路, 直接報錯當機.
5. 解決方案: 當按下「硬體合成」時, HLS 工具會自動在背景定義 __SYNTHESIS__. 這時 #ifndef __SYNTHESIS__ 條件失敗, 編譯器就會直接把這整段 std::cout 的除錯程式碼當作透明的（完全忽略）, 不會把它轉換成硬體電路.

# 問題與解法
## 編譯器在跑模擬 (CSIM) 時, 找不到你的標頭檔 top.h
1. 點擊工具列的 Project -> Project Settings
2. 在左側選擇 Simulation
3. 在右側選取你的 Testbench 檔案 (tb_top.cpp)
4. 點擊 Edit CFLAGS 按鈕
5. 輸入: -I../src (假設你的 top.h 放在與 tb 並列的 src 資料夾中)
6. 同樣的操作也要在 Synthesis 標籤頁對 top.cpp 做一次 (設定其 CFLAGS)
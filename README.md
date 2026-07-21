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
1. Launch Debugger: 編譯你的 C/C++ 程式碼與 Testbench, 但不會直接把程式跑完, 而是會自動切換到 Debug Perspective(除錯介面). 當你的 C Simulation 結果不如預期、發生當機(例如 Segmentation Fault), 或是你想確認某個變數在迴圈裡的值是如何變化的時候。你可以利用它來下斷點(Breakpoints)、單步執行(Step Over/Into)並即時監控變數.
2. Build Only: 工具只會執行編譯動作(將程式碼編譯成執行檔 csim.exe), 不會執行你的 Testbench. 當你剛寫完或大幅修改了一段程式碼, 只想快速檢查「有沒有語法錯誤(Syntax Error)」、「標頭檔有沒有 include 成功」或「資料型態有沒有給錯」時.
3. Clean Build: 在編譯之前, 強制清除之前產生的所有暫存檔(Object files)、快取與舊的執行檔, 然後從零開始重新編譯. 強烈建議在修改了 .h 標頭檔(如 Macro 數值變更), 或者你明明改了程式碼, 但模擬出來的結果卻還是舊的時候使用. 這可以避免編譯器因為偷懶而吃到舊的 Cache.
4. Optimizing Compile: 使用較高等級的最佳化參數(類似 GCC 的 -O2 或 -O3)來編譯程式碼. 預設情況下, HLS 是帶有除錯資訊在編譯的, 跑起來比較慢. 當你的 Testbench 資料量非常龐大(例如要處理好幾張 4K 圖片、或是跑極長的通訊封包陣列), 導致普通的 C 模擬要跑好幾分鐘甚至更久時. 勾選這個會稍微增加「編譯時間」, 但能大幅縮短「執行時間」. (注意: 此選項與 Launch Debugger 是互斥的, 兩者不能同時使用).
5. Enable pre-synthesis control flow viewer: 在 C 模擬後, 幫你畫出一張視覺化的「控制流圖 (Control Flow Graph)」, 讓你提早看看 HLS 是怎麼理解你的 if-else 分支和 for/while 迴圈結構的. 如果你是在 Windows 環境下執行 Vitis HLS, 這個選項就會被工具強制反灰鎖死.

## C Synthesis 功能介紹
1. Vivado IP Flow Target: 這就是最經典的 HLS 開發模式, 將 C/C++ 演算法轉換成標準的硬體矽智財(RTL IP).
  - 整合方式: 你必須打開 Vivado, 建立 Block Design, 把這個 IP 拉進畫布中. 然後手動把時鐘(ap_clk)、重置信號(ap_rst_n)、AXI 總線一根一根連起來, 甚至手動配置 AXI DMA 來搬運資料.
  - 控制方式: 通常在 Zynq PS 端寫 Bare-metal(裸機)C code 或自製 Linux Driver, 透過讀寫 AXI4-Lite 暫存器來控制 IP 啟動.

2. Vitis Kernel Flow Target: 這是 Xilinx 較新推廣的異質運算(Heterogeneous Computing)模式, 主要搭配 XRT (Xilinx Runtime) 使用.
  - 整合方式: 你不需要打開 Vivado 畫 Block Design. 你只需把 .xo 檔交給 Vitis, 設定好要連結幾個 DDR 記憶體通道, Vitis 的 v++ 編譯器會在背景自動幫你生成 Vivado 專案, 自動把 AXI 總線和記憶體控制器接好.
  - 控制方式: 在運行 Embedded Linux 的 Zynq SoC(或資料中心的 Alveo 卡)上, 軟體端會使用 OpenCL 或 XRT API(C++/Python)來呼叫這個 Kernel, 就像呼叫軟體函式庫一樣, 作業系統會自動處理資料搬運與硬體啟動.

## C/RTL Co-simulation 功能介紹
1. Vivado XSIM / ModelSim / Riviera: 預設選 Vivado XSIM 就好, 這是 Xilinx 內建且免費的. ModelSim 或 Riviera 是第三方的商業軟體, 模擬速度通常更快, 但你需要另外購買授權並安裝. 如果你選了第三方軟體, 才需要填寫下方的 Compiled Library Location 告訴工具函式庫在哪裡.
2. Verilog / VHDL: 業界通常以 Verilog 為主流, 直接選 Verilog 即可.
3. Optimizing Compile: 如果你的 Testbench 餵進去的測資非常龐大(例如跑幾萬個週期的訊號取樣點), 一般的 RTL 模擬會跑非常久. 勾選這個會拉長一開始的「編譯時間」, 但能大幅縮短後續的「模擬執行時間」.
4. Input Arguments: 讓你傳遞參數給 C Testbench 裡的 main(int argc, char *argv[]) 函數. 如果你的 Testbench 是寫成動態讀取外部檔案(例如 csim.exe input_data.txt), 你就在這格填入 input_data.txt, 如果沒有用到就留空.
5. Dump Trace: 決定是否要記錄硬體的波形變化. 下拉有 none (不記錄)、port (只記錄頂層 I/O 引腳)、all (記錄所有內部訊號).
6. Random Stall: 在模擬過程中, 工具會隨機地把 AXI4-Stream 的 TREADY 或握手訊號拉低, 假裝「下游模組突然很忙，暫時不收資料」或是「上游突然斷流」.
7. Wave Debug: 上面的 Dump Trace 設為 none 就會反灰. 如果你改成 all, 這個選項就會亮起. 勾選後, 可以在檢視波形時, 看到更直觀的 Dataflow 各任務(Tasks)之間的啟動與停止時序圖.
8. Disable Deadlock Detection: HLS 預設會在 Dataflow 電路中插入偵測「死結 (Deadlock)」的額外邏輯(死結就是 Task A 等 Task B 給資料，Task B 又在等 Task A 釋放空間, 導致整個系統卡死). 平常不要勾(保持偵測開啟). 只有在 IP 已經驗證非常穩定, 想要省下那麼一點點 FPGA 邏輯資源時, 才在最終合成前勾選並重新跑流程.
9. Channel (PIPO/FIFO) Profiling: 追蹤 Dataflow 任務之間互相傳遞資料的 FIFO 或 PIPO (Ping-Pong Buffer) 的使用深度. 當你發現硬體的吞吐量(Throughput)不如預期, 資料卡在某個階段時勾選. 模擬跑完後, 工具會出具報告, 告訴你哪個 FIFO 容量設得太小導致瓶頸, 你就可以針對性地去修改 #pragma HLS stream depth=N 來加大緩衝區.
10. Dynamic Deadlock Prevention: 在模擬期間自動調整內部 FIFO 的大小, 以防止因為緩衝區不足而導致的死結. 當你遇到 Dataflow 死結, 但不確定 FIFO 該設多大時, 可以勾選讓工具幫你動態嘗試.

## top.h
1. void 在 C++ 中代表「空」或「無類型」. 在這裡, 它放在 function(函式)名稱 tfm_modulator 的最前面, 用來表示這個函式執行完畢後, 不會回傳任何數值.
2. void tfm_modulator 則代表它純粹執行內部的硬體邏輯, 資料的輸入與輸出都是透過參數(如 stream)來處理, 不需要傳統的 return 數值.
3. :: 叫做「範圍解析運算子」, hls::stream 的意思就是: 「請使用在 hls 命名空間中所定義的 stream(串流)資料型態」, 這樣可以避免跟其他庫(例如標準庫 std)中可能同名的東西發生衝突.
4. &i_out 中的 & 符號代表「引用(Reference)」, 在 C++ 的函式參數中, 加上 & 意味著「參數傳遞時, 使用的是同一個記憶體位置(或硬體線路), 而不是複製一份新的資料」.
5. #ifndef __TOP_H__ (If Not Defined), 意思是: 「如果現在編譯器還沒有定義 __TOP_H__ 這個巨集(Macro)的話, 就繼續往下看」. #define __TOP_H__ 意思是: 「馬上定義 __TOP_H__ 這個巨集」. #endif 意思是: 「到這裡, 整個流程結束, 關閉 #ifndef 的判斷.」
6. 第一次引入這個檔案時, __TOP_H__ 還沒被定義, 所以會進入區塊, 定義它, 並讀取裡面的程式碼. 第二次如果有其他檔案又引入了同一個標頭檔, 編譯器看到 #ifndef __TOP_H__ 時, 發現「剛剛已經定義過了」, 就會直接跳過到 #endif, 裡面的程式碼就不會被重複讀取.
7. ap_axiu<32, 0, 0, 0>: 四個參數的用途. TDATA: 設定實際傳輸資料的位元寬度. TUSER: 設定使用者自定義的旁帶訊號寬度, 例如設定為 1 來標記「一張影像的第一個像素(Start of Frame, SOF)」, 讓硬體知道新畫面來了. TID: 設定資料流的 ID 寬度, 當你的硬體設計需要把多個不同的資料流(例如兩條獨立的音訊線)「合併(Multiplex)」到同一條實體線上傳輸時, 接收端需要靠 TID 來分辨這筆資料是誰的. TDEST: 設定資料流的目的地寬度, 當你的資料要經過 AXI Router(路由網路), 需要被分發到不同的硬體模組時, 這就像是信封上的「收件區號」, 決定資料該往哪裡送.

## top.cpp
1. << 符號: 搭配 std::cout(C++ 的標準輸出)一起使用時, 你可以把 << 想像成一個「資料流動的箭頭」. 運作邏輯: 它會把右邊的資料(字串、變數、計算結果), 順著箭頭的方向「推」進左邊的 std::cout 裡, 最後顯示在你的電腦螢幕(終端機)上.
2. __SYNTHESIS__ 是 Xilinx Vivado/Vitis HLS 編譯器內部預設定義好的一個巨集(Macro). 這段語法的核心目的是: 「區分現在是在跑『軟體模擬』還是『硬體合成』」.
3. 在軟體模擬階段(C Simulation): 你在電腦上寫好 C++, 需要印出(std::cout)變數數值來檢查邏輯對不對. 此時編譯器不會定義 __SYNTHESIS__, 所以 #ifndef(If Not Defined)條件成立, 這段印出數值的程式碼會被執行.
4. 在硬體合成階段(C Synthesis): HLS 工具準備把你的 C++ 轉成硬體電路(Verilog/VHDL). 但是，硬體晶片(FPGA)是沒有「螢幕」可以印出字串的! 如果硬體合成工具看到 std::cout, 它會不知道該怎麼把它轉成電路, 直接報錯當機.
5. 解決方案: 當按下「硬體合成」時, HLS 工具會自動在背景定義 __SYNTHESIS__. 這時 #ifndef __SYNTHESIS__ 條件失敗, 編譯器就會直接把這整段 std::cout 的除錯程式碼當作透明的(完全忽略), 不會把它轉換成硬體電路.

## AXI-Stream 協定
### 核心握手訊號
1. TVALID (Master -> Slave, 必須有): 發送端(Master)告訴接收端(Slave): 「我現在放在 TDATA 上的資料是有效的, 你隨時可以拿走.」
2. TREADY (Slave -> Master, 可選，但強烈建議有): 接收端告訴發送端: 「我現在有空, 準備好接收新資料了.」
### 資料與結構訊號
1. TDATA (Master -> Slave, 可選, 但通常都有): 資料主體, 它的寬度可以是 8, 16, 32, 64, 128... 甚至到 1024-bit.
2. TKEEP (Master -> Slave, 可選): 位元組有效指示(Byte Qualifier). 當傳輸不是整數倍字組的資料時, 用來標記哪些 Byte 是有效資料.
3. TSTRB (Master -> Slave, 可選): 位元組選通(Strobe / Position Qualifier). TSTRB 是用來支援「稀疏矩陣/稀疏流(Sparse Stream)」.
4. TLAST (Master -> Slave, 可選): 封包結束指示(Packet Boundary). 用來告訴接收端: 「這是這一個 Data Packet 的最後一筆資料.」, 代表一次 DMA burst 結束.
### 路由與標籤訊號
1. TID (Master -> Slave, 可選): 資料流 ID(Stream Identifier). 多路 ADC 採樣, 可以用 TID 來區分這筆資料是屬於「通道 0」還是「通道 1」.
2. TDEST (Master -> Slave, 可選): 目的地路由(Destination Identifier). 告訴 AXI-Stream Switch(交換器)這筆資料要送去哪一個 downstream IP.
3. TUSER (Master -> Slave, 可選): 用戶自定義訊號(User-defined Sideband). 這是官方留給開發者的「傳小抄」通道, 協定本身不管裡面裝什麼.
### 全域系統訊號
1. ACLK (全域, 必須): 時脈訊號. 所有的資料採樣、握手都是在 ACLK 的上升沿(Rising Edge)觸發.
2. ARESETn (全域, 必須): 非同步重置訊號, 低電位有效(Active Low).

## AXI4-Lite 協定
### 寫入地址通道 (Write Address Channel - AW)
### 寫入資料通道 (Write Data Channel - W)
### 寫入回應通道 (Write Response Channel - B)
### 讀取地址通道 (Read Address Channel - AR)
### 讀取資料通道 (Read Data Channel - R)
### 全域訊號 (Global Signals)

## Block-Level Control
pragma HLS INTERFACE s_axilite port=return bundle=CTRL → 這顆 IP 是 ap_ctrl_hs 協定, 也就是需要 PS 端寫 ap_start(或設定 auto_restart bit)才會開始跑, 不是天生 free-running 的資料流 IP. 純資料流 IP 一般會用 ap_ctrl_none.
1. 在預設情況下, 當你用 C++ 寫了一個硬體函式(例如你的 tfm_modulator), 這個 IP 合成出來後, 必須要有幾個基本的腳位來讓別人控制它.
2. ap_start: 告訴 IP「開始運算!」.
3. ap_done: IP 算完一輪後, 會拉高這個訊號說「我算完了!」.
4. ap_idle: IP 告訴外面「我現在閒閒沒事做, 正在發呆」.
5. ap_ready: IP 告訴外面「我準備好接收下一筆新任務了」.

## ADRV9009 TX 串接路徑
TX 資料路徑實際串接鏈(由 DDR 往 RF 方向)

DDR (PS)
  → axi_adrv9009_tx_dma (axi_dmac, CYCLIC=1, 128-bit)
  → axi_adrv9009_dacfifo (跨時脈 FIFO：dma_clk → axi_adrv9009_tx_clkgen/clk_0)
  → util_adrv9009_tx_upack (把 I/Q 兩個 channel 的資料 unpack/解交錯, 4 channel, 每 channel 2 sample, 每 sample 16-bit → s_axis_data 是 128-bit)
  → tx_fir_interpolator (8x 內插: 15.36 MSPS → 122.88 MSPS)
  → tx_adrv9009_tpl_core (JESD204 TPL, dac_data_0~3, dac_data_0 = I, dac_data_1 = Q)
  → axi_adrv9009_tx_jesd → axi_adrv9009_tx_xcvr → GT → ADRV9009

## 硬體 reset vs 軟體 reset 的差別
- 硬體 reset: 是一條實體線(例如 block design 裡常見的 peripheral_aresetn), 直接接到 IP 內部所有暫存器的 reset pin. 它由系統的 reset controller 產生, 只要這條線被 assert, 下一個 clock edge 硬體立刻清零, 不需要 CPU 介入, 即使軟體還沒開始跑, 或當機了, 這個 reset 依然有效.
- 軟體 reset(你現在的做法): reset 這個訊號被你用 #pragma HLS INTERFACE s_axilite 變成一個可以被 CPU 透過 AXI-Lite 寫入的暫存器 bit, 函式內部的 if (reset) {...} 本質上就是一段普通的運算邏輯分支. 這代表必須靠軟體主動去寫這顆暫存器, reset 才會發生. 系統的全域 reset net(如 block design 裡的 sys_cpu_resetn)不會自動連到它, 因為打包出來的 IP 甚至不會有一個實體 reset pin 讓你在 block design 接. 如果 CPU 忘記寫, 或開機早期驅動還沒載入, IP 內部狀態會停在上電後的不確定初始值, 直到你真的寫入為止.

## Kuiper Linux
ADI 官方釋出的 Kuiper Linux 映像檔(Image)採取的是「大補帖 (Universal)」的設計理念. 當工程師們在編譯這個 Linux 系統時, 他們已經把 AD9361, ADRV9009, ADRV9002 等數十種晶片的 Linux Kernel Driver(基於 IIO 子系統)全部編譯進去了.
1. 燒錄 SD 卡: 使用 Rufus 或 BalenaEtcher 等軟體, 將下載來的 Kuiper Linux .img 檔燒進一張至少 16GB 的 SD 卡中.
2. 尋找你的硬體組合: 燒錄完成後，在電腦上打開 SD 卡的 BOOT 磁碟槽. 進入 zynqmp-zcu102-rev10-adrv9009 這個資料夾(名稱可能略有不同，請認明 ZCU102 與 ADRV9009 的組合).
3. 複製開機檔: 將該資料夾裡面的所有檔案(通常包含 BOOT.BIN, system.dtb, Image 等), 複製並貼到 BOOT 磁碟區的最外層(根目錄)，覆蓋掉原本的檔案.
4. 開機: 將 SD 卡插上 ZCU102, 將開發板的指撥開關切換為「SD Card 開機模式」,然後開啟電源.

## 修改 PL 後的打包與 SD 卡更新步驟
1. 開啟 Vitis, 選擇 Create Platform Project, 並匯入你剛剛從 Vivado 匯出的 .xsa 檔案.
2. Vitis 會讀取 .xsa, 建置 Platform 後, 會自動幫你生成兩個開機必備的基礎檔案:
- zynqmp_fsbl.elf (First Stage Boot Loader)
- pmufw.elf (Platform Management Unit Firmware)
3. 同時, Vitis 也會將你包在 .xsa 裡面的 FPGA 邏輯檔解壓縮出來, 名為 system.bit (或根據你的專案名稱命名).
4. 除了上述 3 個與硬體相關的檔案外, 要讓 Kuiper Linux 順利開機, 還需要 2 個負責引導 Linux 的軟體檔案 (通常可以從 ADI GitHub 釋出的 ZCU102 Boot 分支中取得, 或用 Petalinux 生成):
- bl31.elf (ARM Trusted Firmware)
- u-boot.elf (U-Boot Bootloader)
5. 在 Vitis 的選單列, 找到 Xilinx -> Create Boot Image
6. 選擇架構為 Zynq MP，並在 BIF (Boot Image Format) 的設定區塊中, 依照嚴格順序加入以下 5 個檔案(順序錯了會無法開機):
- zynqmp_fsbl.elf (Partition Type: bootloader)
- pmufw.elf (Partition Type: pmu)
- system.bit (Partition Type: datafile)  <-- 你的 SOQPSK 就在這裡面
- bl31.elf (Partition Type: datafile, Exception Level: EL-3, TrustZone)
- u-boot.elf (Partition Type: datafile, Exception Level: EL-2)
7. 點擊 Create Image, Vitis 就會幫你打包出一個全新的 BOOT.BIN 檔案.
8. 打包完成後, 你只需要把這個新產生的 BOOT.BIN 放到 SD 卡的 BOOT 磁碟區(根目錄)即可.
SD 卡內必須維持的檔案清單 (於 BOOT 根目錄):
- BOOT.BIN
- Image (原本 Kuiper Linux 的核心檔, 不變)
- system.dtb (原本對應 ADRV9009 的設備樹, 只要你沒有修改 AXI 位址或中斷腳位, 通常可以不變)
- uEnv.txt (開機參數設定檔, 不變)

# 問題與解法
## 編譯器在跑模擬 (CSIM) 時, 找不到你的標頭檔 top.h
1. 點擊工具列的 Project -> Project Settings
2. 在左側選擇 Simulation
3. 在右側選取你的 Testbench 檔案 (tb_top.cpp)
4. 點擊 Edit CFLAGS 按鈕
5. 輸入: -I../src (假設你的 top.h 放在與 tb 並列的 src 資料夾中)
6. 同樣的操作也要在 Synthesis 標籤頁對 top.cpp 做一次 (設定其 CFLAGS)

## 如何還原 Analog Device Reference Design?
1. https://github.com/analogdevicesinc/hdl.git 下載
2. 切換到對應電腦安裝的版本的分支, 如電腦安裝 2023.2, 分支切換到 hdl_2023_r2
3. 開啟 Vivado Tcl Shell, cd C:/XilinxWorkspace/Vivado/hdl/projects/adrv9009/zcu102
4. source ./system_project.tcl
5. 腳本會預設你已經提前把所有相依的自定義 IP(例如 JESD204 介面、基礎通訊協定等)都建置完畢了. 如果沒有依照順序先建置這些基礎設施ㄝVivado 就會因為不認得這些專屬的 Tcl 指令而直接報錯中斷.
6. 在 ADI 的官方規範中, 強烈建議不要直接在 Vivado Console 裡手動 source 專案腳本, 而是要依賴 make 指令來自動處理相依性

## 如何讓 windows git bash 可以執行 make?
1. 前往 ezwinports (SourceForge) 下載 make-4.3-without-guile-w32-bin.zip(版本號可能隨時間更新, 選擇最新的 without-guile 版本即可)
2. 將壓縮檔解開, 在 bin 資料夾中找到 make.exe
3. 將 make.exe 複製並貼到你 Git 的安裝目錄底下的 C:\Program Files\Git\usr\bin\
4. 重新開啟一個 Git Bash 視窗, 輸入 make -v. 如果成功顯示版本資訊, 代表 Git Bash 已經學會 make 指令了.
5. Git Bash (MINGW64) 是一個非常輕量級的終端機, 它主要只提供 Git 相關的功能, 預設並沒有包含 flock 這個 Linux 核心工具, 有可能不能用.
6. 參考 Cygwin 使用教學

## Cygwin 使用教學
1. 因為 Windows 的系統架構(NT 核心)跟 Linux 完全不同, Linux 的程式無法直接在 Windows 上跑. Cygwin 的做法是: 提供一個巨大的轉換層(DLL 檔), 當 Linux 工具(像是 flock)發出指令時, Cygwin 會即時把它「翻譯」成 Windows 聽得懂的指令.
2. 請將左上角的 View 切換成 "Full", 然後在 Search 欄位中逐一搜尋並勾選以下套件(點擊 "Skip" 讓它變成版本號即代表要安裝)
  - make: 編譯專案的核心工具
  - git: 版本控制與清理專案狀態必備
  - bc: ADI 腳本裡做數學運算時會呼叫
  - dos2unix: 處理 Windows/Linux 換行字元衝突的救星
3. Cygwin 會把 Windows 的所有磁碟機, 統一掛載在 /cygdrive/ 這個根目錄底下, Cygwin 視角: /cygdrive/c/XilinxWorkspace/Vivado/hdl/...
4. Bash 指令(複製至 Cygwin 終端機)
  - 永久寫入環境變數: echo "export PATH=\$PATH:/cygdrive/c/Xilinx/Vivado/2023.2/bin" >> ~/.bashrc
  - 套用設定: source ~/.bashrc
  - 切換至專案目錄: cd /cygdrive/c/XilinxWorkspace/Vivado/hdl/projects/adrv9009/zcu102
  - 清理殘留檔案: git clean -xdf
  - 開始編譯: make
  - 中間會一直 FAILED, 重新輸入 make 可以繼續編譯

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

## 待確認事項(2026-07-09 追加討論, 尚未定案, 明天接續)
- **`clk_0` 實際頻率有矛盾, 需要用權威來源重新確認, 不能用我們雙方互相引用的推論**:
  - 之前(2026-07-07)以為 `clk_0=122.88MHz` 的依據, 是 `ad_add_interpolation_filter` 呼叫時傳給 Xilinx FIR Compiler wizard 的 `Clock_Frequency=122.88` 這個 IP 設定參數, 但這個參數只是餵給 wizard 決定內部架構用的, 不保證等於實際接線的 clock 淨頻率.
  - 重新拿 RTL 事實反推: DAC 最終速率(鐵的事實, 8x 內插 15.36→122.88 MSPS 得到)= 122.88 MSPS; `DATA_PATH_WIDTH=2`(鐵的事實, 直接讀 RTL 得到)= 每個 `clk` cycle bus 上就有 2 筆同一 channel 的樣本. 兩者相除:`122.88 MSPS / 2 = 61.44MHz`——這個算法反而指向 `clk_0` 應該是 **61.44MHz**, 跟之前的假設矛盾.
  - **這個數字目前只是推論, 兩個版本都沒有被權威來源證實過**. 下一步要嘛跑一次完整 Vivado build 後看 `report_clocks`/timing summary 直接讀 `clk_0` net 的實際頻率, 要嘛找負責這份 Vivado 專案的人確認 `axi_adrv9009_tx_clkgen`(`CLKIN_PERIOD=4`, `VCO_MUL=4`, `VCO_DIV=1`, `CLK0_DIV=4`)那顆 MMCM 的輸入時脈來源到底是多少.
  - 這個數字會直接決定 `tfm_modulator` 要不要改成「每個 clock 內部平行算 2 組樣本」, 還是維持現在「每個 clock 1 組樣本」再外掛一個簡單的 2:1 打包/serializer 就好, 兩條路的改動量差很多, 要等確認後才能定案.
- ADRV9009 的 profile(TES 燒錄那邊)是否真的是 122.88 MSPS, 需要使用者用 TES 或現有的 profile 檔再次確認, 這份筆記只確認了 FPGA 端 HDL 原始碼寫的數字.

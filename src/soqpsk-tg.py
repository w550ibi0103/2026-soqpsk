import numpy as np
import matplotlib.pyplot as plt
import pandas as pd

# 1. 參數設定 (基於 Table 2-4)
rho = 0.70
B = 1.25
T1 = 1.5
T2 = 0.5
Tb = 1.0  # 位元時間歸一化, 傳輸一個 bit 所需要的時間, 這意味著我們把時間的單位設為「一個位元的長度」.傳輸速率（Bit Rate）Rb = 1 / Tb
sps = 16  # 超取樣率 (Samples per Symbol), 建議設高一點觀察波形. 在一個位元時間 (Tb) 內, 你切成了多少個取樣點. 如果你要在 FPGA 輸出這組訊號, 你的 DAC 時鐘頻率必須是 Rb * sps.
L = 8     # 脈衝長度為 8 個 Tb, 頻率脈衝 g(t) 在時間軸上持續影響的範圍.

# 2. 原始資料產生 (32-bit 範例)
bits = np.random.randint(0, 2, 32)
raymond_bits = np.array([1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1])
# print(f"Original bits is: {bits}")

# --- Block 1: Differential Encoder  ---
def diff_encode(b):
    delta = np.zeros_like(b)
    last_delta = 0
    for i in range(len(b)//2):
        # Even bit: delta(2i) = b(2i) ^ ~delta(2i-1)
        # delta(0) = b(0) ^ (NOT delta(-1))
        delta[2*i] = b[2*i] ^ (1 - last_delta)
        # Odd bit: delta(2i+1) = b(2i+1) ^ delta(2i)
        # delta(1) = b(1) ^ delta(0)
        delta[2*i+1] = b[2*i+1] ^ delta[2*i]
        last_delta = delta[2*i+1]
    return delta

diff_bits = diff_encode(raymond_bits)
# print(f"Differential bits is: {diff_bits}")

# --- Block 2: Shift Binary ---
# 0 -> -1, 1 -> 1
diff_bits_shift = 2 * diff_bits - 1
# print(f"Ternary bits is: {diff_bits_shift}")

# --- Block 3: Precoder (alpha)  ---
def precoder(t):
    alpha = np.zeros(len(t))
    # 需處理邊界, 整個通訊開始的「第一包資料」的「最開頭」才需要. 假設 t[-1]=1, t[-2]=-1 (標準常見初始狀態)
    padded_t = np.concatenate(([-1, 1], t))  # 把兩個 array 沿著同一個 axis（預設 axis=0）接起來, 在 t 的開頭插入 [-1, 1]
    for i in range(len(t)):
        idx = i + 2
        # alpha_i = (-1)^(i+1) * t_{i-1} * (t_i - t_{i-2}) / 2
        alpha[i] = ((-1)**(i+1)) * (padded_t[idx-1] * (padded_t[idx] - padded_t[idx-2])) / 2
    return alpha

alpha = precoder(diff_bits_shift)
# print(f"alpha ternary bits is: {alpha}")

# --- Block 4: Frequency Pulse g(t) Generation ---
t = np.linspace(-L*Tb/2, L*Tb/2, L*sps)
# w(t) 窗函數
w = np.where(np.abs(t/(2*Tb)) <= T1, 1, np.where(np.abs(t/(2*Tb)) <= T1+T2, 0.5 + 0.5*np.cos(np.pi*(np.abs(t/(2*Tb))-T1)/T2), 0))
# g(t) 主公式
A = 1.0 # 初始 A, 稍後需歸一化 
term1 = (A * np.cos(np.pi * rho * B * t / (2*Tb))) / (1 - 4*(rho * B * t / (2*Tb))**2)
term2 = np.sinc(B * t / (2*Tb)) # np.sinc(x) is sin(pi*x)/(pi*x)
g = term1 * term2 * w  # g(t) = term1 * term2 * w(t)
# 把 g(t) 面積歸一化為 1/2, 是為了確保「一個位元」剛好對應「90 度的相位變化」. 把陣列裡所有的點加起來 * 時間步長 (delta_t).
g = g / (np.sum(g) * (Tb/sps)) * 0.5 # 歸一化面積為 1/2, delta_phi = 2 * pi * h (積分 alpha_i * g(t) * dt). 如果我們把 g(t) 的總積分面積設定為 1/2, delta_phi = 2 * pi * 0.5 * (1 * 1/2) = pi/2

# --- Block 5: Create Impulses & FM Modulator ---
# Upsampling alpha
impulses = np.zeros(len(alpha) * sps)
impulses[::sps] = alpha

# 卷積得到瞬時頻率 (切片)
# freq_dev = np.convolve(impulses, g, mode='full')[:len(impulses)]
# 讓捲積跑出完整長度 (不切片)
freq_dev = np.convolve(impulses, g, mode='full')
# 相位積分 (h=0.5)
phase = 2 * np.pi * 0.5 * np.cumsum(freq_dev) * (Tb/sps)

# I/Q 輸出
I = np.cos(phase)
Q = np.sin(phase)

# --- 繪圖分析 ---
plt.figure(figsize=(24, 10))

plt.subplot(5,3,1)
plt.step(range(len(raymond_bits)), raymond_bits, where='post')  # 畫一個階梯圖, where='post' 在「區間的右邊」才跳
plt.plot(raymond_bits, linestyle='None', marker='.')
plt.title("Original Bits")
plt.grid(True)

plt.subplot(5,3,4)
plt.step(range(len(diff_bits)), diff_bits, where='post')
plt.plot(diff_bits, linestyle='None', marker='.')
plt.title("Differential Bits")
plt.grid(True)

plt.subplot(5,3,7)
plt.step(range(len(diff_bits_shift)), diff_bits_shift, where='post')
plt.plot(diff_bits_shift, linestyle='None', marker='.')
plt.title("Differential Bits (shift)")
plt.grid(True)

plt.subplot(5,3,10)
plt.step(range(len(alpha)), alpha, where='post')
plt.plot(alpha, linestyle='None', marker='.')
plt.title("Precoder Output (Ternary Alpha)")
plt.grid(True)

plt.subplot(5,3,2)
plt.step(range(len(t)), t, where='post')
plt.plot(t, linestyle='None', marker='.')
plt.title("t")
plt.grid(True)

plt.subplot(5,3,5)
plt.step(range(len(w)), w, where='post')
plt.plot(w, linestyle='None', marker='.')
plt.title("Window")
plt.grid(True)

plt.subplot(5,3,8)
plt.plot(g, linestyle='None', marker='.')
plt.title("Frequency Pulse g(t) (RCC 106-22 Standard)")
plt.grid(True)

plt.subplot(5,3,3)
plt.plot(impulses, linestyle='solid', marker='.')
plt.title("Upsampling")
plt.grid(True)

plt.subplot(5,3,6)
plt.plot(freq_dev, linestyle='None', marker='.')
plt.title("Convolution")
plt.grid(True)

plt.subplot(5,3,9)
plt.plot(phase, linestyle='None', marker='.')
plt.title("Phase")
plt.grid(True)

plt.subplot(5,3,12)
plt.plot(I, label='I-data')
plt.plot(Q, label='Q-data')
plt.title("I/Q Time Domain Signals")
plt.legend()
plt.grid(True)

plt.subplot(5,3,15)
plt.plot(I, Q, marker='.')
plt.title("Constellation (Should be a circle - Constant Envelope)")
plt.axis('equal')
plt.grid(True)

plt.tight_layout()
plt.show()


'''
data = {
    'Original_bit': pd.Series(raymond_bits),
    'Alpha_Ternary': pd.Series(alpha), # 只有 32 點
    'I_Data': pd.Series(I),                    # 有 512 點
    'Q_Data': pd.Series(Q)                     # 有 512 點
}

# 轉換成 DataFrame
df = pd.DataFrame(data)

# --- 匯出成 CSV 檔 ---
file_name = "soqpsk_test_vectors.csv"
df.to_csv(file_name, index=False)

print(f"檔案已儲存至: {file_name}")
'''

'''
# 在你原本的 Python 程式碼最後面加上這幾行產生 g(t)
with open('g_coeffs.inc', 'w') as f:
    for val in g:
        # 將數值寫成符合 C 語言陣列的格式
        f.write(f"{val},\n")
print("g_coeffs.inc 檔案已產生.")
'''
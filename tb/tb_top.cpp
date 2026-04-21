#include "top.h"
#include <iostream>
#include <fstream>
#include <iomanip>

int main() {
    // --------------------------------------------------------
    // 1. Define AXI-Stream interfaces
    // --------------------------------------------------------
    hls::stream<bit_pkt> bit_in("bit_in_stream");
    hls::stream<sample_pkt> i_out("i_out_stream");
    hls::stream<sample_pkt> q_out("q_out_stream");
    hls::stream<sample_pkt> phase_out("phase_out_stream");
    hls::stream<sample_pkt> freq_out("freq_out_stream");

    // --------------------------------------------------------
    // 2. Prepare test vectors (Simulating DMA behavior)
    // --------------------------------------------------------
    // We will send 1 words (32 bits total) to the IP
    // 0xDE747267 = 0b11011110011101000111001001100111 = [1,1,1,0,0,1,1,0,0,1,0,0,1,1,1,0,0,0,1,0,1,1,1,0,0,1,1,1,1,0,1,1]
    const int NUM_WORDS = 1;
    uint32_t test_data[NUM_WORDS] = {
        0xDE747267  // Ends with a few 1s, mostly 0s
    };

    std::cout << ">> Starting SOQPSK-TG IP Simulation..." << std::endl;

    // Push data into the input stream (Simulating DMA writing to FIFO)
    for (int i = 0; i < NUM_WORDS; i++) {
        bit_pkt pkt;
        pkt.data = test_data[i];
        // Assert TLAST on the final word of the burst
        pkt.last = (i == NUM_WORDS - 1);
        pkt.keep = -1;
        bit_in.write(pkt);
    }

    // --------------------------------------------------------
    // 3. Reset the IP
    // --------------------------------------------------------
    // Call the IP once with reset=true to initialize static variables
    tfm_modulator(bit_in, true, i_out, q_out, phase_out, freq_out);

    // --------------------------------------------------------
    // 4. Run the IP
    // --------------------------------------------------------
    // Each function call processes exactly 1 bit and generates 16 samples.
    // 2 words * 32 bits/word = 64 bits (64 function calls).
    // We run an extra 10 calls to test the "Idle Mode" (underflow protection).
    int total_calls = (NUM_WORDS * 32);

    for (int i = 0; i < total_calls; i++) {
        // Call the IP with reset=false
        tfm_modulator(bit_in, false, i_out, q_out, phase_out, freq_out);
    }

    // --------------------------------------------------------
    // 5. Read outputs and save to CSV (for Python plotting)
    // --------------------------------------------------------
    std::ofstream outfile("output_waveform.csv");
    outfile << "Sample,I_Data,Q_Data,Phase,Freq_Dev,TLAST" << std::endl;

    int sample_idx = 0;
    bool pass = true;

    // Read until the output streams are empty
    while (!i_out.empty() && !q_out.empty()) {
        sample_pkt i_pkt = i_out.read();
        sample_pkt q_pkt = q_out.read();
        sample_pkt p_pkt = phase_out.read();
        sample_pkt f_pkt = freq_out.read();

        // Convert the raw 24-bit integer back to floating point for verification
        // Reinterpret the raw bits as our ap_fixed<24,8> data_t
        data_t i_val; i_val.range(23,0) = i_pkt.data;
        data_t q_val; q_val.range(23,0) = q_pkt.data;
        data_t p_val; p_val.range(23,0) = p_pkt.data;
        data_t f_val; f_val.range(23,0) = f_pkt.data;

        outfile << sample_idx << ","
                << i_val.to_double() << ","
                << q_val.to_double() << ","
				<< p_val.to_double() << ","
				<< f_val.to_double() << ","
                << i_pkt.last << std::endl;

        // Simple check: Output should not exceed unit circle significantly
        if (i_val.to_double() > 1.2 || i_val.to_double() < -1.2) {
            pass = false;
        }

        sample_idx++;
    }

    outfile.close();

    // --------------------------------------------------------
    // 6. Print Simulation Result
    // --------------------------------------------------------
    std::cout << ">> Simulation completed. Generated " << sample_idx << " samples." << std::endl;
    std::cout << ">> Results saved to 'output_waveform.csv'." << std::endl;

    if (pass) {
        std::cout << ">> TEST PASSED!" << std::endl;
        return 0; // Return 0 indicates success to Vitis HLS
    } else {
        std::cout << ">> TEST FAILED! (Values out of bounds)" << std::endl;
        return 1; // Return non-zero indicates failure
    }
}

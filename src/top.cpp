#include "top.h"
#include <hls_stream.h>    // Required for AXI-Stream interface
#include <ap_axi_sdata.h>  // Required for AXI-Stream sideband signals (TLAST, TKEEP)

// --- Include standard I/O for debugging during C simulation ---
#ifndef __SYNTHESIS__
#include <iostream>
#endif

// AXI-Stream packet type definitions
// ap_axiu<Data Width, User Width, ID Width, Destination Width>
typedef ap_axiu<32, 0, 0, 0> bit_pkt;     // Input packet: 32-bit data from DMA
typedef ap_axiu<24, 0, 0, 0> sample_pkt;  // Output packet: 24-bit I/Q samples to RF

// Pre-calculated pulse shaping filter coefficients g(t) (generated via Python)
static const data_t g_coeff[G_LEN] = {
    #include "g_coeffs.inc" 
};

void tfm_modulator(
	// The '&' indicates a C++ reference. In HLS, it maps to a physical hardware port rather than passing data by value.
    hls::stream<bit_pkt> &bit_in,
    bool reset,
    hls::stream<sample_pkt> &i_out,
    hls::stream<sample_pkt> &q_out,
	hls::stream<sample_pkt> &phase_out,
	hls::stream<sample_pkt> &freq_out
) {
    // Hardware interface pragmas for Vitis HLS (AXI-Lite for control, AXI-Stream for data)
    // Map data ports to AXI4-Stream
    #pragma HLS INTERFACE axis port=bit_in
    #pragma HLS INTERFACE axis port=i_out
    #pragma HLS INTERFACE axis port=q_out
	#pragma HLS INTERFACE axis port=phase_out
	#pragma HLS INTERFACE axis port=freq_out

    // Map control signals (reset, start, done, idle) to AXI4-Lite
    #pragma HLS INTERFACE s_axilite port=reset bundle=CTRL
    #pragma HLS INTERFACE s_axilite port=return bundle=CTRL

    // Internal state registers (Static variables map to Flip-Flops or BRAM)
    static bool idle_mode = true;
    static int last_delta = 0;
    static int bit_cnt = 0;

    // Static array of size 3 to retain past states. The 3rd element is reserved for padding/redundancy.
    static data_t t_prev[3] = {-1, 1, 0};
    static data_t shift_reg[G_LEN] = {0};
    static data_t current_phase = 0;

    // --- Serializer (Parallel-to-Serial) State Registers ---
    static uint32_t current_word = 0;  // Holds the current 32-bit data chunk
    static int bit_index = 32;         // 32 means the buffer is empty and needs new data
    static bool is_burst_end = false;  // Flags if the current 32-bit word is the end of the DMA burst

    // Hardware reset logic
    if (reset) {
        idle_mode = true;
        last_delta = 0;
        bit_cnt = 0;
        current_phase = 0;
        bit_index = 32;       // Empty the buffer
        is_burst_end = false; // Clear burst flag
        for(int i=0; i<G_LEN; i++) shift_reg[i] = 0;
        return;
    }

    int current_bit;

    // --- State Machine: Non-blocking Data Fetch & Serialization ---
    // If the 32-bit buffer is fully consumed (index reaches 32), fetch the next word
    // If the stream is empty, the IP enters idle mode and outputs dummy data to maintain phase continuity.
    if (bit_index >= 32) {
        bit_pkt in_val;
        if (bit_in.read_nb(in_val)) {
            // Data available in FIFO: Load the new 32-bit word
            current_word = in_val.data;
            is_burst_end = in_val.last; // TRUE only on the final word of the DMA burst (e.g., word 1024)
            bit_index = 0;              // Reset bit pointer to LSB
            idle_mode = false;
        } else {
            // FIFO underflow: Enter idle mode
            idle_mode = true;
        }
    }

    // Extract the current bit (LSB first) or insert dummy data
    if (idle_mode) {
        current_bit = 0; // Dummy data to maintain continuous RF carrier phase
    } else {
        current_bit = (current_word >> bit_index) & 0x1;
    }

    // --- Block 1 & 2: Differential Encoder ---
    int delta;
    if (bit_cnt % 2 == 0) {
        delta = current_bit ^ (1 - last_delta);
    } else {
        delta = current_bit ^ last_delta;
    }

    // Always update the state for the next bit call
    last_delta = delta;

    // Convert binary to bipolar format (+1.0 or -1.0), data_t is defined in top.h as ap_fixed<24, 8>.
    // Out of 24 bits, 8 are for the integer part and 16 for the fractional part.
    // Type casting to data_t. Ternary operator syntax: (condition) ? (true_val) : (false_val)
    data_t t_now = (delta == 1) ? (data_t)1.0 : (data_t)-1.0;

    // --- Block 3: SOQPSK Precoder ---
    // Calculate alpha based on current and previous symbol states
    // 1. Calculate the difference
    data_t diff = t_now - t_prev[0];

    // 2. Multiply with previous bit
    data_t mult = t_prev[1] * diff;

    // 3. Divide by 2 using hardware arithmetic right-shift (costs 0 DSP resources!)
    data_t half_mult = mult >> 1;

    // 4. Apply sign based on even/odd bit count
    data_t alpha = (bit_cnt % 2 == 0) ? (data_t)(-half_mult) : (data_t)(half_mult);

    // Update history registers
    t_prev[0] = t_prev[1];
    t_prev[1] = t_now;

    // --- NEW: Debug print (Only active during C Simulation) ---
	#ifndef __SYNTHESIS__
    	// Convert fixed-point alpha to double for printing
    	std::cout << "[IP Debug] Bit Index: " << (bit_index - 1)
    			<< " | Current Bit: " << current_bit
				<< " | t_now: " << t_now
				<< " | Alpha: " << alpha.to_double()
				<< " | Idle Mode: " << (idle_mode ? "YES" : "NO")
				<< std::endl;
	#endif
    // ----------------------------------------------------------

    // --- Block 4 & 5: Upsampling, FIR Filtering, and Phase Integration ---
    for (int s = 0; s < SPS; s++) {
        #pragma HLS PIPELINE II=1

        // Upsampling: Insert impulse at the first sample, zero-stuff the rest
        data_t impulse = (s == 0) ? alpha : (data_t)0;

        // Shift register for the FIR filter convolution
        // All elements shift right by one index, and the new impulse is placed at index 0.
        for (int j = G_LEN - 1; j > 0; j--) {
            shift_reg[j] = shift_reg[j-1];
        }
        shift_reg[0] = impulse;

        // FIR filter convolution (Multiply-Accumulate)
        data_t freq_dev = 0;
        for (int j = 0; j < G_LEN; j++) {
            #pragma HLS UNROLL factor=16  // Unroll loop to utilize parallel DSP slices
            freq_dev += shift_reg[j] * g_coeff[j];  // FIR convolution
        }

        // Phase Integration (Accumulator), accumulate freq_dev to calculate the current_phase
        current_phase += freq_dev * (data_t)(3.1415926535 / (double)SPS);

        // Phase Wrapping: Bound the phase between -PI and +PI
        if (current_phase > (data_t)3.1415926535) current_phase -= (data_t)6.283185307;
        else if (current_phase < (data_t)-3.1415926535) current_phase += (data_t)6.283185307;

        // --- Output Formatting & TLAST Propagation ---
        // out_i.data expects raw bits, while hls::cos returns a fixed-point object.
        sample_pkt out_i, out_q, out_phase, out_freq;

        // Extract the raw 24 bits from the fixed-point result
        // 1. Force the output of hls::cos/sin to align with our 24-bit data_t format
        data_t cos_val = hls::cos(current_phase);
        data_t sin_val = hls::sin(current_phase);

        // 2. Safely extract the 24 raw bits into the packet payload
        out_i.data = cos_val.range(23, 0);
        out_q.data = sin_val.range(23, 0);
        out_phase.data = current_phase.range(23, 0);
        out_freq.data = freq_dev.range(23, 0);

        // Assert output TLAST ONLY IF:
        // 1. IP is not in idle mode
        // 2. We are processing the final 32-bit word of the DMA burst
        // 3. We are on the final bit (bit 31) of that word
        // 4. We are generating the final sample (SPS - 1) of that bit
        bool real_last = (!idle_mode && is_burst_end && (bit_index == 31) && (s == SPS - 1));
        out_i.last = real_last;
        out_q.last = real_last;
        out_phase.last = real_last;
        out_freq.last = real_last;

        // TKEEP mask: -1 (all 1s) indicates all bytes in the payload are valid
        out_i.keep = -1; out_q.keep = -1; out_phase.keep = -1; out_freq.keep = -1;  // <--- NEW: Keep all bytes valid

        i_out.write(out_i);
        q_out.write(out_q);
        phase_out.write(out_phase);
        freq_out.write(out_freq);
    }

    // --- Update State Pointers ---
    if (!idle_mode) {
        bit_index++; // Move to the next bit in the 32-bit word
    }
    bit_cnt++;       // Global symbol counter advances regardless of idle state
}

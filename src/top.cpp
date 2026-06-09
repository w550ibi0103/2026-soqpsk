#include "top.h"

// --- Include standard I/O for debugging during C simulation ---
#ifndef __SYNTHESIS__
#include <iostream>
#endif

// Pre-calculated pulse shaping filter coefficients g(t) (generated via Python)
static const data_t g_coeff[G_LEN] = {
	#include "g_coeffs.inc"
};

void tfm_modulator(
	// The '&' indicates a C++ reference. In HLS, it maps to a physical hardware port rather than passing data by value
	hls::stream<bit_pkt> &bit_in,
	bool reset,
	hls::stream<sample_pkt> &i_out,
	hls::stream<sample_pkt> &q_out,
	int &debug_current_bit,  // 1 debug_current_bit 1 function call
	data_t &debug_alpha,  // 1 debug_alpha 1 function call
	hls::stream<data_t> &debug_pulse,
	hls::stream<data_t> &debug_phase,
	hls::stream<data_t> &debug_freq
)	{
	// Hardware interface pragmas for Vitis HLS (AXI-Lite for control, AXI-Stream for data)
	// Map data ports to AXI4-Stream
	#pragma HLS INTERFACE axis port=bit_in
	#pragma HLS INTERFACE axis port=i_out
	#pragma HLS INTERFACE axis port=q_out

	#pragma HLS INTERFACE ap_none port=debug_current_bit
	#pragma HLS INTERFACE ap_none port=debug_alpha
	#pragma HLS INTERFACE axis port=debug_pulse
	#pragma HLS INTERFACE axis port=debug_phase
	#pragma HLS INTERFACE axis port=debug_freq

	// Map control signals (reset, start, done, idle) to AXI4-Lite
	#pragma HLS INTERFACE s_axilite port=reset bundle=CTRL
	#pragma HLS INTERFACE s_axilite port=return bundle=CTRL

	// Internal state registers (Static variables map to Flip-Flops or BRAM)
	static bool idle_mode = true;
	static int last_delta = 0;  // I32
	static bool odd_flag = false;

	// Static array of size 3 to retain past states. The 3rd element is reserved for padding/redundancy
	static data_t t_prev[3] = {-1, 1, 0};  // Never reset
	static data_t shift_reg[G_LEN] = {0};  // Initialize G_LEN array (G_LEN=16*8=128)
	static data_t current_phase = 0;  // data_t is 24-bit word length, 8-bit integer part

	// --- Serializer (Parallel-to-Serial) State Registers ---
	static uint32_t current_word = 0;  // Holds the current 32-bit data chunk
	static int bit_index = 32;         // 32 means the buffer is empty and needs new data
	static bool is_burst_end = false;  // Flags if the current 32-bit word is the end of the DMA burst

	// Hardware reset logic
	if (reset) {
		idle_mode = true;
		last_delta = 0;
		odd_flag = false;
		current_phase = 0;
		bit_index = 32;        // Empty the buffer
		is_burst_end = false;  // Clear burst flag
		for(int i=0; i<G_LEN; i++) shift_reg[i] = 0;
		return;
	}

	int current_bit;  // Dummy data or extract the current bit

	// --- State Machine: Non-blocking Data Fetch & Serialization ---
	// If the 32-bit buffer is fully consumed (index reaches 32), fetch the next word
	// If the stream is empty, the IP enters idle mode and outputs dummy data to maintain phase continuity
	// bit_pkt is 32-bit
	if (bit_index >= 32) {
		bit_pkt in_val;
		if (bit_in.read_nb(in_val)) {
			// Data available in FIFO: Load the new 32-bit word
			current_word = in_val.data;
			is_burst_end = in_val.last;  // TRUE only on the final word of the DMA burst (e.g., word 1024)
			bit_index = 0;               // Reset bit pointer to LSB
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
		current_bit = (current_word >> bit_index) & 0x1;  // Right shift and mask from bit_index=0
	}
	debug_current_bit = current_bit;

	// --- Block 1 & 2: Differential Encoder ---
	int delta;
	if (!odd_flag) {
		delta = current_bit ^ (1 - last_delta);  // Bitwise XOR, Ek=ek^(-(ok-1))
	} else {
		delta = current_bit ^ last_delta;  // Ok+1= ok+1^Ek
	}

	// Always update the state for the next bit call
	last_delta = delta;

	// Convert binary to bipolar format (+1.0 or -1.0), data_t is defined in top.h as ap_fixed<24, 8>.
	// Out of 24 bits, 8 are for the integer part and 16 for the fractional part.
	// Type casting to data_t. Ternary operator syntax: (condition) ? (true_val) : (false_val)
	data_t t_now = (delta == 1) ? (data_t)1.0 : (data_t)-1.0;  // alpha_i=-1 when ith bit=0, alpha_i=+1 when ith bit=1

	// --- Block 3: SOQPSK Precoder ---
	// Calculate alpha based on current and previous symbol states, alpha_i=(-1)^(i+1)*alpha_i-1*(alpha_i-alpha_i-2)/2
	// 1. Calculate the difference
	data_t diff = t_now - t_prev[0];

	// 2. Multiply with previous bit
	data_t mult = t_prev[1] * diff;

	// 3. Divide by 2 using hardware arithmetic right-shift (costs 0 DSP resources!)
	data_t half_mult = mult >> 1;  // Right shift

	// 4. Apply sign based on even/odd bit count
	data_t alpha = (!odd_flag) ? (data_t)(-half_mult) : (data_t)(half_mult);
	debug_alpha = alpha;

	// Update history registers, init={-1, 1, 0}, update={1, t_now, 0}
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
	for (int s = 0; s < SPS; s++) {  // SPS=16
		#pragma HLS PIPELINE II=1

		// Upsampling: Insert impulse at the first sample, zero-stuff the rest
		data_t impulse = (s == 0) ? alpha : (data_t)0;
		debug_pulse.write(impulse);

		// Shift register for the FIR filter convolution
		// All elements shift right by one index, and the new impulse is placed at index 0
		// G_LEN=L*SPS=8*16=128
		for (int j = G_LEN - 1; j > 0; j--) {
			shift_reg[j] = shift_reg[j-1];  // shift_reg is G_LEN array (G_LEN=16*8=128)
		}
		shift_reg[0] = impulse;  // impulse inserts into shift_reg 16 times

		// FIR filter convolution (Multiply-Accumulate)
		data_t freq_dev = 0;  // Reset freq_dev to 0
		for (int j = 0; j < G_LEN; j++) {
			#pragma HLS UNROLL factor=16  // Unroll loop to utilize parallel DSP slices
			freq_dev += shift_reg[j] * g_coeff[j];  // FIR convolution
		}

		// Phase Integration (Accumulator), accumulate freq_dev to calculate the current_phase
		current_phase += freq_dev * (data_t)(3.1415926535 / (double)SPS);  // phase = 2*pi*h*integral(f(t)*dt, h=0.5

		// Phase Wrapping: Bound the phase between -PI and +PI
		if (current_phase > (data_t)3.1415926535) current_phase -= (data_t)6.283185307;
		else if (current_phase < (data_t)-3.1415926535) current_phase += (data_t)6.283185307;

		debug_phase.write(current_phase);
		debug_freq.write(freq_dev);

		// --- Output Formatting & TLAST Propagation ---
		// out_i.data expects raw bits, while hls::cos returns a fixed-point object.
		sample_pkt out_i, out_q;  // sample_pkt is 24-bit

		// Extract the raw 24 bits from the fixed-point result
		// 1. Force the output of hls::cos/sin to align with our 24-bit data_t format
		data_t cos_val = hls::cos(current_phase);
		data_t sin_val = hls::sin(current_phase);

		// 2. Safely extract the 24 raw bits into the packet payload
		out_i.data = cos_val.range(23, 0);
		out_q.data = sin_val.range(23, 0);

		// Assert output TLAST ONLY IF:
		// 1. IP is not in idle mode
		// 2. We are processing the final 32-bit word of the DMA burst
		// 3. We are on the final bit (bit 31) of that word
		// 4. We are generating the final sample (SPS - 1) of that bit
		bool real_last = (!idle_mode && is_burst_end && (bit_index == 31) && (s == SPS - 1));
		out_i.last = real_last;
		out_q.last = real_last;

		// TKEEP mask: -1 (all 1s) indicates all bytes in the payload are valid
		out_i.keep = -1; out_q.keep = -1;  // <--- NEW: Keep all bytes valid

		i_out.write(out_i);
		q_out.write(out_q);
	}

	// --- Update State Pointers ---
	if (!idle_mode) {
		bit_index++;  // Move to the next bit in the 32-bit word
	}
	odd_flag = !odd_flag;  // Global symbol counter advances regardless of idle state
}

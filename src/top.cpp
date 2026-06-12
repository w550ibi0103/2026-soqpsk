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
	hls::stream<sample_pkt> &q_out

	#ifdef HW_DEBUG_MODE
		, int &debug_current_bit  // 1 debug_current_bit 1 function call
		, data_t &debug_alpha  // 1 debug_alpha 1 function call
		, hls::stream<data_t> &debug_pulse
		, hls::stream<data_t> &debug_phase
		, hls::stream<data_t> &debug_freq
	#endif
)	{
	// Hardware interface pragmas for Vitis HLS (AXI-Lite for control, AXI-Stream for data)
	// Map data ports to AXI4-Stream
	#pragma HLS INTERFACE axis port=bit_in
	#pragma HLS INTERFACE axis port=i_out
	#pragma HLS INTERFACE axis port=q_out

	#ifdef HW_DEBUG_MODE
		#pragma HLS INTERFACE ap_none port=debug_current_bit
		#pragma HLS INTERFACE ap_none port=debug_alpha
		#pragma HLS INTERFACE axis port=debug_pulse
		#pragma HLS INTERFACE axis port=debug_phase
		#pragma HLS INTERFACE axis port=debug_freq
	#endif

	// Map control signals (start, done, idle, ready) to AXI4-Lite
	#pragma HLS INTERFACE s_axilite port=reset bundle=CTRL  // Mapping reset to AXI4-Lite
	#pragma HLS INTERFACE s_axilite port=return bundle=CTRL  // Mapping start, done, idle, ready to AXI4-Lite

	// =====================================================================
	// Internal state registers (Static variables map to Flip-Flops)
	// =====================================================================
	static int last_delta = 0;  // I32
	static bool odd_flag = false;

	// Static array of size 3 to retain past states. The 3rd element is reserved for padding/redundancy
	static data_t t_prev[3] = {-1, 1, 0};  // Never reset

	// Shift register for FIR filter — FULLY PARTITIONED into individual registers
	// This eliminates the BRAM bottleneck: all 128 values are accessible in a single cycle
	static data_t shift_reg[G_LEN] = {0};
	#pragma HLS ARRAY_PARTITION variable=shift_reg complete dim=1

	// Ensure g_coeff is also fully partitioned for parallel MAC access
	#pragma HLS ARRAY_PARTITION variable=g_coeff complete dim=1

	static data_t current_phase = 0;  // data_t is 16-bit length, 4-bit integer part

	// =====================================================================
	// Hardware reset logic
	// =====================================================================
	if (reset) {
		last_delta = 0;
		odd_flag = false;
		current_phase = 0;
		RESET_LOOP: for (int i = 0; i < G_LEN; i++) {
			#pragma HLS UNROLL
			shift_reg[i] = 0;
		}
		return;
	}

	// =====================================================================
	// Non-blocking byte read from AXI-Stream
	// =====================================================================
	// Each function call reads one byte and processes all 8 bits (128 IQ samples).
	// If FIFO is empty, the IP produces 128 dummy samples to maintain phase continuity.
	uint8_t current_byte = 0;
	bool is_burst_end = false;
	bool idle_mode;

	bit_pkt in_val;
	if (bit_in.read_nb(in_val)) {
		current_byte = in_val.data;
		is_burst_end = in_val.last;  // TRUE only on the final byte of the DMA burst
		idle_mode = false;
	} else {
		// FIFO underflow: process dummy data
		idle_mode = true;
	}

	// =====================================================================
	// Main processing loop: 8 bits × 16 SPS = 128 samples per function call
	// =====================================================================
	// Flattened loop replaces the old per-bit function call + inner SPS loop.
	// bit_idx = iter / 16 (which bit, 0-7), s = iter % 16 (which sample, 0-15)
	// Precoding is done once per bit (when s==0); FIR/phase/cos/sin done every sample.

	data_t alpha = 0;       // Holds current symbol's alpha value across 16 samples
	int current_bit = 0;    // Holds current bit value for debug output

	MAIN_LOOP: for (int iter = 0; iter < SPS * 8; iter++) {
		#pragma HLS PIPELINE II=1

		int bit_idx = iter >> 4;  // iter / 16, SPS=16=2^4, zero-cost in hardware
		int s = iter & 0xF;       // iter % 16, zero-cost in hardware

		// =============================================================
		// Per-bit processing: Differential Encoder + SOQPSK Precoder
		// Executes once every 16 iterations (at the first sample of each bit)
		// Loop-carried dependency distance = 16, easily met with II=1
		// =============================================================
		if (s == 0) {
			// Extract the current bit (LSB first) or use dummy data
			if (idle_mode) {
				current_bit = 0;  // Dummy data to maintain continuous RF carrier phase
			} else {
				current_bit = (current_byte >> bit_idx) & 0x1;
			}

			#ifdef HW_DEBUG_MODE
			debug_current_bit = current_bit;
			#endif

			// --- Block 1 & 2: Differential Encoder ---
			int delta;
			if (!odd_flag) {
				delta = current_bit ^ (1 - last_delta);  // Ek=ek^(-(ok-1))
			} else {
				delta = current_bit ^ last_delta;  // Ok+1= ok+1^Ek
			}

			// Update state for the next bit
			last_delta = delta;

			// Convert binary to bipolar format (+1.0 or -1.0)
			data_t t_now = (delta == 1) ? (data_t)1.0 : (data_t)-1.0;

			// --- Block 3: SOQPSK Precoder ---
			// alpha_i=(-1)^(i+1)*alpha_i-1*(alpha_i-alpha_i-2)/2
			data_t diff = t_now - t_prev[0];
			data_t mult = t_prev[1] * diff;
			data_t half_mult = mult >> 1;  // Right shift = divide by 2, costs 0 DSP
			alpha = (!odd_flag) ? (data_t)(-half_mult) : (data_t)(half_mult);

			#ifdef HW_DEBUG_MODE
			debug_alpha = alpha;
			#endif

			// Update history registers
			t_prev[0] = t_prev[1];
			t_prev[1] = t_now;

			// Toggle odd/even flag
			odd_flag = !odd_flag;

			// --- Debug print (Only active during C Simulation) ---
			#ifndef __SYNTHESIS__
				std::cout << "[IP Debug] Bit Index: " << bit_idx
						<< " | Current Bit: " << current_bit
						<< " | t_now: " << t_now
						<< " | Alpha: " << alpha.to_double()
						<< " | Idle Mode: " << (idle_mode ? "YES" : "NO")
						<< std::endl;
			#endif
		}

		// =============================================================
		// Per-sample processing: Upsampling, FIR, Phase, cos/sin
		// Executes every iteration (128 times per function call)
		// =============================================================

		// Upsampling: Insert impulse at the first sample, zero-stuff the rest
		data_t impulse = (s == 0) ? alpha : (data_t)0;

		#ifdef HW_DEBUG_MODE
		debug_pulse.write(impulse);
		#endif

		// Shift register for the FIR filter convolution
		// With ARRAY_PARTITION complete, all 128 shifts happen in parallel (1 cycle)
		for (int j = G_LEN - 1; j > 0; j--) {
			#pragma HLS UNROLL
			shift_reg[j] = shift_reg[j-1];
		}
		shift_reg[0] = impulse;

		// FIR filter convolution (Multiply-Accumulate)
		// With ARRAY_PARTITION complete, all 128 MACs execute in parallel
		// followed by an adder tree (log2(128) = 7 levels)
		data_t freq_dev = 0;
		for (int j = 0; j < G_LEN; j++) {
			#pragma HLS UNROLL
			freq_dev += shift_reg[j] * g_coeff[j];
		}

		// Phase Integration (Accumulator)
		current_phase += freq_dev * (data_t)(3.1415926535 / (double)SPS);  // phase = 2*pi*h*integral(f(t)*dt, h=0.5

		// Phase Wrapping: Bound the phase between -PI and +PI
		if (current_phase > (data_t)3.1415926535) current_phase -= (data_t)6.283185307;
		else if (current_phase < (data_t)-3.1415926535) current_phase += (data_t)6.283185307;

		#ifdef HW_DEBUG_MODE
		debug_phase.write(current_phase);
		debug_freq.write(freq_dev);
		#endif

		// --- Output Formatting & TLAST Propagation ---
		sample_pkt out_i, out_q;

		data_t cos_val = hls::cos(current_phase);
		data_t sin_val = hls::sin(current_phase);

		out_i.data = cos_val.range(15, 0);
		out_q.data = sin_val.range(15, 0);

		// Assert output TLAST ONLY IF:
		// 1. IP is not in idle mode
		// 2. We are processing the final byte of the DMA burst
		// 3. We are on the final bit (bit 7) of that byte
		// 4. We are generating the final sample (SPS - 1) of that bit
		bool real_last = (!idle_mode && is_burst_end && (bit_idx == 7) && (s == SPS - 1));
		out_i.last = real_last;
		out_q.last = real_last;

		// TKEEP mask: -1 (all 1s) indicates all bytes in the payload are valid
		out_i.keep = -1; out_q.keep = -1;

		i_out.write(out_i);
		q_out.write(out_q);
	}
}

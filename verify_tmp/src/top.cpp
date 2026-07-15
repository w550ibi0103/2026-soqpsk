#include "top.h"

// --- Include standard I/O for debugging during C simulation ---
#ifndef __SYNTHESIS__
#include <iostream>
#endif

// Pre-calculated pulse shaping filter coefficients g(t), one table per supported SPS
// (generated via scripts/gen_g_coeffs.ps1, same formula/parameters as the reference
// model in iGPS-PlutoSDR/tests/soqpsk-tg/soqpsk-tg.py). Tables for SPS < SPS_MAX have
// fewer than G_LEN_MAX taps; the C++ aggregate initializer zero-fills the remainder,
// so the unused high-index taps simply contribute 0 to the MAC below.
static const data_t g_coeff_sps16[G_LEN_MAX] = {
	#include "g_coeffs_sps16.inc"
};
static const data_t g_coeff_sps8[G_LEN_MAX] = {
	#include "g_coeffs_sps8.inc"
};
static const data_t g_coeff_sps4[G_LEN_MAX] = {
	#include "g_coeffs_sps4.inc"
};

// Phase increment scale = pi / active_sps, indexed by sps_sel (0->16, 1->8, 2->4).
// sps_sel==3 is reserved (SPS=2 removed) and is clamped to index 0 before use.
static const data_t PHASE_SCALE[3] = {
	(data_t)(3.1415926535 / 16.0),
	(data_t)(3.1415926535 / 8.0),
	(data_t)(3.1415926535 / 4.0)
};

void tfm_modulator(
	// The '&' indicates a C++ reference. In HLS, it maps to a physical hardware port rather than passing data by value
	hls::stream<bit_pkt> &bit_in,  // 8-bits
	hls::stream<sample_pkt> &i_out,  // 16-bits
	hls::stream<sample_pkt> &q_out  // 16-bits

	#ifdef HW_DEBUG_MODE
		, int &debug_current_bit  // 1 debug_current_bit 1 function call
		, data_t &debug_alpha  // 1 debug_alpha 1 function call
		, hls::stream<data_t> &debug_pulse  // 16-bit word length, 4-bit integer part
		, hls::stream<data_t> &debug_phase  // 16-bit word length, 4-bit integer part
		, hls::stream<data_t> &debug_freq  // 16-bit word length, 4-bit integer part
	#endif
)	{
	// Hardware interface pragmas for Vitis HLS (AXI-Lite for sps_sel, AXI-Stream for data)
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

	// Free-running data stream IP: no ap_start/ap_done/ap_idle handshake, the loop
	// below runs forever from the moment ap_rst_n deasserts.
	#pragma HLS INTERFACE ap_ctrl_none port=return

	// COSIM-VERIFICATION-ONLY: sps_sel pinned to a compile-time constant instead of
	// the real design's runtime s_axilite register (see top.h comment).
	const ap_uint<2> sps_sel = VERIFY_FIXED_SPS_SEL;

	// =====================================================================
	// Internal state registers (Static variables map to Flip-Flops)
	// Reset by the hardware ap_rst_n pin (HLS default reset behavior) instead of
	// a software-writable register.
	// =====================================================================
	static int last_delta = 0;  // I32
	static bool odd_flag = false;

	// Static array of size 3 to retain past states. The 3rd element is reserved for padding/redundancy
	static data_t t_prev[3] = {-1, 1, 0};  // Never reset

	// Shift register for FIR filter, sized for the largest supported SPS.
	// Fully partitioned into individual registers: all 128 values accessible in a single cycle.
	static data_t shift_reg[G_LEN_MAX] = {0};
	#pragma HLS ARRAY_PARTITION variable=shift_reg complete dim=1

	// Ensure each g_coeff table is fully partitioned for parallel MAC access
	#pragma HLS ARRAY_PARTITION variable=g_coeff_sps16 complete dim=1
	#pragma HLS ARRAY_PARTITION variable=g_coeff_sps8  complete dim=1
	#pragma HLS ARRAY_PARTITION variable=g_coeff_sps4  complete dim=1

	static data_t current_phase = 0;  // data_t is 16-bit length, 4-bit integer part

	// =====================================================================
	// Byte/sample-boundary state (see README "迴圈攤平" note, 2026-07-09).
	// These used to be plain locals living inside BYTE_LOOP's per-byte scope,
	// implicitly held constant across MAIN_LOOP's inner iterations by C++ block
	// scoping. Now that BYTE_LOOP and MAIN_LOOP are merged into one flat,
	// always-pipelined loop, there is no enclosing scope to rely on any more:
	// each of these must be `static` to survive from one loop pass to the next.
	// =====================================================================
	static int iter_in_byte = 0;         // sample index within the current byte (0 .. active_sps*8-1)
	static int shift_amt = 4;            // log2(active_sps), latched once per byte
	static int active_sps = 16;          // 1 << shift_amt, latched once per byte
	static ap_uint<2> phase_idx = 0;     // PHASE_SCALE index, latched once per byte (sps_sel==3 clamped to 0)
	static uint8_t current_byte = 0;     // latched once per byte
	static bool is_burst_end = false;    // latched once per byte
	static bool idle_mode = true;        // latched once per byte
	static data_t alpha = 0;             // holds current symbol's alpha value across active_sps samples
	static int current_bit = 0;          // holds current bit value for debug output

	// C-sim-only counter to break the otherwise-infinite BYTE_LOOP below (see CSIM_MAX_ITERS).
	#ifndef __SYNTHESIS__
	static int csim_iter_count = 0;
	#endif

	// =====================================================================
	// Free-running sample loop: in hardware this never exits (true dataflow IP).
	// One flat, always-pipelined (II=1) loop replaces the old BYTE_LOOP/MAIN_LOOP
	// nesting; iter_in_byte tracks position within the current byte in place of
	// the old inner `for` loop's induction variable, since active_sps*8 (the old
	// inner loop bound) is a runtime value and can't be flattened automatically.
	// =====================================================================
	BYTE_LOOP: while (1) {
		#pragma HLS PIPELINE II=1

		// =============================================================
		// Byte boundary: decode the runtime SPS selection and do the
		// non-blocking byte read, once per byte instead of once per sample.
		// =============================================================
		if (iter_in_byte == 0) {
			// Decode the runtime SPS selection (see top.h for encoding).
			switch (sps_sel) {
				case 1:  shift_amt = 3; phase_idx = 1; break;  // SPS=8
				case 2:  shift_amt = 2; phase_idx = 2; break;  // SPS=4
				default: shift_amt = 4; phase_idx = 0; break;  // SPS=16 (also covers reserved sps_sel==3)
			}
			active_sps = 1 << shift_amt;

			// Non-blocking byte read from AXI-Stream. If the FIFO is empty, the
			// IP produces dummy samples this byte to maintain phase continuity.
			bit_pkt in_val;
			if (bit_in.read_nb(in_val)) {
				current_byte = in_val.data;
				is_burst_end = in_val.last;  // TRUE only on the final byte of the DMA burst
				idle_mode = false;
			} else {
				idle_mode = true;
			}
		}

		// bit_idx = iter_in_byte / active_sps (which bit, 0-7)
		// s = iter_in_byte % active_sps (which sample within that bit)
		int bit_idx = iter_in_byte >> shift_amt;  // zero-cost in hardware (active_sps is a power of 2)
		int s = iter_in_byte & ((1 << shift_amt) - 1);

		// =============================================================
		// Per-bit processing: Differential Encoder + SOQPSK Precoder
		// Executes once every active_sps iterations (at the first sample of each bit)
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
		// Executes every iteration (active_sps*8 times per byte)
		// =============================================================

		// Upsampling: Insert impulse at the first sample, zero-stuff the rest
		data_t impulse = (s == 0) ? alpha : (data_t)0;

		#ifdef HW_DEBUG_MODE
		debug_pulse.write(impulse);
		#endif

		// Shift register for the FIR filter convolution
		// With ARRAY_PARTITION complete, all 128 shifts happen in parallel (1 cycle)
		for (int j = G_LEN_MAX - 1; j > 0; j--) {
			#pragma HLS UNROLL
			shift_reg[j] = shift_reg[j-1];
		}
		shift_reg[0] = impulse;

		// FIR filter convolution (Multiply-Accumulate) using the coefficient table
		// selected by sps_sel. Taps beyond the active table's real length are zero
		// (see the aggregate-init comment above), so they contribute nothing here.
		// With ARRAY_PARTITION complete, all 128 MACs execute in parallel
		// followed by an adder tree (log2(128) = 7 levels)
		data_t freq_dev = 0;
		for (int j = 0; j < G_LEN_MAX; j++) {
			#pragma HLS UNROLL
			data_t coeff;
			switch (sps_sel) {
				case 1:  coeff = g_coeff_sps8[j];  break;
				case 2:  coeff = g_coeff_sps4[j];  break;
				default: coeff = g_coeff_sps16[j]; break;  // also covers reserved sps_sel==3
			}
			freq_dev += shift_reg[j] * coeff;
		}

		// Phase Integration (Accumulator)
		current_phase += freq_dev * PHASE_SCALE[phase_idx];  // phase = 2*pi*h*integral(f(t)*dt, h=0.5

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
		// 4. We are generating the final sample (active_sps - 1) of that bit
		bool real_last = (!idle_mode && is_burst_end && (bit_idx == 7) && (s == active_sps - 1));
		out_i.last = real_last;
		out_q.last = real_last;

		// TKEEP mask: -1 (all 1s) indicates all bytes in the payload are valid
		out_i.keep = -1; out_q.keep = -1;

		i_out.write(out_i);
		q_out.write(out_q);

		// =============================================================
		// Advance to the next sample, wrapping back to a new byte once
		// active_sps*8 samples have been emitted for this one.
		// =============================================================
		if (iter_in_byte == active_sps * 8 - 1) {
			iter_in_byte = 0;

			// C simulation cannot execute a truly infinite while(1); break once
			// the testbench's queued bytes are processed, at a byte boundary so
			// no byte is cut short. This block does not exist in synthesized
			// hardware, where BYTE_LOOP runs forever.
			#ifndef __SYNTHESIS__
			if (++csim_iter_count >= CSIM_MAX_ITERS) break;
			#endif
		} else {
			iter_in_byte++;
		}
	}
}

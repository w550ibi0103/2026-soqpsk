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

// Sin/Cos lookup tables (see top.h and gen_sincos_lut.ps1). Entry i covers
// phase = -pi + i*(2*pi/LUT_SIZE), matching current_phase's wrap range below.
static const data_t SIN_LUT[LUT_SIZE] = {
	#include "sin_lut.inc"
};
static const data_t COS_LUT[LUT_SIZE] = {
	#include "cos_lut.inc"
};

// Converts a phase in [-pi, pi) into a LUT table position: LUT_POS_SCALE * (phase + pi).
// Its integer part is the table index, its fractional part is the interpolation
// weight to the next entry -- one multiply gets both without a separate divide.
static const phase_pos_t LUT_POS_SCALE = (phase_pos_t)(LUT_SIZE / (2.0 * 3.1415926535));

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
		, hls::stream<data_t> &debug_alpha_stream  // DIAGNOSTIC-ONLY, see top.h
		, hls::stream<ap_uint<1> > &debug_idle_stream  // DIAGNOSTIC-ONLY, see top.h
		, hls::stream<ap_uint<8> > &debug_current_bit_stream  // DIAGNOSTIC-ONLY, see top.h
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
		#pragma HLS INTERFACE axis port=debug_alpha_stream
		#pragma HLS INTERFACE axis port=debug_idle_stream
		#pragma HLS INTERFACE axis port=debug_current_bit_stream
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

	// --- SOQPSK Precoder history (see "Block 3" below) ---------------------
	// Replaced 2026-07-16 with a delta-bit history + compare/select table.
	// Original bipolar (+/-1) history array, kept for reference:
	// Static array of size 3 to retain past states. The 3rd element is reserved for padding/redundancy
	// static data_t t_prev[3] = {-1, 1, 0};  // Never reset
	//
	// delta_prev1/delta_prev2 hold the precoder's own delta[n-1]/delta[n-2]
	// bit history (0/1), replacing t_prev[1]/t_prev[0]'s bipolar values.
	// Seeded to (1, 0) to reproduce t_prev's original (+1, -1) seed exactly
	// -- note this is intentionally NOT the same seed as last_delta's initial
	// 0 above: t_prev bootstraps the precoder's own recursion, independent of
	// the differential encoder's history, and always has been.
	static ap_uint<1> delta_prev1 = 1;  // Never reset (matches old t_prev[1] == +1)
	static ap_uint<1> delta_prev2 = 0;  // Never reset (matches old t_prev[0] == -1)

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

	// DIAGNOSTIC-ONLY (verify_tmp experiment, not yet in src/top.cpp -- see
	// Note.md section 16/17): true until the very first real byte has been
	// read. While true, BYTE_LOOP retries bit_in.read_nb() every single
	// cycle (instead of only once per active_sps*8-cycle byte period) and
	// keeps iter_in_byte pinned at 0, so a first-cycle-after-reset race with
	// bit_in's AXI4-Stream register-slice settling (regslice_both is held/
	// cleared for as long as ap_rst_n is low, so it needs at least one real
	// post-release clock edge to reflect data that was already stable on its
	// input) can't cause byte0 to be misread as idle and permanently offset
	// all subsequent bytes by one whole byte. i_out/q_out still get written
	// every cycle throughout (idle carrier, alpha=0) -- this only delays
	// when the per-byte bookkeeping truly starts, it never withholds output.
	static bool cold_start = true;

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
				cold_start = false;  // the very first real byte has now been captured
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
		// Executes once every active_sps iterations (at the first sample of each bit).
		// Suppressed while still cold_start-retrying (s is pinned to 0 every
		// cycle then, and we don't want the differential encoder/precoder
		// state machine to churn once per retry attempt) -- it runs for the
		// first time on the same cycle cold_start becomes false.
		// =============================================================
		if (s == 0 && !cold_start) {
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

			// --- Block 3: SOQPSK Precoder (delta-history compare/select) ---
			// alpha_i=(-1)^(i+1)*alpha_i-1*(alpha_i-alpha_i-2)/2
			//
			// Replaced 2026-07-16: the formula above only ever multiplies
			// bipolar (+/-1) values, so it was really picking one of just
			// three outcomes {-1,0,+1}. csynth.rpt confirmed this cost a
			// real 16x16 DSP multiplier (mul_16s_16s_28_1_1_U2, ln185) to
			// make that pick. Original bipolar version, kept for reference:
			// data_t t_now = (delta == 1) ? (data_t)1.0 : (data_t)-1.0;
			// data_t diff = t_now - t_prev[0];
			// data_t mult = t_prev[1] * diff;
			// data_t half_mult = mult >> 1;  // Right shift = divide by 2, costs 0 DSP
			// alpha = (!odd_flag) ? (data_t)(-half_mult) : (data_t)(half_mult);
			//
			// Since t_now/t_prev are just bipolar(delta), alpha depends only
			// on 3 delta bits (current, n-1, n-2) and odd_flag -- a pure
			// boolean function of a 16-combination space. Derivation:
			//   half_mult = 0             if delta[n] == delta[n-2]
			//             = +1            if delta[n] == delta[n-1] (and != delta[n-2])
			//             = -1            otherwise (delta[n-1] == delta[n-2] != delta[n])
			bool eq_prev2 = (delta == (int)delta_prev2);  // delta[n] == delta[n-2] ?
			bool eq_prev1 = (delta == (int)delta_prev1);  // delta[n] == delta[n-1] ?
			int half_mult = eq_prev2 ? 0 : (eq_prev1 ? 1 : -1);
			alpha = (!odd_flag) ? (data_t)(-half_mult) : (data_t)(half_mult);

			#ifdef HW_DEBUG_MODE
			debug_alpha = alpha;
			debug_alpha_stream.write(alpha);  // DIAGNOSTIC-ONLY: one beat per bit, see top.h
			debug_idle_stream.write((ap_uint<1>)(idle_mode ? 1 : 0));  // DIAGNOSTIC-ONLY
			debug_current_bit_stream.write((ap_uint<8>)current_bit);  // DIAGNOSTIC-ONLY
			#endif

			// Update precoder history registers (replaces old t_prev[] shift)
			delta_prev2 = delta_prev1;
			delta_prev1 = (ap_uint<1>)delta;

			// Toggle odd/even flag
			odd_flag = !odd_flag;

			// --- Debug print (Only active during C Simulation) ---
			#ifndef __SYNTHESIS__
				std::cout << "[IP Debug] Bit Index: " << bit_idx
						<< " | Current Bit: " << current_bit
						<< " | Delta: " << delta
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

		// FIR filter convolution (Multiply-Select-Accumulate) using the coefficient
		// table selected by sps_sel. Taps beyond the active table's real length are
		// zero (see the aggregate-init comment above), so they contribute nothing here.
		//
		// shift_reg[] only ever holds impulse-train values in {-1, 0, +1}: it is
		// loaded exclusively from `impulse` above, which is either `alpha` (proven
		// in Block 3 to be one of {-1,0,+1}) or 0 from zero-stuffing. With one
		// operand always restricted to those 3 values, shift_reg[j] * coeff[j] is
		// really a 3-way select (0 / +coeff / -coeff), not a general multiply --
		// so it's replaced below with a compare/select instead of `*`. This holds
		// for every supported SPS (16/8/4): a smaller SPS only means more of the
		// 128 coefficient taps are pre-zeroed, and the select still produces 0 for
		// those taps exactly as the multiply did, so no SPS-specific handling is
		// needed here. Measured effect: removes ~58 of the design's 109 DSP48s
		// (hls_prj/solution1/syn/report/csynth.rpt, the ln245 MAC entries), since
		// Vivado no longer needs a real multiplier per tap -- just a small mux
		// feeding the same adder tree.
		// With ARRAY_PARTITION complete, all 128 selects execute in parallel
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
			data_t contribution;
			if (shift_reg[j] == (data_t)0)     contribution = (data_t)0;
			else if (shift_reg[j] > (data_t)0) contribution = coeff;
			else                                contribution = -coeff;
			freq_dev += contribution;
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

		// Sin/Cos via 256-entry linear-interpolated LUT (replaces hls::cos/hls::sin,
		// see top.h comment). lut_idx1 = lut_idx0+1 wraps 255->0 for free via 8-bit
		// unsigned overflow, which is correct: entry 0 (phase=-pi) and the implicit
		// entry LUT_SIZE (phase=+pi) are the same point on the unit circle.
		phase_pos_t phase_pos = ((phase_pos_t)current_phase + (phase_pos_t)3.1415926535) * LUT_POS_SCALE;
		ap_uint<8> lut_idx0 = (ap_uint<8>)phase_pos;
		ap_uint<8> lut_idx1 = lut_idx0 + 1;
		data_t lut_frac = (data_t)(phase_pos - (phase_pos_t)lut_idx0);

		data_t cos_val = COS_LUT[lut_idx0] + (data_t)(lut_frac * (COS_LUT[lut_idx1] - COS_LUT[lut_idx0]));
		data_t sin_val = SIN_LUT[lut_idx0] + (data_t)(lut_frac * (SIN_LUT[lut_idx1] - SIN_LUT[lut_idx0]));

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
		if (cold_start) {
			// Still waiting for the first real byte: keep iter_in_byte
			// pinned at 0 so next cycle retries bit_in.read_nb() again.
			// Deliberately does NOT touch csim_iter_count/break below --
			// a cold_start retry is not a completed byte.
			iter_in_byte = 0;
		} else if (iter_in_byte == active_sps * 8 - 1) {
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

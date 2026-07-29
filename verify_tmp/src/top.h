// Include guard to prevent multiple inclusions
#ifndef __TOP_H__
#define __TOP_H__

//  --- *********************************** Important *********************************** ---
//  --- Only for debugging during C simulation, comment out this line if release IP ---
// DIAGNOSTIC-ONLY, re-enabled temporarily: debug_current_bit/debug_alpha/
// debug_pulse/debug_phase/debug_freq trip Vitis HLS cosim's ap_ctrl_none
// restriction (same as sps_sel would), but that's not a problem here since
// this build is only used for csim_design + a hand-rolled XSIM testbench,
// never cosim_design. Needed to capture debug_phase for the divergence
// investigation. Must revert to commented-out before using this project for
// its normal cosim-verification purpose again.
#define HW_DEBUG_MODE
//  --- *********************************** Important *********************************** ---

// COSIM-VERIFICATION-ONLY: sps_sel pinned to a synthesis-time constant (see
// tfm_modulator's signature below) so this build has only AXI4-Stream top-level I/O,
// satisfying Vitis HLS cosim's ap_ctrl_none restriction. The real hls_prj/ design keeps
// sps_sel as a runtime-writable s_axilite register; that version is not cosim-able in
// Vitis HLS 2023.2 because ap_ctrl_none (free-running) designs have no ap_start to
// anchor an AXI4-Lite register write against. This constant only exists to let cosim
// confirm the RTL pipeline matches the C model; it is not a change to the real IP.
#define VERIFY_FIXED_SPS_SEL 1

// Vitis HLS libraries for fixed-point arithmetic and math functions
#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_stream.h>   // Required for hls::stream interface
#include <ap_axi_sdata.h> // Required for AXI-Stream packet structures (ap_axiu)

// Fixed-point type definition: 24-bit word length, 8-bit integer part
// Range is -128 to +127.99..., resolution is 2^-16 (~0.000015)
// Fixed-point type definition: 16-bit word length, 4-bit integer part
// Range is -8 to +7.99..., resolution is 2^-12 (~0.000244)
typedef ap_fixed<16, 4> data_t;

// --- Sin/Cos lookup table (replaces hls::sin/hls::cos, see Note.md "CORDIC
// 發散問題調查"): hls::sin/hls::cos (hls_math.h, CORDIC-based) was confirmed
// bit-exact into current_phase but produced a growing, non-deterministic
// RTL-vs-C drift out of sin/cos itself under this design's free-running
// (ap_ctrl_none) + fully-pipelined (II=1) configuration. A LUT is ordinary
// combinational/ROM logic with no hidden pipeline state, so it can't exhibit
// that failure mode; its own error is instead a small, bounded, and known
// quantity from table quantization (see gen_sincos_lut.ps1 for the sizing).
#define LUT_SIZE 256  // entries per table; linear-interpolated between them
// Holds a table position: integer part 0..LUT_SIZE-1 selects the LUT entry,
// fractional part is the interpolation weight to the next entry.
typedef ap_fixed<24, 10> phase_pos_t;

// SOQPSK-TG parameters
#define L 8         // Because the energy of one bit needs to last for L=8 cycles
#define SPS_MAX 16  // Largest supported upsampling factor (samples per symbol)
#define G_LEN_MAX (L * SPS_MAX)  // Max length of w(t)/g(t)/shift register (128 taps)

// Dynamic SPS selection (see README item 9/11): sps_sel picks which precomputed
// g_coeffs table and upsampling factor is active.
//   sps_sel = 0 -> SPS=16, 1 -> SPS=8, 2 -> SPS=4
//   sps_sel = 3 is reserved (SPS=2 was removed 2026-07-09: not enough oversampling
//   margin for symbol timing recovery) and falls back to SPS=16, same as sps_sel=0.
// Only change sps_sel while ap_rst_n is asserted; switching mid-stream is not
// glitch-free (shift_reg/current_phase are not re-aligned to the new rate).

// Number of BYTE_LOOP iterations the free-running loop runs in C simulation before
// breaking out (ignored during synthesis, where the loop is truly infinite).
// Must match tb_top.cpp's NUM_BYTES.
#define CSIM_MAX_ITERS 8

// --- AXI-Stream packet type definitions ---
// bit_pkt: 8-bit AXI-Stream packet used to receive a 1-bit payload
// ap_axiu<Data Width, User Width, ID Width, Destination Width>
typedef ap_axiu<8, 0, 0, 0> bit_pkt;

// sample_pkt: 16-bit AXI-Stream packet used to output I/Q samples
// ap_axiu<Data Width, User Width, ID Width, Destination Width>
typedef ap_axiu<16, 0, 0, 0> sample_pkt;

// Top-level function declaration
void tfm_modulator(
	hls::stream<bit_pkt> &bit_in,
	hls::stream<sample_pkt> &i_out,
	hls::stream<sample_pkt> &q_out
	#ifdef HW_DEBUG_MODE
		, int &debug_current_bit
		, data_t &debug_alpha
		, hls::stream<data_t> &debug_pulse
		, hls::stream<data_t> &debug_phase
		, hls::stream<data_t> &debug_freq
		// DIAGNOSTIC-ONLY (temporary, verify_tmp only): a proper named debug
		// stream for alpha, written once per bit right where alpha is
		// computed. Added because reverse-engineering which auto-generated
		// RTL signal holds alpha (via debug_alpha, which is a dangling
		// ap_none port, or via guessing hierarchical-reference names) proved
		// unreliable after several wrong guesses -- this sidesteps that by
		// giving the value an unambiguous, self-defined output.
		, hls::stream<data_t> &debug_alpha_stream
		// DIAGNOSTIC-ONLY (temporary, verify_tmp only): same rationale as
		// debug_alpha_stream -- written once per bit, right where idle_mode
		// and current_bit are decided, so the byte0-vs-idle timing question
		// can be checked directly instead of inferred from alpha's shape.
		, hls::stream<ap_uint<1> > &debug_idle_stream
		, hls::stream<ap_uint<8> > &debug_current_bit_stream
	#endif
);

#endif

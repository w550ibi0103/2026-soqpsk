// Include guard to prevent multiple inclusions
#ifndef __TOP_H__
#define __TOP_H__

//  --- *********************************** Important *********************************** ---
//  --- Only for debugging during C simulation, comment out this line if release IP ---
#define HW_DEBUG_MODE
//  --- *********************************** Important *********************************** ---

// Vitis HLS libraries for fixed-point arithmetic and math functions
#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_math.h>
#include <hls_stream.h>   // Required for hls::stream interface
#include <ap_axi_sdata.h> // Required for AXI-Stream packet structures (ap_axiu)

// Fixed-point type definition: 24-bit word length, 8-bit integer part
// Range is -128 to +127.99..., resolution is 2^-16 (~0.000015)
// Fixed-point type definition: 16-bit word length, 4-bit integer part
// Range is -8 to +7.99..., resolution is 2^-12 (~0.000244)
typedef ap_fixed<16, 4> data_t;

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
	ap_uint<2> sps_sel,
	hls::stream<sample_pkt> &i_out,
	hls::stream<sample_pkt> &q_out
	#ifdef HW_DEBUG_MODE
		, int &debug_current_bit
		, data_t &debug_alpha
		, hls::stream<data_t> &debug_pulse
		, hls::stream<data_t> &debug_phase
		, hls::stream<data_t> &debug_freq
	#endif
);

#endif

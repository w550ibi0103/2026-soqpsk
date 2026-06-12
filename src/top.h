// Include guard to prevent multiple inclusions
#ifndef __TOP_H__
#define __TOP_H__

//  --- *********************************** Important *********************************** ---
//  --- Only for debugging during C simulation, comment out this line if release IP ---
#define HW_DEBUG_MODE
//  --- *********************************** Important *********************************** ---

// Vitis HLS libraries for fixed-point arithmetic and math functions
#include <ap_fixed.h>
#include <hls_math.h>
#include <hls_stream.h>   // Required for hls::stream interface
#include <ap_axi_sdata.h> // Required for AXI-Stream packet structures (ap_axiu)

// Fixed-point type definition: 24-bit word length, 8-bit integer part
// Range is -128 to +127.99..., resolution is 2^-16 (~0.000015)
// Fixed-point type definition: 16-bit word length, 4-bit integer part
// Range is -8 to +7.99..., resolution is 2^-12 (~0.000244)
typedef ap_fixed<16, 4> data_t;

// SOQPSK-TG parameters
#define SPS 16  // Upsampling, 16 sample per symbol, each cycle has 16 SPS points
#define L 8  // Because the energy of one bit needs to last for L=8 cycles
#define G_LEN (L * SPS)  // length of w(t) and g(t)

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
	bool reset,
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

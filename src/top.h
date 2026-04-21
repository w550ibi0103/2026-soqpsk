// Include guard to prevent multiple inclusions
#ifndef __TOP_H__
#define __TOP_H__

// Vitis HLS libraries for fixed-point arithmetic and math functions
#include <ap_fixed.h>
#include <hls_math.h>
#include <hls_stream.h>   // Required for hls::stream interface
#include <ap_axi_sdata.h> // Required for AXI-Stream packet structures (ap_axiu)

// Fixed-point type definition: 24-bit word length, 8-bit integer part.
// Range is -128 to +127.99..., resolution is 2^-16 (~0.000015).
typedef ap_fixed<24, 8> data_t;

// SOQPSK-TG parameters
#define SPS 16
#define L 8
#define G_LEN (L * SPS)

// --- AXI-Stream packet type definitions ---
// bit_pkt: 32-bit AXI-Stream packet used to receive a 1-bit payload
typedef ap_axiu<32, 0, 0, 0> bit_pkt;

// sample_pkt: 24-bit AXI-Stream packet used to output I/Q samples
typedef ap_axiu<24, 0, 0, 0> sample_pkt;

// Top-level function declaration
void tfm_modulator(
    hls::stream<bit_pkt> &bit_in,
    bool reset,
    hls::stream<sample_pkt> &i_out,
    hls::stream<sample_pkt> &q_out,
	hls::stream<sample_pkt> &phase_out,
	hls::stream<sample_pkt> &freq_out
);

#endif

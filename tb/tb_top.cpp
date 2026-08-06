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
	#ifdef HW_DEBUG_MODE
		int debug_current_bit;
		data_t debug_alpha;
		hls::stream<data_t> debug_pulse;
		hls::stream<data_t> debug_phase;
		hls::stream<data_t> debug_freq;
	#endif

	// --------------------------------------------------------
	// 2. Prepare test vectors (Simulating DMA behavior)
	// --------------------------------------------------------
	// In Python: [1,1,1,0,0,1,1,0,0,1,0,0,1,1,1,0,0,0,1,0,1,1,1,0,0,1,1,1,1,0,1,1], index 0 first
	// We will send 8 bytes (64 bits total) to the IP
	// 0x67 = 0b'01100111
	// 0x72 = 0b'01110010
	// 0x74 = 0b'01110100
	// 0xDE = 0b'11011110
	// 0x67 = 0b'01100111
	// 0x72 = 0b'01110010
	// 0x74 = 0b'01110100
	// 0xDE = 0b'11011110
	// IP processes LSB first
	const int NUM_BYTES = 8;
	uint32_t test_data[NUM_BYTES] = {
		0x67,  // Ends with a few 1s, mostly 0s
		0x72,
		0x74,
		0xDE,
		0x67,
		0x72,
		0x74,
		0xDE
	};

	std::cout << ">> Starting SOQPSK-TG IP Simulation..." << std::endl;

	// Push data into the input stream (Simulating DMA writing to FIFO)
	for (int i = 0; i < NUM_BYTES; i++) {
		bit_pkt pkt;
		pkt.data = test_data[i];
		// Assert TLAST on the final word of the burst
		pkt.last = (i == NUM_BYTES - 1);
		pkt.keep = -1;
		bit_in.write(pkt);
	}

	// --------------------------------------------------------
	// 3. Run the IP
	// --------------------------------------------------------
	// tfm_modulator is now ap_ctrl_none / free-running: it is called ONCE and its
	// internal BYTE_LOOP keeps consuming bytes until CSIM_MAX_ITERS is reached
	// (that constant only exists in C simulation; in hardware the loop is infinite).
	// No explicit reset call is needed: the static state's C++ initializers already
	// give the correct power-on values, matching what ap_rst_n would do in hardware.
	// TEST_SPS_SEL can be overridden at compile time (-DTEST_SPS_SEL=N) to csim the
	// other sps_sel branch; defaults to 0 (SPS=8, the ap_rst_n reset default).
	// Only 0/1 are valid (2/3 are reserved/unused, both fall back to SPS=16 same
	// as sps_sel==1 -- see top.h; SPS=4 removed 2026-08-06).
	#ifndef TEST_SPS_SEL
	#define TEST_SPS_SEL 0
	#endif
	#if TEST_SPS_SEL > 1
	#error "TEST_SPS_SEL must be 0 or 1 (sps_sel==2/3 are reserved/unused, both fall back to SPS=16 same as sps_sel==1)"
	#endif
	const ap_uint<2> sps_sel = TEST_SPS_SEL;
	const int SPS_TABLE[2] = {8, 16};
	const int SPS = SPS_TABLE[TEST_SPS_SEL];  // used only to size the debug drain below

	tfm_modulator(bit_in, sps_sel, i_out, q_out
		#ifdef HW_DEBUG_MODE
			, debug_current_bit, debug_alpha, debug_pulse, debug_phase, debug_freq
		#endif
	);

	#ifdef HW_DEBUG_MODE
		// The single call above produced NUM_BYTES * SPS * 8 debug samples in total.
		for (int j = 0; j < NUM_BYTES * SPS * 8; j++) {
			data_t db_pulse = debug_pulse.read();
			data_t db_phase = debug_phase.read();
			data_t db_freq = debug_freq.read();
		}
	#endif

	// --------------------------------------------------------
	// 4. Read outputs and save to CSV (for Python plotting)
	// --------------------------------------------------------
	std::ofstream outfile("output_waveform.csv");
	outfile << "Sample,I_Data,Q_Data,TLAST" << std::endl;

	int sample_idx = 0;
	bool pass = true;

	// Read until the output streams are empty
	while (!i_out.empty() && !q_out.empty()) {
		sample_pkt i_pkt = i_out.read();
		sample_pkt q_pkt = q_out.read();

		// Convert the raw 16-bit integer back to floating point for verification
		// Reinterpret the raw bits as our ap_fixed<16,1,AP_RND,AP_SAT> dac_q15_t
		// (Q1.15 -- matches tx_adrv9009_tpl_core's DAC sample format, see Note.md)
		dac_q15_t i_val; i_val.range(15,0) = i_pkt.data;
		dac_q15_t q_val; q_val.range(15,0) = q_pkt.data;

		outfile << sample_idx << ","
				<< i_val.to_double() << ","
				<< q_val.to_double() << ","
				<< i_pkt.last << std::endl;

		// Simple check: Output should not exceed unit circle significantly.
		// Bound tightened to dac_q15_t's actual representable range (Q1.15,
		// saturates at [-1.0, +0.999969], see top.h) -- the old +-1.2 bound
		// was sized for data_t's wider Q4.12 range and could never fire once
		// the output type moved to dac_q15_t (Note.md section 49).
		if (i_val.to_double() > 1.00003 || i_val.to_double() < -1.00003) {
			pass = false;
		}

		sample_idx++;
	}

	outfile.close();

	// --------------------------------------------------------
	// 5. Print Simulation Result
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

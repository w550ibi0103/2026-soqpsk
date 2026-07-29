// -----------------------------------------------------------------------
// LUT-based sin/cos verification: same stimulus/interface as
// tb_xsim_verify_sps8.sv (verify_tmp/ RTL, sps_sel pinned to a
// compile-time constant = 1/SPS=8, no s_axi_CTRL bundle), rerun against
// the RTL synthesized from the 256-entry LUT sin/cos (replaces
// hls::cos/hls::sin -- see Note.md "CORDIC 發散問題調查" and
// src/top.cpp). Drops the tb_xsim_verify_sps8.sv internal
// `ap_sig_allocacmp_in` current_phase probe: that hierarchical signal
// name is HLS-line-number-derived and not guaranteed stable across a
// source change, and isn't needed here -- this check only needs
// i_out/q_out vs. a freshly-generated LUT-based C-model golden CSV
// (NOT xsim_verify/golden_sps8_output_waveform.csv, which is the OLD
// CORDIC-based golden and is expected to differ at the ~0.3 LSB LUT
// quantization level, not a bug).
// -----------------------------------------------------------------------
`timescale 1ns/1ps

module tb_xsim_verify_sps8_lut;

    localparam CLK_PERIOD = 6.25;
    localparam integer NUM_BYTES = 8;
    localparam integer RUN_CYCLES = 900;

    reg ap_clk = 0;
    reg ap_rst_n = 0;

    reg  [7:0] bit_in_TDATA  = 8'h00;
    reg        bit_in_TVALID = 1'b0;
    wire       bit_in_TREADY;
    reg  [0:0] bit_in_TKEEP  = 1'b1;
    reg  [0:0] bit_in_TSTRB  = 1'b1;
    reg  [0:0] bit_in_TLAST  = 1'b0;

    wire [15:0] i_out_TDATA;
    wire        i_out_TVALID;
    reg         i_out_TREADY = 1'b1;
    wire [1:0]  i_out_TKEEP;
    wire [1:0]  i_out_TSTRB;
    wire [0:0]  i_out_TLAST;

    wire [15:0] q_out_TDATA;
    wire        q_out_TVALID;
    reg         q_out_TREADY = 1'b1;
    wire [1:0]  q_out_TKEEP;
    wire [1:0]  q_out_TSTRB;
    wire [0:0]  q_out_TLAST;

    // debug_current_bit/debug_alpha are dead ports (see README); tied to 0.
    reg [31:0] debug_current_bit = 32'h0;
    reg [15:0] debug_alpha       = 16'h0;

    wire [15:0] debug_pulse_TDATA;
    wire        debug_pulse_TVALID;
    reg         debug_pulse_TREADY = 1'b1;
    wire [15:0] debug_phase_TDATA;
    wire        debug_phase_TVALID;
    reg         debug_phase_TREADY = 1'b1;
    wire [15:0] debug_freq_TDATA;
    wire        debug_freq_TVALID;
    reg         debug_freq_TREADY = 1'b1;

    // DIAGNOSTIC-ONLY (verify_tmp only): proper named debug stream for alpha,
    // written once per bit -- see verify_tmp/src/top.h/top.cpp.
    wire [15:0] debug_alpha_stream_TDATA;
    wire        debug_alpha_stream_TVALID;
    reg         debug_alpha_stream_TREADY = 1'b1;

    // DIAGNOSTIC-ONLY (verify_tmp only): idle_mode/current_bit, same write
    // point/cadence as debug_alpha_stream -- see verify_tmp/src/top.h/top.cpp.
    wire [7:0]  debug_idle_stream_TDATA;
    wire        debug_idle_stream_TVALID;
    reg         debug_idle_stream_TREADY = 1'b1;
    wire [7:0]  debug_current_bit_stream_TDATA;
    wire        debug_current_bit_stream_TVALID;
    reg         debug_current_bit_stream_TREADY = 1'b1;

    tfm_modulator dut (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .bit_in_TDATA(bit_in_TDATA),
        .bit_in_TVALID(bit_in_TVALID),
        .bit_in_TREADY(bit_in_TREADY),
        .bit_in_TKEEP(bit_in_TKEEP),
        .bit_in_TSTRB(bit_in_TSTRB),
        .bit_in_TLAST(bit_in_TLAST),
        .i_out_TDATA(i_out_TDATA),
        .i_out_TVALID(i_out_TVALID),
        .i_out_TREADY(i_out_TREADY),
        .i_out_TKEEP(i_out_TKEEP),
        .i_out_TSTRB(i_out_TSTRB),
        .i_out_TLAST(i_out_TLAST),
        .q_out_TDATA(q_out_TDATA),
        .q_out_TVALID(q_out_TVALID),
        .q_out_TREADY(q_out_TREADY),
        .q_out_TKEEP(q_out_TKEEP),
        .q_out_TSTRB(q_out_TSTRB),
        .q_out_TLAST(q_out_TLAST),
        .debug_current_bit(debug_current_bit),
        .debug_alpha(debug_alpha),
        .debug_pulse_TDATA(debug_pulse_TDATA),
        .debug_pulse_TVALID(debug_pulse_TVALID),
        .debug_pulse_TREADY(debug_pulse_TREADY),
        .debug_phase_TDATA(debug_phase_TDATA),
        .debug_phase_TVALID(debug_phase_TVALID),
        .debug_phase_TREADY(debug_phase_TREADY),
        .debug_freq_TDATA(debug_freq_TDATA),
        .debug_freq_TVALID(debug_freq_TVALID),
        .debug_freq_TREADY(debug_freq_TREADY),
        .debug_alpha_stream_TDATA(debug_alpha_stream_TDATA),
        .debug_alpha_stream_TVALID(debug_alpha_stream_TVALID),
        .debug_alpha_stream_TREADY(debug_alpha_stream_TREADY),
        .debug_idle_stream_TDATA(debug_idle_stream_TDATA),
        .debug_idle_stream_TVALID(debug_idle_stream_TVALID),
        .debug_idle_stream_TREADY(debug_idle_stream_TREADY),
        .debug_current_bit_stream_TDATA(debug_current_bit_stream_TDATA),
        .debug_current_bit_stream_TVALID(debug_current_bit_stream_TVALID),
        .debug_current_bit_stream_TREADY(debug_current_bit_stream_TREADY)
    );

    always #(CLK_PERIOD/2.0) ap_clk = ~ap_clk;

    // BUGFIX: previously waited one extra clock edge after the handshake
    // completed before deasserting TVALID. Since `regslice_both` (register-
    // sliced AXI ports) can plausibly hold TREADY high for 2+ consecutive
    // cycles, that extra held-high TVALID cycle risked a second, unintended
    // TVALID&&TREADY beat -- i.e. sending the same byte to the DUT twice.
    // Fixed: deassert TVALID in the same simulation step the handshake is
    // detected, so exactly one beat occurs.
    task automatic stream_byte(input [7:0] data, input is_last);
        begin
            @(posedge ap_clk);
            bit_in_TDATA  <= data;
            bit_in_TLAST  <= is_last;
            bit_in_TVALID <= 1'b1;
            while (!(bit_in_TVALID && bit_in_TREADY)) @(posedge ap_clk);
            bit_in_TVALID <= 1'b0;
        end
    endtask

    // Absolute wall-clock cycle counter, common coordinate for the internal
    // phase probe and i_out/q_out below (see Note.md "debug axis stream 的
    // 陷阱" -- axis-stream TVALID pulse counters between different streams
    // don't reliably align, so key everything to the same clock-edge count
    // instead). Starts counting from ap_rst_n release.
    longint cycle_count;
    initial cycle_count = 0;
    always @(posedge ap_clk) begin
        if (ap_rst_n) cycle_count = cycle_count + 1;
    end

    integer csv_file;
    integer sample_idx;
    real i_val, q_val;

    initial begin
        csv_file = $fopen("output_waveform_xsim_verify_sps8_lut.csv", "w");
        $fwrite(csv_file, "Sample,Cycle,I_Data,Q_Data,TLAST\n");
        sample_idx = 0;
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n && i_out_TVALID && q_out_TVALID) begin
            i_val = $signed(i_out_TDATA) / 4096.0;
            q_val = $signed(q_out_TDATA) / 4096.0;
            $fwrite(csv_file, "%0d,%0d,%f,%f,%0d\n", sample_idx, cycle_count, i_val, q_val, i_out_TLAST);
            sample_idx = sample_idx + 1;
        end
    end

    // current_phase read directly via hierarchical reference to the internal
    // signal `ap_sig_allocacmp_p_load234` (traced from the generated RTL,
    // same method as the original CORDIC investigation's `ap_sig_allocacmp_in`
    // -- the exact wire name is HLS-line-number-derived and changes whenever
    // the source changes, re-traced here via:
    // grep -n "debug_phase_TDATA_int_regslice = " tfm_modulator.v -> sext_lnNNN_fu..._p0
    // grep -n "sext_lnNNN_fu..._p0 =" tfm_modulator.v -> the actual register).
    // Sampled every single clock edge, no axis-stream handshake involved, so
    // there is no cross-stream pipeline-offset ambiguity for this signal itself.
    integer phase_file;
    real phase_val;

    initial begin
        phase_file = $fopen("current_phase_xsim_verify_sps8_lut.csv", "w");
        $fwrite(phase_file, "Cycle,Phase\n");
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n) begin
            phase_val = $signed(dut.ap_sig_allocacmp_p_load239) / 4096.0;
            $fwrite(phase_file, "%0d,%f\n", cycle_count, phase_val);
        end
    end

    // freq_dev read directly via hierarchical reference to the internal
    // register `freq_dev_1_reg_5705` (traced from the generated RTL: this is
    // the completed 128-tap FIR sum, before the debug_freq axis stream's own
    // extra pipeline delay -- see
    // grep -n "regslice_both_debug_freq_U(" -A5 tfm_modulator.v ->
    // .data_in(freq_dev_1_reg_5705_pp0_iter5_reg), which is just a delayed
    // copy of this same register for that stream's own timing). Sampled
    // every clock edge, same method as the current_phase probe above, to
    // test whether the 128-tap FIR adder-tree sum itself (not yet fed into
    // the phase accumulator) matches the C model's sequential sum.
    integer freq_file;
    real freq_val;

    initial begin
        freq_file = $fopen("freq_dev_xsim_verify_sps8_lut.csv", "w");
        $fwrite(freq_file, "Cycle,Freq\n");
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n) begin
            freq_val = $signed(dut.freq_dev_1_reg_5812) / 4096.0;
            $fwrite(freq_file, "%0d,%f\n", cycle_count, freq_val);
        end
    end

    // alpha read via the new debug_alpha_stream axis port (verify_tmp/
    // src/top.cpp writes it once per bit, right where alpha is computed) --
    // NOT a guessed hierarchical-reference name. Three earlier guesses
    // (icmp_ln183_reg_5645 combo, debug_pulse_TDATA_int_regslice,
    // p_0_0_012527_fu_358) all gave wrong/garbage results, so this sidesteps
    // that entirely: the stream's own emission order is exactly one beat per
    // bit (64 beats total for 8 bytes), compared index-for-index against
    // verify_tmp/tb/tb_top.cpp's debug_alpha_stream.csv (same emission order
    // on the C-model side) -- no offset needs to be found/guessed at all.
    integer alpha_file;
    integer alpha_idx;
    real alpha_val;

    initial begin
        alpha_file = $fopen("debug_alpha_stream_xsim_verify_sps8_lut.csv", "w");
        $fwrite(alpha_file, "BitIndex,Alpha\n");
        alpha_idx = 0;
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n && debug_alpha_stream_TVALID && debug_alpha_stream_TREADY) begin
            alpha_val = $signed(debug_alpha_stream_TDATA) / 4096.0;
            $fwrite(alpha_file, "%0d,%f\n", alpha_idx, alpha_val);
            alpha_idx = alpha_idx + 1;
        end
    end

    // idle_mode/current_bit, same emission cadence as debug_alpha_stream
    // (one beat per bit) -- lets us directly tell, for each captured alpha
    // beat, whether that bit was real data or idle/dummy padding.
    integer idle_file;
    integer idle_idx;
    integer idle_val, curbit_val;

    initial begin
        idle_file = $fopen("debug_idle_stream_xsim_verify_sps8_lut.csv", "w");
        $fwrite(idle_file, "BitIndex,Idle,CurrentBit\n");
        idle_idx = 0;
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n && debug_idle_stream_TVALID && debug_idle_stream_TREADY
            && debug_current_bit_stream_TVALID && debug_current_bit_stream_TREADY) begin
            idle_val = debug_idle_stream_TDATA;
            curbit_val = debug_current_bit_stream_TDATA;
            $fwrite(idle_file, "%0d,%0d,%0d\n", idle_idx, idle_val, curbit_val);
            idle_idx = idle_idx + 1;
        end
    end

    // debug_pulse/phase/freq streams, captured in their own emission order
    // (own counter, NOT cross-aligned to i_out/q_out's cycle timing -- see
    // Note.md "debug axis stream 的陷阱"). Compared index-for-index against
    // verify_tmp/tb/tb_top.cpp's debug_signals.csv (same emission order on
    // the C-model side), to isolate whether current_phase itself matches
    // before it reaches the LUT, independent of the i_out/q_out alignment
    // question above.
    integer dbg_file;
    integer dbg_idx;
    real dbg_pulse, dbg_phase, dbg_freq;

    initial begin
        dbg_file = $fopen("debug_signals_xsim_verify_sps8_lut.csv", "w");
        $fwrite(dbg_file, "Sample,Pulse,Phase,Freq\n");
        dbg_idx = 0;
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n && debug_pulse_TVALID && debug_phase_TVALID && debug_freq_TVALID) begin
            dbg_pulse = $signed(debug_pulse_TDATA) / 4096.0;
            dbg_phase = $signed(debug_phase_TDATA) / 4096.0;
            dbg_freq  = $signed(debug_freq_TDATA) / 4096.0;
            $fwrite(dbg_file, "%0d,%f,%f,%f\n", dbg_idx, dbg_pulse, dbg_phase, dbg_freq);
            dbg_idx = dbg_idx + 1;
        end
    end

    reg [7:0] test_data [0:NUM_BYTES-1];
    integer k;

    initial begin
        test_data[0] = 8'h67;
        test_data[1] = 8'h72;
        test_data[2] = 8'h74;
        test_data[3] = 8'hDE;
        test_data[4] = 8'h67;
        test_data[5] = 8'h72;
        test_data[6] = 8'h74;
        test_data[7] = 8'hDE;

        // CHANGED (2026-07-28): byte0 used to be pre-loaded before reset
        // released, on the theory that this would match tb_top.cpp's C-model
        // behavior (all 8 bytes queued before the single tfm_modulator()
        // call, so no byte is ever "not yet available"). That reasoning
        // doesn't actually transfer to RTL: tb_top.cpp's "all queued upfront"
        // behavior is an artifact of hls::stream's csim semantics (C
        // simulation has no concept of real time at all), not a timing
        // requirement of top.cpp itself -- top.cpp has no timing model,
        // only a data-content algorithm. A real AXI4-Stream master (DMA/PS)
        // would present data via a normal post-reset handshake, not by
        // holding it through reset. Pre-loading byte0 through reset also
        // interacts badly with `regslice_both`: that register slice is held/
        // cleared for as long as ap_rst_n is low regardless of how long
        // TVALID was stable beforehand, so it needs a settle cycle after
        // release either way -- pre-loading doesn't skip that. So byte0 now
        // goes through the exact same stream_byte() path as bytes 1-7,
        // which is both simpler and a more realistic stimulus.
        // byte0 goes through the exact same stream_byte() path as bytes 1-7
        // -- see Note.md: confirmed (2026-07-28) that the OLD byte0-before-
        // reset stimulus hits the exact same idle-misfire, so this isn't a
        // stimulus-timing workaround, it's simply the simpler/more realistic
        // way to drive this interface.
        ap_rst_n = 1'b0;
        repeat (10) @(posedge ap_clk);
        ap_rst_n = 1'b1;

        for (k = 0; k < NUM_BYTES; k = k + 1) begin
            stream_byte(test_data[k], (k == NUM_BYTES-1));
        end

        repeat (RUN_CYCLES) @(posedge ap_clk);

        $fclose(csv_file);
        $display(">> XSIM (verify_tmp RTL, LUT sin/cos, SPS=8 hardwired) finished. %0d samples captured.", sample_idx);
        $finish;
    end

endmodule

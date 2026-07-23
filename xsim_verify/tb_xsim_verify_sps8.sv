// -----------------------------------------------------------------------
// Cleanest possible SPS=8 RTL vs C check: drives the verify_tmp/ RTL
// (sps_sel pinned to a compile-time constant = 1/SPS=8, HW_DEBUG_MODE off,
// no s_axi_CTRL bundle at all -- see verify_tmp/src/top.h). With no
// s_axilite port in this interface, there is no AXI4-Lite write and
// therefore no possibility of the sps_sel race found in tb_xsim_top.sv
// (real hls_prj/ interface). Data is presented before reset releases, same
// as tb_xsim_top_sps16_default.sv, to also rule out a data-availability race.
// Diffed against xsim_verify/golden_sps8_output_waveform.csv (the SPS=8 C
// model golden, generated the same way from this same verify_tmp/ source).
// -----------------------------------------------------------------------
`timescale 1ns/1ps

module tb_xsim_verify_sps8;

    localparam CLK_PERIOD = 6.25;
    localparam integer NUM_BYTES = 8;
    localparam integer SPS = 8;
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
        .debug_freq_TREADY(debug_freq_TREADY)
    );

    always #(CLK_PERIOD/2.0) ap_clk = ~ap_clk;

    task automatic stream_byte(input [7:0] data, input is_last);
        begin
            @(posedge ap_clk);
            bit_in_TDATA  <= data;
            bit_in_TLAST  <= is_last;
            bit_in_TVALID <= 1'b1;
            while (!(bit_in_TVALID && bit_in_TREADY)) @(posedge ap_clk);
            @(posedge ap_clk);
            bit_in_TVALID <= 1'b0;
        end
    endtask

    integer csv_file;
    integer sample_idx;
    real i_val, q_val;

    // Absolute wall-clock cycle counter, common coordinate for both captures
    // below -- avoids the debug-axis-stream TVALID pipeline-offset ambiguity
    // entirely by keying everything to the same clock-edge count instead of
    // independent per-stream pulse counters (see chat: the earlier
    // debug_phase-stream-based attempt gave an offset that didn't cleanly
    // resolve). Starts counting from ap_rst_n release.
    longint cycle_count;
    initial cycle_count = 0;
    always @(posedge ap_clk) begin
        if (ap_rst_n) cycle_count = cycle_count + 1;
    end

    initial begin
        csv_file = $fopen("output_waveform_xsim_verify_sps8.csv", "w");
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
    // signal `ap_sig_allocacmp_in` (traced from the generated RTL: this is
    // exactly what debug_phase_TDATA is sign-extended from -- see
    // tfm_modulator.v: `assign sext_ln302_fu_5727_p0 = ap_sig_allocacmp_in;`
    // then `debug_phase_TDATA_int_regslice = $signed(sext_ln302_fu_5727_p0);`
    // -- but sampled here on every single clock edge with no axis-stream
    // handshake/backpressure involved at all, so there is no independent
    // pipeline-offset question for THIS signal itself: whatever cycle
    // i_out_TVALID fires for a given iteration, current_phase for that same
    // iteration appeared on this wire some fixed number of cycles earlier,
    // and we now have both logged against the same absolute cycle_count to
    // find that fixed offset empirically instead of guessing it.
    integer phase_file;
    real phase_val;

    initial begin
        phase_file = $fopen("current_phase_xsim.csv", "w");
        $fwrite(phase_file, "Cycle,Phase\n");
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n) begin
            phase_val = $signed({dut.ap_sig_allocacmp_in[14], dut.ap_sig_allocacmp_in}) / 4096.0;
            $fwrite(phase_file, "%0d,%f\n", cycle_count, phase_val);
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

        // Present the first byte before releasing reset -- matches
        // verify_tmp/tb/tb_top.cpp, which queues all 8 bytes before the
        // single tfm_modulator() call (data is never unavailable there).
        ap_rst_n = 1'b0;
        bit_in_TDATA  = test_data[0];
        bit_in_TLAST  = (NUM_BYTES == 1);
        bit_in_TVALID = 1'b1;
        repeat (10) @(posedge ap_clk);
        ap_rst_n = 1'b1;

        while (!(bit_in_TVALID && bit_in_TREADY)) @(posedge ap_clk);
        @(posedge ap_clk);
        bit_in_TVALID <= 1'b0;

        for (k = 1; k < NUM_BYTES; k = k + 1) begin
            stream_byte(test_data[k], (k == NUM_BYTES-1));
        end

        repeat (RUN_CYCLES) @(posedge ap_clk);

        $fclose(csv_file);
        $fclose(phase_file);
        $display(">> XSIM (verify_tmp RTL, SPS=8 hardwired, no AXI/race) finished. %0d samples captured.", sample_idx);
        $finish;
    end

endmodule

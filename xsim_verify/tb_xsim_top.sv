// -----------------------------------------------------------------------
// Hand-rolled RTL testbench for tfm_modulator, bypassing Vitis HLS's own
// cosim/post-check machinery entirely (see README "Cosim ap_ctrl_none
// limitation": HLS cosim rejects this design outright because it mixes
// ap_ctrl_none with an s_axilite scalar port, and even the verify_tmp/
// workaround's post-check comparison is unreliable for free-running
// designs). This drives the REAL exported RTL (hls_prj/solution1/syn/verilog),
// with the real s_axi_CTRL bundle and the real axis ports, directly in XSIM.
//
// Drives the same 8-byte test vector as tb/tb_top.cpp, at ap_clk = 6.25ns
// (160MHz, matching scripts/run_hls.tcl's official clock target for the
// customer's 20Mbit/s @ SPS=8 -> 160MSPS spec), with sps_sel=1 (SPS=8) set
// via one AXI4-Lite write transaction to ADDR_SPS_SEL_DATA_0 (0x10) before
// streaming starts.
//
// Outputs: dumps i_out/q_out to output_waveform_xsim.csv in the same format
// as tb_top.cpp's output_waveform.csv (Sample,I_Data,Q_Data,TLAST) for a
// direct diff/plot against the golden C-model CSV, and logs all signals for
// waveform viewing (via the xsim -wdb ... run, see scripts/run_xsim_verify.sh).
// -----------------------------------------------------------------------
`timescale 1ns/1ps

module tb_xsim_top;

    // ap_clk = 6.25ns (160MHz), matches scripts/run_hls.tcl's create_clock.
    localparam CLK_PERIOD = 6.25;
    localparam ADDR_SPS_SEL_DATA_0 = 5'h10;
    localparam integer NUM_BYTES = 8;
    localparam integer SPS = 8;              // sps_sel = 1 -> SPS=8
    // Enough cycles to drain the full 8-byte burst (NUM_BYTES*SPS*8 samples)
    // plus pipeline fill latency and the AXI4-Lite setup transaction, with margin.
    localparam integer RUN_CYCLES = 900;

    reg ap_clk = 0;
    reg ap_rst_n = 0;

    // bit_in (AXI-Stream slave on the IP)
    reg  [7:0] bit_in_TDATA  = 8'h00;
    reg        bit_in_TVALID = 1'b0;
    wire       bit_in_TREADY;
    reg  [0:0] bit_in_TKEEP  = 1'b1;
    reg  [0:0] bit_in_TSTRB  = 1'b1;
    reg  [0:0] bit_in_TLAST  = 1'b0;

    // i_out / q_out (AXI-Stream master on the IP)
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

    // Dead ports (see README: debug_current_bit/debug_alpha are unconnected
    // RTL inputs -- writing to them in C++ never reaches an output pin under
    // ap_ctrl_none). Tied to 0; value is a don't-care.
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

    // s_axi_CTRL (AXI4-Lite, only reg is sps_sel @ 0x10)
    reg         s_axi_CTRL_AWVALID = 1'b0;
    wire        s_axi_CTRL_AWREADY;
    reg  [4:0]  s_axi_CTRL_AWADDR  = 5'h0;
    reg         s_axi_CTRL_WVALID  = 1'b0;
    wire        s_axi_CTRL_WREADY;
    reg  [31:0] s_axi_CTRL_WDATA   = 32'h0;
    reg  [3:0]  s_axi_CTRL_WSTRB   = 4'h0;
    reg         s_axi_CTRL_ARVALID = 1'b0;
    wire        s_axi_CTRL_ARREADY;
    reg  [4:0]  s_axi_CTRL_ARADDR  = 5'h0;
    wire        s_axi_CTRL_RVALID;
    reg         s_axi_CTRL_RREADY  = 1'b0;
    wire [31:0] s_axi_CTRL_RDATA;
    wire [1:0]  s_axi_CTRL_RRESP;
    wire        s_axi_CTRL_BVALID;
    reg         s_axi_CTRL_BREADY  = 1'b0;
    wire [1:0]  s_axi_CTRL_BRESP;

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
        .s_axi_CTRL_AWVALID(s_axi_CTRL_AWVALID),
        .s_axi_CTRL_AWREADY(s_axi_CTRL_AWREADY),
        .s_axi_CTRL_AWADDR(s_axi_CTRL_AWADDR),
        .s_axi_CTRL_WVALID(s_axi_CTRL_WVALID),
        .s_axi_CTRL_WREADY(s_axi_CTRL_WREADY),
        .s_axi_CTRL_WDATA(s_axi_CTRL_WDATA),
        .s_axi_CTRL_WSTRB(s_axi_CTRL_WSTRB),
        .s_axi_CTRL_ARVALID(s_axi_CTRL_ARVALID),
        .s_axi_CTRL_ARREADY(s_axi_CTRL_ARREADY),
        .s_axi_CTRL_ARADDR(s_axi_CTRL_ARADDR),
        .s_axi_CTRL_RVALID(s_axi_CTRL_RVALID),
        .s_axi_CTRL_RREADY(s_axi_CTRL_RREADY),
        .s_axi_CTRL_RDATA(s_axi_CTRL_RDATA),
        .s_axi_CTRL_RRESP(s_axi_CTRL_RRESP),
        .s_axi_CTRL_BVALID(s_axi_CTRL_BVALID),
        .s_axi_CTRL_BREADY(s_axi_CTRL_BREADY),
        .s_axi_CTRL_BRESP(s_axi_CTRL_BRESP)
    );

    // ap_clk generation
    always #(CLK_PERIOD/2.0) ap_clk = ~ap_clk;

    // ---------------------------------------------------------------
    // AXI4-Lite write task: set sps_sel (independent AW/W handshake,
    // then wait for BVALID/BREADY).
    // ---------------------------------------------------------------
    task automatic axilite_write(input [4:0] addr, input [31:0] data);
        begin
            @(posedge ap_clk);
            s_axi_CTRL_AWADDR  <= addr;
            s_axi_CTRL_AWVALID <= 1'b1;
            s_axi_CTRL_WDATA   <= data;
            s_axi_CTRL_WSTRB   <= 4'hF;
            s_axi_CTRL_WVALID  <= 1'b1;

            // Wait for both AWREADY and WREADY, each may complete on a
            // different cycle; drop VALID as soon as its own READY is seen.
            fork
                begin : aw_wait
                    while (!(s_axi_CTRL_AWVALID && s_axi_CTRL_AWREADY)) @(posedge ap_clk);
                    s_axi_CTRL_AWVALID <= 1'b0;
                end
                begin : w_wait
                    while (!(s_axi_CTRL_WVALID && s_axi_CTRL_WREADY)) @(posedge ap_clk);
                    s_axi_CTRL_WVALID <= 1'b0;
                end
            join

            @(posedge ap_clk);
            s_axi_CTRL_BREADY <= 1'b1;
            while (!s_axi_CTRL_BVALID) @(posedge ap_clk);
            @(posedge ap_clk);
            s_axi_CTRL_BREADY <= 1'b0;
            $display("[DIAG] t=%0t axilite_write done. dut.sps_sel(internal wire)=%b int_sps_sel=%b",
                      $time, dut.sps_sel, dut.CTRL_s_axi_U.int_sps_sel);
        end
    endtask

    // ---------------------------------------------------------------
    // AXI-Stream master task for bit_in: hold TDATA/TVALID until the
    // IP's non-blocking read pulses TREADY (once per byte, at the
    // active_sps*8 byte boundary).
    // ---------------------------------------------------------------
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

    // ---------------------------------------------------------------
    // CSV capture: same layout as tb_top.cpp's output_waveform.csv
    // (Sample,I_Data,Q_Data,TLAST), values converted from ap_fixed<16,4>
    // raw bits back to real (16-bit word, 4 integer bits -> 12 fractional
    // bits, scale = 2^12 = 4096, matches src/top.h's data_t).
    // ---------------------------------------------------------------
    integer csv_file;
    integer pulse_file;
    integer sample_idx;
    real i_val, q_val, pulse_val;

    initial begin
        csv_file = $fopen("output_waveform_xsim.csv", "w");
        $fwrite(csv_file, "Sample,I_Data,Q_Data,TLAST\n");
        // debug_pulse == impulse (== alpha at s==0 samples, 0 elsewhere), same
        // sample index as i_out/q_out (written in the same loop iteration) --
        // lets us diff the per-bit alpha sequence directly instead of only
        // the phase-accumulated I/Q, to localize exactly which bit diverges.
        pulse_file = $fopen("debug_pulse_xsim.csv", "w");
        $fwrite(pulse_file, "Sample,Pulse\n");
        sample_idx = 0;
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n && i_out_TVALID && q_out_TVALID) begin
            i_val = $signed(i_out_TDATA) / 4096.0;
            q_val = $signed(q_out_TDATA) / 4096.0;
            $fwrite(csv_file, "%0d,%f,%f,%0d\n", sample_idx, i_val, q_val, i_out_TLAST);
            if (debug_pulse_TVALID) begin
                pulse_val = $signed(debug_pulse_TDATA) / 4096.0;
                $fwrite(pulse_file, "%0d,%f\n", sample_idx, pulse_val);
            end
            sample_idx = sample_idx + 1;
        end
    end

    // ---------------------------------------------------------------
    // Stimulus: reset -> AXI4-Lite sps_sel=1 write -> stream 8 bytes
    // (identical data/order to tb/tb_top.cpp) -> let the pipeline drain.
    // ---------------------------------------------------------------
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

        ap_rst_n = 1'b0;
        repeat (10) @(posedge ap_clk);
        ap_rst_n = 1'b1;
        repeat (5) @(posedge ap_clk);

        // sps_sel = 1 -> SPS=8, matches the customer's 20Mbit/s @ ap_clk=160MHz spec
        axilite_write(ADDR_SPS_SEL_DATA_0, 32'h00000001);

        repeat (5) @(posedge ap_clk);

        for (k = 0; k < NUM_BYTES; k = k + 1) begin
            stream_byte(test_data[k], (k == NUM_BYTES-1));
        end

        repeat (RUN_CYCLES) @(posedge ap_clk);

        $fclose(csv_file);
        $fclose(pulse_file);
        $display(">> XSIM testbench finished. %0d samples captured to output_waveform_xsim.csv", sample_idx);
        $finish;
    end

endmodule

// -----------------------------------------------------------------------
// Clean control test: NO AXI4-Lite write at all, sps_sel stays at its
// hardware reset default (0 -> SPS=16). This sidesteps the sps_sel write
// race found in tb_xsim_top.sv (int_sps_sel and the whole datapath share
// ap_rst_n, so BYTE_LOOP's very first byte-boundary decode can win the race
// against any AXI4-Lite write attempting to change sps_sel away from its
// default -- see README). With no write involved, there is no race, so this
// gives a clean, race-free RTL vs C comparison directly against the existing
// golden hls_prj/solution1/csim/build/output_waveform.csv (SPS=16, matches
// tb_top.cpp's default TEST_SPS_SEL=0).
// -----------------------------------------------------------------------
`timescale 1ns/1ps

module tb_xsim_top_sps16_default;

    localparam CLK_PERIOD = 6.25;
    localparam integer NUM_BYTES = 8;
    localparam integer SPS = 16;             // sps_sel stays at its reset default (0) -> SPS=16
    localparam integer RUN_CYCLES = 1400;    // NUM_BYTES*SPS*8 = 1024 samples + pipeline fill + margin

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

    // s_axi_CTRL left completely idle -- no write, sps_sel stays at reset default.
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
    integer pulse_file;
    integer sample_idx;
    real i_val, q_val, pulse_val;

    initial begin
        csv_file = $fopen("output_waveform_xsim_sps16.csv", "w");
        $fwrite(csv_file, "Sample,I_Data,Q_Data,TLAST\n");
        pulse_file = $fopen("debug_pulse_xsim_sps16.csv", "w");
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

        // Present the first byte BEFORE releasing reset, so there is zero
        // ambiguity about whether data is available in time for the very
        // first byte-boundary decode (matches tb_top.cpp, which queues all
        // 8 bytes before the single tfm_modulator() call -- data is never
        // unavailable there).
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
        $fclose(pulse_file);
        $display(">> XSIM (SPS=16 default, no AXI write) finished. %0d samples captured.", sample_idx);
        $finish;
    end

endmodule

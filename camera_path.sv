// Camera capture path: CSI-2 RX pixels -> AXI-stream words for the PDMA.
//
//   cam0_csi2_rx --> camera_write --> camera_fifo --> camera_gearbox --> PDMA dat2_i
//                    (can't stall)    (buffer)        (can stall)
//
// - camera_write: brings data to Efinix IP FIFO
// - camera_fifo:  Efinix FIFO IP, 1024 x 41, single clock, FWFT.
// - camera_gearbox: packs 4 RAW10 pixels into 5 bytes, regroups into 32-bit words,
//   asserts tlast every FRAME_WORDS words, *forces tlast to close a short frame*.
//
// Everything runs on one clock (io_ddrMasters_0_clk in the top level), reset active high
module camera_path #(
    parameter int unsigned FRAME_WORDS = 648000
) (
    input  logic        clk,
    input  logic        reset,
    input  logic [39:0] pixel_data,
    input  logic        pixel_data_valid,
    input  logic        vsync_vc0,
    input  logic        enable,
    output logic [31:0] tdata,
    output logic        tvalid,
    input  logic        tready,
    output logic        tlast,
    output logic        overflow,     
    output logic [10:0] fifo_level    
);
    logic [40:0] fifo_wdata;
    logic        fifo_wr_en;
    logic        fifo_full;
    logic        fifo_rst_busy;
    logic [40:0] fifo_rdata;
    logic        fifo_empty;
    logic        fifo_rd_en;

    camera_write u_camera_write (
        .clk              (clk),
        .reset            (reset),
        .pixel_data       (pixel_data),
        .pixel_data_valid (pixel_data_valid),
        .vsync_vc0        (vsync_vc0),
        .enable           (enable),
        .fifo_full        (fifo_full),
        .fifo_rst_busy    (fifo_rst_busy),
        .fifo_wdata       (fifo_wdata),
        .fifo_wr_en       (fifo_wr_en),
        .overflow         (overflow)
    );

    camera_fifo u_camera_fifo (
        .clk_i          (clk),
        .a_rst_i        (reset),
        .wr_en_i        (fifo_wr_en),
        .wdata          (fifo_wdata),
        .full_o         (fifo_full),
        .rst_busy       (fifo_rst_busy),
        .rd_en_i        (fifo_rd_en),
        .rdata          (fifo_rdata),
        .empty_o        (fifo_empty),
        .datacount_o    (fifo_level),
        // unused status outputs
        .almost_full_o  (),
        .almost_empty_o (),
        .wr_ack_o       (),
        .rd_valid_o     (),
        .overflow_o     (),
        .underflow_o    ()
    );

    camera_gearbox #(.FRAME_WORDS(FRAME_WORDS)) u_camera_gearbox (
        .clk    (clk),
        .reset  (reset),
        .valid  (!fifo_empty),
        .data   (fifo_rdata),
        .tready (tready),
        .tdata  (tdata),
        .tvalid (tvalid),
        .tlast  (tlast),
        .rd_en  (fifo_rd_en)
    );
endmodule

// Takes in camera data from FIFO, and packs
// each burst's 4 pixels (40 bits) into 5 clean bytes of data 
// for the DMA
// *previous design padded data which clipped ethernet threshold
module camera_gearbox #(
    parameter int unsigned FRAME_WORDS = 648000
) (
    input logic clk,
    input logic reset,
    input logic valid,
    input logic [40:0] data,
    input logic tready,

    output logic [31:0] tdata,
    output logic tvalid,
    output logic tlast,
    output logic rd_en
);
    if (FRAME_WORDS % 5 != 0) begin : g_check_frame_words
         $error("FRAME_WORDS (%0d) must be a multiple of 5", FRAME_WORDS);
    end

    logic short_frame;
    logic [31:0] word_count;
    logic [9:0] p0, p1, p2, p3; // pixels in RAW10 || 4 pixels at 10 bits each = 40 bits / 8 = 5 bytes
    logic [7:0] n0, n1, n2, n3, n4; // reassembled bytes
    logic [7:0] b0, b1, b2, b3; 
    logic [7:0] l0, l1, l2, l3; // Store bytes we haven't sent yet but have from previous receives

    // 10-bit pixel data
    assign p0 = data[9:0];
    assign p1 = data[19:10];
    assign p2 = data[29:20];
    assign p3 = data[39:30];

    assign n0 = p0[9:2];
    assign n1 = p1[9:2];
    assign n2 = p2[9:2];
    assign n3 = p3[9:2];
    assign n4 = {p3[1:0], p2[1:0], p1[1:0], p0[1:0]};
    assign tdata = short_frame ? {l3, l2, l1, l0} : {b3, b2, b1, b0};

    localparam [2:0] STATE_0   = 3'b000,
                     STATE_1   = 3'b001,
                     STATE_2   = 3'b010,
                     STATE_3   = 3'b011,
                     STATE_4   = 3'b100;
    logic [2:0] phase_state;

assign rd_en =  short_frame ? '0 : ((tvalid && tready) && (phase_state < 4)) ? 1'b1 : 1'b0; 
assign tvalid = short_frame ? '1 : (phase_state <= 3) ? valid : 1;
assign tlast =  short_frame ? '1 : (word_count == FRAME_WORDS - 1);
assign short_frame = valid && data[40] && word_count != 0;

always_comb begin
    b0 = 0;
    b1 = 0;
    b2 = 0;
    b3 = 0;
    case (phase_state)
        STATE_0: begin
            b0 = n0;
            b1 = n1;
            b2 = n2;
            b3 = n3;
        end
        STATE_1: begin
            b0 = l0;
            b1 = n0;
            b2 = n1;
            b3 = n2;
        end
        STATE_2: begin
            b0 = l0;
            b1 = l1;
            b2 = n0;
            b3 = n1;
        end
        STATE_3: begin
            b0 = l0;
            b1 = l1;
            b2 = l2;
            b3 = n0;
        end
        STATE_4: begin
            b0 = l0;
            b1 = l1;
            b2 = l2;
            b3 = l3;
        end
        default: begin
            b0 = 0;
            b1 = 0;
            b2 = 0;
            b3 = 0;
        end
    endcase
end

always_ff @(posedge clk) begin
    if (reset) begin
        phase_state <= STATE_0;
        word_count <= 0;
    end
    else if (tvalid && tready) begin
        case (phase_state)
            STATE_0: begin
                phase_state <= STATE_1;
                l0 <= n4;
            end
            STATE_1: begin
                phase_state <= STATE_2;
                l0 <= n3;
                l1 <= n4;
            end
            STATE_2: begin
                phase_state <= STATE_3;
                l0 <= n2; 
                l1 <= n3;          
                l2 <= n4;
            end
            STATE_3: begin
                phase_state <= STATE_4;
                l0 <= n1;       
                l1 <= n2;         
                l2 <= n3;
                l3 <= n4;
            end
            STATE_4: begin
                phase_state <= STATE_0;
            end
            default: begin
                phase_state <= STATE_0;
                l0 <= 0;
                l1 <= 0;
                l2 <= 0;
                l3 <= 0;
            end
        endcase
        if (word_count == FRAME_WORDS - 1) begin
            word_count <= 0;
        end else begin
            word_count <= word_count + 1;
        end

        if (short_frame) begin
            phase_state <= 0;
            word_count <= 0;
        end
    end
end
endmodule

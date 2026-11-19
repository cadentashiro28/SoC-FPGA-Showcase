// Handles integration of the camera -> FIFO
module camera_write (
    input logic clk,
    input logic reset,
    input logic [39:0] pixel_data,
    input logic pixel_data_valid,
    input logic vsync_vc0,
    input logic enable,
    input logic fifo_full,
    input logic fifo_rst_busy,

    output logic [40:0] fifo_wdata,
    output logic fifo_wr_en,
    output logic overflow
);

    logic vsync_prev;
    logic capturing;
    logic sof_pending;
    logic dropping;
    logic vsync_rise;
    logic want_write;

    assign want_write = pixel_data_valid && capturing && !fifo_rst_busy;
    assign vsync_rise = (vsync_vc0 && !vsync_prev);
    assign overflow = want_write && fifo_full && !dropping;
    assign fifo_wr_en = want_write && !fifo_full && !dropping;
    assign fifo_wdata = {sof_pending, pixel_data};

    always_ff @(posedge clk) begin
        if (reset) begin
            vsync_prev <= '0;
            capturing <= '0;
            sof_pending <= '0;
            dropping <= '0;
        end 
        
        vsync_prev <= vsync_vc0;

        if (!reset) begin
            if (fifo_wr_en) begin
                sof_pending <= '0;
            end 

            if (overflow) begin
                dropping <= '1;
            end
            
            if (vsync_rise) begin
                capturing <= enable;
                sof_pending <= '1;
                dropping <= '0;
            end         
        end
    end
endmodule


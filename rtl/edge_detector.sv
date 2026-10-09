module edge_detector (
    input  logic clk,
    input  logic in,
    output logic out
);

logic[31:0] shift;
always_ff @(posedge clk) begin
    shift[0] <= in;
    for (int i = 1; i < 32; ++i) begin
        shift[i] <= shift[i-1];
    end
end

assign out = ~|shift[31:16] & &shift[15:0];

endmodule

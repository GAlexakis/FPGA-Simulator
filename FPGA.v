// Demo design: replace with your own. Port names match the board XDC.
module FPGA (
    input  wire        CLK100MHZ,
    input  wire [15:0] SW,
    output wire [15:0] LED,
    output wire        LED16_B, LED16_G, LED16_R,
    output wire        LED17_B, LED17_G, LED17_R,
    output wire        CA, CB, CC, CD, CE, CF, CG, DP,
    output wire [7:0]  AN,
    input  wire        CPU_RESETN, BTNC, BTNU, BTNL, BTNR, BTND,
    output wire [3:0]  VGA_R, VGA_G, VGA_B,
    output wire        VGA_HS, VGA_VS
);

assign LED16_B = 0;
assign LED16_G = 0;
assign LED16_R = 0;
assign LED17_B = 0;
assign LED17_G = 0;
assign LED17_R = 0;
assign CA = 0;
assign CB = 0;
assign CC = 0;
assign CD = 0;
assign CE = 0;
assign CF = 0;
assign CG = 0;
assign DP = 0;
assign LED[15:3] = 0;
top i_top (
    .clk         (CLK100MHZ),
    .rst         (CPU_RESETN),
    .button_left (BTNL),
    .button_right(BTNR),
    .button_up   (BTNU),
    .button_down (BTND),
    .button_play (BTNC),
    .winA        (LED[0]),
    .winB        (LED[1]),
    .tie         (LED[2]),
    .red         (VGA_R),
    .green       (VGA_G),
    .blue        (VGA_B),
    .hsync       (VGA_HS),
    .vsync       (VGA_VS)
);


endmodule

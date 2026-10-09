module top(
	input logic clk,
	input logic rst,

	input logic button_left,
	input logic button_right,
	input logic button_up,
	input logic button_down,
  
  	input logic button_play,

	output logic winA,
	output logic winB,
	output logic tie,

	output logic [3:0] red,
	output logic [3:0] green,
	output logic [3:0] blue,
	output logic hsync,
	output logic vsync
	);

	// inv reset
	logic n_rst;
	assign n_rst = ~rst;

	// edge detecors
	logic up;
	edge_detector up_button(.clk(clk), 
							.in(button_up), 
							.out(up));
	logic down;
	edge_detector down_button(.clk(clk),
							  .in(button_down), 
							  .out(down));
	logic right;
	edge_detector right_button(.clk(clk),
							   .in(button_right), 
							   .out(right));
	logic left;
	edge_detector left_button(.clk(clk),
							  .in(button_left), 
							  .out(left));
	logic play;
	edge_detector play_button(.clk(clk),
							  .in(button_play), 
							  .out(play));

	// ouputs
	logic winX;
	logic winO;
	logic check_tie;
	assign winA = winX;
	assign winB = winO;
	assign tie = check_tie;

	// game over
	logic game_over;
	assign game_over = winX | winO | check_tie;

	// control signals
	logic [8:0] rX;
	logic [8:0] rO;
	logic [1:0] cursor_x;
	logic [1:0] cursor_y;
	logic [7:0] win_typeX;
	logic [7:0] win_typeO;
	logic turn;

	// current square
	logic [3:0] box;
	assign box = cursor_x + 3*cursor_y;

	// oponent move generator
	logic [8:0] newO;
	MoveGen mg (.x(rX),
				.o(rO),
				.newO(newO));

	// play
	always_ff @(posedge clk) begin
		if (n_rst) begin
			cursor_x <= 0;
			cursor_y <= 0;
			turn <= 0;
			rX <= 0;
			rO <= 0;
		end 
		else if (!game_over && !turn) begin
			if (up && cursor_y != 0)
				cursor_y <= cursor_y - 1;
			else if (down && cursor_y != 2)
				cursor_y <= cursor_y + 1;
			else if (right && cursor_x != 2)
				cursor_x <= cursor_x + 1;
			else if (left && cursor_x != 0)
				cursor_x <= cursor_x - 1;
			else if (play && ~rX[box] && ~rO[box]) begin
				cursor_x <= 0;
				cursor_y <= 0;
				turn <= ~turn;
				rX[box] <= 1;
			end
		end
		else if (!game_over && turn) begin
			rO <= newO;
			turn <= ~turn;	
		end
	end

	// win conditions
	always_comb begin
		casez(rX)
			9'b???_???_111: begin 
				winX = 1; 
				win_typeX = 8'b00_000_001;
			end
			9'b???_111_???: begin 
				winX = 1; 
				win_typeX = 8'b00_000_010;
			end
			9'b111_???_???: begin 
				winX = 1; 
				win_typeX = 8'b00_000_100;
			end
			9'b??1_??1_??1: begin 
				winX = 1; 
				win_typeX = 8'b00_001_000;
			end
			9'b?1?_?1?_?1?: begin 
				winX = 1; 
				win_typeX = 8'b00_010_000;
			end
			9'b1??_1??_1??: begin
				winX = 1; 
				win_typeX = 8'b00_100_000;
			end
			9'b1??_?1?_??1: begin 
				winX = 1; 
				win_typeX = 8'b01_000_000;
			end
			9'b??1_?1?_1??: begin 
				winX = 1; 
				win_typeX = 8'b10_000_000;
			end
			default: begin 
				winX = 0;
				win_typeX = 8'b00_000_000;
			end
		endcase
	end
	always_comb begin
		casez (rO)
			9'b???_???_111: begin
				winO = 1; 
				win_typeO = 8'b00_000_001;
			end
			9'b???_111_???: begin
				winO = 1; 
				win_typeO = 8'b00_000_010;
			end
			9'b111_???_???: begin
				winO = 1; 
				win_typeO = 8'b00_000_100;
			end
			9'b??1_??1_??1: begin
				winO = 1; 
				win_typeO = 8'b00_001_000;
			end
			9'b?1?_?1?_?1?: begin
				winO = 1; 
				win_typeO = 8'b00_010_000;
			end
			9'b1??_1??_1??: begin
				winO = 1; 
				win_typeO = 8'b00_100_000;
			end
			9'b1??_?1?_??1: begin
				winO = 1; 
				win_typeO = 8'b01_000_000;
			end
			9'b??1_?1?_1??: begin
				winO = 1; 
				win_typeO = 8'b10_000_000;
			end
			default: begin 
				winO = 0;
				win_typeO = 8'b00_000_000;
			end
		endcase
	end
	assign check_tie = (& (rX | rO)) & ~winX & ~winO;
	
	// pixel clock
	logic pxlClk;
  	always_ff @(posedge clk) begin
    		if (n_rst) 
				pxlClk <= 1'b1;
    		else	
				pxlClk <= ~pxlClk;
  	end

	// pixel counter
  	logic [10:0] pixel;
  	logic [9:0] line;
	// logic c_frame;
  	always_ff @(posedge clk) begin
    	if (n_rst) begin
      		pixel <= 0;
      		line <= 0;
			// c_frame <= 0;
    	end else begin
      		if (pxlClk) begin
	  			pixel <= pixel + 1;
	  			if(pixel >= 1039) begin
	    			line <= line + 1;
	    			pixel <= 0;  
          			if(line >= 665) 
						line <= 0;
						// c_frame <= ~c_frame;
	  			end
      		end
    	end
  	end

	// vga protocol c pixel
  	assign hsync = (pixel >= 856 && pixel < 976) ? 1'b0 : 1'b1;
  	assign vsync = (line >= 637 && line < 646) ? 1'b0 : 1'b1;

	// screen c pixel
	logic screen;
	assign screen = (pixel >= 0 && pixel < 800 && line >= 0 && line < 600);

	// bounds
	logic bounds;
	assign bounds = (pixel >= 100 && pixel < 700 && line >= 0 && line <600);

	// border shape
	logic [3:0] border_line;
	logic border;
	assign border_line[0] 	= (pixel >= 290 && pixel < 305 && bounds);
	assign border_line[1] 	= (pixel >= 495 && pixel < 510 && bounds);
	assign border_line[2] 	= (line >= 190 && line < 205 && bounds);
	assign border_line[3] 	= (line >= 395 && line < 410 && bounds);
	assign border 			= | border_line;

	// indicator shape
	logic [3:0] ind_bounds;
	logic indicator;
	assign ind_bounds[0] 	= (pixel >= 110 + 205*cursor_x);
	assign ind_bounds[1] 	= (line >= 10 + 205*cursor_y);
	assign ind_bounds[2] 	= (pixel < 130 + 205*cursor_x);
	assign ind_bounds[3] 	= (line < 30 + 205*cursor_y);
	assign indicator 		= & ind_bounds;

	// X and O square bounds
	logic [8:0] square;
	assign square[0] = (pixel >= 145 && pixel < 245 && line >= 45 && line < 145);
	assign square[1] = (pixel >= 350 && pixel < 450 && line >= 45 && line < 145);
	assign square[2] = (pixel >= 555 && pixel < 655 && line >= 45 && line < 145);
	assign square[3] = (pixel >= 145 && pixel < 245 && line >= 250 && line < 350);
	assign square[4] = (pixel >= 350 && pixel < 450 && line >= 250 && line < 350);
	assign square[5] = (pixel >= 555 && pixel < 655 && line >= 250 && line < 350);
	assign square[6] = (pixel >= 145 && pixel < 245 && line >= 455 && line < 555);
	assign square[7] = (pixel >= 350 && pixel < 450 && line >= 455 && line < 555);
	assign square[8] = (pixel >= 555 && pixel < 655 && line >= 455 && line < 555);

	// control rom output
	logic [6:0] row;
	assign row = (square[0] || square[1] || square[2])? line - 45:
				 (square[3] || square[4] || square[5])? line - 250:
				 (square[6] || square[7] || square[8])? line - 455: 0;
	logic [6:0] col;
	assign col = (square[0] || square[3] || square[6])? pixel - 145:
				 (square[1] || square[4] || square[7])? pixel - 350:
				 (square[2] || square[5] || square[8])? pixel - 555: 0;	

	// X and O shapes
	logic [8:0] X;
	logic [8:0] O;
	logic [99:0] x_data;
	logic [99:0] o_data;
	X_ROM take_x(.address(row),
				 .data(x_data));
	O_ROM take_o(.address(row),
				 .data(o_data));
	integer i;
	always_comb begin
		for (i = 0; i < 9; i++) begin
			if (square[i]) begin
				X[i] = x_data[col];
				O[i] = o_data[col];
			end else begin
				X[i] = 0;
				O[i] = 0;
			end
		end
  	end
	logic color_X;
	logic color_O;
	assign color_X = | (rX & X);
	assign color_O = | (rO & O);

	// cross lines on win
	logic [7:0] win_cross;
	assign win_cross[0] = (line >= 90 && line < 100 && bounds);
	assign win_cross[1] = (line >= 295 && line < 305 && bounds);
	assign win_cross[2] = (line >= 500 && line < 510 && bounds);
	assign win_cross[3] = (pixel >= 190 && pixel < 200 && bounds);
	assign win_cross[4] = (pixel >= 395 && pixel < 405 && bounds);
	assign win_cross[5] = (pixel >= 600 && pixel < 610 && bounds);
	assign win_cross[6] = ((pixel <= line + 105) && (pixel >= line + 95) && bounds);
	assign win_cross[7] = ((pixel + line <= 705) && (pixel + line >= 695) && bounds);
	logic color_crossX;
	logic color_crossO;
	assign color_crossX = | (win_cross & win_typeX);
	assign color_crossO = | (win_cross & win_typeO);
	
	// tie shape
	logic tie_shape;
	logic [1:0] letterT;
	logic letterI;
	logic [3:0] letterE;
	assign letterT[0] 	= (pixel >= 150 && pixel < 250 && line >= 200 && line < 250);
	assign letterT[1] 	= (pixel >= 175 && pixel < 225 && line >= 200 && line < 400);
	assign letterI 		= (pixel >= 375 && pixel < 425 && line >= 200 && line < 400);
	assign letterE[0] 	= (pixel >= 550 && pixel < 600 && line >= 200 && line < 400);
	assign letterE[1] 	= (pixel >= 550 && pixel < 650 && line >= 200 && line < 250);
	assign letterE[2] 	= (pixel >= 550 && pixel < 650 && line >= 275 && line < 325);
	assign letterE[3] 	= (pixel >= 550 && pixel < 650 && line >= 350 && line < 400);
	assign tie_shape 	= (| letterT) | letterI | (| letterE);
	logic color_tie;
	assign color_tie 	= check_tie & tie_shape;

	// colors
	logic [11:0] color;
	assign color =(!screen)? 		0:
				  (color_tie)? 		12'b1111_0000_1111:
				  (color_crossX)? 	12'b1111_1111_0000:
				  (color_crossO)? 	12'b0000_1111_1111:
				  (border)? 		12'b1111_1111_1111:
				  (indicator)? 		12'b1111_0000_0000:
				  (color_X)? 		12'b0000_1111_0000:
				  (color_O)? 		12'b0000_0000_1111: 0;
  	assign red = color[11:8];
  	assign green = color[7:4];
	assign blue = color[3:0];
endmodule

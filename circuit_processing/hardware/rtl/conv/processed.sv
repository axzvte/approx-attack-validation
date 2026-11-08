// Generated from the typed scalar IR; do not edit arithmetic by hand.
module conv_processed_core(
  input wire flip_enable,
  input wire signed [31:0] p0,
  input wire signed [31:0] p1,
  input wire signed [31:0] p2,
  input wire signed [31:0] p3,
  input wire signed [31:0] p4,
  input wire signed [31:0] p5,
  input wire signed [31:0] p6,
  input wire signed [31:0] p7,
  input wire signed [31:0] p8,
  output wire signed [31:0] y0
);
function automatic signed [63:0] round_shift64;
  input signed [63:0] v; input integer bits;
  reg signed [63:0] half;
  begin
    if (bits==0) round_shift64=v;
    else begin half=64'sd1 << (bits-1);
      if (v>=0) round_shift64=(v+half) >>> bits;
      else round_shift64=-(((-v)+half) >>> bits);
    end
  end
endfunction
wire signed [31:0] n0;
assign n0 = $signed(p0) - 32'sd128;
wire signed [31:0] n1;
assign n1 = $signed(n0) * (-32'sd1);
wire signed [31:0] n2;
assign n2 = $signed(p1) - 32'sd128;
wire signed [31:0] n3;
assign n3 = $signed(n2) * (-32'sd1);
wire signed [31:0] n4;
assign n4 = $signed(p2) - 32'sd128;
wire signed [31:0] n5;
assign n5 = $signed(n4) * (-32'sd1);
wire signed [31:0] n6;
assign n6 = $signed(p3) - 32'sd128;
wire signed [31:0] n7;
assign n7 = $signed(n6) * (-32'sd1);
wire signed [31:0] n8;
assign n8 = $signed(p4) - 32'sd128;
wire signed [31:0] n9;
assign n9 = $signed(n8) * (-32'sd1);
wire signed [31:0] n10;
assign n10 = $signed(p5) - 32'sd128;
wire signed [31:0] n11;
assign n11 = $signed(n10) * (-32'sd1);
wire signed [31:0] n12;
assign n12 = $signed(p6) - 32'sd128;
wire signed [31:0] n13;
assign n13 = $signed(n12) * (-32'sd1);
wire signed [31:0] n14;
assign n14 = $signed(p7) - 32'sd128;
wire signed [31:0] n15;
assign n15 = $signed(n14) * (-32'sd1);
wire signed [31:0] n16;
assign n16 = $signed(p8) - 32'sd128;
wire signed [31:0] n17;
assign n17 = $signed(n16) * (-32'sd1);
wire signed [31:0] n18;
assign n18 = $signed(p4) - 32'sd128;
wire signed [31:0] n19;
assign n19 = $signed(n18) * (32'sd8);
wire signed [31:0] n20_raw;
assign n20_raw = $signed(n1) + $signed(n3);
wire signed [31:0] n20 = n20_raw ^ (flip_enable ? 32'sd32 : 32'sd0);
wire signed [31:0] n21;
wire signed [31:0] n21_lhs = $signed(n5);
wire signed [31:0] n21_rhs = $signed(n7);
wire [12:0] n21_sum;
approx_5QC cell_n21(.A(n21_lhs[11:0]),.B(n21_rhs[11:0]),.Y(n21_sum));
assign n21 = $signed(n21_sum);
wire signed [31:0] n22;
assign n22 = $signed(n19) + $signed(n11);
wire signed [31:0] n23;
assign n23 = $signed(n13) + $signed(n15);
wire signed [31:0] n24;
assign n24 = $signed(n20) + $signed(n21);
wire signed [31:0] n25;
assign n25 = $signed(n22) + $signed(n23);
wire signed [31:0] n26;
assign n26 = $signed(n24) + $signed(n25);
wire signed [31:0] n27;
assign n27 = $signed(n26) + $signed(n17);
wire signed [31:0] n28;
assign n28 = ($signed(n27) < 32'sd0) ? -($signed(n27)) : $signed(n27);
wire signed [63:0] n29;
assign n29 = $signed(n28) * (64'sd1);
wire signed [63:0] n30;
assign n30 = ($signed(n29) < 64'sd0) ? 64'sd0 : (($signed(n29) > 64'sd255) ? 64'sd255 : $signed(n29));
assign y0 = $signed(n30);
endmodule

module conv_processed(
  input wire clk,rst_n,frame_start,in_valid,
  input wire [15:0] frame_width,frame_height,
  input wire [71:0] samples,
  output wire in_ready,
  output reg out_valid,
  output reg [7:0] pixel
);
wire active; wire [15:0] x,y,width,height,roi_left,roi_top,roi_right,roi_bottom;
wire accept=in_valid && in_ready;
roi_position #(.STEP(1),.L(120),.T(135),.R(193),.B(201)) position(
 .clk(clk),.rst_n(rst_n),.frame_start(frame_start),.advance(accept),
 .frame_width(frame_width),.frame_height(frame_height),.active(active),.x(x),.y(y),.width(width),.height(height),
 .roi_left(roi_left),.roi_top(roi_top),.roi_right(roi_right),.roi_bottom(roi_bottom));
assign in_ready=active && !frame_start && rst_n;
wire interior=(x >= 16'd1) && (y >= 16'd1) && ({1'b0,x}+17'd1 < {1'b0,width}) && ({1'b0,y}+17'd1 < {1'b0,height});
wire inside_roi=(x>=roi_left && x<roi_right && y>=roi_top && y<roi_bottom);
wire flip_enable=inside_roi && interior;
wire signed [31:0] core_pixel;
conv_processed_core arithmetic(.flip_enable(flip_enable),
 .p0($signed({24'd0,samples[0 +: 8]})),
 .p1($signed({24'd0,samples[8 +: 8]})),
 .p2($signed({24'd0,samples[16 +: 8]})),
 .p3($signed({24'd0,samples[24 +: 8]})),
 .p4($signed({24'd0,samples[32 +: 8]})),
 .p5($signed({24'd0,samples[40 +: 8]})),
 .p6($signed({24'd0,samples[48 +: 8]})),
 .p7($signed({24'd0,samples[56 +: 8]})),
 .p8($signed({24'd0,samples[64 +: 8]})),
 .y0(core_pixel));
always @(posedge clk or negedge rst_n) begin
 if(!rst_n) begin out_valid<=0; pixel<=0; end
 else if(frame_start) begin out_valid<=0; pixel<=0; end
 else begin out_valid<=accept;
   if(accept) pixel<=interior ? core_pixel[7:0] : samples[32 +: 8];
 end
end
endmodule

// Generated from the typed scalar IR; do not edit arithmetic by hand.
module sobel_baseline_core(
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
assign n0 = $signed(p5) * (32'sd2);
wire signed [31:0] n1;
wire signed [31:0] n1_lhs = $signed(p2);
wire signed [31:0] n1_rhs = $signed(n0);
wire [12:0] n1_sum;
approx_5QC cell_n1(.A(n1_lhs[11:0]),.B(n1_rhs[11:0]),.Y(n1_sum));
assign n1 = $signed(n1_sum);
wire signed [31:0] n2;
assign n2 = $signed(n1) + $signed(p8);
wire signed [31:0] n3;
assign n3 = $signed(p3) * (32'sd2);
wire signed [31:0] n4;
wire signed [31:0] n4_lhs = $signed(p0);
wire signed [31:0] n4_rhs = $signed(n3);
wire [12:0] n4_sum;
approx_5L8 cell_n4(.A(n4_lhs[11:0]),.B(n4_rhs[11:0]),.Y(n4_sum));
assign n4 = $signed(n4_sum);
wire signed [31:0] n5;
assign n5 = $signed(n4) + $signed(p6);
wire signed [31:0] n6;
assign n6 = $signed(n5) * (-32'sd1);
wire signed [31:0] n7;
wire signed [31:0] n7_lhs = $signed(n2);
wire signed [31:0] n7_rhs = $signed(n6);
wire [12:0] n7_sum;
approx_5RP cell_n7(.A(n7_lhs[11:0]),.B(n7_rhs[11:0]),.Y(n7_sum));
assign n7 = $signed(n7_sum);
wire signed [31:0] n8;
assign n8 = $signed(p7) * (32'sd2);
wire signed [31:0] n9;
assign n9 = $signed(p6) + $signed(n8);
wire signed [31:0] n10;
assign n10 = $signed(n9) + $signed(p8);
wire signed [31:0] n11;
assign n11 = $signed(p1) * (32'sd2);
wire signed [31:0] n12;
wire signed [31:0] n12_lhs = $signed(p0);
wire signed [31:0] n12_rhs = $signed(n11);
wire [12:0] n12_sum;
approx_5TE cell_n12(.A(n12_lhs[11:0]),.B(n12_rhs[11:0]),.Y(n12_sum));
assign n12 = $signed(n12_sum);
wire signed [31:0] n13;
assign n13 = $signed(n12) + $signed(p2);
wire signed [31:0] n14;
assign n14 = $signed(n13) * (-32'sd1);
wire signed [31:0] n15;
wire signed [31:0] n15_lhs = $signed(n10);
wire signed [31:0] n15_rhs = $signed(n14);
wire [12:0] n15_sum;
approx_5QC cell_n15(.A(n15_lhs[11:0]),.B(n15_rhs[11:0]),.Y(n15_sum));
assign n15 = $signed(n15_sum);
wire signed [31:0] n16;
assign n16 = ($signed(n7) < 32'sd0) ? -($signed(n7)) : $signed(n7);
wire signed [31:0] n17;
assign n17 = ($signed(n15) < 32'sd0) ? -($signed(n15)) : $signed(n15);
wire signed [31:0] n18;
assign n18 = $signed(n16) + $signed(n17);
wire signed [63:0] n19;
assign n19 = $signed(n18) * (64'sd1);
wire signed [63:0] n20;
assign n20 = ($signed(n19) < 64'sd0) ? 64'sd0 : (($signed(n19) > 64'sd255) ? 64'sd255 : $signed(n19));
assign y0 = $signed(n20);
endmodule

// Area top: preassembled windows and combinational output in every variant.
module sobel_baseline(
 input wire [71:0] samples,
 output wire [7:0] pixel
);
wire signed [31:0] core_pixel;
sobel_baseline_core arithmetic(.flip_enable(1'b0),
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
assign pixel=core_pixel[7:0];
endmodule

// Generated from the typed scalar IR; do not edit arithmetic by hand.
module sharpen_baseline_core(
  input wire flip_enable,
  input wire signed [31:0] p0,
  input wire signed [31:0] p1,
  input wire signed [31:0] p2,
  input wire signed [31:0] p3,
  input wire signed [31:0] p4,
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
assign n0 = $signed(p0) * (32'sd5);
wire signed [31:0] n1;
assign n1 = $signed(p1) * (-32'sd1);
wire signed [31:0] n2;
wire signed [31:0] n2_lhs = $signed(n0);
wire signed [31:0] n2_rhs = $signed(n1);
wire [12:0] n2_sum;
approx_5QC cell_n2(.A(n2_lhs[11:0]),.B(n2_rhs[11:0]),.Y(n2_sum));
assign n2 = $signed(n2_sum);
wire signed [31:0] n3;
assign n3 = $signed(p2) * (-32'sd1);
wire signed [31:0] n4;
wire signed [31:0] n4_lhs = $signed(n2);
wire signed [31:0] n4_rhs = $signed(n3);
wire [12:0] n4_sum;
approx_5L8 cell_n4(.A(n4_lhs[11:0]),.B(n4_rhs[11:0]),.Y(n4_sum));
assign n4 = $signed(n4_sum);
wire signed [31:0] n5;
assign n5 = $signed(p3) * (-32'sd1);
wire signed [31:0] n6;
wire signed [31:0] n6_lhs = $signed(n4);
wire signed [31:0] n6_rhs = $signed(n5);
wire [12:0] n6_sum;
approx_5RP cell_n6(.A(n6_lhs[11:0]),.B(n6_rhs[11:0]),.Y(n6_sum));
assign n6 = $signed(n6_sum);
wire signed [31:0] n7;
assign n7 = $signed(p4) * (-32'sd1);
wire signed [31:0] n8;
wire signed [31:0] n8_lhs = $signed(n6);
wire signed [31:0] n8_rhs = $signed(n7);
wire [12:0] n8_sum;
approx_5TE cell_n8(.A(n8_lhs[11:0]),.B(n8_rhs[11:0]),.Y(n8_sum));
assign n8 = $signed(n8_sum);
wire signed [63:0] n9;
assign n9 = $signed(n8) * (64'sd1);
wire signed [63:0] n10;
assign n10 = ($signed(n9) < 64'sd0) ? 64'sd0 : (($signed(n9) > 64'sd255) ? 64'sd255 : $signed(n9));
assign y0 = $signed(n10);
endmodule

// Area top: preassembled windows and combinational output in every variant.
module sharpen_baseline(
 input wire [39:0] samples,
 output wire [7:0] pixel
);
wire signed [31:0] core_pixel;
sharpen_baseline_core arithmetic(.flip_enable(1'b0),
 .p0($signed({24'd0,samples[0 +: 8]})),
 .p1($signed({24'd0,samples[8 +: 8]})),
 .p2($signed({24'd0,samples[16 +: 8]})),
 .p3($signed({24'd0,samples[24 +: 8]})),
 .p4($signed({24'd0,samples[32 +: 8]})),
 .y0(core_pixel));
assign pixel=core_pixel[7:0];
endmodule

module sobel_baseline(
  input wire [7:0] p1,
  input wire [7:0] p2,
  input wire [7:0] p3,
  input wire [7:0] p4,
  input wire [7:0] p6,
  input wire [7:0] p7,
  input wire [7:0] p8,
  input wire [7:0] p9,
  output wire [7:0] pixel
);
  // One arithmetic kernel; no full-frame buffering or runtime configuration storage.
  wire signed [12:0] n0_a = p3;
  wire signed [12:0] n0_b = 2*p6;
  wire [12:0] n0_baseline_raw;
  evo_5RP base0(.A(n0_a[11:0]),.B(n0_b[11:0]),.Y(n0_baseline_raw));
  wire signed [12:0] n0_base = $signed(n0_baseline_raw);
  wire signed [12:0] n0 = n0_base;
  wire signed [12:0] n1_a = n0;
  wire signed [12:0] n1_b = p9;
  wire signed [12:0] n1 = n1_a + n1_b;
  wire signed [12:0] n2_a = p1;
  wire signed [12:0] n2_b = 2*p4;
  wire [12:0] n2_baseline_raw;
  evo_5RP base2(.A(n2_a[11:0]),.B(n2_b[11:0]),.Y(n2_baseline_raw));
  wire signed [12:0] n2_base = $signed(n2_baseline_raw);
  wire signed [12:0] n2 = n2_base;
  wire signed [12:0] n3_a = n2;
  wire signed [12:0] n3_b = p7;
  wire signed [12:0] n3 = n3_a + n3_b;
  wire signed [12:0] n4_a = n1;
  wire signed [12:0] n4_b = -n3;
  wire [12:0] n4_baseline_raw;
  evo_5RP base4(.A(n4_a[11:0]),.B(n4_b[11:0]),.Y(n4_baseline_raw));
  wire signed [12:0] n4_base = $signed(n4_baseline_raw);
  wire signed [12:0] n4 = n4_base;
  wire signed [12:0] n5_a = p7;
  wire signed [12:0] n5_b = 2*p8;
  wire signed [12:0] n5 = n5_a + n5_b;
  wire signed [12:0] n6_a = n5;
  wire signed [12:0] n6_b = p9;
  wire signed [12:0] n6 = n6_a + n6_b;
  wire signed [12:0] n7_a = p1;
  wire signed [12:0] n7_b = 2*p2;
  wire [12:0] n7_baseline_raw;
  evo_5RP base7(.A(n7_a[11:0]),.B(n7_b[11:0]),.Y(n7_baseline_raw));
  wire signed [12:0] n7_base = $signed(n7_baseline_raw);
  wire signed [12:0] n7 = n7_base;
  wire signed [12:0] n8_a = n7;
  wire signed [12:0] n8_b = p3;
  wire signed [12:0] n8 = n8_a + n8_b;
  wire signed [12:0] n9_a = n6;
  wire signed [12:0] n9_b = -n8;
  wire [12:0] n9_baseline_raw;
  evo_5RP base9(.A(n9_a[11:0]),.B(n9_b[11:0]),.Y(n9_baseline_raw));
  wire signed [12:0] n9_base = $signed(n9_baseline_raw);
  wire signed [12:0] n9 = n9_base;
  wire signed [12:0] n10_a = (n4 < 0 ? -n4 : n4);
  wire signed [12:0] n10_b = (n9 < 0 ? -n9 : n9);
  wire signed [12:0] n10 = n10_a + n10_b;
  assign pixel = n10 < 13'sd0 ? 8'd0 : (n10 > 13'sd255 ? 8'd255 : n10[7:0]);
endmodule

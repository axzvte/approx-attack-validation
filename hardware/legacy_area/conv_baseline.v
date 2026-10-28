module conv_baseline(
  input wire [7:0] p1,
  input wire [7:0] p2,
  input wire [7:0] p3,
  input wire [7:0] p4,
  input wire [7:0] p5,
  input wire [7:0] p6,
  input wire [7:0] p7,
  input wire [7:0] p8,
  input wire [7:0] p9,
  output wire [7:0] pixel
);
  // One arithmetic kernel; no full-frame buffering or runtime configuration storage.
  wire signed [12:0] centered0 = $signed({5'b0,p1}) - 13'sd128;
  wire signed [12:0] w0 = -1 * centered0;
  wire signed [12:0] centered1 = $signed({5'b0,p2}) - 13'sd128;
  wire signed [12:0] w1 = -1 * centered1;
  wire signed [12:0] centered2 = $signed({5'b0,p3}) - 13'sd128;
  wire signed [12:0] w2 = -1 * centered2;
  wire signed [12:0] centered3 = $signed({5'b0,p4}) - 13'sd128;
  wire signed [12:0] w3 = -1 * centered3;
  wire signed [12:0] centered4 = $signed({5'b0,p5}) - 13'sd128;
  wire signed [12:0] w4 = 8 * centered4;
  wire signed [12:0] centered5 = $signed({5'b0,p6}) - 13'sd128;
  wire signed [12:0] w5 = -1 * centered5;
  wire signed [12:0] centered6 = $signed({5'b0,p7}) - 13'sd128;
  wire signed [12:0] w6 = -1 * centered6;
  wire signed [12:0] centered7 = $signed({5'b0,p8}) - 13'sd128;
  wire signed [12:0] w7 = -1 * centered7;
  wire signed [12:0] centered8 = $signed({5'b0,p9}) - 13'sd128;
  wire signed [12:0] w8 = -1 * centered8;
  wire signed [12:0] n0_a = w0;
  wire signed [12:0] n0_b = w1;
  wire signed [12:0] n0 = n0_a + n0_b;
  wire signed [12:0] n1_a = w2;
  wire signed [12:0] n1_b = w3;
  wire [12:0] n1_baseline_raw;
  evo_5RP base1(.A(n1_a[11:0]),.B(n1_b[11:0]),.Y(n1_baseline_raw));
  wire signed [12:0] n1_base = $signed(n1_baseline_raw);
  wire signed [12:0] n1 = n1_base;
  wire signed [12:0] n2_a = w4;
  wire signed [12:0] n2_b = w5;
  wire [12:0] n2_baseline_raw;
  evo_5RP base2(.A(n2_a[11:0]),.B(n2_b[11:0]),.Y(n2_baseline_raw));
  wire signed [12:0] n2_base = $signed(n2_baseline_raw);
  wire signed [12:0] n2 = n2_base;
  wire signed [12:0] n3_a = w6;
  wire signed [12:0] n3_b = w7;
  wire signed [12:0] n3 = n3_a + n3_b;
  wire signed [12:0] n4_a = n0;
  wire signed [12:0] n4_b = n1;
  wire [12:0] n4_baseline_raw;
  evo_5RP base4(.A(n4_a[11:0]),.B(n4_b[11:0]),.Y(n4_baseline_raw));
  wire signed [12:0] n4_base = $signed(n4_baseline_raw);
  wire signed [12:0] n4 = n4_base;
  wire signed [12:0] n5_a = n2;
  wire signed [12:0] n5_b = n3;
  wire signed [12:0] n5 = n5_a + n5_b;
  wire signed [12:0] n6_a = n4;
  wire signed [12:0] n6_b = n5;
  wire signed [12:0] n6 = n6_a + n6_b;
  wire signed [12:0] n7_a = n6;
  wire signed [12:0] n7_b = w8;
  wire [12:0] n7_baseline_raw;
  evo_5RP base7(.A(n7_a[11:0]),.B(n7_b[11:0]),.Y(n7_baseline_raw));
  wire signed [12:0] n7_base = $signed(n7_baseline_raw);
  wire signed [12:0] n7 = n7_base;
  wire signed [12:0] magnitude = n7 < 0 ? -n7 : n7;
  assign pixel = magnitude > 13'sd255 ? 8'd255 : magnitude[7:0];
endmodule

module sharpen_baseline(
  input wire [7:0] center,
  input wire [7:0] top,
  input wire [7:0] bottom,
  input wire [7:0] left,
  input wire [7:0] right,
  output wire [7:0] pixel
);
  // One arithmetic kernel; no full-frame buffering or runtime configuration storage.
  wire signed [12:0] c = $signed({5'b0,center});
  wire signed [12:0] t = $signed({5'b0,top});
  wire signed [12:0] b = $signed({5'b0,bottom});
  wire signed [12:0] l = $signed({5'b0,left});
  wire signed [12:0] r = $signed({5'b0,right});
  wire signed [12:0] n0_a = (c <<< 2) + c;
  wire signed [12:0] n0_b = -t;
  wire [12:0] n0_baseline_raw;
  evo_5RP base0(.A(n0_a[11:0]),.B(n0_b[11:0]),.Y(n0_baseline_raw));
  wire signed [12:0] n0_base = $signed(n0_baseline_raw);
  wire signed [12:0] n0 = n0_base;
  wire signed [12:0] n1_a = n0;
  wire signed [12:0] n1_b = -b;
  wire [12:0] n1_baseline_raw;
  evo_5RP base1(.A(n1_a[11:0]),.B(n1_b[11:0]),.Y(n1_baseline_raw));
  wire signed [12:0] n1_base = $signed(n1_baseline_raw);
  wire signed [12:0] n1 = n1_base;
  wire signed [12:0] n2_a = n1;
  wire signed [12:0] n2_b = -l;
  wire [12:0] n2_baseline_raw;
  evo_5RP base2(.A(n2_a[11:0]),.B(n2_b[11:0]),.Y(n2_baseline_raw));
  wire signed [12:0] n2_base = $signed(n2_baseline_raw);
  wire signed [12:0] n2 = n2_base;
  wire signed [12:0] n3_a = n2;
  wire signed [12:0] n3_b = -r;
  wire [12:0] n3_baseline_raw;
  evo_5RP base3(.A(n3_a[11:0]),.B(n3_b[11:0]),.Y(n3_baseline_raw));
  wire signed [12:0] n3_base = $signed(n3_baseline_raw);
  wire signed [12:0] n3 = n3_base;
  assign pixel = n3 < 13'sd0 ? 8'd0 : (n3 > 13'sd255 ? 8'd255 : n3[7:0]);
endmodule

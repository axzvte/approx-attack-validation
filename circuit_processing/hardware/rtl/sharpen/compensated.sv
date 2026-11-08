// Generated from the typed scalar IR; do not edit arithmetic by hand.
module sharpen_compensated_core(
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
assign n4 = $signed(n2) + $signed(n3);
wire signed [31:0] n5;
assign n5 = $signed(p3) * (-32'sd1);
wire signed [31:0] n6;
assign n6 = $signed(n4) + $signed(n5);
wire signed [31:0] n7;
assign n7 = $signed(p4) * (-32'sd1);
wire signed [31:0] n8;
assign n8 = $signed(n6) + $signed(n7);
wire signed [63:0] n9;
assign n9 = $signed(n8) * (64'sd1);
wire signed [63:0] n10;
assign n10 = ($signed(n9) < 64'sd0) ? 64'sd0 : (($signed(n9) > 64'sd255) ? 64'sd255 : $signed(n9));
assign y0 = $signed(n10);
endmodule

module sharpen_compensated(
  input wire clk,rst_n,frame_start,in_valid,
  input wire [15:0] frame_width,frame_height,
  input wire [39:0] samples,
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
wire flip_enable=1'b0;
wire signed [31:0] core_pixel;
sharpen_compensated_core arithmetic(.flip_enable(flip_enable),
 .p0($signed({24'd0,samples[0 +: 8]})),
 .p1($signed({24'd0,samples[8 +: 8]})),
 .p2($signed({24'd0,samples[16 +: 8]})),
 .p3($signed({24'd0,samples[24 +: 8]})),
 .p4($signed({24'd0,samples[32 +: 8]})),
 .y0(core_pixel));
always @(posedge clk or negedge rst_n) begin
 if(!rst_n) begin out_valid<=0; pixel<=0; end
 else if(frame_start) begin out_valid<=0; pixel<=0; end
 else begin out_valid<=accept;
   if(accept) pixel<=interior ? core_pixel[7:0] : samples[0 +: 8];
 end
end
endmodule

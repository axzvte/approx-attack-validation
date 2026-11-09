// Pixel-space half-open ROI bounds are supplied at frame_start.
module roi_position #(
  parameter integer STEP=1, L=120,T=135,R=193,B=201
)(
  input wire clk,rst_n,frame_start,advance,
  input wire [15:0] frame_width,frame_height,
  input wire [15:0] frame_roi_left,frame_roi_top,frame_roi_right,frame_roi_bottom,
  output reg active,
  output reg [15:0] x,y,width,height,
  output reg [15:0] roi_left,roi_top,roi_right,roi_bottom
);
always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin active<=0; x<=0; y<=0; width<=0; height<=0;
    roi_left<=0; roi_top<=0; roi_right<=0; roi_bottom<=0;
  end else if(frame_start) begin
    active<=(frame_width!=0 && frame_height!=0); x<=0; y<=0;
    width<=frame_width; height<=frame_height;
    roi_left<=frame_roi_left; roi_top<=frame_roi_top;
    roi_right<=frame_roi_right; roi_bottom<=frame_roi_bottom;
  end else if(advance && active) begin
    if({1'b0,x}+STEP < {1'b0,width}) x<=x+STEP;
    else begin x<=0;
      if({1'b0,y}+STEP < {1'b0,height}) y<=y+STEP;
      else active<=0;
    end
  end
end
endmodule

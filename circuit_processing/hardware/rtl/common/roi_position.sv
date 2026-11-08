// Normalized half-open ROI is projected by pixel-center membership.
module roi_position #(
  parameter integer STEP=1, L=120,T=135,R=193,B=201
)(
  input wire clk,rst_n,frame_start,advance,
  input wire [15:0] frame_width,frame_height,
  output reg active,
  output reg [15:0] x,y,width,height,
  output reg [15:0] roi_left,roi_top,roi_right,roi_bottom
);
function automatic [15:0] project_edge;
  input integer coordinate; input [15:0] extent;
  reg [31:0] numerator;
  begin numerator=2*coordinate*{16'd0,extent}+32'd319;
    project_edge=numerator/32'd640;
  end
endfunction
always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin active<=0; x<=0; y<=0; width<=0; height<=0;
    roi_left<=0; roi_top<=0; roi_right<=0; roi_bottom<=0;
  end else if(frame_start) begin
    active<=(frame_width!=0 && frame_height!=0); x<=0; y<=0;
    width<=frame_width; height<=frame_height;
    roi_left<=project_edge(L,frame_width); roi_top<=project_edge(T,frame_height);
    roi_right<=project_edge(R,frame_width); roi_bottom<=project_edge(B,frame_height);
  end else if(advance && active) begin
    if({1'b0,x}+STEP < {1'b0,width}) x<=x+STEP;
    else begin x<=0;
      if({1'b0,y}+STEP < {1'b0,height}) y<=y+STEP;
      else active<=0;
    end
  end
end
endmodule

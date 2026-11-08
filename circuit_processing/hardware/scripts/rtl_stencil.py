"""Coordinate control and registered stencil wrappers, shared by all variants."""
from rtl_emit import emit_graph


def position_rtl():
    return '''// Normalized half-open ROI is projected by pixel-center membership.
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
'''


def emit_stencil(profile, plan, app, version, bbox):
    if profile['ir']['tile']!=[1,1] or len(profile['ir']['outputs'])!=1:
        raise ValueError('Stencil RTL requires one pixel output')
    if version not in ('baseline','compensated','processed'): raise ValueError('Unknown RTL version')
    if plan['mode']!='ir_internal': raise ValueError('RTL generator requires an internal certified configuration')
    module=f'{app}_{version}'
    count=len(profile['ir']['inputs'])
    core=emit_graph(profile,module+'_core',[] if version=='baseline' else plan['exact_nodes'],
                    plan['node'] if version=='processed' else None,plan['bit'])
    ins=profile['ir']['inputs']
    minx,miny=min(x['dx'] for x in ins),min(x['dy'] for x in ins)
    maxx,maxy=max(x['dx'] for x in ins),max(x['dy'] for x in ins)
    l,t,r,b=bbox
    ports=['input wire clk,rst_n,frame_start,in_valid','input wire [15:0] frame_width,frame_height',
           f'input wire [{8*count-1}:0] samples','output wire in_ready','output reg out_valid','output reg [7:0] pixel']
    body=[f'module {module}(\n  '+',\n  '.join(ports)+'\n);',
        'wire active; wire [15:0] x,y,width,height,roi_left,roi_top,roi_right,roi_bottom;',
        'wire accept=in_valid && in_ready;',
        f'roi_position #(.STEP(1),.L({l}),.T({t}),.R({r}),.B({b})) position(',
        ' .clk(clk),.rst_n(rst_n),.frame_start(frame_start),.advance(accept),',
        ' .frame_width(frame_width),.frame_height(frame_height),.active(active),.x(x),.y(y),.width(width),.height(height),',
        ' .roi_left(roi_left),.roi_top(roi_top),.roi_right(roi_right),.roi_bottom(roi_bottom));',
        'assign in_ready=active && !frame_start && rst_n;',
        f"wire interior=(x >= 16'd{-minx}) && (y >= 16'd{-miny}) && ({{1'b0,x}}+17'd{maxx} < {{1'b0,width}}) && ({{1'b0,y}}+17'd{maxy} < {{1'b0,height}});",
        'wire inside_roi=(x>=roi_left && x<roi_right && y>=roi_top && y<roi_bottom);',
        'wire flip_enable='+('inside_roi && interior;' if version=='processed' else "1'b0;"),
        'wire signed [31:0] core_pixel;',f'{module}_core arithmetic(.flip_enable(flip_enable),']
    body += [f" .{x['id']}($signed({{24'd0,samples[{8*i} +: 8]}}))," for i,x in enumerate(ins)]
    body+=[' .y0(core_pixel));']
    if profile['ir'].get('border_output','input')=='zero': border="8'd0"
    else:
        center=next(i for i,x in enumerate(ins) if x['dx']==0 and x['dy']==0)
        border=f'samples[{8*center} +: 8]'
    body += ['always @(posedge clk or negedge rst_n) begin',
        ' if(!rst_n) begin out_valid<=0; pixel<=0; end',
        ' else if(frame_start) begin out_valid<=0; pixel<=0; end',
        ' else begin out_valid<=accept;',f'   if(accept) pixel<=interior ? core_pixel[7:0] : {border};',
        ' end','end','endmodule','']
    return core+'\n'+'\n'.join(body)

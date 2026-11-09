"""Core-level area wrappers with externally projected frame bounds."""
from rtl_emit import emit_graph


FRAME_PORTS = """ input wire [15:0] frame_width,frame_height,
 input wire [15:0] frame_roi_left,frame_roi_top,frame_roi_right,frame_roi_bottom,
"""

POSITION_WIRES = """wire active;
wire [15:0] x,y,width,height,roi_left,roi_top,roi_right,roi_bottom;
"""


def area_position(step):
    return f"""roi_position #(.STEP({step})) position(
 .clk(clk),.rst_n(rst_n),.frame_start(frame_start),.advance(accept),
 .frame_width(frame_width),.frame_height(frame_height),.active(active),
 .frame_roi_left(frame_roi_left),.frame_roi_top(frame_roi_top),
 .frame_roi_right(frame_roi_right),.frame_roi_bottom(frame_roi_bottom),
 .x(x),.y(y),.width(width),.height(height),
 .roi_left(roi_left),.roi_top(roi_top),.roi_right(roi_right),.roi_bottom(roi_bottom));
"""


def emit_area_stencil(profile, plan, app, version):
    """Wrap the same window datapath in all variants and add ROI control to C."""
    top = f'{app}_{version}'
    processed = version == 'processed'
    exact_nodes = [] if version == 'baseline' else plan['exact_nodes']
    core = emit_graph(profile, top+'_core', exact_nodes,
                      plan['node'] if processed else None, plan['bit'])
    inputs = profile['ir']['inputs']
    ports = control = ''
    enable = "1'b0"
    if processed:
        ports = ' input wire clk,rst_n,frame_start,in_valid,\n'+FRAME_PORTS+' output wire in_ready,\n'
        left = -min(item['dx'] for item in inputs)
        right = max(item['dx'] for item in inputs)
        top_edge = -min(item['dy'] for item in inputs)
        bottom = max(item['dy'] for item in inputs)
        control = f"""wire accept=in_valid && in_ready;
{POSITION_WIRES}{area_position(1)}assign in_ready=active && rst_n && !frame_start;
wire interior=(x>=16'd{left}) && (y>=16'd{top_edge}) &&
 ({{1'b0,x}}+17'd{right}<{{1'b0,width}}) && ({{1'b0,y}}+17'd{bottom}<{{1'b0,height}});
wire flip_enable=accept && interior && x>=roi_left && x<roi_right && y>=roi_top && y<roi_bottom;
"""
        enable = 'flip_enable'
    operands = ''.join(f" .{item['id']}($signed({{24'd0,samples[{8*i} +: 8]}})),\n"
                       for i, item in enumerate(inputs))
    return core+f"""
// Area top: preassembled windows and combinational output in every variant.
module {top}(
{ports} input wire [{8*len(inputs)-1}:0] samples,
 output wire [7:0] pixel
);
{control}wire signed [31:0] core_pixel;
{top}_core arithmetic(.flip_enable({enable}),
{operands} .y0(core_pixel));
assign pixel=core_pixel[7:0];
endmodule
"""

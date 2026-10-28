// Fixed monitor/bit payload; configuration must be loaded before use.
// MONITOR_WIDTH is 12 (input) or 13 (pre-flip output); FLIP_BIT is 0..11.
module bitflip_trigger #(
    parameter integer MONITOR_WIDTH = 12,
    parameter integer FLIP_BIT = 0
) (
    input wire signed [MONITOR_WIDTH-1:0] monitor_value,
    input wire signed [MONITOR_WIDTH-1:0] lower,
    input wire signed [MONITOR_WIDTH-1:0] upper,
    input wire [12:0] baseline_value,
    output wire [12:0] attacked_value
);
    localparam [12:0] FLIP_MASK = 13'b1 << FLIP_BIT;
    wire active = (monitor_value >= lower) && (monitor_value <= upper);
    assign attacked_value = baseline_value ^ (active ? FLIP_MASK : 13'b0);
endmodule

// One parallel load, no reset, no double buffering. Hold when cfg_load is low.
module bitflip_bounds #(
    parameter integer WIDTH = 12
) (
    input wire clk,
    input wire cfg_load,
    input wire signed [WIDTH-1:0] cfg_lower,
    input wire signed [WIDTH-1:0] cfg_upper,
    output reg signed [WIDTH-1:0] lower,
    output reg signed [WIDTH-1:0] upper
);
    always @(posedge clk) begin
        if (cfg_load) begin
            lower <= cfg_lower;
            upper <= cfg_upper;
        end
    end
endmodule

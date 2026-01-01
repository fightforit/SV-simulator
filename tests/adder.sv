module adder #(parameter WIDTH = 8) (
    input logic clk,
    input logic rstn,
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    output logic [WIDTH-1:0] sum
);
    logic [WIDTH-1:0] wSum;

    assign wSum = a + b;

    always_ff @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            sum <= '0;
        end else begin
            sum <= wSum;
        end
    end
    
endmodule
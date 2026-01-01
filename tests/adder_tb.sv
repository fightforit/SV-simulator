module adder_tb();

    parameter CLK_PERIOD = 10;
    parameter WIDTH = 8;

    logic clk = 1'b0;
    initial forever #(CLK_PERIOD/2) clk = ~clk;  

    // module instantiation
    logic rstn;
    logic [WIDTH-1:0] a;
    logic [WIDTH-1:0] b;
    logic [WIDTH-1:0] sum;
    adder #(
        .WIDTH(WIDTH)
     ) adder (
        .clk (clk),
        .rstn(rstn),
        .a   (a),
        .b   (b),
        .sum (sum)
    );

    // signals initialization
    initial begin
        rstn = 1'b0;
        #(CLK_PERIOD) rstn = 1'b1;
        a = '0;
        b = '0;

        #(CLK_PERIOD);
        a = 8'd15;
        b = 8'd10;

        #(CLK_PERIOD);
        a = 8'd25;
        b = 8'd30;

        #(CLK_PERIOD);
        $finish();
    end

    // monitor
    initial begin
        $monitor("Time: %0t | rstn: %b | a: %d | b: %d | sum: %d", $time, rstn, a, b, sum);
    end

endmodule
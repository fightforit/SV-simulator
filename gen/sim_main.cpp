#include "sim/runtime.h"
#include "mult.cpp"
#include "adder.cpp"
#include "adder_tb.cpp"

int main() {
    sim::Kernel kernel;
    gen::adder_tb top(kernel);
    kernel.run();
    return 0;
}

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "sim/runtime.h"

namespace gen {

class adder {
public:
    adder(sim::Kernel& kernel, sim::Signal& clk, sim::Signal& rstn, sim::Signal& a, sim::Signal& b, sim::Signal& sum, uint32_t WIDTH = 8)
        : kernel(kernel), clk(clk), rstn(rstn), a(a), b(b), sum(sum), wSum(8) {
        kernel.register_continuous([this]() { eval_comb_0(); }, {&a, &b});
        kernel.register_edge([this]() { eval_ff_0(); },         {{&clk, sim::Edge::Pos}, {&rstn, sim::Edge::Neg}});
    }

private:
    sim::Kernel& kernel;
    sim::Signal& clk; // input
    sim::Signal& rstn; // input
    sim::Signal& a; // input
    sim::Signal& b; // input
    sim::Signal& sum; // output
    sim::Signal wSum;

    void eval_comb_0() {
        wSum.set((a.value() + b.value()));
    }

    void eval_ff_0() {
        if ((!rstn.value())) {
            kernel.nba_assign(sum, 0);
        } else {
            kernel.nba_assign(sum, wSum.value());
        }
    }
};

} // namespace gen

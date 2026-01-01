#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "sim/runtime.h"

namespace gen {

class adder_tb {
public:
    adder_tb(sim::Kernel& kernel, uint32_t CLK_PERIOD = 10, uint32_t WIDTH = 8)
        : kernel(kernel), clk(1), rstn(1), a(8), b(8), sum(8), adder_inst(kernel, clk, rstn, a, b, sum) {
        {
            auto tick = std::make_shared<std::function<void()>>();
            *tick = [this, tick]() {
                clk.set((~clk.value()));
                this->kernel.schedule_at(this->kernel.time() + static_cast<uint64_t>((10 / 2)), *tick);
            };
            kernel.schedule_at(static_cast<uint64_t>((10 / 2)), *tick);
        }
        {
            uint64_t t1 = 0;
            kernel.schedule_at(t1, [this]() {
                rstn.set(0);
            });
            t1 += static_cast<uint64_t>(10);
            kernel.schedule_at(t1, [this]() {
                rstn.set(1);
            });
            kernel.schedule_at(t1, [this]() {
                a.set(0);
            });
            kernel.schedule_at(t1, [this]() {
                b.set(0);
            });
            t1 += static_cast<uint64_t>(10);
            kernel.schedule_at(t1, [this]() {
                a.set(15);
            });
            kernel.schedule_at(t1, [this]() {
                b.set(10);
            });
            t1 += static_cast<uint64_t>(10);
            kernel.schedule_at(t1, [this]() {
                a.set(25);
            });
            kernel.schedule_at(t1, [this]() {
                b.set(30);
            });
            t1 += static_cast<uint64_t>(10);
            kernel.schedule_at(t1, [this]() { this->kernel.finish(); });
        }
        {
            uint64_t t2 = 0;
            kernel.schedule_at(t2, [this]() {
                this->kernel.register_monitor("Time: %0t | rstn: %b | a: %d | b: %d | sum: %d", {sim::MonitorArg::time(), sim::MonitorArg::signalArg(&rstn), sim::MonitorArg::signalArg(&a), sim::MonitorArg::signalArg(&b), sim::MonitorArg::signalArg(&sum)});
            });
        }
    }

private:
    sim::Kernel& kernel;
    sim::Signal clk;
    sim::Signal rstn;
    sim::Signal a;
    sim::Signal b;
    sim::Signal sum;
    adder adder_inst;
};

} // namespace gen

#include "sim/runtime.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace sim {

namespace {

uint64_t maskToWidth(uint64_t value, uint32_t width) {
    if (width >= 64)
        return value;
    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
    return value & mask;
}

} // namespace

Signal::Signal(uint32_t width) : width_(width ? width : 1) {}

void Signal::attach(Kernel* kernel) {
    if (!kernel_)
        kernel_ = kernel;
}

void Signal::set(uint64_t value) {
    uint64_t masked = maskToWidth(value, width_);
    if (value_ == masked)
        return;

    uint64_t old = value_;
    value_ = masked;

    if (kernel_)
        kernel_->onSignalChange(*this, old, masked);
}

void Kernel::register_continuous(Callback cb, const std::vector<Signal*>& deps) {
    auto proc = std::make_unique<Process>();
    proc->run = std::move(cb);

    for (auto* sig : deps) {
        if (!sig)
            continue;
        sig->attach(this);
        sig->levelSensitive.push_back(proc.get());
    }

    scheduleProcess(*proc, currentTime);
    processes.push_back(std::move(proc));
}

void Kernel::register_edge(Callback cb, const std::vector<EdgeEvent>& deps) {
    auto proc = std::make_unique<Process>();
    proc->run = std::move(cb);

    for (const auto& dep : deps) {
        if (!dep.signal)
            continue;
        dep.signal->attach(this);
        switch (dep.edge) {
            case Edge::Pos:
                dep.signal->posedgeSensitive.push_back(proc.get());
                break;
            case Edge::Neg:
                dep.signal->negedgeSensitive.push_back(proc.get());
                break;
            case Edge::Any:
            default:
                dep.signal->levelSensitive.push_back(proc.get());
                break;
        }
    }

    processes.push_back(std::move(proc));
}

void Kernel::register_monitor(const std::string& format, const std::vector<MonitorArg>& args) {
    auto mon = std::make_unique<Monitor>();
    mon->format = format;
    mon->args = args;

    auto proc = std::make_unique<Process>();
    proc->run = [this, monPtr = mon.get()]() {
        std::string out;
        size_t argIndex = 0;
        for (size_t i = 0; i < monPtr->format.size(); ++i) {
            if (monPtr->format[i] != '%' || i + 1 >= monPtr->format.size()) {
                out.push_back(monPtr->format[i]);
                continue;
            }
            if (monPtr->format[i + 1] == '%') {
                out.push_back('%');
                i++;
                continue;
            }
            std::string spec;
            spec.push_back(monPtr->format[i + 1]);
            if (monPtr->format[i + 1] == '0' && i + 2 < monPtr->format.size()) {
                spec.push_back(monPtr->format[i + 2]);
                i++;
            }
            i++;
            if (argIndex >= monPtr->args.size())
                continue;
            const auto& arg = monPtr->args[argIndex++];
            uint64_t value = 0;
            uint32_t width = 64;
            if (arg.kind == MonitorArgKind::Time) {
                value = currentTime;
                width = 64;
            } else if (arg.signal) {
                value = arg.signal->value();
                width = arg.signal->width();
            }
            if (spec == "0t") {
                out += std::to_string(value);
            } else if (spec == "b") {
                std::string bits;
                for (int bit = int(width) - 1; bit >= 0; --bit)
                    bits.push_back(((value >> bit) & 1U) ? '1' : '0');
                out += bits;
            } else if (spec == "d") {
                out += std::to_string(value);
            } else {
                out.push_back('%');
                out += spec;
            }
        }
        std::cout << out << "\n";
    };

    for (const auto& arg : mon->args) {
        if (arg.kind != MonitorArgKind::Signal || !arg.signal)
            continue;
        arg.signal->attach(this);
        arg.signal->monitorSensitive.push_back(proc.get());
    }

    monitors.push_back(std::move(mon));
    processes.push_back(std::move(proc));

    scheduleProcess(*processes.back(), currentTime);
}

void Kernel::schedule_at(uint64_t time, Callback cb) {
    scheduleAt(time, std::move(cb));
}

void Kernel::nba_assign(Signal& signal, uint64_t value) {
    signal.attach(this);
    nbaQueue.push_back({&signal, value});
}

void Kernel::scheduleAt(uint64_t time, Callback action) {
    uint64_t order = nextOrder++;
    if (time == currentTime) {
        activeQueue.push_back(Event{time, order, std::move(action)});
        return;
    }
    eventQueue.push(Event{time, order, std::move(action)});
}

void Kernel::scheduleProcess(Process& proc, uint64_t at) {
    scheduleAt(at, [&proc]() {
        proc.scheduled = false;
        proc.run();
    });
    proc.scheduled = true;
}

void Kernel::applyNba() {
    auto pending = std::move(nbaQueue);
    nbaQueue.clear();
    for (const auto& nba : pending) {
        if (nba.signal)
            nba.signal->set(nba.value);
    }
}

void Kernel::onSignalChange(Signal& signal, uint64_t oldValue, uint64_t newValue) {
    for (auto* proc : signal.levelSensitive) {
        if (!proc->scheduled)
            scheduleProcess(*proc, currentTime);
    }

    bool oldZero = (oldValue == 0);
    bool newZero = (newValue == 0);

    if (oldZero && !newZero) {
        for (auto* proc : signal.posedgeSensitive) {
            if (!proc->scheduled)
                scheduleProcess(*proc, currentTime);
        }
    }

    if (!oldZero && newZero) {
        for (auto* proc : signal.negedgeSensitive) {
            if (!proc->scheduled)
                scheduleProcess(*proc, currentTime);
        }
    }

    for (auto* proc : signal.monitorSensitive) {
        if (!proc->scheduled)
            scheduleProcess(*proc, currentTime);
    }
}

void Kernel::run() {
    while (!finished && (!eventQueue.empty() || !activeQueue.empty() || !nbaQueue.empty())) {
        if (activeQueue.empty() && !eventQueue.empty()) {
            uint64_t nextTime = eventQueue.top().time;
            currentTime = nextTime;
            while (!eventQueue.empty() && eventQueue.top().time == nextTime) {
                Event event = eventQueue.top();
                eventQueue.pop();
                activeQueue.push_back(std::move(event));
            }
        }

        while (!activeQueue.empty()) {
            auto event = std::move(activeQueue.front());
            activeQueue.pop_front();
            event.action();
        }

        if (!nbaQueue.empty())
            applyNba();
    }
}

} // namespace sim

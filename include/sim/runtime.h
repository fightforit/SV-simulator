#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace sim {

class Kernel;
class Signal;

enum class Edge {
    Any,
    Pos,
    Neg
};

enum class MonitorArgKind {
    Time,
    Signal
};

struct MonitorArg {
    MonitorArgKind kind = MonitorArgKind::Signal;
    Signal* signal = nullptr;

    static MonitorArg time() { return {MonitorArgKind::Time, nullptr}; }
    static MonitorArg signalArg(Signal* sig) { return {MonitorArgKind::Signal, sig}; }
};

struct Process {
    std::function<void()> run;
    bool scheduled = false;
};

class Signal {
public:
    explicit Signal(uint32_t width = 1);

    uint64_t value() const { return value_; }
    uint32_t width() const { return width_; }

    void set(uint64_t value);

private:
    friend class Kernel;

    void attach(Kernel* kernel);

    uint32_t width_ = 1;
    uint64_t value_ = 0;
    Kernel* kernel_ = nullptr;

    std::vector<Process*> levelSensitive;
    std::vector<Process*> posedgeSensitive;
    std::vector<Process*> negedgeSensitive;
    std::vector<Process*> monitorSensitive;
};

class Kernel {
public:
    using Callback = std::function<void()>;

    struct EdgeEvent {
        Signal* signal = nullptr;
        Edge edge = Edge::Any;
    };

    Kernel() = default;
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    void register_continuous(Callback cb, const std::vector<Signal*>& deps);
    void register_edge(Callback cb, const std::vector<EdgeEvent>& deps);
    void register_monitor(const std::string& format, const std::vector<MonitorArg>& args);

    void schedule_at(uint64_t time, Callback cb);

    void nba_assign(Signal& signal, uint64_t value);

    void run();
    void finish() { finished = true; }

    uint64_t time() const { return currentTime; }

private:
    friend class Signal;

    struct Event {
        uint64_t time = 0;
        uint64_t order = 0;
        Callback action;
    };

    struct EventCompare {
        bool operator()(const Event& a, const Event& b) const {
            if (a.time != b.time)
                return a.time > b.time;
            return a.order > b.order;
        }
    };

    struct NbaAssign {
        Signal* signal = nullptr;
        uint64_t value = 0;
    };

    struct Monitor {
        std::string format;
        std::vector<MonitorArg> args;
    };

    uint64_t currentTime = 0;
    uint64_t nextOrder = 0;
    bool finished = false;

    std::priority_queue<Event, std::vector<Event>, EventCompare> eventQueue;
    std::deque<Callback> activeQueue;
    std::vector<NbaAssign> nbaQueue;
    std::vector<std::unique_ptr<Process>> processes;
    std::vector<std::unique_ptr<Monitor>> monitors;

    void scheduleAt(uint64_t time, Callback action);
    void scheduleProcess(Process& proc, uint64_t at);
    void applyNba();
    void onSignalChange(Signal& signal, uint64_t oldValue, uint64_t newValue);
};

} // namespace sim

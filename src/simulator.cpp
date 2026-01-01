#include "sim/simulator.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/TimingControl.h"

namespace sim {

using namespace slang;
using namespace slang::ast;

namespace {

struct Value {
    uint64_t value = 0;
    uint32_t width = 1;
};

uint32_t widthOrDefault(uint32_t width, uint32_t fallback) {
    return width ? width : fallback;
}

uint32_t widthOrDefault(const std::optional<uint32_t>& width, uint32_t fallback) {
    return width.has_value() ? *width : fallback;
}

uint64_t maskToWidth(uint64_t value, uint32_t width) {
    if (width >= 64)
        return value;
    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
    return value & mask;
}

uint32_t exprWidth(const Expression& expr) {
    auto w = expr.type->getBitWidth();
    return widthOrDefault(w, 64);
}

struct Process;

struct Signal {
    const ValueSymbol* symbol = nullptr;
    std::string name;
    uint32_t width = 1;
    uint64_t value = 0;
    std::vector<Process*> levelSensitive;
    std::vector<Process*> posedgeSensitive;
    std::vector<Process*> negedgeSensitive;
    std::vector<Process*> monitorSensitive;
};

enum class ProcessKind {
    ContinuousAssign,
    AlwaysFF,
    AlwaysComb,
    Monitor
};

struct Process {
    ProcessKind kind = ProcessKind::ContinuousAssign;
    std::function<void()> run;
    bool scheduled = false;
};

struct Event {
    uint64_t time = 0;
    uint64_t order = 0;
    std::function<void()> action;
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
    std::vector<const Expression*> args;
};

} // namespace

struct Simulator::Impl {
    Impl(Compilation& compilation, const InstanceSymbol& top) :
        compilation(compilation), top(top) {}

    void build() {
        collectSignals(top.body, std::string(top.name));
        for (auto& inst : top.body.membersOfType<InstanceSymbol>())
            collectSignals(inst.body, std::string(top.name) + "." + std::string(inst.name));

        for (auto& inst : top.body.membersOfType<InstanceSymbol>()) {
            connectPorts(inst);
            collectProcesses(inst.body);
        }

        collectInitials(top.body);
        for (auto& proc : processes) {
            if (proc->kind == ProcessKind::ContinuousAssign ||
                proc->kind == ProcessKind::AlwaysComb)
                scheduleProcess(*proc, /*at*/ 0);
        }
    }

    void run() {
        while (!finished && (!eventQueue.empty() || !activeQueue.empty())) {
            if (activeQueue.empty()) {
                uint64_t nextTime = eventQueue.top().time;
                currentTime = nextTime;
                while (!eventQueue.empty() && eventQueue.top().time == nextTime) {
                    activeQueue.push_back(std::move(eventQueue.top().action));
                    eventQueue.pop();
                }
            }

            while (!activeQueue.empty()) {
                auto action = std::move(activeQueue.front());
                activeQueue.pop_front();
                action();
            }

            if (!nbaQueue.empty()) {
                applyNba();
            }
        }
    }

    Compilation& compilation;
    const InstanceSymbol& top;

    uint64_t currentTime = 0;
    uint64_t nextOrder = 0;
    bool finished = false;

    std::priority_queue<Event, std::vector<Event>, EventCompare> eventQueue;
    std::deque<std::function<void()>> activeQueue;
    std::vector<NbaAssign> nbaQueue;
    std::vector<std::unique_ptr<Signal>> signalStore;
    std::unordered_map<const ValueSymbol*, Signal*> signalMap;
    std::vector<std::unique_ptr<Process>> processes;
    std::vector<std::unique_ptr<Monitor>> monitors;

    void scheduleAt(uint64_t time, std::function<void()> action) {
        if (time == currentTime) {
            activeQueue.push_back(std::move(action));
            return;
        }
        eventQueue.push(Event{time, nextOrder++, std::move(action)});
    }

    void scheduleProcess(Process& proc, uint64_t at) {
        scheduleAt(at, [&proc]() {
            proc.scheduled = false;
            proc.run();
        });
        proc.scheduled = true;
    }

    void applyNba() {
        auto pending = std::move(nbaQueue);
        nbaQueue.clear();
        for (const auto& nba : pending)
            setSignal(*nba.signal, nba.value);
    }

    void setSignal(Signal& sig, uint64_t value) {
        uint64_t masked = maskToWidth(value, sig.width);
        if (sig.value == masked)
            return;

        uint64_t old = sig.value;
        sig.value = masked;

        for (auto* proc : sig.levelSensitive) {
            if (!proc->scheduled)
                scheduleProcess(*proc, currentTime);
        }

        bool oldZero = (old == 0);
        bool newZero = (masked == 0);
        if (oldZero && !newZero) {
            for (auto* proc : sig.posedgeSensitive) {
                if (!proc->scheduled)
                    scheduleProcess(*proc, currentTime);
            }
        }
        if (!oldZero && newZero) {
            for (auto* proc : sig.negedgeSensitive) {
                if (!proc->scheduled)
                    scheduleProcess(*proc, currentTime);
            }
        }

        for (auto* proc : sig.monitorSensitive) {
            if (!proc->scheduled)
                scheduleProcess(*proc, currentTime);
        }
    }

    Value evalExpr(const Expression& expr) {
        switch (expr.kind) {
            case ExpressionKind::IntegerLiteral: {
                auto& lit = expr.as<IntegerLiteral>();
                auto opt = lit.getValue().as<uint64_t>();
                uint64_t v = opt.value_or(0);
                uint32_t w = exprWidth(expr);
                return {maskToWidth(v, w), w};
            }
            case ExpressionKind::UnbasedUnsizedIntegerLiteral: {
                auto& lit = expr.as<UnbasedUnsizedIntegerLiteral>();
                auto opt = lit.getValue().as<uint64_t>();
                uint64_t v = opt.value_or(0);
                uint32_t w = exprWidth(expr);
                return {maskToWidth(v, w), w};
            }
            case ExpressionKind::NamedValue: {
                auto& named = expr.as<NamedValueExpression>();
                auto& sym = named.symbol;
                if (sym.kind == SymbolKind::Parameter) {
                    auto cv = sym.as<ParameterSymbol>().getValue();
                    auto opt = cv.integer().as<uint64_t>();
                    uint64_t v = opt.value_or(0);
                    uint32_t w = exprWidth(expr);
                    return {maskToWidth(v, w), w};
                }
                auto it = signalMap.find(&sym);
                if (it == signalMap.end())
                    return {0, 1};
                return {it->second->value, it->second->width};
            }
            case ExpressionKind::UnaryOp: {
                auto& un = expr.as<UnaryExpression>();
                auto v = evalExpr(un.operand());
                switch (un.op) {
                    case UnaryOperator::BitwiseNot: {
                        uint64_t inv = ~v.value;
                        return {maskToWidth(inv, v.width), v.width};
                    }
                    case UnaryOperator::LogicalNot: {
                        return {v.value == 0 ? 1U : 0U, 1};
                    }
                    default:
                        return {0, exprWidth(expr)};
                }
            }
            case ExpressionKind::BinaryOp: {
                auto& bin = expr.as<BinaryExpression>();
                auto lhs = evalExpr(bin.left());
                auto rhs = evalExpr(bin.right());
                uint32_t w = exprWidth(expr);
                uint64_t result = 0;
                switch (bin.op) {
                    case BinaryOperator::Add:
                        result = lhs.value + rhs.value;
                        break;
                    case BinaryOperator::Subtract:
                        result = lhs.value - rhs.value;
                        break;
                    case BinaryOperator::Multiply:
                        result = lhs.value * rhs.value;
                        break;
                    case BinaryOperator::Divide:
                        result = rhs.value ? (lhs.value / rhs.value) : 0;
                        break;
                    default:
                        result = 0;
                        break;
                }
                return {maskToWidth(result, w), w};
            }
            case ExpressionKind::Call: {
                auto& call = expr.as<CallExpression>();
                if (call.isSystemCall() && call.getSubroutineName() == "$time")
                    return {currentTime, 64};
                return {0, exprWidth(expr)};
            }
            default:
                return {0, exprWidth(expr)};
        }
    }

    uint64_t evalConstExpr(const Expression& expr) {
        auto v = evalExpr(expr);
        return v.value;
    }

    Signal* getSignalFromExpr(const Expression& expr) {
        if (auto sym = expr.getSymbolReference()) {
            if (ValueSymbol::isKind(sym->kind)) {
                auto* val = &sym->as<ValueSymbol>();
                auto it = signalMap.find(val);
                if (it != signalMap.end())
                    return it->second;
            }
        }
        return nullptr;
    }

    void collectExprSymbols(const Expression& expr,
                            std::unordered_set<const ValueSymbol*>& deps) {
        expr.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
            if (!ValueSymbol::isKind(sym.kind))
                return;
            if (sym.kind == SymbolKind::Parameter)
                return;
            deps.insert(&sym.as<ValueSymbol>());
        });
    }

    void collectStatementSymbols(const Statement& stmt,
                                 std::unordered_set<const ValueSymbol*>& deps) {
        switch (stmt.kind) {
            case StatementKind::Block: {
                auto& block = stmt.as<BlockStatement>();
                collectStatementSymbols(block.body, deps);
                break;
            }
            case StatementKind::List: {
                auto& list = stmt.as<StatementList>();
                for (auto* s : list.list)
                    collectStatementSymbols(*s, deps);
                break;
            }
            case StatementKind::Conditional: {
                auto& cond = stmt.as<ConditionalStatement>();
                collectExprSymbols(*cond.conditions[0].expr, deps);
                collectStatementSymbols(cond.ifTrue, deps);
                if (cond.ifFalse)
                    collectStatementSymbols(*cond.ifFalse, deps);
                break;
            }
            case StatementKind::Timed: {
                auto& ts = stmt.as<TimedStatement>();
                if (ts.timing.kind == TimingControlKind::Delay) {
                    auto& delay = ts.timing.as<DelayControl>();
                    collectExprSymbols(delay.expr, deps);
                }
                collectStatementSymbols(ts.stmt, deps);
                break;
            }
            case StatementKind::ExpressionStatement: {
                auto& es = stmt.as<ExpressionStatement>();
                if (es.expr.kind == ExpressionKind::Assignment) {
                    auto& a = es.expr.as<AssignmentExpression>();
                    collectExprSymbols(a.right(), deps);
                } else {
                    collectExprSymbols(es.expr, deps);
                }
                break;
            }
            default:
                break;
        }
    }

    void registerDependencies(Process& proc, const std::unordered_set<const ValueSymbol*>& deps) {
        for (const auto* sym : deps) {
            auto it = signalMap.find(sym);
            if (it == signalMap.end())
                continue;
            it->second->levelSensitive.push_back(&proc);
        }
    }

    void collectSignals(const Scope& scope, const std::string& prefix) {
        for (auto& member : scope.members()) {
            if (!ValueSymbol::isKind(member.kind))
                continue;
            if (member.kind == SymbolKind::Parameter)
                continue;

            auto& val = member.as<ValueSymbol>();
            auto width = val.getType().getBitWidth();
            uint32_t w = widthOrDefault(width, 1);
            auto sig = std::make_unique<Signal>();
            sig->symbol = &val;
            sig->name = prefix + "." + std::string(val.name);
            sig->width = w;
            sig->value = 0;

            if (auto init = val.getInitializer()) {
                auto v = evalConstExpr(*init);
                sig->value = maskToWidth(v, w);
            }

            signalMap[&val] = sig.get();
            signalStore.push_back(std::move(sig));
        }
    }

    void connectPorts(const InstanceSymbol& inst) {
        for (auto* conn : inst.getPortConnections()) {
            const auto& port = conn->port.as<PortSymbol>();
            if (!port.internalSymbol)
                continue;
            if (!ValueSymbol::isKind(port.internalSymbol->kind))
                continue;

            auto* internal = &port.internalSymbol->as<ValueSymbol>();
            const Expression* actualExpr = conn->getExpression();
            if (!actualExpr)
                continue;
            auto* actualSignal = getSignalFromExpr(*actualExpr);
            if (!actualSignal)
                continue;

            signalMap[internal] = actualSignal;
        }
    }

    void collectProcesses(const Scope& scope) {
        for (auto& member : scope.members()) {
            if (member.kind == SymbolKind::ContinuousAssign)
                addContinuousAssign(member.as<ContinuousAssignSymbol>());
            else if (member.kind == SymbolKind::ProceduralBlock)
                addProceduralBlock(member.as<ProceduralBlockSymbol>());
        }
    }

    void addContinuousAssign(const ContinuousAssignSymbol& assign) {
        const Expression& expr = assign.getAssignment();
        if (expr.kind != ExpressionKind::Assignment)
            return;

        auto& a = expr.as<AssignmentExpression>();
        Signal* lhs = getSignalFromExpr(a.left());
        if (!lhs)
            return;

        auto proc = std::make_unique<Process>();
        proc->kind = ProcessKind::ContinuousAssign;
        proc->run = [this, lhs, &a]() {
            auto rhs = evalExpr(a.right());
            setSignal(*lhs, rhs.value);
        };

        a.right().visitSymbolReferences([&](const Expression&, const Symbol& sym) {
            if (!ValueSymbol::isKind(sym.kind))
                return;
            if (sym.kind == SymbolKind::Parameter)
                return;
            auto it = signalMap.find(&sym.as<ValueSymbol>());
            if (it != signalMap.end())
                it->second->levelSensitive.push_back(proc.get());
        });

        processes.push_back(std::move(proc));
    }

    void addProceduralBlock(const ProceduralBlockSymbol& block) {
        if (block.procedureKind == ProceduralBlockKind::AlwaysFF) {
            const Statement& body = block.getBody();
            const Statement* stmtBody = &body;
            const TimingControl* timing = nullptr;
            if (body.kind == StatementKind::Timed) {
                auto& ts = body.as<TimedStatement>();
                timing = &ts.timing;
                stmtBody = &ts.stmt;
            }

            auto proc = std::make_unique<Process>();
            proc->kind = ProcessKind::AlwaysFF;
            proc->run = [this, stmtBody]() { evalStatement(*stmtBody, /*allowNba*/ true); };

            if (timing) {
                registerEventSensitivity(*timing, *proc);
            }

            processes.push_back(std::move(proc));
            return;
        }

        if (block.procedureKind == ProceduralBlockKind::AlwaysComb) {
            const Statement& body = block.getBody();
            const Statement* stmtBody = &body;
            auto proc = std::make_unique<Process>();
            proc->kind = ProcessKind::AlwaysComb;
            proc->run = [this, stmtBody]() { evalStatement(*stmtBody, /*allowNba*/ false); };

            std::unordered_set<const ValueSymbol*> deps;
            collectStatementSymbols(*stmtBody, deps);
            registerDependencies(*proc, deps);

            processes.push_back(std::move(proc));
        }
    }

    void registerEventSensitivity(const TimingControl& timing, Process& proc) {
        if (timing.kind == TimingControlKind::EventList) {
            auto& list = timing.as<EventListControl>();
            for (auto* ev : list.events)
                registerEventSensitivity(*ev, proc);
        } else if (timing.kind == TimingControlKind::SignalEvent) {
            auto& ev = timing.as<SignalEventControl>();
            Signal* sig = getSignalFromExpr(ev.expr);
            if (!sig)
                return;
            if (ev.edge == EdgeKind::PosEdge)
                sig->posedgeSensitive.push_back(&proc);
            else if (ev.edge == EdgeKind::NegEdge)
                sig->negedgeSensitive.push_back(&proc);
            else
                sig->levelSensitive.push_back(&proc);
        }
    }

    void evalStatement(const Statement& stmt, bool allowNba) {
        switch (stmt.kind) {
            case StatementKind::Block: {
                auto& block = stmt.as<BlockStatement>();
                evalStatement(block.body, allowNba);
                break;
            }
            case StatementKind::List: {
                auto& list = stmt.as<StatementList>();
                for (auto* s : list.list)
                    evalStatement(*s, allowNba);
                break;
            }
            case StatementKind::Conditional: {
                auto& cond = stmt.as<ConditionalStatement>();
                auto v = evalExpr(*cond.conditions[0].expr);
                if (v.value != 0) {
                    evalStatement(cond.ifTrue, allowNba);
                } else if (cond.ifFalse) {
                    evalStatement(*cond.ifFalse, allowNba);
                }
                break;
            }
            case StatementKind::ExpressionStatement: {
                auto& es = stmt.as<ExpressionStatement>();
                if (es.expr.kind == ExpressionKind::Assignment) {
                    auto& a = es.expr.as<AssignmentExpression>();
                    Signal* lhs = getSignalFromExpr(a.left());
                    if (!lhs)
                        break;
                    auto rhs = evalExpr(a.right());
                    if (a.isNonBlocking() && allowNba) {
                        nbaQueue.push_back({lhs, rhs.value});
                    } else {
                        setSignal(*lhs, rhs.value);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void collectInitials(const Scope& scope) {
        for (auto& block : scope.membersOfType<ProceduralBlockSymbol>()) {
            if (block.procedureKind != ProceduralBlockKind::Initial)
                continue;
            const Statement& body = block.getBody();
            if (body.kind == StatementKind::ForeverLoop) {
                setupClock(body.as<ForeverLoopStatement>());
            } else {
                uint64_t t = 0;
                scheduleSequential(body, t);
            }
        }
    }

    void setupClock(const ForeverLoopStatement& loop) {
        const Statement& body = loop.body;
        if (body.kind != StatementKind::Timed)
            return;
        auto& ts = body.as<TimedStatement>();
        if (ts.timing.kind != TimingControlKind::Delay)
            return;
        auto& delay = ts.timing.as<DelayControl>();

        const Statement& stmt = ts.stmt;
        if (stmt.kind != StatementKind::ExpressionStatement)
            return;
        auto& es = stmt.as<ExpressionStatement>();
        if (es.expr.kind != ExpressionKind::Assignment)
            return;
        auto& a = es.expr.as<AssignmentExpression>();
        Signal* lhs = getSignalFromExpr(a.left());
        if (!lhs)
            return;

        uint64_t delayTicks = evalConstExpr(delay.expr);
        if (delayTicks == 0)
            return;

        auto tick = std::make_shared<std::function<void()>>();
        *tick = [this, lhs, &a, delayTicks, tick]() {
            auto rhs = evalExpr(a.right());
            setSignal(*lhs, rhs.value);
            scheduleAt(currentTime + delayTicks, *tick);
        };

        scheduleAt(delayTicks, *tick);
    }

    void scheduleSequential(const Statement& stmt, uint64_t& time) {
        switch (stmt.kind) {
            case StatementKind::Block: {
                auto& block = stmt.as<BlockStatement>();
                scheduleSequential(block.body, time);
                break;
            }
            case StatementKind::List: {
                auto& list = stmt.as<StatementList>();
                for (auto* s : list.list)
                    scheduleSequential(*s, time);
                break;
            }
            case StatementKind::Timed: {
                auto& ts = stmt.as<TimedStatement>();
                if (ts.timing.kind == TimingControlKind::Delay) {
                    auto& delay = ts.timing.as<DelayControl>();
                    time += evalConstExpr(delay.expr);
                    scheduleSequential(ts.stmt, time);
                }
                break;
            }
            case StatementKind::ExpressionStatement: {
                auto& es = stmt.as<ExpressionStatement>();
                if (es.expr.kind == ExpressionKind::Call) {
                    handleSystemTask(es.expr.as<CallExpression>(), time);
                } else if (es.expr.kind == ExpressionKind::Assignment) {
                    auto& a = es.expr.as<AssignmentExpression>();
                    Signal* lhs = getSignalFromExpr(a.left());
                    if (!lhs)
                        break;
                    scheduleAt(time, [this, lhs, &a]() {
                        auto rhs = evalExpr(a.right());
                        setSignal(*lhs, rhs.value);
                    });
                }
                break;
            }
            default:
                break;
        }
    }

    void handleSystemTask(const CallExpression& call, uint64_t time) {
        if (!call.isSystemCall())
            return;
        if (call.getSubroutineName() == "$finish") {
            scheduleAt(time, [this]() { finished = true; });
            return;
        }
        if (call.getSubroutineName() == "$monitor") {
            if (call.arguments().empty())
                return;
            if (call.arguments()[0]->kind != ExpressionKind::StringLiteral)
                return;
            auto& fmt = call.arguments()[0]->as<StringLiteral>();

            auto mon = std::make_unique<Monitor>();
            mon->format = std::string(fmt.getValue());
            for (size_t i = 1; i < call.arguments().size(); ++i)
                mon->args.push_back(call.arguments()[i]);

            auto proc = std::make_unique<Process>();
            proc->kind = ProcessKind::Monitor;
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
                    const Expression* expr = monPtr->args[argIndex++];
                    if (spec == "0t") {
                        auto v = evalExpr(*expr);
                        out += std::to_string(v.value);
                    } else if (spec == "b") {
                        auto v = evalExpr(*expr);
                        std::string bits;
                        for (int bit = int(v.width) - 1; bit >= 0; --bit)
                            bits.push_back(((v.value >> bit) & 1) ? '1' : '0');
                        out += bits;
                    } else if (spec == "d") {
                        auto v = evalExpr(*expr);
                        out += std::to_string(v.value);
                    } else {
                        out.push_back('%');
                        out += spec;
                    }
                }
                std::cout << out << "\n";
            };

            for (auto* expr : mon->args) {
                expr->visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                    if (!ValueSymbol::isKind(sym.kind))
                        return;
                    if (sym.kind == SymbolKind::Parameter)
                        return;
                    auto it = signalMap.find(&sym.as<ValueSymbol>());
                    if (it != signalMap.end())
                        it->second->monitorSensitive.push_back(proc.get());
                });
            }

            monitors.push_back(std::move(mon));
            processes.push_back(std::move(proc));

            scheduleAt(time, [this, procPtr = processes.back().get()]() {
                if (!procPtr->scheduled)
                    scheduleProcess(*procPtr, currentTime);
            });
        }
    }
};

} // namespace sim

namespace sim {

Simulator::Simulator(Compilation& compilation, const InstanceSymbol& top) :
    impl(std::make_unique<Impl>(compilation, top)) {}

Simulator::~Simulator() = default;

void Simulator::build() {
    impl->build();
}

void Simulator::run() {
    impl->run();
}

} // namespace sim

#include "sim/codegen.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"

namespace sim {

using namespace slang;
using namespace slang::ast;

namespace {

struct PortInfo {
    std::string name;
    ArgumentDirection direction = ArgumentDirection::InOut;
    const ValueSymbol* internal = nullptr;
    uint32_t width = 1;
    const PortSymbol* portSymbol = nullptr;
};

uint32_t widthOrDefault(uint32_t width, uint32_t fallback) {
    return width ? width : fallback;
}

template<typename T>
uint32_t widthOrDefault(const std::optional<T>& width, uint32_t fallback) {
    return width.has_value() ? static_cast<uint32_t>(*width) : fallback;
}

uint32_t bitWidth(const Type& type, uint32_t fallback = 1) {
    auto w = type.getBitWidth();
    return widthOrDefault(w, fallback);
}

const ValueSymbol* getValueSymbol(const Symbol* symbol) {
    if (!symbol)
        return nullptr;
    if (ValueSymbol::isKind(symbol->kind))
        return &symbol->as<ValueSymbol>();
    return nullptr;
}

std::string cppIdent(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 1);
    if (name.empty() || (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')))
        out.push_back('_');
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out;
}

std::string directionString(ArgumentDirection dir) {
    switch (dir) {
        case ArgumentDirection::In:
            return "input";
        case ArgumentDirection::Out:
            return "output";
        case ArgumentDirection::InOut:
            return "inout";
        case ArgumentDirection::Ref:
            return "ref";
        default:
            return "inout";
    }
}

std::vector<PortInfo> collectPorts(const InstanceBodySymbol& body) {
    std::vector<PortInfo> ports;
    std::unordered_set<std::string> seenPortNames;

    for (auto* sym : body.getPortList()) {
        if (sym->kind == SymbolKind::Port) {
            auto& port = sym->as<PortSymbol>();
            PortInfo info;
            info.name = cppIdent(port.name);
            info.direction = port.direction;
            info.internal = getValueSymbol(port.internalSymbol);
            info.portSymbol = &port;
            if (info.internal)
                info.width = bitWidth(info.internal->getType(), 1);
            if (seenPortNames.insert(info.name).second)
                ports.push_back(info);
        } else if (sym->kind == SymbolKind::MultiPort) {
            auto& multi = sym->as<MultiPortSymbol>();
            for (auto* p : multi.ports) {
                PortInfo info;
                info.name = cppIdent(p->name);
                info.direction = p->direction;
                info.internal = getValueSymbol(p->internalSymbol);
                info.portSymbol = p;
                if (info.internal)
                    info.width = bitWidth(info.internal->getType(), 1);
                if (seenPortNames.insert(info.name).second)
                    ports.push_back(info);
            }
        }
    }

    return ports;
}

void collectInstances(const InstanceSymbol& inst,
                      std::unordered_map<std::string, const InstanceSymbol*>& defs) {
    std::string defName(inst.getDefinition().name);
    if (defs.find(defName) == defs.end())
        defs.emplace(defName, &inst);

    for (auto& child : inst.body.membersOfType<InstanceSymbol>())
        collectInstances(child, defs);
}

std::string emitExpr(const Expression& expr,
                     const std::unordered_map<const ValueSymbol*, std::string>& names);

const ValueSymbol* getValueSymbolFromExpr(const Expression& expr) {
    if (auto sym = expr.getSymbolReference()) {
        if (ValueSymbol::isKind(sym->kind))
            return &sym->as<ValueSymbol>();
    }
    return nullptr;
}

std::string emitExpr(const Expression& expr,
                     const std::unordered_map<const ValueSymbol*, std::string>& names) {
    switch (expr.kind) {
        case ExpressionKind::IntegerLiteral: {
            auto& lit = expr.as<IntegerLiteral>();
            auto value = lit.getValue();
            if (auto opt = value.as<uint64_t>())
                return std::to_string(*opt);
            if (auto opt = value.as<int64_t>())
                return std::to_string(*opt);
            return value.toString();
        }
        case ExpressionKind::UnbasedUnsizedIntegerLiteral: {
            auto& lit = expr.as<UnbasedUnsizedIntegerLiteral>();
            auto value = lit.getValue();
            if (auto opt = value.as<uint64_t>())
                return std::to_string(*opt);
            if (auto opt = value.as<int64_t>())
                return std::to_string(*opt);
            return value.toString();
        }
        case ExpressionKind::RealLiteral: {
            auto& lit = expr.as<RealLiteral>();
            return std::to_string(lit.getValue());
        }
        case ExpressionKind::TimeLiteral: {
            auto& lit = expr.as<TimeLiteral>();
            return std::to_string(lit.getValue());
        }
        case ExpressionKind::NamedValue: {
            auto& named = expr.as<NamedValueExpression>();
            auto& sym = named.symbol;
            if (sym.kind == SymbolKind::Parameter) {
                auto cv = sym.as<ParameterSymbol>().getValue();
                auto opt = cv.integer().as<uint64_t>();
                return std::to_string(opt.value_or(0));
            }
            auto it = names.find(&sym.as<ValueSymbol>());
            if (it != names.end())
                return it->second + ".value()";
            return "0";
        }
        case ExpressionKind::Conversion: {
            auto& conv = expr.as<ConversionExpression>();
            return emitExpr(conv.operand(), names);
        }
        case ExpressionKind::UnaryOp: {
            auto& un = expr.as<UnaryExpression>();
            std::string rhs = emitExpr(un.operand(), names);
            switch (un.op) {
                case UnaryOperator::LogicalNot:
                    return "(!" + rhs + ")";
                case UnaryOperator::BitwiseNot:
                    return "(~" + rhs + ")";
                default:
                    return "(" + rhs + ")";
            }
        }
        case ExpressionKind::BinaryOp: {
            auto& bin = expr.as<BinaryExpression>();
            std::string lhs = emitExpr(bin.left(), names);
            std::string rhs = emitExpr(bin.right(), names);
            switch (bin.op) {
                case BinaryOperator::Add:
                    return "(" + lhs + " + " + rhs + ")";
                case BinaryOperator::Subtract:
                    return "(" + lhs + " - " + rhs + ")";
                case BinaryOperator::Multiply:
                    return "(" + lhs + " * " + rhs + ")";
                case BinaryOperator::Divide:
                    return "(" + lhs + " / " + rhs + ")";
                case BinaryOperator::LogicalAnd:
                    return "(" + lhs + " && " + rhs + ")";
                case BinaryOperator::LogicalOr:
                    return "(" + lhs + " || " + rhs + ")";
                default:
                    return "0";
            }
        }
        case ExpressionKind::Call: {
            auto& call = expr.as<CallExpression>();
            if (call.isSystemCall() && call.getSubroutineName() == "$time")
                return "kernel.time()";
            return "0";
        }
        default:
            return "0";
    }
}

std::string emitMonitorArg(const Expression& expr,
                           const std::unordered_map<const ValueSymbol*, std::string>& names) {
    if (expr.kind == ExpressionKind::Call) {
        auto& call = expr.as<CallExpression>();
        if (call.isSystemCall() && call.getSubroutineName() == "$time")
            return "sim::MonitorArg::time()";
    }
    const ValueSymbol* sym = getValueSymbolFromExpr(expr);
    if (sym) {
        auto it = names.find(sym);
        if (it != names.end())
            return "sim::MonitorArg::signalArg(&" + it->second + ")";
    }
    return "sim::MonitorArg::time()";
}

bool emitInitialStatement(const Statement& stmt,
                          const std::unordered_map<const ValueSymbol*, std::string>& names,
                          std::ostream& out,
                          int indent,
                          const std::string& timeVar) {
    auto pad = std::string(static_cast<size_t>(indent), ' ');
    switch (stmt.kind) {
        case StatementKind::Block: {
            auto& block = stmt.as<BlockStatement>();
            return emitInitialStatement(block.body, names, out, indent, timeVar);
        }
        case StatementKind::List: {
            auto& list = stmt.as<StatementList>();
            for (auto* s : list.list) {
                if (!emitInitialStatement(*s, names, out, indent, timeVar))
                    return false;
            }
            return true;
        }
        case StatementKind::Timed: {
            auto& ts = stmt.as<TimedStatement>();
            if (ts.timing.kind == TimingControlKind::Delay) {
                auto& delay = ts.timing.as<DelayControl>();
                std::string expr = emitExpr(delay.expr, names);
                out << pad << timeVar << " += static_cast<uint64_t>(" << expr << ");\n";
                if (ts.stmt.kind == StatementKind::Empty)
                    return true;
                return emitInitialStatement(ts.stmt, names, out, indent, timeVar);
            }
            return false;
        }
        case StatementKind::Empty:
            return true;
        case StatementKind::ExpressionStatement: {
            auto& es = stmt.as<ExpressionStatement>();
            if (es.expr.kind == ExpressionKind::Call) {
                auto& call = es.expr.as<CallExpression>();
                if (!call.isSystemCall())
                    return false;
                auto name = call.getSubroutineName();
                if (name == "$finish") {
                    out << pad << "kernel.schedule_at(" << timeVar
                        << ", [this]() { this->kernel.finish(); });\n";
                    return true;
                }
                if (name == "$monitor") {
                    if (call.arguments().empty())
                        return false;
                    if (call.arguments()[0]->kind != ExpressionKind::StringLiteral)
                        return false;
                    auto& fmt = call.arguments()[0]->as<StringLiteral>();
                    out << pad << "kernel.schedule_at(" << timeVar << ", [this]() {\n";
                    out << pad << "    this->kernel.register_monitor(\"" << fmt.getValue()
                        << "\", {";
                    bool first = true;
                    for (size_t i = 1; i < call.arguments().size(); ++i) {
                        const Expression* arg = call.arguments()[i];
                        if (!first)
                            out << ", ";
                        first = false;
                        out << emitMonitorArg(*arg, names);
                    }
                    out << "});\n";
                    out << pad << "});\n";
                    return true;
                }
                return false;
            }
            if (es.expr.kind == ExpressionKind::Assignment) {
                auto& a = es.expr.as<AssignmentExpression>();
                const ValueSymbol* lhsSym = getValueSymbolFromExpr(a.left());
                if (!lhsSym)
                    return false;
                auto it = names.find(lhsSym);
                if (it == names.end())
                    return false;
                std::string rhs = emitExpr(a.right(), names);
                out << pad << "kernel.schedule_at(" << timeVar << ", [this]() {\n";
                if (a.isNonBlocking()) {
                    out << pad << "    this->kernel.nba_assign(" << it->second << ", " << rhs
                        << ");\n";
                } else {
                    out << pad << "    " << it->second << ".set(" << rhs << ");\n";
                }
                out << pad << "});\n";
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

void collectExprSignals(const Expression& expr,
                        std::unordered_set<const ValueSymbol*>& deps) {
    expr.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
        if (!ValueSymbol::isKind(sym.kind))
            return;
        if (sym.kind == SymbolKind::Parameter)
            return;
        deps.insert(&sym.as<ValueSymbol>());
    });
}

void collectStatementSignals(const Statement& stmt,
                             std::unordered_set<const ValueSymbol*>& deps) {
    switch (stmt.kind) {
        case StatementKind::Block: {
            auto& block = stmt.as<BlockStatement>();
            collectStatementSignals(block.body, deps);
            break;
        }
        case StatementKind::List: {
            auto& list = stmt.as<StatementList>();
            for (auto* s : list.list)
                collectStatementSignals(*s, deps);
            break;
        }
        case StatementKind::Conditional: {
            auto& cond = stmt.as<ConditionalStatement>();
            collectExprSignals(*cond.conditions[0].expr, deps);
            collectStatementSignals(cond.ifTrue, deps);
            if (cond.ifFalse)
                collectStatementSignals(*cond.ifFalse, deps);
            break;
        }
        case StatementKind::Timed: {
            auto& ts = stmt.as<TimedStatement>();
            if (ts.timing.kind == TimingControlKind::Delay) {
                auto& delay = ts.timing.as<DelayControl>();
                collectExprSignals(delay.expr, deps);
            }
            collectStatementSignals(ts.stmt, deps);
            break;
        }
        case StatementKind::ExpressionStatement: {
            auto& es = stmt.as<ExpressionStatement>();
            if (es.expr.kind == ExpressionKind::Assignment) {
                auto& a = es.expr.as<AssignmentExpression>();
                collectExprSignals(a.right(), deps);
            } else {
                collectExprSignals(es.expr, deps);
            }
            break;
        }
        default:
            break;
    }
}

void emitStatement(const Statement& stmt,
                   const std::unordered_map<const ValueSymbol*, std::string>& names,
                   std::ostream& out,
                   int indent,
                   bool allowNba) {
    auto pad = std::string(static_cast<size_t>(indent), ' ');
    switch (stmt.kind) {
        case StatementKind::Block: {
            auto& block = stmt.as<BlockStatement>();
            emitStatement(block.body, names, out, indent, allowNba);
            break;
        }
        case StatementKind::List: {
            auto& list = stmt.as<StatementList>();
            for (auto* s : list.list)
                emitStatement(*s, names, out, indent, allowNba);
            break;
        }
        case StatementKind::Conditional: {
            auto& cond = stmt.as<ConditionalStatement>();
            std::string expr = emitExpr(*cond.conditions[0].expr, names);
            out << pad << "if (" << expr << ") {\n";
            emitStatement(cond.ifTrue, names, out, indent + 4, allowNba);
            out << pad << "}";
            if (cond.ifFalse) {
                out << " else {\n";
                emitStatement(*cond.ifFalse, names, out, indent + 4, allowNba);
                out << pad << "}";
            }
            out << "\n";
            break;
        }
        case StatementKind::ExpressionStatement: {
            auto& es = stmt.as<ExpressionStatement>();
            if (es.expr.kind == ExpressionKind::Assignment) {
                auto& a = es.expr.as<AssignmentExpression>();
                const ValueSymbol* lhsSym = getValueSymbolFromExpr(a.left());
                if (!lhsSym)
                    break;
                auto it = names.find(lhsSym);
                if (it == names.end())
                    break;
                std::string rhs = emitExpr(a.right(), names);
                if (a.isNonBlocking() && allowNba) {
                    out << pad << "kernel.nba_assign(" << it->second << ", " << rhs << ");\n";
                } else {
                    out << pad << it->second << ".set(" << rhs << ");\n";
                }
            }
            break;
        }
        default:
            out << pad << "// unsupported statement\n";
            break;
    }
}

void emitSensitivity(const TimingControl& timing,
                     const std::unordered_map<const ValueSymbol*, std::string>& names,
                     std::ostream& out,
                     int indent) {
    auto pad = std::string(static_cast<size_t>(indent), ' ');
    if (timing.kind == TimingControlKind::EventList) {
        auto& list = timing.as<EventListControl>();
        bool first = true;
        out << pad << "{";
        for (auto* ev : list.events) {
            if (!first)
                out << ", ";
            first = false;
            emitSensitivity(*ev, names, out, 0);
        }
        out << "}";
    } else if (timing.kind == TimingControlKind::SignalEvent) {
        auto& ev = timing.as<SignalEventControl>();
        const ValueSymbol* sig = getValueSymbolFromExpr(ev.expr);
        if (!sig)
            return;
        auto it = names.find(sig);
        if (it == names.end())
            return;
        const char* edge = "Any";
        if (ev.edge == EdgeKind::PosEdge)
            edge = "Pos";
        else if (ev.edge == EdgeKind::NegEdge)
            edge = "Neg";
        out << "{&" << it->second << ", sim::Edge::" << edge << "}";
    }
}

bool emitModule(const InstanceSymbol& inst, const std::string& outDir) {
    std::string defName(inst.getDefinition().name);
    std::filesystem::path outPath = std::filesystem::path(outDir) / (defName + ".cpp");
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outPath << "\n";
        return false;
    }

    const InstanceBodySymbol& body = inst.body;
    std::vector<PortInfo> ports = collectPorts(body);
    std::unordered_set<const ValueSymbol*> portInternals;
    for (const auto& port : ports) {
        if (port.internal)
            portInternals.insert(port.internal);
    }

    std::vector<const ParameterSymbol*> params;
    for (auto* paramBase : body.getParameters()) {
        if (paramBase->symbol.kind == SymbolKind::Parameter)
            params.push_back(&paramBase->symbol.as<ParameterSymbol>());
    }

    std::vector<const ValueSymbol*> internals;
    for (auto& member : body.membersOfType<ValueSymbol>()) {
        if (member.kind == SymbolKind::Parameter)
            continue;
        if (portInternals.find(&member) != portInternals.end())
            continue;
        internals.push_back(&member);
    }

    std::vector<std::pair<std::string, uint32_t>> extraSignals;
    struct CombProc {
        std::vector<const ValueSymbol*> deps;
        const AssignmentExpression* assign = nullptr;
        const Statement* stmt = nullptr;
    };
    std::vector<CombProc> combProcs;

    std::unordered_map<const ValueSymbol*, std::string> nameMap;
    for (const auto& port : ports) {
        if (port.internal)
            nameMap[port.internal] = port.name;
    }
    for (const auto* sig : internals) {
        std::string name = cppIdent(sig->name);
        nameMap[sig] = name;
    }

    struct ChildInst {
        std::string name;
        std::string className;
        std::vector<std::string> args;
    };

    std::vector<ChildInst> children;
    int childIndex = 0;
    for (auto& child : body.membersOfType<InstanceSymbol>()) {
        ChildInst ci;
        ci.name = cppIdent(child.name);
        if (ci.name.empty())
            ci.name = "inst_" + std::to_string(childIndex);
        ci.className = cppIdent(child.getDefinition().name);
        if (ci.name == ci.className)
            ci.name += "_inst";
        ci.args.push_back("kernel");

        std::unordered_map<const PortSymbol*, const Expression*> portExprs;
        for (auto* conn : child.getPortConnections()) {
            const auto& port = conn->port.as<PortSymbol>();
            portExprs[&port] = conn->getExpression();
        }

        auto childPorts = collectPorts(child.body);
        int dummyIndex = 0;
        for (const auto& port : childPorts) {
            std::string arg;
            if (port.portSymbol) {
                auto it = portExprs.find(port.portSymbol);
                if (it != portExprs.end() && it->second) {
                    const ValueSymbol* actual = getValueSymbolFromExpr(*it->second);
                    if (actual) {
                        auto nameIt = nameMap.find(actual);
                        if (nameIt != nameMap.end())
                            arg = nameIt->second;
                    }
                }
            }
            if (arg.empty()) {
                std::string dummy = ci.name + "_unconn_" + std::to_string(dummyIndex++);
                extraSignals.emplace_back(dummy, port.width);
                arg = dummy;
            }
            ci.args.push_back(arg);
        }

        children.push_back(std::move(ci));
        childIndex++;
    }

    out << "#include <cstdint>\n";
    out << "#include <functional>\n";
    out << "#include <memory>\n";
    out << "#include <vector>\n";
    out << "#include \"sim/runtime.h\"\n\n";
    out << "namespace gen {\n\n";
    out << "class " << cppIdent(defName) << " {\n";
    out << "public:\n";

    out << "    " << cppIdent(defName) << "(sim::Kernel& kernel";
    for (const auto& port : ports)
        out << ", sim::Signal& " << port.name;
    for (const auto* param : params) {
        auto opt = param->getValue().integer().as<uint64_t>();
        uint64_t value = opt.value_or(0);
        out << ", uint32_t " << cppIdent(param->name) << " = " << value;
    }
    out << ")\n";
    out << "        : kernel(kernel)";
    for (const auto& port : ports)
        out << ", " << port.name << "(" << port.name << ")";
    for (const auto* sig : internals) {
        std::string name = nameMap[sig];
        uint32_t width = bitWidth(sig->getType(), 1);
        out << ", " << name << "(" << width << ")";
    }
    for (const auto& extra : extraSignals)
        out << ", " << extra.first << "(" << extra.second << ")";
    for (const auto& child : children) {
        out << ", " << child.name << "(";
        for (size_t i = 0; i < child.args.size(); ++i) {
            if (i != 0)
                out << ", ";
            out << child.args[i];
        }
        out << ")";
    }
    out << " {\n";

    for (auto& assign : body.membersOfType<ContinuousAssignSymbol>()) {
        const Expression& expr = assign.getAssignment();
        if (expr.kind != ExpressionKind::Assignment)
            continue;
        auto& a = expr.as<AssignmentExpression>();
        CombProc proc;
        proc.assign = &a;
        std::unordered_set<const ValueSymbol*> deps;
        collectExprSignals(a.right(), deps);
        proc.deps.assign(deps.begin(), deps.end());
        combProcs.push_back(std::move(proc));
    }

    int ffIndex = 0;
    for (auto& block : body.membersOfType<ProceduralBlockSymbol>()) {
        if (block.procedureKind != ProceduralBlockKind::AlwaysFF)
            continue;

        const Statement& bodyStmt = block.getBody();
        const Statement* stmtBody = &bodyStmt;
        const TimingControl* timing = nullptr;
        if (bodyStmt.kind == StatementKind::Timed) {
            auto& ts = bodyStmt.as<TimedStatement>();
            timing = &ts.timing;
            stmtBody = &ts.stmt;
        }

        out << "        kernel.register_edge([this]() { eval_ff_" << ffIndex << "(); }, ";
        if (timing) {
            emitSensitivity(*timing, nameMap, out, 8);
        } else {
            out << "{}";
        }
        out << ");\n";
        ffIndex++;
    }

    for (auto& block : body.membersOfType<ProceduralBlockSymbol>()) {
        if (block.procedureKind != ProceduralBlockKind::AlwaysComb)
            continue;

        const Statement& bodyStmt = block.getBody();
        std::unordered_set<const ValueSymbol*> deps;
        collectStatementSignals(bodyStmt, deps);

        CombProc proc;
        proc.stmt = &bodyStmt;
        proc.deps.assign(deps.begin(), deps.end());
        combProcs.push_back(std::move(proc));
    }

    int combProcIndex = 0;
    for (const auto& comb : combProcs) {
        out << "        kernel.register_continuous([this]() { eval_comb_proc_"
            << combProcIndex << "(); }, {";
        bool first = true;
        for (const auto* dep : comb.deps) {
            auto it = nameMap.find(dep);
            if (it == nameMap.end())
                continue;
            if (!first)
                out << ", ";
            first = false;
            out << "&" << it->second;
        }
        out << "});\n";
        combProcIndex++;
    }

    int initIndex = 0;
    for (auto& block : body.membersOfType<ProceduralBlockSymbol>()) {
        if (block.procedureKind != ProceduralBlockKind::Initial)
            continue;
        const Statement& bodyStmt = block.getBody();

        if (bodyStmt.kind == StatementKind::ForeverLoop) {
            auto& loop = bodyStmt.as<ForeverLoopStatement>();
            const Statement& inner = loop.body;
            if (inner.kind == StatementKind::Timed) {
                auto& ts = inner.as<TimedStatement>();
                if (ts.timing.kind == TimingControlKind::Delay) {
                    auto& delay = ts.timing.as<DelayControl>();
                    std::string delayExpr = emitExpr(delay.expr, nameMap);
                    out << "        {\n";
                    out << "            auto tick = std::make_shared<std::function<void()>>();\n";
                    out << "            *tick = [this, tick]() {\n";
                    if (ts.stmt.kind == StatementKind::ExpressionStatement) {
                        auto& es = ts.stmt.as<ExpressionStatement>();
                        if (es.expr.kind == ExpressionKind::Assignment) {
                            auto& a = es.expr.as<AssignmentExpression>();
                            const ValueSymbol* lhs = getValueSymbolFromExpr(a.left());
                            if (lhs) {
                                auto it = nameMap.find(lhs);
                                if (it != nameMap.end()) {
                                    std::string rhs = emitExpr(a.right(), nameMap);
                                    if (a.isNonBlocking()) {
                                        out << "                this->kernel.nba_assign("
                                            << it->second
                                            << ", " << rhs << ");\n";
                                    } else {
                                        out << "                " << it->second << ".set(" << rhs
                                            << ");\n";
                                    }
                                }
                            }
                        }
                    }
                    out << "                this->kernel.schedule_at(this->kernel.time() + "
                           "static_cast<uint64_t>("
                        << delayExpr << "), *tick);\n";
                    out << "            };\n";
                    out << "            kernel.schedule_at(static_cast<uint64_t>(" << delayExpr
                        << "), *tick);\n";
                    out << "        }\n";
                }
            }
        } else {
            std::string timeVar = "t" + std::to_string(initIndex);
            out << "        {\n";
            out << "            uint64_t " << timeVar << " = 0;\n";
            emitInitialStatement(bodyStmt, nameMap, out, 12, timeVar);
            out << "        }\n";
        }
        initIndex++;
    }

    out << "    }\n\n";
    out << "private:\n";
    out << "    sim::Kernel& kernel;\n";
    for (const auto& port : ports) {
        out << "    sim::Signal& " << port.name << "; // "
            << directionString(port.direction) << "\n";
    }
    for (const auto* sig : internals) {
        std::string name = nameMap[sig];
        out << "    sim::Signal " << name << ";\n";
    }
    for (const auto& extra : extraSignals)
        out << "    sim::Signal " << extra.first << ";\n";
    for (const auto& child : children)
        out << "    " << child.className << " " << child.name << ";\n";

    ffIndex = 0;
    for (auto& block : body.membersOfType<ProceduralBlockSymbol>()) {
        if (block.procedureKind != ProceduralBlockKind::AlwaysFF)
            continue;

        const Statement& bodyStmt = block.getBody();
        const Statement* stmtBody = &bodyStmt;
        if (bodyStmt.kind == StatementKind::Timed) {
            auto& ts = bodyStmt.as<TimedStatement>();
            stmtBody = &ts.stmt;
        }

        out << "\n    void eval_ff_" << ffIndex++ << "() {\n";
        emitStatement(*stmtBody, nameMap, out, 8, true);
        out << "    }\n";
    }

    combProcIndex = 0;
    for (const auto& comb : combProcs) {
        out << "\n    void eval_comb_proc_" << combProcIndex++ << "() {\n";
        if (comb.assign) {
            const ValueSymbol* lhs = getValueSymbolFromExpr(comb.assign->left());
            if (lhs) {
                auto it = nameMap.find(lhs);
                if (it != nameMap.end()) {
                    std::string rhs = emitExpr(comb.assign->right(), nameMap);
                    out << "        " << it->second << ".set(" << rhs << ");\n";
                }
            }
        } else if (comb.stmt) {
            emitStatement(*comb.stmt, nameMap, out, 8, false);
        } else {
            out << "        // unsupported combinational block\n";
        }
        out << "    }\n";
    }

    out << "};\n\n";
    out << "} // namespace gen\n";

    return true;
}

bool emitTopDriver(const InstanceSymbol& top,
                   const std::unordered_map<std::string, const InstanceSymbol*>& defs,
                   const std::string& outDir) {
    std::filesystem::path outPath = std::filesystem::path(outDir) / "sim_main.cpp";
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outPath << "\n";
        return false;
    }

    out << "#include \"sim/runtime.h\"\n";
    for (const auto& [name, inst] : defs) {
        out << "#include \"" << name << ".cpp\"\n";
    }
    out << "\n";
    out << "int main() {\n";
    out << "    sim::Kernel kernel;\n";

    const auto ports = collectPorts(top.body);
    for (const auto& port : ports) {
        out << "    sim::Signal " << port.name << "(" << port.width << ");\n";
    }

    out << "    gen::" << cppIdent(top.getDefinition().name) << " top(kernel";
    for (const auto& port : ports) {
        out << ", " << port.name;
    }
    out << ");\n";
    out << "    kernel.run();\n";
    out << "    return 0;\n";
    out << "}\n";

    return true;
}

} // namespace

bool writeCppOutput(const InstanceSymbol& top, const std::string& outputDir) {
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        std::cerr << "Failed to create output directory: " << outputDir << "\n";
        return false;
    }

    std::unordered_map<std::string, const InstanceSymbol*> defs;
    collectInstances(top, defs);

    for (const auto& [name, inst] : defs) {
        if (!emitModule(*inst, outputDir))
            return false;
    }

    if (!emitTopDriver(top, defs, outputDir))
        return false;

    return true;
}

} // namespace sim

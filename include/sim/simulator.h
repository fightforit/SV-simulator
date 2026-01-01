#pragma once

#include <memory>

namespace slang::ast {
class Compilation;
class InstanceSymbol;
}

namespace sim {

class Simulator {
public:
    Simulator(slang::ast::Compilation& compilation, const slang::ast::InstanceSymbol& top);
    ~Simulator();

    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;

    void build();
    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace sim

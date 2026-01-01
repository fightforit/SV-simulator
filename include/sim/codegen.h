#pragma once

#include <string>

namespace slang::ast {
class InstanceSymbol;
}

namespace sim {

bool writeCppOutput(const slang::ast::InstanceSymbol& top, const std::string& outputDir);

} // namespace sim

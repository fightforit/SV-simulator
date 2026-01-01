#pragma once

#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace slang::ast {
class Compilation;
class InstanceSymbol;
}

namespace slang::syntax {
class SyntaxTree;
}

namespace sim {

std::optional<std::shared_ptr<slang::syntax::SyntaxTree>> loadFile(const std::string& path);
const slang::ast::InstanceSymbol* findTop(slang::ast::Compilation& compilation,
                                          std::string_view name);

bool writeAstJson(const std::vector<std::shared_ptr<slang::syntax::SyntaxTree>>& trees,
                  const std::string& outputPath);

} // namespace sim

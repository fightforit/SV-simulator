#include "sim/frontend.h"

#include <fstream>
#include <iostream>

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/syntax/CSTSerializer.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/Json.h"

namespace sim {

using slang::JsonWriter;
using slang::ast::Compilation;
using slang::ast::InstanceSymbol;
using slang::syntax::CSTJsonMode;
using slang::syntax::CSTSerializer;
using slang::syntax::SyntaxTree;

std::optional<std::shared_ptr<SyntaxTree>> loadFile(const std::string& path) {
    auto result = SyntaxTree::fromFile(path);
    if (!result) {
        auto err = result.error();
        std::cerr << "Failed to load " << path << ": " << err.first.message();
        if (!err.second.empty())
            std::cerr << " " << err.second;
        std::cerr << "\n";
        return std::nullopt;
    }
    return *result;
}

const InstanceSymbol* findTop(Compilation& compilation, std::string_view name) {
    for (auto* inst : compilation.getRoot().topInstances) {
        if (inst->getDefinition().name == name)
            return inst;
    }
    return nullptr;
}

bool writeAstJson(const std::vector<std::shared_ptr<SyntaxTree>>& trees,
                  const std::string& outputPath) {
    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Failed to open AST output file: " << outputPath << "\n";
        return false;
    }

    JsonWriter writer;
    writer.setPrettyPrint(true);
    writer.setIndentSize(2);
    writer.startArray();

    for (const auto& tree : trees) {
        CSTSerializer serializer(writer, CSTJsonMode::NoTrivia);
        serializer.serialize(*tree);
    }

    writer.endArray();
    out << writer.view() << "\n";
    return true;
}

} // namespace sim

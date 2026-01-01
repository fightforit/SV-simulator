#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"

#include "sim/codegen.h"
#include "sim/frontend.h"
#include "sim/simulator.h"

namespace {

bool isFileList(std::string_view path) {
    return path.size() >= 2 && path.substr(path.size() - 2) == ".f";
}

std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' ||
                                   text[start] == '\n'))
        start++;
    size_t end = text.size();
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                           text[end - 1] == '\n'))
        end--;
    return std::string(text.substr(start, end - start));
}

bool appendFileList(const std::string& path, std::vector<std::string>& files) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Failed to open file list: " << path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string cleaned = trim(line);
        if (cleaned.empty())
            continue;
        if (cleaned.rfind("#", 0) == 0 || cleaned.rfind("//", 0) == 0)
            continue;
        files.push_back(cleaned);
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> inputFiles;
    std::string topName;
    std::string astOutPath;
    std::string cppOutDir;
    bool runSim = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ast-out" && i + 1 < argc) {
            astOutPath = argv[++i];
        } else if (arg == "--cpp-out" && i + 1 < argc) {
            cppOutDir = argv[++i];
        } else if (arg == "--no-sim") {
            runSim = false;
        } else if (arg == "--top" && i + 1 < argc) {
            topName = argv[++i];
        } else if (arg == "-file" && i + 1 < argc) {
            while (i + 1 < argc) {
                if (argv[i + 1][0] == '-')
                    break;
                std::string path = argv[++i];
                if (isFileList(path)) {
                    if (!appendFileList(path, inputFiles))
                        return 1;
                } else {
                    inputFiles.push_back(path);
                }
            }
        } else {
            inputFiles.push_back(arg);
        }
    }

    if (inputFiles.empty()) {
        std::cerr << "No input files provided\n";
        return 1;
    }

    if (topName.empty()) {
        std::cerr << "Missing required --top <module> argument\n";
        return 1;
    }

    slang::ast::Compilation compilation;
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> trees;
    for (const auto& path : inputFiles) {
        auto tree = sim::loadFile(path);
        if (!tree)
            return 1;
        trees.push_back(*tree);
    }

    for (const auto& tree : trees)
        compilation.addSyntaxTree(tree);

    const auto& diags = compilation.getAllDiagnostics();
    if (!diags.empty()) {
        const auto* sourceManager = compilation.getSourceManager();
        if (sourceManager) {
            std::string report = slang::DiagnosticEngine::reportAll(*sourceManager, diags);
            if (!report.empty())
                std::cerr << report;
        }
    }
    if (compilation.hasIssuedErrors())
        return 1;

    if (!astOutPath.empty()) {
        if (!sim::writeAstJson(trees, astOutPath))
            return 1;
    }

    auto* top = sim::findTop(compilation, topName);
    if (!top) {
        std::cerr << "Top module " << topName << " not found\n";
        return 1;
    }

    if (!cppOutDir.empty()) {
        if (!sim::writeCppOutput(*top, cppOutDir))
            return 1;
    }

    if (runSim) {
        sim::Simulator sim(compilation, *top);
        sim.build();
        sim.run();
    }
    return 0;
}

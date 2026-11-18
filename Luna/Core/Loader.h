// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Ast.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Luwu::Dsl
{

struct SourceFile
{
    std::string path;                        // as shown in diagnostics and depfiles, e.g. "defs/lowering/arith.luna"
    std::string canonicalPath;               // used for include-once and cycle detection
    std::optional<SourceLocation> includedFrom; // the include form that first pulled this file in (none for the root)
    std::vector<size_t> includes;            // files included directly, in order (indices into files())
};

// Loads a root .luna file and everything it includes. Each (include "path") is replaced in place by the top-level
// forms of the included file, so definitions keep their source order. Files are included at most once; including a
// file that is still being loaded is a cycle and an error.
class SourceLoader
{
public:
    std::vector<SExpr> load(const std::string& rootPath);

    // Every loaded file, root first, in the order they were first included
    const std::vector<SourceFile>& files() const { return loadedFiles; }

    // Locations of the include forms that led to `path`, innermost first (empty for the root or unknown files)
    std::vector<SourceLocation> includeStack(const std::string& path) const;

private:
    void loadFile(const std::string& path, const std::string& canonicalPath, std::optional<SourceLocation> includedFrom,
        std::vector<SExpr>& out);
    std::string resolveInclude(const SExpr& form, const std::string& includerPath, std::string& canonicalPath) const;

    std::string sourceRoot; // canonical directory of the root file; includes may not leave it
    std::vector<SourceFile> loadedFiles;
    std::unordered_map<std::string, size_t> fileByCanonicalPath;
    std::unordered_map<std::string, size_t> fileByPath;
    std::vector<std::string> activeChain; // display paths of the files currently being loaded, outermost first
};

} // namespace Luwu::Dsl

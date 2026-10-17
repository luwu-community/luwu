// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Loader.h"

#include "CompileError.h"
#include "Lexer.h"
#include "Parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Luwu::Dsl
{

static bool isIncludeForm(const SExpr& expr)
{
    return expr.kind == SExprKind::List && expr.isList("include");
}

// include is a top-level form; anywhere else it would be evaluated as an (unknown) operation
static void rejectNestedIncludes(const SExpr& expr)
{
    for (const auto& el : expr.elements)
    {
        if (isIncludeForm(el))
            throw CompileError(el, "include is only allowed at the top level of a file");
        rejectNestedIncludes(el);
    }
}

std::vector<SExpr> SourceLoader::load(const std::string& rootPath)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::path(rootPath), ec);
    if (ec || !fs::is_regular_file(canonical))
        throw CompileError(SourceLocation{rootPath, 0, 0}, "Could not open input file");

    sourceRoot = canonical.parent_path().generic_string();

    std::vector<SExpr> program;
    loadFile(rootPath, canonical.generic_string(), std::nullopt, program);
    return program;
}

std::vector<SourceLocation> SourceLoader::includeStack(const std::string& path) const
{
    std::vector<SourceLocation> stack;
    for (auto it = fileByPath.find(path); it != fileByPath.end();)
    {
        const SourceFile& file = loadedFiles[it->second];
        if (!file.includedFrom)
            break;
        stack.push_back(*file.includedFrom);
        it = fileByPath.find(file.includedFrom->file);
    }
    return stack;
}

std::string SourceLoader::resolveInclude(const SExpr& form, const std::string& includerPath, std::string& canonicalPath) const
{
    if (form.elements.size() != 2 || !form.elements[1].isString())
        throw CompileError(form, "include expects a single string path: (include \"file.luna\")");

    const std::string& target = form.elements[1].text;
    fs::path targetPath(target);
    if (target.empty() || targetPath.is_absolute())
        throw CompileError(form.elements[1], "include path must be relative to the including file: \"" + target + "\"");

    // Paths are relative to the including file; diagnostics keep them relative to the working directory like the root
    std::string path = (fs::path(includerPath).parent_path() / targetPath).lexically_normal().generic_string();

    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
    if (ec || !fs::is_regular_file(canonical))
        throw CompileError(form.elements[1], "Could not open included file \"" + target + "\" (resolved to " + path + ")");

    fs::path relative = canonical.lexically_relative(sourceRoot);
    if (relative.empty() || *relative.begin() == "..")
        throw CompileError(form.elements[1], "include path \"" + target + "\" leaves the source root " + sourceRoot);

    canonicalPath = canonical.generic_string();
    return path;
}

void SourceLoader::loadFile(const std::string& path, const std::string& canonicalPath, std::optional<SourceLocation> includedFrom,
    std::vector<SExpr>& out)
{
    std::ifstream stream(path);
    if (!stream)
        throw CompileError(includedFrom ? *includedFrom : SourceLocation{path, 0, 0}, "Could not open " + path);

    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string source = buffer.str();

    size_t fileIndex = loadedFiles.size();
    loadedFiles.push_back({path, canonicalPath, includedFrom, {}});
    fileByCanonicalPath[canonicalPath] = fileIndex;
    fileByPath[path] = fileIndex;

    Lexer lexer(source, path);
    Parser parser(lexer);
    std::vector<SExpr> forms = parser.parseProgram();

    activeChain.push_back(path);

    for (auto& form : forms)
    {
        rejectNestedIncludes(form);

        if (!isIncludeForm(form))
        {
            out.push_back(std::move(form));
            continue;
        }

        std::string includedCanonical;
        std::string includedPath = resolveInclude(form, path, includedCanonical);

        auto existing = fileByCanonicalPath.find(includedCanonical);
        if (existing != fileByCanonicalPath.end())
        {
            const SourceFile& file = loadedFiles[existing->second];

            for (const auto& active : activeChain)
            {
                if (active != file.path)
                    continue;

                std::string chain;
                bool inCycle = false;
                for (const auto& p : activeChain)
                {
                    inCycle = inCycle || p == file.path;
                    if (inCycle)
                        chain += p + " -> ";
                }
                throw CompileError(form, "Include cycle: " + chain + file.path);
            }

            // Already loaded through another include: its definitions are in the program once
            loadedFiles[fileIndex].includes.push_back(existing->second);
            continue;
        }

        loadedFiles[fileIndex].includes.push_back(loadedFiles.size());
        loadFile(includedPath, includedCanonical, form.loc, out);
    }

    activeChain.pop_back();
}

} // namespace Luwu::Dsl

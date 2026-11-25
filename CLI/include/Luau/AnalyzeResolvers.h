// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Config.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/Location.h"
#include "Luau/TypeCheckLimits.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Luwu: the file and config resolvers luwu-analyze checks with, shared with the Analysis C API (luaanalysis.h) so
// both resolve requires and read `.luaurc`/`.config.luau` files the same way.

// Reads modules from disk, naming each by its absolute path, and resolves requires with the Luau require-by-string
// navigator.
struct CliFileResolver : Luau::FileResolver
{
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override;
    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* node, const Luau::TypeCheckLimits& limits) override;
    std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override;

    // Luwu: a module is named by its absolute path, which is what `resolveModule` gets from the require
    // navigator. A file passed on the command line has to be named the same way, or a file that is both
    // passed and required is checked (and reported) twice, once under each spelling.
    std::string moduleNameForPath(const std::string& path) const;

    std::optional<std::string> cwd;

    CliFileResolver();
};

// Reads `.luaurc` and `.config.luau` files from a module's directory and every directory above it.
struct CliConfigResolver : Luau::ConfigResolver
{
    Luau::Config defaultConfig;

    mutable std::unordered_map<std::string, Luau::Config> configCache;
    mutable std::vector<std::pair<std::string, std::string>> configErrors;

    // Luwu: one copy of a directory's config per language, since a `.luau` and a `.luwu` file there share everything else.
    mutable std::map<std::pair<std::string, Luau::Language>, Luau::Config> languageConfigCache;

    CliConfigResolver(Luau::Mode mode);

    const Luau::Config& getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits& limits) const override;
    const Luau::Config& readConfigRec(const std::string& path, const Luau::TypeCheckLimits& limits) const;
};

enum class DiagnosticSeverity
{
    Error,
    Warning,
    Note,
};

// One reported line: a type error, a lint or a note, named the way luwu-analyze prints it.
struct AnalyzeDiagnostic
{
    std::string moduleName;
    Luau::Location location;
    DiagnosticSeverity severity;
    // "TypeError", "SyntaxError", "Typo", "Note" or the lint's name
    std::string kind;
    std::string message;
    // The error is "Unknown require: unsupported path", which a host with its own resolver may explain better.
    bool unresolvedRequire = false;
};

// The diagnostics of one checked module, in the order luwu-analyze prints them: each error followed by its notes,
// the notes about no error, then lint errors and lint warnings. `notationUsed` collects the mismatch path notations the
// errors use, for `Luau::mismatchNotationLegend`.
std::vector<AnalyzeDiagnostic> collectModuleDiagnostics(
    Luau::Frontend& frontend,
    const Luau::ModuleName& name,
    const Luau::CheckResult& result,
    uint8_t& notationUsed
);

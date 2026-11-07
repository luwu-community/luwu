// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/AnalyzeResolvers.h"

#include "Luau/AnalyzeRequirer.h"
#include "Luau/Error.h"
#include "Luau/FileUtils.h"
#include "Luau/LuauConfig.h"
#include "Luau/ModuleResolver.h"
#include "Luau/RequireNavigator.h"
#include "Luau/StringUtils.h"
#include "Luau/TimeTrace.h"

#include "lua.h"

struct LuauConfigInterruptInfo
{
    Luau::TypeCheckLimits limits;
    std::string module;
};

static void initConfigInterrupt(lua_State* L, LuauConfigInterruptInfo* info)
{
    lua_setthreaddata(L, info);
}

static void configInterrupt(lua_State* L, int gc)
{
    LuauConfigInterruptInfo* info = static_cast<LuauConfigInterruptInfo*>(lua_getthreaddata(L));
    if (info->limits.finishTime && Luau::TimeTrace::getClock() > *info->limits.finishTime)
        throw Luau::TimeLimitError{info->module};
    if (info->limits.cancellationToken && info->limits.cancellationToken->requested())
        throw Luau::UserCancelError{info->module};
}

CliFileResolver::CliFileResolver()
    : cwd(getCurrentWorkingDirectory())
{
}

std::optional<Luau::SourceCode> CliFileResolver::readSource(const Luau::ModuleName& name)
{
    Luau::SourceCode::Type sourceType;
    std::optional<std::string> source = std::nullopt;

    // If the module name is "-", then read source from stdin
    if (name == "-")
    {
        source = readStdin();
        sourceType = Luau::SourceCode::Script;
    }
    else
    {
        source = readFile(name);
        sourceType = Luau::SourceCode::Module;
    }

    if (!source)
        return std::nullopt;

    Luau::SourceCode code{*source, sourceType};
    code.luwon = isLuwonPath(name);
    return code;
}

std::optional<Luau::ModuleInfo> CliFileResolver::resolveModule(
    const Luau::ModuleInfo* context,
    Luau::AstExpr* node,
    const Luau::TypeCheckLimits& limits
)
{
    if (Luau::AstExprConstantString* expr = node->as<Luau::AstExprConstantString>())
    {
        std::string path{expr->value.data, expr->value.size};

        FileNavigationContext navigationContext{context->name};

        LuauConfigInterruptInfo info = {limits, path};
        navigationContext.luauConfigInit = [&info](lua_State* L)
        {
            initConfigInterrupt(L, &info);
        };
        navigationContext.luauConfigInterrupt = configInterrupt;

        Luau::Require::ErrorHandler nullErrorHandler{};

        Luau::Require::Navigator navigator(navigationContext, nullErrorHandler);
        if (navigator.navigate(std::move(path)) != Luau::Require::Navigator::Status::Success)
            return std::nullopt;

        if (!navigationContext.isModulePresent())
            return std::nullopt;

        if (std::optional<std::string> identifier = navigationContext.getIdentifier())
            return {{*identifier}};
    }

    return std::nullopt;
}

std::string CliFileResolver::getHumanReadableModuleName(const Luau::ModuleName& name) const
{
    if (name == "-")
        return "stdin";
    // modules are named by absolute path (see `moduleNameForPath`); show them the way they'd be typed
    if (cwd && name.size() > cwd->size() + 1 && name.compare(0, cwd->size(), *cwd) == 0 && name[cwd->size()] == '/')
        return name.substr(cwd->size() + 1);
    return name;
}

std::string CliFileResolver::moduleNameForPath(const std::string& path) const
{
    if (path == "-")
        return path;
    if (!cwd || isAbsolutePath(path))
        return normalizePath(path);
    return normalizePath(*cwd + "/" + path);
}

CliConfigResolver::CliConfigResolver(Luau::Mode mode)
{
    defaultConfig.mode = mode;
}

const Luau::Config& CliConfigResolver::getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits& limits) const
{
    std::optional<std::string> path = getParentPath(name);
    const Luau::Config& directoryConfig = path ? readConfigRec(*path, limits) : defaultConfig;

    Luau::Language language = languageOfPath(name);
    if (directoryConfig.language == language)
        return directoryConfig;

    auto [it, inserted] = languageConfigCache.try_emplace({path.value_or(""), language}, directoryConfig);
    if (inserted)
        it->second.language = language;
    return it->second;
}

const Luau::Config& CliConfigResolver::readConfigRec(const std::string& path, const Luau::TypeCheckLimits& limits) const
{
    auto it = configCache.find(path);
    if (it != configCache.end())
        return it->second;

    std::optional<std::string> parent = getParentPath(path);
    Luau::Config result = parent ? readConfigRec(*parent, limits) : defaultConfig;

    std::optional<std::string> configPath = joinPaths(path, Luau::kConfigName);
    if (!isFile(*configPath))
        configPath = std::nullopt;

    std::optional<std::string> luauConfigPath = joinPaths(path, Luau::kLuauConfigName);
    if (!isFile(*luauConfigPath))
        luauConfigPath = std::nullopt;

    if (configPath && luauConfigPath)
    {
        std::string ambiguousError = Luau::format("Both %s and %s files exist", Luau::kConfigName, Luau::kLuauConfigName);
        configErrors.emplace_back(*configPath, std::move(ambiguousError));
    }
    else if (configPath)
    {
        if (std::optional<std::string> contents = readFile(*configPath))
        {
            Luau::ConfigOptions::AliasOptions aliasOpts;
            aliasOpts.configLocation = *configPath;
            aliasOpts.overwriteAliases = true;

            Luau::ConfigOptions opts;
            opts.aliasOptions = std::move(aliasOpts);

            std::optional<std::string> error = Luau::parseConfig(*contents, result, opts);
            if (error)
                configErrors.emplace_back(*configPath, *error);
        }
    }
    else if (luauConfigPath)
    {
        if (std::optional<std::string> contents = readFile(*luauConfigPath))
        {
            Luau::ConfigOptions::AliasOptions aliasOpts;
            // Luwu: upstream reads `*configPath` here, which is empty in this branch.
            aliasOpts.configLocation = *luauConfigPath;
            aliasOpts.overwriteAliases = true;

            Luau::InterruptCallbacks callbacks;
            LuauConfigInterruptInfo info{limits, *luauConfigPath};
            callbacks.initCallback = [&info](lua_State* L)
            {
                initConfigInterrupt(L, &info);
            };
            callbacks.interruptCallback = configInterrupt;

            std::optional<std::string> error = Luau::extractLuauConfig(*contents, result, aliasOpts, std::move(callbacks));
            if (error)
                configErrors.emplace_back(*luauConfigPath, *error);
        }
    }

    return configCache[path] = result;
}

static AnalyzeDiagnostic describeTypeError(const Luau::Frontend& frontend, const Luau::TypeError& error)
{
    AnalyzeDiagnostic diagnostic;
    diagnostic.moduleName = frontend.fileResolver->getHumanReadableModuleName(error.moduleName);
    diagnostic.location = error.location;
    diagnostic.severity = DiagnosticSeverity::Error;

    if (const Luau::SyntaxError* syntaxError = Luau::get_if<Luau::SyntaxError>(&error.data))
    {
        diagnostic.kind = "SyntaxError";
        diagnostic.message = syntaxError->message;
        return diagnostic;
    }

    // Luwu (helpful subtyping errors): a misspelled name is labeled as one, so it isn't read as a broken type.
    // Luwu doc references: errors in doc comments are labeled apart, so they don't read as broken code
    if (Luau::isTypo(error))
        diagnostic.kind = "Typo";
    else if (Luau::get_if<Luau::DocSyntaxError>(&error.data))
        diagnostic.kind = "DocSyntaxError";
    else if (error.inDocComment)
        diagnostic.kind = "DocTypeError";
    else
        diagnostic.kind = "TypeError";

    diagnostic.message = Luau::toString(error, Luau::TypeErrorToStringOptions{frontend.fileResolver});

    const Luau::UnknownRequire* unknownRequire = Luau::get_if<Luau::UnknownRequire>(&error.data);
    diagnostic.unresolvedRequire = unknownRequire && unknownRequire->modulePath.empty();
    return diagnostic;
}

static AnalyzeDiagnostic describeLint(const std::string& moduleName, const Luau::LintWarning& warning, DiagnosticSeverity severity)
{
    AnalyzeDiagnostic diagnostic;
    diagnostic.moduleName = moduleName;
    diagnostic.location = warning.location;
    diagnostic.severity = severity;
    diagnostic.kind = Luau::LintWarning::getName(warning.code);
    diagnostic.message = warning.text;
    return diagnostic;
}

std::vector<AnalyzeDiagnostic> collectModuleDiagnostics(
    Luau::Frontend& frontend,
    const Luau::ModuleName& name,
    const Luau::CheckResult& result,
    uint8_t& notationUsed
)
{
    std::vector<AnalyzeDiagnostic> diagnostics;
    std::string humanReadableName = frontend.fileResolver->getHumanReadableModuleName(name);

    // Luwu: notes (Module::notes) follow the error they're about, and any about no error reported here come after
    Luau::ModulePtr module = frontend.moduleResolver.getModule(name);
    std::vector<bool> noteReported(module ? module->notes.size() : 0);
    auto addNotes = [&](std::optional<Luau::Location> origin)
    {
        for (size_t i = 0; i < noteReported.size(); ++i)
        {
            const Luau::DiagnosticNote& note = module->notes[i];
            if (noteReported[i] || (origin && note.origin != origin))
                continue;

            AnalyzeDiagnostic diagnostic;
            diagnostic.moduleName = humanReadableName;
            diagnostic.location = note.location;
            diagnostic.severity = DiagnosticSeverity::Note;
            diagnostic.kind = "Note";
            diagnostic.message = note.message;
            diagnostics.push_back(std::move(diagnostic));
            noteReported[i] = true;
        }
    };

    for (const Luau::TypeError& error : result.errors)
    {
        const Luau::TypeMismatch* mismatch = Luau::get_if<Luau::TypeMismatch>(&error.data);
        if (mismatch && mismatch->explained)
            notationUsed |= mismatch->explained->notation;

        diagnostics.push_back(describeTypeError(frontend, error));
        if (error.moduleName == name)
            addNotes(error.location);
    }
    addNotes(std::nullopt);

    for (const Luau::LintWarning& error : result.lintResult.errors)
        diagnostics.push_back(describeLint(humanReadableName, error, DiagnosticSeverity::Error));
    for (const Luau::LintWarning& warning : result.lintResult.warnings)
        diagnostics.push_back(describeLint(humanReadableName, warning, DiagnosticSeverity::Warning));

    return diagnostics;
}

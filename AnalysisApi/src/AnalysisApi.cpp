// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "luaanalysis.h"

#include "Luau/AnalyzeResolvers.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Error.h"
#include "Luau/FileUtils.h"
#include "Luau/Flags.h"
#include "Luau/Frontend.h"
#include "Luau/StringUtils.h"

#include "lua.h"

#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Why a host's Lua resolver couldn't resolve one require
struct ResolveFailure
{
    Luau::Location location;
    std::string message;
};

// Resolves requires through a host's Lua function when it has one, and by the Luau require-by-string rules otherwise.
struct LuaFileResolver : CliFileResolver
{
    lua_State* L = nullptr;
    int ref = LUA_NOREF;

    // Each requirer's failed requires, kept across checks: a module checked once isn't traced again until it changes
    std::unordered_map<Luau::ModuleName, std::vector<ResolveFailure>> failures;

    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* node, const Luau::TypeCheckLimits& limits)
        override
    {
        if (!L || ref == LUA_NOREF)
            return CliFileResolver::resolveModule(context, node, limits);

        Luau::AstExprConstantString* expr = node->as<Luau::AstExprConstantString>();
        if (!context || !expr)
            return std::nullopt;

        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushlstring(L, context->name.data(), context->name.size());
        lua_pushlstring(L, expr->value.data, expr->value.size);

        std::optional<Luau::ModuleInfo> resolved;
        std::string failure;
        if (lua_pcall(L, 2, 2, 0) != LUA_OK)
            failure = describeRaisedError(-1);
        else if (lua_type(L, -2) == LUA_TSTRING)
            resolved = Luau::ModuleInfo{moduleNameForPath(lua_tostring(L, -2))};
        else if (lua_type(L, -1) == LUA_TSTRING)
            failure = lua_tostring(L, -1);
        else
            failure = "The require resolver returned no path for this require";
        lua_settop(L, top);

        if (!resolved)
            recordFailure(context->name, expr->location, std::move(failure));
        return resolved;
    }

    std::string describeRaisedError(int index)
    {
        if (const char* message = lua_tostring(L, index))
            return Luau::format("The require resolver raised an error: %s", message);
        return "The require resolver raised an error";
    }

    void recordFailure(const Luau::ModuleName& requirer, const Luau::Location& location, std::string message)
    {
        std::vector<ResolveFailure>& moduleFailures = failures[requirer];
        for (ResolveFailure& existing : moduleFailures)
        {
            if (existing.location == location)
            {
                existing.message = std::move(message);
                return;
            }
        }
        moduleFailures.push_back({location, std::move(message)});
    }

    // The resolver's message for the unresolved require that `errorLocation` (the require call) holds
    const std::string* failureAt(const Luau::ModuleName& requirer, const Luau::Location& errorLocation) const
    {
        auto it = failures.find(requirer);
        if (it == failures.end())
            return nullptr;

        for (const ResolveFailure& failure : it->second)
        {
            if (errorLocation.encloses(failure.location))
                return &failure.message;
        }
        return nullptr;
    }
};

struct luwu_Analysis
{
    LuaFileResolver fileResolver;
    CliConfigResolver configResolver{Luau::Mode::Strict};
    Luau::Frontend frontend;

    // Builtin and loaded definitions are frozen at the first check; nothing can be loaded after it.
    bool globalsFrozen = false;

    std::vector<Luau::ModuleName> queued;
    std::vector<AnalyzeDiagnostic> diagnostics;
    std::string legend;
    std::string lastError;
    bool hasLastError = false;

    luwu_Analysis()
        : frontend(Luau::SolverMode::New, &fileResolver, &configResolver, frontendOptions())
    {
        Luau::registerBuiltinGlobals(frontend, frontend.globals);
    }

    static Luau::FrontendOptions frontendOptions()
    {
        Luau::FrontendOptions options;
        options.runLintChecks = true;
        return options;
    }

    int fail(std::string message)
    {
        lastError = std::move(message);
        hasLastError = true;
        return 1;
    }

    // A diagnostic about the file `path` as a whole or about a config file, which has no checked module
    void addFileDiagnostic(const std::string& path, const char* kind, std::string message, Luau::Location location = {})
    {
        AnalyzeDiagnostic diagnostic;
        diagnostic.moduleName = fileResolver.getHumanReadableModuleName(fileResolver.moduleNameForPath(path));
        diagnostic.location = location;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.kind = kind;
        diagnostic.message = std::move(message);
        diagnostics.push_back(std::move(diagnostic));
    }

    int loadDefinitions(const std::string& path)
    {
        if (globalsFrozen)
            return fail(Luau::format("Load '%s' before the first check; definitions can't change after it", path.c_str()));

        diagnostics.clear();
        legend.clear();

        std::optional<std::string> source = readFile(path);
        if (!source)
            return fail(Luau::format("Can't read the definition file '%s'", path.c_str()));

        Luau::LoadDefinitionFileResult result =
            frontend.loadDefinitionFile(frontend.globals, frontend.globals.globalScope, *source, "@" + path, /* captureComments */ false);

        for (const Luau::ParseError& error : result.parseResult.errors)
            addFileDiagnostic(path, "SyntaxError", error.getMessage(), error.getLocation());

        if (result.module)
        {
            for (const Luau::TypeError& error : result.module->errors)
                addFileDiagnostic(path, "TypeError", Luau::toString(error, Luau::TypeErrorToStringOptions{&fileResolver}), error.location);
        }

        if (!result.success)
            return fail(Luau::format("The definition file '%s' has errors and didn't load", path.c_str()));
        return 0;
    }

    void freezeGlobals()
    {
        if (globalsFrozen)
            return;
        Luau::freeze(frontend.globals.globalTypes);
        globalsFrozen = true;
    }

    // Replaces "Unknown require: unsupported path" with the host resolver's reason, when it gave one.
    void explainUnresolvedRequire(const Luau::ModuleName& name, AnalyzeDiagnostic& diagnostic) const
    {
        if (!diagnostic.unresolvedRequire)
            return;

        if (const std::string* message = fileResolver.failureAt(name, diagnostic.location))
        {
            diagnostic.kind = "RequireError";
            diagnostic.message = *message;
        }
    }

    void collectDiagnostics(const std::vector<Luau::ModuleName>& names)
    {
        uint8_t notationUsed = 0;
        std::unordered_set<Luau::ModuleName> reported;

        for (const Luau::ModuleName& name : names)
        {
            if (!reported.insert(name).second)
                continue;

            std::optional<Luau::CheckResult> result = frontend.getCheckResult(name, false);
            if (!result || !frontend.getSourceModule(name))
            {
                addFileDiagnostic(name, "ReadError", "Can't read this file");
                continue;
            }

            for (AnalyzeDiagnostic& diagnostic : collectModuleDiagnostics(frontend, name, *result, notationUsed))
            {
                explainUnresolvedRequire(name, diagnostic);
                diagnostics.push_back(std::move(diagnostic));
            }
        }

        for (const auto& [path, message] : configResolver.configErrors)
            addFileDiagnostic(path, "ConfigError", message);
        configResolver.configErrors.clear();

        legend = Luau::mismatchNotationLegend(notationUsed, Luau::Helpifier{});
    }

    int check()
    {
        freezeGlobals();

        std::vector<Luau::ModuleName> names = std::move(queued);
        queued.clear();
        diagnostics.clear();
        legend.clear();

        frontend.queueModuleCheck(names);
        frontend.checkQueuedModules();

        collectDiagnostics(names);
        return 0;
    }
};

// Runs one API call, turning any exception it throws into a failing status. An exception must never reach the caller:
// it may be C or Rust, which can't unwind C++ frames.
template<typename F>
static int guarded(luwu_Analysis* analysis, F&& call)
{
    analysis->hasLastError = false;
    try
    {
        return call();
    }
    catch (const Luau::InternalCompilerError& error)
    {
        std::string where;
        if (error.moduleName)
            where = analysis->fileResolver.getHumanReadableModuleName(*error.moduleName);
        if (error.location)
            where += Luau::format("(%d,%d)", error.location->begin.line + 1, error.location->begin.column + 1);
        if (where.empty())
            return analysis->fail("Internal compiler error: " + error.message);
        return analysis->fail(where + ": internal compiler error: " + error.message);
    }
    catch (const std::exception& error)
    {
        return analysis->fail(error.what());
    }
    catch (...)
    {
        return analysis->fail("Unknown error");
    }
}

void luwu_analysis_enabledefaultflags()
{
    setLuauFlagsDefault();
}

luwu_Analysis* luwu_analysis_new()
{
    try
    {
        return new luwu_Analysis();
    }
    catch (...)
    {
        return nullptr;
    }
}

void luwu_analysis_free(luwu_Analysis* analysis)
{
    try
    {
        delete analysis;
    }
    catch (...)
    {
    }
}

int luwu_analysis_loaddefinitions(luwu_Analysis* analysis, const char* path)
{
    return guarded(
        analysis,
        [&]
        {
            return analysis->loadDefinitions(path);
        }
    );
}

void luwu_analysis_setresolver(luwu_Analysis* analysis, lua_State* L, int ref)
{
    analysis->fileResolver.L = L;
    analysis->fileResolver.ref = ref;
}

void luwu_analysis_queue(luwu_Analysis* analysis, const char* path)
{
    guarded(
        analysis,
        [&]
        {
            analysis->queued.push_back(analysis->fileResolver.moduleNameForPath(path));
            return 0;
        }
    );
}

int luwu_analysis_check(luwu_Analysis* analysis)
{
    return guarded(
        analysis,
        [&]
        {
            return analysis->check();
        }
    );
}

size_t luwu_analysis_diagnosticcount(luwu_Analysis* analysis)
{
    return analysis->diagnostics.size();
}

int luwu_analysis_getdiagnostic(luwu_Analysis* analysis, size_t index, luwu_Diagnostic* out)
{
    if (index >= analysis->diagnostics.size())
        return 1;

    const AnalyzeDiagnostic& diagnostic = analysis->diagnostics[index];
    out->module = diagnostic.moduleName.c_str();
    out->kind = diagnostic.kind.c_str();
    out->message = diagnostic.message.c_str();
    out->beginline = int(diagnostic.location.begin.line);
    out->begincolumn = int(diagnostic.location.begin.column);
    out->endline = int(diagnostic.location.end.line);
    out->endcolumn = int(diagnostic.location.end.column);

    switch (diagnostic.severity)
    {
    case DiagnosticSeverity::Error:
        out->severity = LUWU_DIAGNOSTIC_ERROR;
        break;
    case DiagnosticSeverity::Warning:
        out->severity = LUWU_DIAGNOSTIC_WARNING;
        break;
    case DiagnosticSeverity::Note:
        out->severity = LUWU_DIAGNOSTIC_NOTE;
        break;
    }
    return 0;
}

const char* luwu_analysis_legend(luwu_Analysis* analysis)
{
    if (analysis->legend.empty())
        return nullptr;
    return analysis->legend.c_str();
}

const char* luwu_analysis_lasterror(luwu_Analysis* analysis)
{
    if (!analysis->hasLastError)
        return nullptr;
    return analysis->lastError.c_str();
}

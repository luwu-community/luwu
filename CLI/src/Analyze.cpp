// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Config.h"
#include "Luau/Frontend.h"
#include "Luau/LuauConfig.h"
#include "Luau/ModuleResolver.h"
#include "Luau/PrettyPrinter.h"
#include "Luau/StringUtils.h"
#include "Luau/TimeTrace.h"
#include "Luau/TypeAttach.h"
#include "Luau/TypeCheckLimits.h"
#include "Luau/TypeInfer.h"

#include "Luau/AnalyzeResolvers.h"
#include "Luau/FileUtils.h"
#include "Luau/Flags.h"
#include "Luau/RequireNavigator.h"

#include "lua.h"
#include "lualib.h"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <fstream>

#ifdef CALLGRIND
#include <valgrind/callgrind.h>
#endif

LUAU_FASTFLAG(DebugLuauTimeTracing)
LUAU_FASTFLAG(DebugLuauLogSolverToJsonFile)

enum class ReportFormat
{
    Default,
    Luacheck,
    Gnu,
};

static void report(ReportFormat format, const char* name, const Luau::Location& loc, const char* type, const char* message)
{
    switch (format)
    {
    case ReportFormat::Default:
        fprintf(stderr, "%s(%d,%d): %s: %s\n", name, loc.begin.line + 1, loc.begin.column + 1, type, message);
        break;

    case ReportFormat::Luacheck:
    {
        // Note: luacheck's end column is inclusive but our end column is exclusive
        // In addition, luacheck doesn't support multi-line messages, so if the error is multiline we'll fake end column as 100 and hope for the best
        int columnEnd = (loc.begin.line == loc.end.line) ? loc.end.column : 100;

        // Use stdout to match luacheck behavior
        fprintf(stdout, "%s:%d:%d-%d: (W0) %s: %s\n", name, loc.begin.line + 1, loc.begin.column + 1, columnEnd, type, message);
        break;
    }

    case ReportFormat::Gnu:
        // Note: GNU end column is inclusive but our end column is exclusive
        fprintf(stderr, "%s:%d.%d-%d.%d: %s: %s\n", name, loc.begin.line + 1, loc.begin.column + 1, loc.end.line + 1, loc.end.column, type, message);
        break;
    }
}

static void reportDiagnostic(ReportFormat format, const AnalyzeDiagnostic& diagnostic)
{
    report(format, diagnostic.moduleName.c_str(), diagnostic.location, diagnostic.kind.c_str(), diagnostic.message.c_str());
}

// Luwu (helpful subtyping errors): the path notation used by every mismatch reported, so its legend is
// printed once after all of them rather than once per error.
static uint8_t mismatchNotationUsed = 0;

static bool reportModuleResult(Luau::Frontend& frontend, const Luau::ModuleName& name, ReportFormat format, bool annotate)
{
    std::optional<Luau::CheckResult> cr = frontend.getCheckResult(name, false);

    if (!cr)
    {
        fprintf(stderr, "Failed to find result for %s\n", name.c_str());
        return false;
    }

    if (!frontend.getSourceModule(name))
    {
        fprintf(stderr, "Error opening %s\n", name.c_str());
        return false;
    }

    for (const AnalyzeDiagnostic& diagnostic : collectModuleDiagnostics(frontend, name, *cr, mismatchNotationUsed))
        reportDiagnostic(format, diagnostic);

    if (annotate)
    {
        Luau::SourceModule* sm = frontend.getSourceModule(name);
        Luau::ModulePtr m = frontend.moduleResolver.getModule(name);

        Luau::attachTypeData(*sm, *m);

        std::string annotated = Luau::prettyPrintWithTypes(*sm->root);

        printf("%s", annotated.c_str());
    }

    return cr->errors.empty() && cr->lintResult.errors.empty();
}

static void displayHelp(const char* argv0)
{
    printf("Usage: %s [--mode] [options] [file list]\n", argv0);
    printf("\n");
    printf("Available modes:\n");
    printf("  omitted: typecheck and lint input files\n");
    printf("  --annotate: typecheck input files and output source with type annotations\n");
    printf("\n");
    printf("Available options:\n");
    printf("  --formatter=plain: report analysis errors in Luacheck-compatible format\n");
    printf("  --formatter=gnu: report analysis errors in GNU-compatible format\n");
    printf("  --mode=nonstrict: default to nonstrict mode when typechecking (Luwu defaults to strict)\n");
    printf("  --mode=strict: default to strict mode when typechecking (the default)\n");
    printf("  --solver={new|old}: selects which typechecker to use (defaults to the new solver)\n");
    printf("  --timetrace: record compiler time tracing information into trace.json\n");
    printf("  --defs=<path>: load a definition file (`declare` globals) before checking; can be repeated\n");
}

static int assertionHandler(const char* expr, const char* file, int line, const char* function)
{
    printf("%s(%d): ASSERTION FAILED: %s\n", file, line, expr);
    fflush(stdout);
    return 1;
}

struct TaskScheduler
{
    TaskScheduler(unsigned threadCount)
        : threadCount(threadCount)
    {
        for (unsigned i = 0; i < threadCount; i++)
        {
            workers.emplace_back(
                [this]
                {
                    workerFunction();
                }
            );
        }
    }

    ~TaskScheduler()
    {
        for (unsigned i = 0; i < threadCount; i++)
            push({});

        for (std::thread& worker : workers)
            worker.join();
    }

    std::function<void()> pop()
    {
        std::unique_lock guard(mtx);

        cv.wait(
            guard,
            [this]
            {
                return !tasks.empty();
            }
        );

        std::function<void()> task = tasks.front();
        tasks.pop();
        return task;
    }

    void push(std::function<void()> task)
    {
        {
            std::unique_lock guard(mtx);
            tasks.push(std::move(task));
        }

        cv.notify_one();
    }

    static unsigned getThreadCount()
    {
        return std::max(std::thread::hardware_concurrency(), 1u);
    }

private:
    void workerFunction()
    {
        while (std::function<void()> task = pop())
            task();
    }

    unsigned threadCount = 1;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
};

int main(int argc, char** argv)
{
    Luau::assertHandler() = assertionHandler;

    setLuauFlagsDefault();

    if (argc >= 2 && strcmp(argv[1], "--help") == 0)
    {
        displayHelp(argv[0]);
        return 0;
    }

    ReportFormat format = ReportFormat::Default;
    // matches Config::mode: Luwu checks strictly unless a file, a .luaurc or `--mode=` says otherwise
    Luau::Mode mode = Luau::Mode::Strict;
    bool annotate = false;
    int threadCount = 0;
    std::string basePath = "";
    Luau::SolverMode solverMode = Luau::SolverMode::New;
    std::vector<std::string> definitionFiles;

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i][0] != '-')
            continue;

        if (strcmp(argv[i], "--formatter=plain") == 0)
            format = ReportFormat::Luacheck;
        else if (strcmp(argv[i], "--formatter=gnu") == 0)
            format = ReportFormat::Gnu;
        else if (strcmp(argv[i], "--mode=strict") == 0)
            mode = Luau::Mode::Strict;
        else if (strcmp(argv[i], "--mode=nonstrict") == 0)
            mode = Luau::Mode::Nonstrict;
        else if (strcmp(argv[i], "--annotate") == 0)
            annotate = true;
        else if (strcmp(argv[i], "--timetrace") == 0)
            FFlag::DebugLuauTimeTracing.value = true;
        else if (strncmp(argv[i], "--fflags=", 9) == 0)
            setLuauFlags(argv[i] + 9);
        else if (strncmp(argv[i], "-j", 2) == 0)
            threadCount = int(strtol(argv[i] + 2, nullptr, 10));
        else if (strncmp(argv[i], "--logbase=", 10) == 0)
            basePath = std::string{argv[i] + 10};
        else if (strcmp(argv[i], "--solver=old") == 0)
            solverMode = Luau::SolverMode::Old;
        else if (strncmp(argv[i], "--defs=", 7) == 0)
            definitionFiles.emplace_back(argv[i] + 7);
    }

#if !defined(LUAU_ENABLE_TIME_TRACE)
    if (FFlag::DebugLuauTimeTracing)
    {
        fprintf(stderr, "To run with --timetrace, Luau has to be built with LUAU_ENABLE_TIME_TRACE enabled\n");
        return 1;
    }
#endif

    Luau::FrontendOptions frontendOptions;
    frontendOptions.retainFullTypeGraphs = annotate;
    frontendOptions.runLintChecks = true;

    CliFileResolver fileResolver;
    CliConfigResolver configResolver(mode);

    Luau::Frontend frontend(solverMode, &fileResolver, &configResolver, frontendOptions);

    if (FFlag::DebugLuauLogSolverToJsonFile)
    {
        frontend.writeJsonLog = [&basePath](const Luau::ModuleName& moduleName, std::string log)
        {
            std::string path = moduleName + ".log.json";
            size_t pos = moduleName.find_last_of('/');
            if (pos != std::string::npos)
                path = moduleName.substr(pos + 1);

            if (!basePath.empty())
                path = joinPaths(basePath, path);

            std::ofstream os(path);

            os << log << std::endl;
            printf("Wrote JSON log to %s\n", path.c_str());
        };
    }

    Luau::registerBuiltinGlobals(frontend, frontend.globals);

    // Luwu: an embedder's globals (seal, lune, Roblox) come from a definition file, which only the
    // language server could load before.
    for (const std::string& path : definitionFiles)
    {
        std::optional<std::string> source = readFile(path);
        if (!source)
        {
            fprintf(stderr, "Error opening definition file %s\n", path.c_str());
            return 1;
        }

        Luau::LoadDefinitionFileResult result = frontend.loadDefinitionFile(
            frontend.globals, frontend.globals.globalScope, *source, "@" + path, /* captureComments */ false
        );
        if (!result.success)
        {
            for (const Luau::ParseError& error : result.parseResult.errors)
                report(format, path.c_str(), error.getLocation(), "SyntaxError", error.getMessage().c_str());

            if (result.module)
            {
                for (const Luau::TypeError& error : result.module->errors)
                    report(
                        format,
                        path.c_str(),
                        error.location,
                        "TypeError",
                        Luau::toString(error, Luau::TypeErrorToStringOptions{frontend.fileResolver}).c_str()
                    );
            }

            fprintf(stderr, "Failed to load definition file %s\n", path.c_str());
            return 1;
        }
    }

    Luau::freeze(frontend.globals.globalTypes);

#ifdef CALLGRIND
    CALLGRIND_ZERO_STATS;
#endif

    std::vector<std::string> files = getSourceFiles(argc, argv);

    for (const std::string& path : files)
        frontend.queueModuleCheck(fileResolver.moduleNameForPath(path));

    std::vector<Luau::ModuleName> checkedModules;

    // If thread count is not set, try to use HW thread count, but with an upper limit
    // When we improve scalability of typechecking, upper limit can be adjusted/removed
    if (threadCount <= 0)
        threadCount = std::min(TaskScheduler::getThreadCount(), 8u);

    try
    {
        TaskScheduler scheduler(threadCount);

        checkedModules = frontend.checkQueuedModules(
            std::nullopt,
            [&](std::vector<std::function<void()>> tasks)
            {
                for (auto& task : tasks)
                    scheduler.push(std::move(task));
            }
        );
    }
    catch (const Luau::InternalCompilerError& ice)
    {
        Luau::Location location = ice.location ? *ice.location : Luau::Location();

        std::string moduleName = ice.moduleName ? *ice.moduleName : "<unknown module>";
        std::string humanReadableName = frontend.fileResolver->getHumanReadableModuleName(moduleName);

        Luau::TypeError error(location, moduleName, Luau::InternalError{ice.message});

        report(
            format,
            humanReadableName.c_str(),
            location,
            "InternalCompilerError",
            Luau::toString(error, Luau::TypeErrorToStringOptions{frontend.fileResolver}).c_str()
        );
        return 1;
    }

    int failed = 0;

    for (const Luau::ModuleName& name : checkedModules)
        failed += !reportModuleResult(frontend, name, format, annotate);

    if (format != ReportFormat::Luacheck)
    {
        std::string legend = Luau::mismatchNotationLegend(mismatchNotationUsed, Luau::Helpifier{});
        if (!legend.empty())
            fprintf(stderr, "\n%s\n", legend.c_str());
    }

    if (!configResolver.configErrors.empty())
    {
        failed += int(configResolver.configErrors.size());

        for (const auto& pair : configResolver.configErrors)
            fprintf(stderr, "%s: %s\n", pair.first.c_str(), pair.second.c_str());
    }

    if (format == ReportFormat::Luacheck)
        return 0;
    else
        return failed ? 1 : 0;
}

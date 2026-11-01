// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <stddef.h>

// Luwu Analysis C API: type checks and lints Luau and Luwu files from C, or from any language with a C FFI. It reports
// what luwu-analyze reports, reading `.luaurc` and `.config.luau` files the same way.
//
// No call raises a C++ exception into its caller: a failure is returned as a status, and `luwu_analysis_lasterror`
// says what went wrong.

// can be used to reconfigure visibility/exports for public APIs
#ifndef LUAANALYSIS_API
#define LUAANALYSIS_API extern
#endif

typedef struct lua_State lua_State;

// A checker: the globals it checks against, its require resolver and every module it has checked. Modules are cached
// across checks, so a second check of an unchanged project is cheap.
typedef struct luwu_Analysis luwu_Analysis;

enum luwu_DiagnosticSeverity
{
    LUWU_DIAGNOSTIC_ERROR,
    LUWU_DIAGNOSTIC_WARNING,
    // Extra detail about the error reported just before it
    LUWU_DIAGNOSTIC_NOTE,
};

typedef struct luwu_Diagnostic
{
    // The file the diagnostic is in, relative to the current directory when it's under it
    const char* module;
    // "TypeError", "SyntaxError", "Typo", "Note", "RequireError", "ConfigError", "ReadError" or a lint's name ("LocalUnused")
    const char* kind;
    const char* message;
    int severity; // luwu_DiagnosticSeverity

    // Zero-based; the end column is exclusive
    int beginline;
    int begincolumn;
    int endline;
    int endcolumn;
} luwu_Diagnostic;

// Turns on every non-experimental `Luau*` and `Luwu*` fast flag: the language luwu-analyze and luwu-lsp check.
// Flags are process-wide, so this also changes the runtime of every Lua state in the process.
LUAANALYSIS_API void luwu_analysis_enabledefaultflags(void);

// A checker for the builtin globals, checking files strictly unless a file or config says otherwise.
LUAANALYSIS_API luwu_Analysis* luwu_analysis_new(void);
LUAANALYSIS_API void luwu_analysis_free(luwu_Analysis* analysis);

// Loads a definition file (`declare` statements) whose globals and types every checked module sees. Its errors replace
// the current diagnostics. Returns 0 when it loaded, or nonzero when it can't be read or doesn't parse. Definitions must be loaded
// before the first check.
LUAANALYSIS_API int luwu_analysis_loaddefinitions(luwu_Analysis* analysis, const char* path);

// Resolves requires with a Lua function instead of the Luau require-by-string rules.
//
// The function at `ref` in L's registry (from lua_ref) is called as `resolve(requirer, path)`, where `requirer` is the
// absolute path of the requiring file and `path` is the string passed to require. It returns the absolute path of the
// required file, or nil and a message saying why it can't be required, which is reported where the require is.
// L must stay valid until the checker is freed or the resolver is unset with ref LUA_NOREF. The checker never frees the
// ref.
LUAANALYSIS_API void luwu_analysis_setresolver(luwu_Analysis* analysis, lua_State* L, int ref);

// Adds a file to the next check.
LUAANALYSIS_API void luwu_analysis_queue(luwu_Analysis* analysis, const char* path);

// Checks the queued files and every module they require, replacing the diagnostics of the previous check with those of
// the queued files. Returns 0 when the check ran, whatever it found, or nonzero when it failed.
LUAANALYSIS_API int luwu_analysis_check(luwu_Analysis* analysis);

LUAANALYSIS_API size_t luwu_analysis_diagnosticcount(luwu_Analysis* analysis);

// Fills `out` with diagnostic `index` of the last check, in the order luwu-analyze prints them. Returns 0, or nonzero
// when the index is out of range. The strings are valid until the next check or load.
LUAANALYSIS_API int luwu_analysis_getdiagnostic(luwu_Analysis* analysis, size_t index, luwu_Diagnostic* out);

// The legend for the path notation the last check's type mismatches use, to print after them, or NULL when they use
// none. Valid until the next check.
LUAANALYSIS_API const char* luwu_analysis_legend(luwu_Analysis* analysis);

// Why the last failing call failed, or NULL. Valid until the next call.
LUAANALYSIS_API const char* luwu_analysis_lasterror(luwu_Analysis* analysis);

// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "luaanalysis.h"

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include "ScopedFlags.h"
#include "doctest.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// A temporary directory of source files and a checker, both thrown away after the test
struct AnalysisApiFixture
{
    fs::path root;
    std::unique_ptr<luwu_Analysis, void (*)(luwu_Analysis*)> analysis{luwu_analysis_new(), luwu_analysis_free};

    AnalysisApiFixture()
    {
        std::random_device device;
        root = fs::temp_directory_path() / ("luwu-analysis-api-" + std::to_string(device()));
        fs::create_directories(root);
    }

    ~AnalysisApiFixture()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    std::string write(const std::string& name, const std::string& source)
    {
        fs::path path = root / name;
        fs::create_directories(path.parent_path());
        std::ofstream(path) << source;
        return path.string();
    }

    std::vector<luwu_Diagnostic> check(const std::vector<std::string>& paths)
    {
        for (const std::string& path : paths)
            luwu_analysis_queue(analysis.get(), path.c_str());
        REQUIRE(luwu_analysis_check(analysis.get()) == 0);
        return diagnostics();
    }

    std::vector<luwu_Diagnostic> diagnostics()
    {
        std::vector<luwu_Diagnostic> result(luwu_analysis_diagnosticcount(analysis.get()));
        for (size_t i = 0; i < result.size(); ++i)
            REQUIRE(luwu_analysis_getdiagnostic(analysis.get(), i, &result[i]) == 0);
        return result;
    }
};

// Compiles `source`, which returns a function, and leaves a registry ref to that function
static int refLuaFunction(lua_State* L, const char* source)
{
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &bytecodeSize);
    int loaded = luau_load(L, "resolver", bytecode, bytecodeSize, 0);
    free(bytecode);
    REQUIRE(loaded == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}

LUAU_FASTFLAG(DebugLuauMagicTypes)

TEST_SUITE_BEGIN("AnalysisApi");

TEST_CASE_FIXTURE(AnalysisApiFixture, "reports_type_errors_where_they_are")
{
    std::string main = write("main.luau", "local x: number = \"one\"\nreturn x\n");

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "TypeError");
    CHECK(found[0].severity == LUWU_DIAGNOSTIC_ERROR);
    CHECK(found[0].beginline == 0);
    CHECK(found[0].begincolumn == 18);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "reports_lints_as_warnings")
{
    std::string main = write("main.luau", "local unused = 1\nreturn nil\n");

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "LocalUnused");
    CHECK(found[0].severity == LUWU_DIAGNOSTIC_WARNING);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "reports_only_the_queued_files")
{
    write("dependency.luau", "local wrong: string = 1\nreturn { value = 1 }\n");
    std::string main = write("main.luau", "local dependency = require(\"./dependency\")\nlocal value: number = dependency.value\nreturn value\n");

    std::vector<luwu_Diagnostic> found = check({main});

    CHECK(found.empty());
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "resolves_requires_by_string_without_a_resolver")
{
    write("dependency.luau", "return { value = 1 }\n");
    std::string main = write("main.luau", "local dependency = require(\"./dependency\")\nlocal value: string = dependency.value\nreturn value\n");

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "TypeError");
    CHECK(found[0].beginline == 1);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "reads_aliases_from_a_config_luau")
{
    write(".config.luau", "return { luau = { aliases = { lib = \"./lib\" } } }\n");
    write("lib/dependency.luau", "return { value = 1 }\n");
    std::string main = write("main.luau", "local dependency = require(\"@lib/dependency\")\nlocal value: string = dependency.value\nreturn value\n");

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "TypeError");
    CHECK(found[0].beginline == 1);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "loads_definition_files")
{
    std::string definitions = write("globals.d.luau", "declare function greet(name: string): string\n");
    std::string main = write("main.luau", "local greeting: number = greet(\"hi\")\nreturn greeting\n");

    REQUIRE(luwu_analysis_loaddefinitions(analysis.get(), definitions.c_str()) == 0);
    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "TypeError");
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "refuses_definitions_after_the_first_check")
{
    std::string definitions = write("globals.d.luau", "declare function greet(name: string): string\n");
    std::string main = write("main.luau", "return nil\n");

    check({main});

    CHECK(luwu_analysis_loaddefinitions(analysis.get(), definitions.c_str()) != 0);
    CHECK(luwu_analysis_lasterror(analysis.get()) != nullptr);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "a_definition_file_that_does_not_parse_fails_with_its_errors")
{
    std::string definitions = write("globals.d.luau", "declare function greet(\n");

    CHECK(luwu_analysis_loaddefinitions(analysis.get(), definitions.c_str()) != 0);
    std::vector<luwu_Diagnostic> found = diagnostics();
    REQUIRE(found.size() >= 1);
    CHECK(std::string(found[0].kind) == "SyntaxError");
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "a_lua_resolver_decides_what_requires_mean")
{
    std::string dependency = write("lib/dependency.luau", "return { value = 1 }\n");
    std::string main = write("main.luau", "local dependency = require(\"@virtual\")\nlocal value: string = dependency.value\nreturn value\n");

    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    lua_pushstring(L, dependency.c_str());
    lua_setglobal(L, "dependency_path");
    int ref = refLuaFunction(L, "return function(requirer, path) if path == '@virtual' then return dependency_path end return nil end");
    luwu_analysis_setresolver(analysis.get(), L, ref);

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "TypeError");
    CHECK(found[0].beginline == 1);
    CHECK(lua_gettop(L) == 0);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "a_lua_resolver_explains_why_a_require_fails")
{
    std::string main = write("main.luau", "local dependency = require(\"@missing/thing\")\nreturn dependency\n");

    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    int ref = refLuaFunction(L, "return function(requirer, path) return nil, 'no alias named missing' end");
    luwu_analysis_setresolver(analysis.get(), L, ref);

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 2);
    CHECK(std::string(found[0].kind) == "RequireError");
    CHECK(std::string(found[0].message) == "no alias named missing");
    CHECK(found[1].severity == LUWU_DIAGNOSTIC_NOTE);
    CHECK(found[0].beginline == 0);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "a_lua_resolver_that_raises_is_reported_not_propagated")
{
    std::string main = write("main.luau", "local dependency = require(\"./thing\")\nreturn dependency\n");

    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    luaL_openlibs(L);
    int ref = refLuaFunction(L, "return function(requirer, path) error('resolver broke') end");
    luwu_analysis_setresolver(analysis.get(), L, ref);

    std::vector<luwu_Diagnostic> found = check({main});

    REQUIRE(found.size() == 2);
    CHECK(std::string(found[0].kind) == "RequireError");
    CHECK(found[1].severity == LUWU_DIAGNOSTIC_NOTE);
    CHECK(std::string(found[0].message).find("resolver broke") != std::string::npos);
    CHECK(lua_gettop(L) == 0);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "an_internal_compiler_error_is_reported_and_the_other_files_still_checked")
{
    // The magic type `_luau_ice` raises an internal compiler error
    ScopedFastFlag magicTypes{FFlag::DebugLuauMagicTypes, true};
    std::string crashing = write("crashing.luau", "local x: _luau_ice = 1\nreturn x\n");
    std::string wrong = write("wrong.luau", "local x: number = \"one\"\nreturn x\n");

    std::vector<luwu_Diagnostic> found = check({crashing, wrong});

    REQUIRE(found.size() == 2);
    CHECK(std::string(found[0].kind) == "InternalCompilerError");
    CHECK(std::string(found[0].module).find("crashing.luau") != std::string::npos);
    CHECK(std::string(found[1].kind) == "TypeError");
    CHECK(std::string(found[1].module).find("wrong.luau") != std::string::npos);
}

TEST_CASE_FIXTURE(AnalysisApiFixture, "a_missing_file_is_a_read_error")
{
    std::vector<luwu_Diagnostic> found = check({(root / "missing.luau").string()});

    REQUIRE(found.size() == 1);
    CHECK(std::string(found[0].kind) == "ReadError");
}

TEST_SUITE_END();

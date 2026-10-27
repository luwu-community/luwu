// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

// Luwu doc references: references and bindings written as Luwu code in doc comments.
#include "Luau/DocRefs.h"
#include "Luau/Frontend.h"

#include "Fixture.h"
#include "ScopedFlags.h"

#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(LuwuInLuwuDocs)
LUAU_FASTFLAG(LuwuClasses)

namespace
{

struct DocRefsFixture : BuiltinsFixture
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag docs{FFlag::LuwuInLuwuDocs, true};
};

} // namespace

TEST_SUITE_BEGIN("TypeInferDocRefs");

TEST_CASE_FIXTURE(DocRefsFixture, "doc_refs_resolve_like_code")
{
    CheckResult result = check(R"(
        --!strict
        local fs = { file = { read = function(path: string): string return path end } }

        --- Reads [path] into a string. To go faster, see [helper], and [fs.file.read] for the details.
        --- Wrap it in [assert] if you don't care about errors.
        local function read(path: string): string
            return helper(path)
        end

        function helper(path: string): string
            return fs.file.read(path)
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_refs_record_their_definitions")
{
    CheckResult result = check(R"(--!strict
type Token = { text: string }
local fs = { file = { read = 1 } }

--- [path], [fs.file.read], [Token]
local function read(path: string) end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    ModulePtr module = getMainModule();

    auto definitionAt = [&](Position position) -> std::optional<Location>
    {
        for (const DocRefTarget& target : module->docRefTargets)
        {
            if (target.location.begin == position)
            {
                CHECK_EQ(target.module, module->name);
                return target.definition;
            }
        }
        return std::nullopt;
    };

    CHECK_EQ(definitionAt({4, 5}), Location{{5, 20}, {5, 24}});   // path, the parameter
    CHECK_EQ(definitionAt({4, 13}), Location{{2, 6}, {2, 8}});    // fs
    CHECK_EQ(definitionAt({4, 16}), Location{{2, 13}, {2, 17}});  // file
    CHECK_EQ(definitionAt({4, 21}), Location{{2, 22}, {2, 26}});  // read
    CHECK_EQ(definitionAt({4, 29}), Location{{1, 0}, {1, 29}});   // Token
}

TEST_CASE_FIXTURE(DocRefsFixture, "unknown_doc_refs_are_type_errors_in_strict")
{
    CheckResult result = check(R"(
        --!strict
        local fs = { file = { read = function(path: string): string return path end } }

        --- See [fs.file.raed] and [nope].
        local x = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    auto prop = get<UnknownProperty>(result.errors[0]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "raed");
    CHECK_EQ(result.errors[0].location, Location{{4, 25}, {4, 29}});
    CHECK(result.errors[0].inDocComment);

    auto symbol = get<UnknownSymbol>(result.errors[1]);
    REQUIRE(symbol);
    CHECK_EQ(symbol->name, "nope");
    CHECK_EQ(result.errors[1].location, Location{{4, 36}, {4, 40}});
}

TEST_CASE_FIXTURE(DocRefsFixture, "unknown_doc_refs_are_quiet_in_nonstrict")
{
    CheckResult result = check(R"(
        --!nonstrict
        --- See [nope].
        local x = 1
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_refs_to_methods_and_types")
{
    CheckResult result = check(R"(
        --!strict
        type Token = { text: string }
        local Parser = {}
        function Parser.advance(self: any): Token
            return { text = "" }
        end

        --[=[
            Hands each [type Token] to [Parser:advance]; also [Token]. Not [Parser:retreat] or [type Tokn].
        ]=]
        local x = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<UnknownProperty>(result.errors[0]));
    CHECK_EQ(get<UnknownProperty>(result.errors[0])->key, "retreat");
    auto symbol = get<UnknownSymbol>(result.errors[1]);
    REQUIRE(symbol);
    CHECK_EQ(symbol->name, "Tokn");
    CHECK_EQ(symbol->context, UnknownSymbol::Type);
}

TEST_CASE_FIXTURE(DocRefsFixture, "markdown_brackets_are_not_doc_refs")
{
    CheckResult result = check(R"(
        --!strict
        --[=[
            A [link](https://example.com) and a [reference link][web], an image ![alt](x.png).
            `[inCode]` and t[1], [i + 1], [optional, default 0], and an escaped \[TODO\].
            - [ ] a task
            - [x] a done task

            ```luwu
            local y = t[idx]
            ```

            [web]: https://example.com
        ]=]
        local x = 1

        --[[
            commented-out code isn't a doc comment: local y = t[idx]
        ]]
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_ref_display_text")
{
    CheckResult result = check(R"(
        --!strict
        local fs = { read = 1 }
        --- [the reader][fs.read] and [fs.read][], but not [the writer][fs.write]
        local x = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto prop = get<UnknownProperty>(result.errors[0]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "write");
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_bindings")
{
    CheckResult result = check(R"(
        --!strict
        local fs = { file = { read = 1, write = 2 } }

        --[=[
            Use [file.read], or [file.write] to write; not [file.append].

            [file = fs.file]
        ]=]
        local x = 1

        --- [file] is only bound in the comment above
        local y = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    auto prop = get<UnknownProperty>(result.errors[0]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "append");
    auto symbol = get<UnknownSymbol>(result.errors[1]);
    REQUIRE(symbol);
    CHECK_EQ(symbol->name, "file");
}

TEST_CASE_FIXTURE(DocRefsFixture, "module_doc_bindings_are_visible_in_every_doc_comment")
{
    CheckResult result = check(R"(--!strict
--[=[
    The module.

    [io = fs.file]
]=]

local fs = { file = { read = 1 } }

--- Reads with [io.read], not [io.nope]
local x = 1
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto prop = get<UnknownProperty>(result.errors[0]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "nope");
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_bindings_require_modules")
{
    fileResolver.resolveStringRequires = true;
    fileResolver.source["game/Json"] = R"(
        --!strict
        return { writefile = function() end }
    )";
    getFrontend().check("game/Json");

    CheckResult result = check(R"(
        --!strict
        --[=[
            For json, see [json.writefile], not [json.readfile] or [missing].

            [json = require("game/Json")]
            [missing = require("game/Missing")]
        ]=]
        local x = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    auto missing = get<UnknownRequire>(result.errors[0]);
    REQUIRE(missing);
    CHECK_EQ(missing->modulePath, "game/Missing");

    // a bare `[require(...)]` points at the module, from where the whole call ends (where a hover looks it up)
    std::vector<DocTextLink> links = resolveDocText(
        "See [require(\"game/Json\")].",
        *getMainSourceModule(),
        *getMainModule(),
        Location{{2, 0}, {2, 0}},
        getBuiltins(),
        NotNull{&fileResolver},
        NotNull<ModuleResolver>{&getFrontend().moduleResolver},
        TypeCheckLimits{},
        nullptr
    );
    REQUIRE_EQ(links.size(), 1);
    REQUIRE(links[0].target);
    CHECK_EQ(links[0].target->module, "game/Json");
    auto prop = get<UnknownProperty>(result.errors[1]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "readfile");
}

// A language server checks modules it only needs for their exports without retainFullTypeGraphs, which drops their
// scopes. Doc text from such a module (a hover on a type it declared) resolves nothing instead of reading the scopes.
TEST_CASE_FIXTURE(DocRefsFixture, "doc_text_in_a_module_checked_without_its_scopes_resolves_nothing")
{
    fileResolver.source["game/Lib"] = R"(
        --!strict
        local x = 1
        return { x = x }
    )";
    FrontendOptions options;
    options.retainFullTypeGraphs = false;
    getFrontend().check("game/Lib", options);

    ModulePtr module = getFrontend().moduleResolver.getModule("game/Lib");
    SourceModule* sourceModule = getFrontend().getSourceModule("game/Lib");
    REQUIRE(module);
    REQUIRE(sourceModule);
    REQUIRE(!module->hasModuleScope());

    std::vector<DocTextLink> links = resolveDocText(
        "See [x].",
        *sourceModule,
        *module,
        Location{{2, 0}, {2, 0}},
        getBuiltins(),
        NotNull{&fileResolver},
        NotNull<ModuleResolver>{&getFrontend().moduleResolver},
        TypeCheckLimits{},
        nullptr
    );
    CHECK(links.empty());
}

TEST_CASE_FIXTURE(DocRefsFixture, "code_that_does_not_belong_in_docs_is_a_doc_syntax_error")
{
    CheckResult result = check(R"(
        --!nonstrict
        --[=[
            [f = function() end]
            [g = for]
            Also [function() end].
        ]=]
        local x = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (const TypeError& error : result.errors)
    {
        CHECK(get<DocSyntaxError>(error));
        CHECK(error.inDocComment);
    }
    CHECK_EQ(get<DocSyntaxError>(result.errors[0])->message, "functions can't be defined in a doc comment");
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_syntax_errors_never_stop_the_module")
{
    ParseResult parse = parseEx(R"(
        --- [f = function() end]
        local x = 1
    )");
    CHECK(parse.errors.empty());
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_comments_of_a_declaration_keep_where_they_were_written")
{
    CheckResult result = check(R"(--!strict
--- A thing
type Thing = { x: number }
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    TypeId thing = requireTypeAlias("Thing");
    REQUIRE(thing->metadata);
    REQUIRE(thing->metadata->docContext);
    CHECK_FALSE(thing->metadata->docContext->generated);
    CHECK_EQ(thing->metadata->docContext->location.begin.line, 2);
}

TEST_CASE_FIXTURE(DocRefsFixture, "a_type_from_a_definitions_file_links_to_its_declaration")
{
    loadDefinition(R"(
export type Pathlike =
    string
    | { path: string }
)");

    CheckResult result = check(R"(--!strict
--- Takes a [Pathlike], or [type Pathlike]
local function read(path: Pathlike) end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    std::vector<DocRefTarget> targets = getMainModule()->docRefTargets;
    REQUIRE_EQ(targets.size(), 2);
    for (const DocRefTarget& target : targets)
    {
        CHECK_EQ(target.module, "@test");
        CHECK_EQ(target.definition.begin, Position{1, 0});
    }
}

TEST_CASE_FIXTURE(DocRefsFixture, "a_require_path_on_its_own_is_a_require")
{
    // the test resolver names a module by its path string
    fileResolver.resolveStringRequires = true;
    fileResolver.source["@lib/json"] = "--!strict\nreturn {}";

    CheckResult result = check(R"(--!strict
--- Uses [@lib/json], not [@missing/module]; [./build.luau] is a file name, not a require.
local x = 1
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto missing = get<UnknownRequire>(result.errors[0]);
    REQUIRE(missing);
    CHECK_EQ(missing->modulePath, "@missing/module");
    CHECK_EQ(result.errors[0].location, Location{{1, 27}, {1, 42}});

    // `[@lib/json]` links to the module
    bool linked = false;
    for (const DocRefTarget& target : getMainModule()->docRefTargets)
        linked = linked || (target.module == "@lib/json" && target.location == Location{{1, 10}, {1, 19}});
    CHECK(linked);
}

TEST_CASE_FIXTURE(Fixture, "doc_refs_are_off_without_the_flag")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag docs{FFlag::LuwuInLuwuDocs, false};

    CheckResult result = check(R"(
        --!strict
        --- See [nope] and [f = function() end]
        local x = 1
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(DocRefsFixture, "doc_refs_on_a_type_function_resolve_in_the_module")
{
    CheckResult result = check(R"(
        --!strict
        local Header = {}

        --- The headings of a [Header] with one more column.
        type function Append(headings: type, name: type): type
            return name
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_SUITE_END();

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

TEST_CASE_FIXTURE(DocRefsFixture, "doc_refs_in_comments_a_type_function_writes_resolve_at_the_call")
{
    CheckResult result = check(R"(--!strict
local fs = { read = 1 }

type function Documented(t: type)
    t:setmetadata({ comments = {
        { prefix = "---", message = "Read with [fs.read], not [fs.raed]." },
        { prefix = "--", message = "[ignored], not a doc comment" },
    } })
    return t
end

type Doc = Documented<{ x: number }>
)");

    // a reference a type function wrote has no place in the source, so it's reported at the call
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto prop = get<UnknownProperty>(result.errors[0]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "raed");
    CHECK(result.errors[0].inDocComment);
    CHECK_EQ(result.errors[0].location.begin.line, 11);

    // and the good one links where `fs.read` is declared
    TypeId doc = requireTypeAlias("Doc");
    REQUIRE(doc->metadata);
    REQUIRE(doc->metadata->docContext);
    CHECK(doc->metadata->docContext->generated);

    std::string text;
    for (const std::string& line : metadataDocLines(*doc->metadata))
        text += line + "\n";
    CHECK_EQ(text, "Read with [fs.read], not [fs.raed].\n");

    ModulePtr module = getMainModule();
    std::vector<DocTextLink> links = resolveDocText(
        text,
        *getMainSourceModule(),
        *module,
        doc->metadata->docContext->location,
        getBuiltins(),
        NotNull{&fileResolver},
        NotNull{&fileResolver},
        TypeCheckLimits{},
        nullptr
    );
    // both references; the one that doesn't resolve has nowhere to point
    REQUIRE_EQ(links.size(), 2);
    CHECK_EQ(text.substr(links[0].open, links[0].close - links[0].open + 1), "[fs.read]");
    REQUIRE(links[0].target);
    CHECK_EQ(links[0].target->definition, Location{{1, 13}, {1, 17}});
    CHECK_FALSE(links[1].target);
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

TEST_CASE_FIXTURE(DocRefsFixture, "a_type_function_documents_a_class_where_it_is_used")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(--!strict
class CatBase
    name: string
end

type function Explain(t: type)
    local meta = t:metadata()
    meta.comments = { { prefix = "---", message = "CatBase, explained" } }
    t:setmetadata(meta)
    return t
end

type Cat = Explain<CatBase>
)");

    LUAU_REQUIRE_NO_ERRORS(result);

    // `Cat` is CatBase, and carries the docs Explain wrote; CatBase itself has none
    std::optional<TypeFun> cat = getMainModule()->getModuleScope()->lookupType("Cat");
    REQUIRE(cat);
    CHECK_EQ(toString(follow(cat->type)), "CatBase");
    const TypeMetadataTable* docs = docMetadataOf(cat->type);
    REQUIRE(docs);
    CHECK_EQ(metadataDocLines(*docs), std::vector<std::string>{"CatBase, explained"});
    REQUIRE(docs->docContext);
    CHECK(docs->docContext->generated);

    std::optional<TypeFun> base = getMainModule()->getModuleScope()->lookupType("CatBase");
    REQUIRE(base);
    CHECK_FALSE(docMetadataOf(base->type));
}

TEST_CASE_FIXTURE(DocRefsFixture, "a_class_given_metadata_has_to_be_what_the_type_function_returns")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(--!strict
class CatBase
    name: string
end

type function Explain(t: type)
    t:setmetadata({ comments = { { prefix = "---", message = "lost" } } })
    return types.number
end

type Cat = Explain<CatBase>
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]).find("its metadata only applies when the type function returns it") != std::string::npos);
}

TEST_CASE_FIXTURE(DocRefsFixture, "a_type_function_sees_the_comments_around_the_literal_it_is_given")
{
    CheckResult result = check(R"(--!strict
type function Freeze(t: type): type
    local out = types.copy(t)
    local comments = out:metadata().comments or {}
    table.insert(comments, { prefix = "---", message = "Brr" })
    out:setmetadata({ comments = comments })
    return out
end

--- above
type SomeLibrary = Freeze<{ --- beside the first line
    x: number,
} --- beside the last line
>
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    std::optional<TypeFun> library = getMainModule()->getModuleScope()->lookupType("SomeLibrary");
    REQUIRE(library);
    const TypeMetadataTable* docs = docMetadataOf(library->type);
    REQUIRE(docs);
    CHECK_EQ(metadataDocLines(*docs), std::vector<std::string>{"above", "beside the first line", "beside the last line", "Brr"});
}

TEST_CASE_FIXTURE(DocRefsFixture, "a_type_function_copying_a_comment_does_not_report_its_errors_again")
{
    CheckResult result = check(R"(--!strict
type function Freeze(t: type): type
    local out = types.copy(t)
    local comments = out:metadata().comments or {}
    table.insert(comments, { prefix = "---", message = "Also see [Missing2]" })
    out:setmetadata({ comments = comments })
    return out
end

--- See [Missing]
type SomeLibrary = Freeze<{
    x: number,
}>
)");

    // `[Missing]` once, in the source comment; the copy the type function wrote doesn't repeat it over the whole call
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location, Location{{9, 9}, {9, 16}});
    // `[Missing2]` exists only in the written text: reported at the type function's name, not the call's every line
    CHECK_EQ(result.errors[1].location, Location{{10, 19}, {10, 25}});
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

TEST_CASE_FIXTURE(DocRefsFixture, "a_fields_comments_are_in_its_function_types_metadata")
{
    CheckResult result = check(R"(--!strict
type function FieldDocs(t: type): type
    if not t:is("table") then
        return types.never
    end
    local docs = {}
    for _, value in t:properties() do
        if value.read then
            for _, comment in value.read:metadata().comments or {} do
                table.insert(docs, comment.message)
            end
        end
    end
    return types.singleton(table.concat(docs, "|"))
end

type Lib = {
    --- Check if string starts with one or more prefixes
    starts_with: (s: string, ...string) -> boolean,
    count: number, --- a number type is shared, so this has nowhere to go
}
type Docs = FieldDocs<Lib>
)");

    LUAU_REQUIRE_NO_ERRORS(result);

    // the field's own function type carries them
    const TableType* lib = get<TableType>(follow(requireTypeAlias("Lib")));
    REQUIRE(lib);
    auto startsWith = lib->props.find("starts_with");
    REQUIRE(startsWith != lib->props.end());
    REQUIRE(startsWith->second.readTy);
    const TypeMetadataTable* fieldDocs = docMetadataOf(*startsWith->second.readTy);
    REQUIRE(fieldDocs);
    CHECK_EQ(metadataDocLines(*fieldDocs), std::vector<std::string>{"Check if string starts with one or more prefixes"});

    // and a type function sees them
    CHECK_EQ(toString(requireTypeAlias("Docs")), "\"Check if string starts with one or more prefixes\"");
}

TEST_CASE_FIXTURE(DocRefsFixture, "properties_carry_their_own_comments")
{
    CheckResult result = check(R"(--!strict
type function PropertyDocs(t: type): type
    if not t:is("table") then
        return types.never
    end
    local docs = {}
    for key, value in t:properties() do
        for _, comment in value.comments or {} do
            table.insert(docs, `{key:value()}: {comment.message}`)
        end
    end
    table.sort(docs)
    return types.singleton(table.concat(docs, "|"))
end

type Config = {
    --- how many retries
    count: number,
    name: string, --- who it is for
}
type Docs = PropertyDocs<Config>
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireTypeAlias("Docs")), "\"count: how many retries|name: who it is for\"");
}

TEST_CASE_FIXTURE(DocRefsFixture, "a_type_function_sets_a_propertys_comments")
{
    CheckResult result = check(R"(--!strict
local fs = { read = 1 }

type function Documented(t: type): type
    if not t:is("table") then
        return types.never
    end
    local out = types.copy(t)
    out:setpropertycomments(types.singleton("count"), { { prefix = "---", message = "See [fs.read], not [fs.raed]." } })
    return out
end

type Config = Documented<{ count: number }>
)");

    // a reference in it is checked at the call
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto prop = get<UnknownProperty>(result.errors[0]);
    REQUIRE(prop);
    CHECK_EQ(prop->key, "raed");
    CHECK_EQ(result.errors[0].location.begin.line, 12);

    const TableType* config = get<TableType>(follow(requireTypeAlias("Config")));
    REQUIRE(config);
    auto count = config->props.find("count");
    REQUIRE(count != config->props.end());
    REQUIRE(count->second.metadata);
    CHECK_EQ(metadataDocLines(*count->second.metadata), std::vector<std::string>{"See [fs.read], not [fs.raed]."});
    REQUIRE(count->second.metadata->docContext);
    CHECK(count->second.metadata->docContext->generated);
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

TEST_SUITE_END();

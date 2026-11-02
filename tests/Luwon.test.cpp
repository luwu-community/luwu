// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu object notation: `.luwon` data files, read by the syntax check, the evaluator and the type checker.
#include "Luau/Luwon.h"
#include "Luau/StringUtils.h"

#include "Fixture.h"
#include "ScopedFlags.h"
#include "Typewriter.h"

#include "doctest.h"

#include <algorithm>
#include <string>
#include <string.h>
#include <vector>

LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuauExportValueSyntax)
LUAU_FASTFLAG(LuauExportValueTypecheck)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(DebugLuwuCollections)

using namespace Luau;

namespace
{

// The first error evaluating `source` gives, or "" when there's none.
std::string firstError(const std::string& source)
{
    LuwonDocument document = readLuwon(source);
    return document.errors.empty() ? "" : document.errors.front().message;
}

LuwonValue evaluate(const std::string& source)
{
    LuwonDocument document = readLuwon(source);
    if (!document.errors.empty())
        FAIL("unexpected error: " << document.errors.front().message);

    REQUIRE(document.value);
    return *document.value;
}

const LuwonValue& field(const LuwonValue& table, const std::string& name)
{
    REQUIRE(table.kind == LuwonValue::Kind::Table);

    LuwonValue key{LuwonValue::Kind::String};
    key.bytes = name;
    const LuwonValue* value = table.table->find(key);
    REQUIRE_MESSAGE(value, "no field " << name);
    return *value;
}

} // namespace

TEST_SUITE_BEGIN("Luwon");

TEST_CASE("luwon_evaluates_consts_and_string_building")
{
    LuwonValue value = evaluate(R"(
type Settings = {
    threads: number,
    aliases: { [string]: string },
}

const num_threads = 123
const root = "./src"
const paths = { cache = `{root}/.cache`, logs = root .. "/logs" }

return {
    threads = num_threads // 16,
    aliases = {
        lib = `{root}/lib`,
        cache = paths.cache,
        logs = paths["logs"],
    },
} :: Settings
)");

    CHECK(field(value, "threads").number == 7);

    const LuwonValue& aliases = field(value, "aliases");
    CHECK(field(aliases, "lib").bytes == "./src/lib");
    CHECK(field(aliases, "cache").bytes == "./src/.cache");
    CHECK(field(aliases, "logs").bytes == "./src/logs");
}

TEST_CASE("luwon_keeps_positional_items_and_entry_order")
{
    LuwonValue value = evaluate(R"(return { "a", "b", z = 1, y = 2, [10] = "ten", [true] = "yes" })");

    REQUIRE(value.table->array.size() == 2);
    CHECK(value.table->array[1].value.bytes == "b");

    REQUIRE(value.table->entries.size() == 4);
    CHECK(value.table->entries[0].key.bytes == "z");
    CHECK(value.table->entries[1].key.bytes == "y");
    CHECK(value.table->entries[2].key.number == 10);
    CHECK(value.table->entries[3].key.boolean);
}

TEST_CASE("luwon_constructors_and_arithmetic")
{
    LuwonValue value = evaluate(R"(
const offset = vector.create(1, 2, 3)
return {
    position = offset * 2 + vector.create(0, 0, 1),
    x = offset.x,
    bytes = buffer.fromstring("\0ab"),
    power = 2 ^ 10,
    remainder = -7 % 3,
    empty = none,
}
)");

    const LuwonValue& position = field(value, "position");
    REQUIRE(position.kind == LuwonValue::Kind::Vector);
    CHECK(position.vector[0] == 2.0f);
    CHECK(position.vector[2] == 7.0f);

    CHECK(field(value, "x").number == 1);
    CHECK(field(value, "bytes").kind == LuwonValue::Kind::Buffer);
    CHECK(field(value, "bytes").bytes == std::string("\0ab", 3));
    CHECK(field(value, "power").number == 1024);
    CHECK(field(value, "remainder").number == 2);
    CHECK(field(value, "empty").kind == LuwonValue::Kind::None);
}

LUAU_FASTFLAG(LuwuDestructuring)

TEST_CASE("luwon_destructuring")
{
    ScopedFastFlag destructuring{FFlag::LuwuDestructuring, true};

    LuwonValue value = evaluate(R"(
const root = "./src"
const .{ cache, logs as log_dir } = { cache = `{root}/.cache`, logs = root .. "/logs" }
return { cache = cache, logs = log_dir }
)");

    CHECK(field(value, "cache").bytes == "./src/.cache");
    CHECK(field(value, "logs").bytes == "./src/logs");

    LuwonDocument result = readLuwon("local .{ a, b } = { a = 1, b = 2 }\nreturn a");
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].message == "Destructure with 'const': data files have no 'local' bindings");
}

TEST_CASE("luwon_positional_destructuring")
{
    ScopedFastFlag destructuring{FFlag::LuwuDestructuring, true};

    // A table gives its items; a constructor call gives the arguments it wrote
    LuwonValue value = evaluate(R"(
const .(first, second) = { "a", "b" }
const .(x, y) = Point(1, 2)
return { first = first, second = second, x = x, y = y }
)");

    CHECK(field(value, "first").bytes == "a");
    CHECK(field(value, "second").bytes == "b");
    CHECK(field(value, "x").number == 1);
    CHECK(field(value, "y").number == 2);
}

TEST_CASE("luwon_nil_fields_are_absent")
{
    LuwonValue value = evaluate("return { a = nil, b = 1 }");

    REQUIRE(value.table->entries.size() == 1);
    CHECK(value.table->entries[0].key.bytes == "b");
}

TEST_CASE("luwon_rejects_everything_but_data")
{
    CHECK(firstError("local x = 1\nreturn x") == "Declare 'x' with 'const': data files have no 'local' bindings");
    CHECK(firstError("return string.rep('a', 3)") == "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file");
    CHECK(firstError("return vector.zero") == "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file");
    CHECK(firstError("return function() end") == "Data files can't define functions");
    CHECK(firstError("return if true then 1 else 2") == "Data files can't branch: if-else expressions aren't allowed");
    CHECK(firstError("return true and 1") == "'and' isn't allowed in a data file, since data files don't branch");
    CHECK(firstError("return not true") == "'not' isn't allowed in a data file, since data files don't branch");
    CHECK(firstError("return 1 == 1") == "'==' isn't allowed in a data file: comparisons only matter for branching");
    CHECK(firstError("return #'abc'") == "'#' isn't allowed in a data file");
    CHECK(firstError("return math.pi") == "Unknown name 'math': a data file can only use consts declared above");
    CHECK(firstError("for i = 1, 2 do end\nreturn 1") == "Data files can't loop");
    CHECK(firstError("if true then end\nreturn 1") == "Data files can't branch: 'if' statements aren't allowed");
    CHECK(firstError("y = 2\nreturn 1") == "Data files can't reassign anything: every binding is const");
    CHECK(firstError("vector.create(1, 2, 3)\nreturn 1") == "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file, to give a const or the returned value");
    CHECK(firstError("print(1)\nreturn 1") == "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file, to give a const or the returned value");
    CHECK(firstError("return 1, 2") == "Return exactly one value from a data file");
    CHECK(firstError("const x = 1") == "End the data file with 'return <value>'");
}

TEST_CASE("luwon_rejects_luau_integers")
{
    ScopedFastFlag integers{FFlag::LuauIntegerType2, true};

    CHECK(firstError("return 7i") == "Luau's 'i' integer suffix isn't allowed in a data file");
}

TEST_CASE("luwon_reports_evaluation_errors_at_the_expression")
{
    LuwonDocument result = readLuwon("const t = {}\n\nreturn 'x' .. t");
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].message == "'..' needs strings or numbers, but got a table");
    CHECK(result.errors[0].location.begin.line == 2);

    CHECK(firstError("return { a = 1, a = 2 }") == "This key is already in the table");
    CHECK(firstError("return { [1] = 'a', 'b' }") == "Index 1 is already a positional item of this table");
    CHECK(firstError("return { [0 / 0] = 1 }") == "A table key can't be NaN");
    CHECK(firstError("return { [{}] = 1 }") == "A table key in a data file is a string, number or boolean, not a table");
    CHECK(firstError("return 1 + 'a'") == "'+' needs numbers, but got a number and a string");
    CHECK(firstError("return vector.create(1, 2)") == "vector.create takes 3 or 4 numbers, but got 2");
    CHECK(firstError("return buffer.fromstring(1)") == "buffer.fromstring takes one string");
    CHECK(firstError("return (1).x") == "Can't read a field from a number");
}

TEST_CASE("luwon_bounds_nesting")
{
    std::string source;
    for (int i = 0; i < 250; i++)
        source += "const t" + std::to_string(i + 1) + " = { " + (i == 0 ? std::string("1") : "t" + std::to_string(i)) + " }\n";
    source += "return t250";

    CHECK(firstError(source) == "Tables in a data file can't nest more than 200 deep");
}

TEST_CASE("luwon_reads_class_constructors_as_objects")
{
    LuwonValue value = evaluate("return { origin = Point(1, 2) }");

    const LuwonValue& origin = field(value, "origin");
    REQUIRE(origin.kind == LuwonValue::Kind::Object);
    CHECK(origin.bytes == "Point");
    REQUIRE(origin.args.size() == 2);
    CHECK(origin.args[1].number == 2);
}

TEST_CASE("luwon_collections_bundles_are_builtin_classes")
{
    CHECK(firstError("return collections.pair(1, 2)") == "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file");

    ScopedFastFlag collections{FFlag::DebugLuwuCollections, true};

    LuwonValue value = evaluate("return { range = collections.pair(1, 10), color = collections.quad(255, 128, 0, 255) }");

    const LuwonValue& range = field(value, "range");
    REQUIRE(range.kind == LuwonValue::Kind::Object);
    CHECK(range.bytes == "collections.pair");
    REQUIRE(range.args.size() == 2);
    CHECK(range.args[1].number == 10);

    CHECK(writeLuwon(value).text == "return { range = collections.pair(1, 10), color = collections.quad(255, 128, 0, 255) }\n");
}

TEST_CASE("luwon_attaches_comments_to_items")
{
    const std::string source = R"(-- settings
return {
    -- how many workers
    -- at most 64
    threads = 4, -- per core
    name = "main",
}
)";
    LuwonDocument document = readLuwon(source);
    REQUIRE(document.errors.empty());
    REQUIRE(document.comments.size() == 4);
    CHECK(document.comments[0].text == "-- settings");

    const LuwonTable& table = *document.value->table;
    const LuwonComments& threads = *table.entries[0].comments;
    REQUIRE(threads.above.size() == 2);
    CHECK(threads.above[0].text == "-- how many workers");
    REQUIRE(threads.after);
    CHECK(threads.after->text == "-- per core");

    // The trailing comment of `threads` sits on the line above `name` but belongs to `threads`
    const LuwonComments& name = *table.entries[1].comments;
    CHECK(name.above.empty());
    CHECK(!name.after);
}

TEST_CASE("luwon_writes_values_as_literals")
{
    LuwonValue value = evaluate(R"(return {
    "first",
    name = "say \"hi\"\n",
    ["two words"] = 1 / 0,
    [3] = vector.create(1, 2, 3),
    nested = { a = 1 },
    origin = Point(0.5, -2),
})");

    CHECK(writeLuwon(value).text == R"(return {
    "first",
    name = "say \"hi\"\n",
    ["two words"] = 1 / 0,
    [3] = vector.create(1, 2, 3),
    nested = { a = 1 },
    origin = Point(0.5, -2),
}
)");

    CHECK(writeLuwon(evaluate("return { 1, 2, 3 }")).text == "return { 1, 2, 3 }\n");
}

// `value` with every comment unset, like a value built by code that never says what the comments are.
static LuwonValue withoutComments(const LuwonValue& value)
{
    if (value.kind != LuwonValue::Kind::Table)
        return value;

    std::shared_ptr<LuwonTable> table = std::make_shared<LuwonTable>();
    for (const LuwonEntry& item : value.table->array)
        table->array.push_back(LuwonEntry{LuwonValue{}, withoutComments(item.value), std::nullopt, std::nullopt});
    for (const LuwonEntry& entry : value.table->entries)
        table->add(entry.key, withoutComments(entry.value));

    LuwonValue result{LuwonValue::Kind::Table};
    result.table = std::move(table);
    return result;
}

// Edits `source` so it holds the value `target` reads to.
static std::string editTo(const std::string& source, const std::string& target)
{
    LuwonWriteResult result = editLuwon(source, withoutComments(evaluate(target)));
    REQUIRE_MESSAGE(!result.error, *result.error);
    return result.text;
}

TEST_CASE("luwon_edits_keep_comments_consts_and_types")
{
    const std::string source = R"(--- The project's settings
type Settings = { threads: number, root: string, lib: string }

const root = "./src"

return {
    -- workers
    threads = 4, -- per core
    root = root,
    lib = `{root}/lib`,
} :: Settings
)";

    // Nothing changed: the text comes back byte for byte
    CHECK(editTo(source, R"(return { threads = 4, root = "./src", lib = "./src/lib" })") == source);

    // Only `threads` is rewritten; the computed values that didn't change are written with the same expressions
    CHECK(editTo(source, R"(return { threads = 8, root = "./src", lib = "./src/lib" })") == R"(--- The project's settings
type Settings = { threads: number, root: string, lib: string }

const root = "./src"

return {
    -- workers
    threads = 8, -- per core
    root = root,
    lib = `{root}/lib`,
} :: Settings
)");
}

TEST_CASE("luwon_edits_add_and_remove_items_in_place")
{
    const std::string source = R"(return {
    -- kept
    a = 1,
    b = 2, -- b's note
    c = 3,
}
)";

    CHECK(editTo(source, "return { a = 1, c = 3, d = { 4, 5 } }") == R"(return {
    -- kept
    a = 1,
    -- b's note
    c = 3,
    d = { 4, 5 },
}
)");

    CHECK(editTo("return { x = 1 }\n", "return { x = 1, y = 2 }") == "return { x = 1, y = 2 }\n");
    CHECK(editTo("return {}\n", "return { y = 2 }") == "return { y = 2 }\n");
    CHECK(editTo("return { 1, 2 }\n", "return { 1, 2, 3 }") == "return { 1, 2, 3 }\n");
}

TEST_CASE("luwon_edits_a_shared_const_where_it_is_built")
{
    const std::string source = R"(const base = { x = 1 }
return { a = base, b = base }
)";

    // Both places still share the table: the const changes
    CHECK(editTo(source, "return { a = { x = 2 }, b = { x = 2 } }") == R"(const base = { x = 2 }
return { a = base, b = base }
)");

    // The places differ now: the second gets a literal of its own
    CHECK(editTo(source, "return { a = { x = 2 }, b = { x = 3 } }") == R"(const base = { x = 2 }
return { a = base, b = { x = 3 } }
)");
}

// A package manifest: consts used by name, and one value built from another
static const char* const kManifest = R"(const name = "chimera"
const version = vector.create(0, 1, 0)

const registry_url = `https://example.com/{name}`

const dependencies = {
    json = "1.0.0",
}

return {
    language = "luwu",
    name = name,
    version = version,
    dependencies = dependencies,
    metadata = {
        url = registry_url,
    },
}
)";

// The value the manifest should read to, given its fields
static std::string manifestWith(const std::string& name, const std::string& version, const std::string& dependencies, const std::string& url)
{
    return "return { language = \"luwu\", name = " + name + ", version = " + version + ", dependencies = " + dependencies +
           ", metadata = { url = " + url + " } }";
}

TEST_CASE("luwon_lossless_json_reads_back_to_the_same_value")
{
    LuwonValue value = evaluate(R"LUWON(
return {
    name = "chimera",
    version = Semver(0, 2, 0),
    window = Rect { width = 640, height = 480 },
    origin = vector.create(1, 2, 3),
    blob = buffer.fromstring("\0\1\255"),
    raw = "\255\254",
    holes = { 1, nil, 3 },
    mixed = { "a", key = "b", [true] = 1 },
    tagged = { ["$vector"] = 1 },
    limits = { 1 / 0, -1 / 0, 0 / 0 },
    nothing = none,
}
)LUWON");

    LuwonWriteResult json = writeLuwonJson(value, /* lossless= */ true);
    REQUIRE(!json.error);

    LuwonDocument back = readLuwonJson(json.text);
    REQUIRE_MESSAGE(back.errors.empty(), back.errors.front().message);
    CHECK(luwonEquals(*back.value, value));
}

TEST_CASE("luwon_plain_json_approximates_what_json_has_no_type_for")
{
    LuwonValue value = evaluate(R"LUWON(
return {
    version = Semver(0, 2, 0),
    window = Rect { width = 640 },
    origin = vector.create(1, 2, 3),
    nothing = none,
    ratio = 1 / 0,
    [2] = "two",
}
)LUWON");

    CHECK_EQ(writeLuwonJson(value, /* lossless= */ false).text, R"({
    "version": [0, 2, 0],
    "window": {
        "width": 640
    },
    "origin": [1, 2, 3],
    "nothing": null,
    "ratio": null,
    "2": "two"
}
)");
}

TEST_CASE("luwon_reads_json")
{
    LuwonDocument document = readLuwonJson(R"({"a": [1, 2.5, "é😀"], "b": null, "c": {"$class": "Point", "$args": [1, 2]}, "d": true})");
    REQUIRE(document.errors.empty());
    CHECK_EQ(writeLuwon(*document.value).text, "return { a = { 1, 2.5, \"\xc3\xa9\xf0\x9f\x98\x80\" }, b = none, c = Point(1, 2), d = true }\n");

    auto firstJsonError = [](const char* json)
    {
        LuwonDocument document = readLuwonJson(json);
        REQUIRE(!document.errors.empty());
        const LuwonError& error = document.errors.front();
        return format("%d:%d: %s", error.location.begin.line + 1, error.location.begin.column + 1, error.message.c_str());
    };

    CHECK_EQ(firstJsonError("{\"a\": [1, 2,}"), "1:13: unexpected '}'");
    CHECK_EQ(firstJsonError("{\"a\": 1, \"a\": 2}"), "1:1: the key 'a' is in this object twice");
    CHECK_EQ(firstJsonError("[1]\n2"), "2:1: expected the end of the file after the value");
    CHECK_EQ(firstJsonError("{\"$vector\": [1, \"x\", 3]}"), "1:1: '$vector' needs 3 or 4 numbers");
}

TEST_CASE("luwon_edits_never_inline_a_const")
{
    const std::string version = "vector.create(0, 1, 0)";
    const std::string url = "\"https://example.com/chimera\"";

    // A new dependency goes into the const's table; the return keeps `dependencies`
    std::string added = editTo(kManifest, manifestWith("\"chimera\"", version, R"({ json = "1.0.0", http = "2.0.0" })", url));
    CHECK(added.find("const dependencies = {\n    json = \"1.0.0\",\n    http = \"2.0.0\",\n}\n") != std::string::npos);
    CHECK(added.find("    dependencies = dependencies,\n") != std::string::npos);

    // A new version changes the const, not the return
    std::string bumped = editTo(kManifest, manifestWith("\"chimera\"", "vector.create(0, 2, 0)", R"({ json = "1.0.0" })", url));
    CHECK(bumped.find("const version = vector.create(0, 2, 0)\n") != std::string::npos);
    CHECK(bumped.find("    version = version,\n") != std::string::npos);

    // A new name that the url follows changes the const
    std::string renamed = editTo(kManifest, manifestWith("\"hydra\"", version, R"({ json = "1.0.0" })", "\"https://example.com/hydra\""));
    CHECK(renamed.find("const name = \"hydra\"\n") != std::string::npos);
    CHECK(renamed.find("    name = name,\n") != std::string::npos);
    CHECK(renamed.find("    url = registry_url,\n") != std::string::npos);

    // A new name the url doesn't follow can't change the const: only that place gets a literal. The version, changed in
    // the same write, still changes its const.
    std::string split = editTo(kManifest, manifestWith("\"hydra\"", "vector.create(0, 2, 0)", R"({ json = "1.0.0" })", url));
    CHECK(split.find("const version = vector.create(0, 2, 0)\n") != std::string::npos);
    CHECK(split.find("    version = version,\n") != std::string::npos);
    CHECK(split.find("const name = \"chimera\"\n") != std::string::npos);
    CHECK(split.find("    name = \"hydra\",\n") != std::string::npos);
    CHECK(split.find("    url = registry_url,\n") != std::string::npos);
}

TEST_CASE("luwon_edits_keep_dot_table_shorthand")
{
    ScopedFastFlag destructuring{FFlag::LuwuDestructuring, true};

    const std::string source = R"(const name = "chimera"
const version = vector.create(0, 1, 0)
const url = `https://example.com/{name}`
const .{ license } = { license = "MIT" }

return .{ name, version, url, license }
)";

    // A new version changes the const; the shorthand stays
    CHECK(editTo(source, R"(return { name = "chimera", version = vector.create(0, 2, 0), url = "https://example.com/chimera", license = "MIT" })") ==
          R"(const name = "chimera"
const version = vector.create(0, 2, 0)
const url = `https://example.com/{name}`
const .{ license } = { license = "MIT" }

return .{ name, version, url, license }
)");

    // A name the url doesn't follow is spelled out at its field, and a new field joins the shorthand
    CHECK(editTo(source, R"(return { name = "hydra", version = vector.create(0, 1, 0), url = "https://example.com/chimera", license = "MIT", private = true })") ==
          R"(const name = "chimera"
const version = vector.create(0, 1, 0)
const url = `https://example.com/{name}`
const .{ license } = { license = "MIT" }

return .{ name = "hydra", version, url, license, private = true }
)");

    // A destructured name gets a literal where it's used: the pattern isn't a place to write a value
    CHECK(editTo(source, R"(return { name = "chimera", version = vector.create(0, 1, 0), url = "https://example.com/chimera", license = "BSD" })") ==
          R"(const name = "chimera"
const version = vector.create(0, 1, 0)
const url = `https://example.com/{name}`
const .{ license } = { license = "MIT" }

return .{ name, version, url, license = "BSD" }
)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwon_requires_get_the_type_the_file_declares")
{
    fileResolver.source["game/settings.luwon"] = R"(
type Settings = {
    threads: number,
    name: string,
}

return { threads = 4, name = "main" } :: Settings
)";

    CheckResult result = check(R"(
local settings = require(game["settings.luwon"])
local threads: number = settings.threads
local name: number = settings.name
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]).find("number") != std::string::npos);
    CHECK(result.errors[0].location.begin.line == 3);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwon_syntax_errors_are_reported_by_analysis")
{
    fileResolver.source["game/settings.luwon"] = "local x = 1\nreturn x";

    CheckResult result = getFrontend().check("game/settings.luwon");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const SyntaxError* error = get<SyntaxError>(result.errors[0]);
    REQUIRE(error);
    CHECK(error->message == "Declare 'x' with 'const': data files have no 'local' bindings");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwon_reports_calling_a_function_as_a_class")
{
    fileResolver.source["game/config.luwon"] = R"(
const x = 2
return { y = print(x), origin = Point(1, 2) }
)";

    CheckResult result = getFrontend().check("game/config.luwon");

    std::vector<std::string> messages;
    for (const TypeError& error : result.errors)
        messages.push_back(toString(error));

    // `Point` may be a class the reader passes in, which analysis can't see: it's only an unknown global
    REQUIRE_EQ(messages.size(), 2);
    CHECK(std::find(
              messages.begin(),
              messages.end(),
              "'print' is a function, and only 'vector.create', 'buffer.fromstring' and classes can be called in a data file"
          ) != messages.end());
    bool reportsPointAsUnknown = get<UnknownSymbol>(result.errors[0]) || get<UnknownSymbol>(result.errors[1]);
    CHECK(reportsPointAsUnknown);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwon_decode_takes_the_type_to_give_its_value")
{
    CheckResult result = check(R"(
type Config = { threads: number }

local config = luwon.decode<<Config>>("return { threads = 4 }")
local threads: number = config.threads
local name: string = config.threads

local text: string = luwon.encode<<Config>>(config)
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(result.errors[0].location.begin.line == 5);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwon_declarations_type_a_data_file")
{
    ScopedFastFlag flags[] = {
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::LuwuClasses, true},
    };

    const std::string source = R"(
declare class Semver(public const major: number, public const minor: number, public const patch: number)
end

return { version = Semver(0, 2, 0), wrong = Semver("a", 2, 0) }
)";

    // The reader skips declarations: they're types only
    CHECK(readLuwon(source).errors.empty());

    fileResolver.source["game/package.luwon"] = source;
    CheckResult result = getFrontend().check("game/package.luwon");

    // The constructor's arguments are checked through the declaration
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(result.errors[0].location.begin.line == 4);
}

// `luwon.reader`'s check is attached when the builtins are registered, so classes are on before the fixture is built
struct LuwonClassesFlags
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
};

struct LuwonClassesFixture
    : LuwonClassesFlags
    , BuiltinsFixture
{
};

TEST_CASE_FIXTURE(LuwonClassesFixture, "luwon_reader_names_each_class_once")
{
    CheckResult result = check(R"LUA(
class Point(public x: number, public y: number)
end
local Other = Point

local reader = luwon.reader({ Point, Other })
local point: Point = reader:decode<<Point>>("return Point(1, 2)")
)LUA");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(result.errors[0].location.begin.line == 5);
    CHECK_EQ(toString(result.errors[0]), "'Point' is in this reader twice; remove this one");
}

TEST_CASE_FIXTURE(LuwonClassesFixture, "luwon_reader_tells_classes_apart_by_name")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = "export class Tag(public name: string)\nend\n";
    fileResolver.source["game/B"] = "export class Tag(public label: string)\nend\n";
    fileResolver.source["game/C"] = "local reader = luwon.reader({ require(game.A).Tag, require(game.B).Tag })\n";

    CheckResult result = getFrontend().check("game/C");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Two classes in this reader are named 'Tag' (the first is on line 1), so a data file couldn't tell them apart"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwon_typewriting")
{
    fileResolver.luwonModules.insert("MainModule");

    // The cases type a field's value. Typing a keyword or `x.` above another statement cascades in any Luwu file.
    const char* before = "const root = \"./src\"\nconst size = 4\n\nreturn {\n    value = ";
    const char* after = ",\n    root = root,\n    size = size,\n}\n";

    checkTypewriting(*this, before, "{ cache = `{root}/.cache`, logs = root .. \"/logs\" }", after);
    checkTypewriting(*this, before, "vector.create(size, size * 2, 0)", after);
    checkTypewriting(*this, before, "buffer.fromstring(root)", after);
}

TEST_SUITE_END();

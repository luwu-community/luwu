// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/RepeatedTypeNames.h"
#include "Luau/TypeNaming.h"

#include "doctest.h"
#include "Fixture.h"
#include "ScopedFlags.h"

LUAU_FASTFLAG(LuwuHelpfulSubtypingErrors)

using namespace Luau;

TEST_SUITE_BEGIN("RepeatedTypeNames");

TEST_CASE("pascal_case_joins_words_and_quiets_shouting")
{
    CHECK_EQ(pascalCase("http_code"), "HttpCode");
    CHECK_EQ(pascalCase("httpCode"), "HttpCode");
    CHECK_EQ(pascalCase("HTTP_CODE"), "HttpCode");
    CHECK_EQ(pascalCase("URLPath"), "URLPath");
    CHECK_EQ(pascalCase("max-age"), "MaxAge");
    CHECK_EQ(pascalCase("_private"), "Private");
    CHECK_EQ(pascalCase("2fa_code"), "FaCode");
    CHECK_EQ(pascalCase("___"), "");
}

TEST_CASE("singular_and_plural_follow_english_spelling")
{
    CHECK_EQ(pluralOf("Animal"), "Animals");
    CHECK_EQ(pluralOf("Entry"), "Entries");
    CHECK_EQ(pluralOf("Key"), "Keys");
    CHECK_EQ(pluralOf("Box"), "Boxes");
    CHECK_EQ(pluralOf("Match"), "Matches");
    CHECK_EQ(pluralOf("Status"), "Statuses");
    CHECK_EQ(pluralOf("Child"), "Children");
    CHECK_EQ(pluralOf("Index"), "Indices");
    CHECK_EQ(pluralOf("Movie"), "Movies");
    CHECK_EQ(pluralOf("HttpRequest"), "HttpRequests");
    CHECK_EQ(pluralOf("Data"), "Data");

    CHECK_EQ(singularOf("Animals"), "Animal");
    CHECK_EQ(singularOf("Entries"), "Entry");
    CHECK_EQ(singularOf("Movies"), "Movie");
    CHECK_EQ(singularOf("Classes"), "Class");
    CHECK_EQ(singularOf("Caches"), "Cache");
    CHECK_EQ(singularOf("Matches"), "Match");
    CHECK_EQ(singularOf("Wolves"), "Wolf");
    CHECK_EQ(singularOf("CatNames"), "CatName");
    CHECK_EQ(singularOf("Status"), "Status");
    CHECK_EQ(singularOf("Axis"), "Axis");
    CHECK_EQ(singularOf("Series"), "Series");
}

TEST_CASE("a_list_name_never_reads_as_its_element")
{
    CHECK_EQ(listNameOf("Animal"), "Animals");
    // An uncountable noun's plural is itself, and a plural's would pile up another `s`.
    CHECK_EQ(listNameOf("Data"), "DataList");
    CHECK_EQ(listNameOf("Animals"), "AnimalsList");
}

TEST_CASE("names_are_taken_in_order_and_never_twice")
{
    TypeNameAllocator allocator{nullptr};
    allocator.reserve("Animals");

    CHECK_EQ(allocator.allocate({"Animals", "Pets"}), "Pets");
    CHECK_EQ(allocator.allocate({"Pets"}), "Nyla");
    CHECK_EQ(allocator.allocate({}), "Hina");
    CHECK(allocator.claimAlias("Animal"));
    CHECK(!allocator.claimAlias("Animal"));
}

// A long type with no alias or binding of its own gets a pet name, declared once under the two types.
TEST_CASE_FIXTURE(BuiltinsFixture, "repeated_type_with_nothing_to_name_it_after_gets_a_pet_name")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Animals = { [string]: { { age: number?, name: string, kind: "Cat" | "Dog" } } }
        local function takes_animals(animals: { [string]: { age: number?, name: string, kind: "Cat" | "Dog" } }) end
        local animals: Animals = {}
        takes_animals(animals)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '{ [string]: Nyla }'\n"
        "but was given\n"
        "    'animals: Animals'\n"
        "where\n"
        "    type Animals = { [string]: {Nyla} }\n"
        "    type Nyla = { age: number?, kind: \"Cat\" | \"Dog\", name: string }\n"
        "\n"
        "The passed type is similar, but has an incompatible path:\n"
        "  • animals[string] is an array where a single 'Nyla' is expected"
    );
}

// A repeated type found under a property is named after it, PascalCased. (Each naming test's mistake is one
// whose bullet doesn't say both types, so the given table is printed in full and the type really repeats.)
TEST_CASE_FIXTURE(BuiltinsFixture, "repeated_type_is_named_after_its_property")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function report(result: { http_code: { status: number, reason: string, retry: boolean }, ok: boolean }) end
        local response: { http_code: { status: number, reason: string, retry: boolean }, ok: { boolean } } = {
            http_code = { status = 200, reason = "OK", retry = false },
            ok = { true },
        }
        report(response)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '{ http_code: HttpCode, ok: boolean }'\n"
        "but was given\n"
        "    'response: { http_code: HttpCode, ok: {boolean} }'\n"
        "where\n"
        "    type HttpCode = { reason: string, retry: boolean, status: number }\n"
        "\n"
        "The passed type is similar, but has an incompatible path:\n"
        "  • response.ok is an array where a single 'boolean' is expected"
    );
}

// A type with the same structure as an alias in scope is printed by that alias, and a list of it by the
// alias's plural.
TEST_CASE_FIXTURE(BuiltinsFixture, "repeated_list_of_an_aliased_type_is_named_with_the_plural")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Animal = { age: number?, name: string, kind: "Cat" | "Dog" }
        local function adopt(shelter: { pets: { { age: number?, name: string, kind: "Cat" | "Dog" } }, open: boolean }) end
        local shelter: { pets: { { age: number?, name: string, kind: "Cat" | "Dog" } }, open: { boolean } } = { pets = {}, open = { true } }
        adopt(shelter)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '{ open: boolean, pets: Animals }'\n"
        "but was given\n"
        "    'shelter: { open: {boolean}, pets: Animals }'\n"
        "where\n"
        "    type Animals = {Animal}\n"
        "\n"
        "The passed type is similar, but has an incompatible path:\n"
        "  • shelter.open is an array where a single 'boolean' is expected"
    );
}

// A name already in scope for another type isn't reused.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_name_taken_in_scope_is_not_reused")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Config = number
        local function load(settings: { config: { host: string, port: number, secure: boolean }, retries: number }) end
        local settings: { config: { host: string, port: number, secure: boolean }, retries: { number } } = {
            config = { host = "localhost", port = 80, secure = false },
            retries = { 3 },
        }
        load(settings)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '{ config: Nyla, retries: number }'\n"
        "but was given\n"
        "    'settings: { config: Nyla, retries: {number} }'\n"
        "where\n"
        "    type Nyla = { host: string, port: number, secure: boolean }\n"
        "\n"
        "The passed type is similar, but has an incompatible path:\n"
        "  • settings.retries is an array where a single 'number' is expected"
    );
}

// A short type reads fine where it is, however often it's printed.
TEST_CASE_FIXTURE(BuiltinsFixture, "short_repeated_types_are_not_named")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function f(x: { a: { n: number }, b: number }) end
        local y: { a: { n: number }, b: string } = { a = { n = 1 }, b = "" }
        f(y)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]).find("where") == std::string::npos);
}

// An error `explainMismatch` didn't explain is named the same way, from its two types alone.
TEST_CASE_FIXTURE(BuiltinsFixture, "unexplained_mismatches_name_repeated_types_too")
{
    CheckResult result = check(R"(
        local a: { inner: { host: string, port: number, secure: boolean }, n: number } = nil :: any
        local b: { inner: { host: string, port: number, secure: boolean }, n: string } = nil :: any
    )");
    LUAU_REQUIRE_NO_ERRORS(result);

    TypeErrorData mismatch = TypeMismatch{requireType("a"), requireType("b")};
    nameRepeatedTypesInError(mismatch, nullptr);
    CHECK_EQ(
        toString(TypeError{Location{}, mismatch}),
        "Expected this to be\n    '{ inner: Inner, n: number }'\nbut got\n    '{ inner: Inner, n: string }'\n"
        "where\n    type Inner = { host: string, port: number, secure: boolean }"
    );

    TypeArena arena;
    TypeErrorData packMismatch = TypePackMismatch{arena.addTypePack({requireType("a")}), arena.addTypePack({requireType("b")})};
    nameRepeatedTypesInError(packMismatch, nullptr);
    CHECK_EQ(
        toString(TypeError{Location{}, packMismatch}),
        "Expected this to be '{ inner: Inner, n: number }', but got '{ inner: Inner, n: string }'\n"
        "where\n    type Inner = { host: string, port: number, secure: boolean }"
    );
}

// A table with more than three fields, or with a long one, is printed a field per line wherever the
// message shows a type on its own lines. A small table stays on one line, even inside a large one.
TEST_CASE_FIXTURE(BuiltinsFixture, "large_tables_are_printed_a_field_per_line")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function save(record: { id: number, name: string, tags: { string }, owner: { id: number } }) end
        local record: { id: string, name: string, tags: { string }, owner: { id: number } } = nil :: any
        save(record)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    {\n"
        "        id: number,\n"
        "        name: string,\n"
        "        owner: { id: number },\n"
        "        tags: {string}\n"
        "    }\n"
        "but was given\n"
        "    'record: { id: string, … }'\n"
        "\n"
        "The passed type is similar, but has an incompatible path:\n"
        "  • record.id: expected 'number', got 'string'"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_long_field_breaks_a_small_table")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function connect(options: { on_connection_established: (number) -> (), retries: number }) end
        local options: { on_connection_established: (number) -> (), retries: string } = nil :: any
        connect(options)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    {\n"
        "        on_connection_established: (number) -> (),\n"
        "        retries: number\n"
        "    }\n"
        "but was given\n"
        "    'options: { retries: string, … }'\n"
        "\n"
        "The passed type is similar, but has an incompatible path:\n"
        "  • options.retries: expected 'number', got 'string'"
    );
}

// A table literal held in a local is printed in full, however long, when a bullet doesn't say both types: a
// repeat in its second half still counts.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_repeat_late_in_a_long_literal_is_named")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Address = { street: string, city: string, postal_code: number, country: string }
        type Customer = { id: number, name: string, notes: string, billing: Address, shipping: Address }
        local function save_customer(customer: Customer) end
        local customer = {
            id = 7,
            name = "Dev",
            notes = { "gift" },
            billing = { street = "1 Main St", city = "Springfield", postal_code = 12345, country = "US" },
            shipping = { street = "1 Main St", city = "Springfield", postal_code = 12345, country = "US" },
        }
        save_customer(customer)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    std::string message = toString(result.errors[0]);
    INFO(message);
    CHECK(message.find("billing: Billing,") != std::string::npos);
    CHECK(message.find("shipping: Billing\n") != std::string::npos);
    CHECK(message.find("    type Billing = {\n") != std::string::npos);
}

TEST_SUITE_END();

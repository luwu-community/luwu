// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

#include <chrono>
#include <thread>

using namespace LunaTest;

TEST_SUITE_BEGIN("LunaDriver");

TEST_CASE_FIXTURE(LunaFixture, "diamond_includes_load_each_file_once_in_include_order")
{
    file("root.luna", "(include \"x/a.luna\")\n(include \"b.luna\")");
    file("x/a.luna", std::string("(include \"../shared.luna\")\n") + kRuleA);
    file("b.luna", std::string("(include \"shared.luna\")\n") + kRuleB);
    file("shared.luna", "(define-subrule (shared-helper [a :op]) (unsupported!))");

    run({"root.luna", "out"});
    requireOk();
    CHECK(out.find("from 4 file(s)") != std::string::npos);

    std::string header = read("out/LoweringRulesA64.h");
    size_t a = header.find("lower_RULE_A");
    size_t b = header.find("lower_RULE_B");
    REQUIRE(a != std::string::npos);
    REQUIRE(b != std::string::npos);
    CHECK(a < b);

    run({"--list-files", "root.luna"});
    requireOk();
    CHECK(out == "root.luna\n"
                 "  x/a.luna\n"
                 "    shared.luna\n"
                 "  b.luna\n"
                 "    shared.luna (already included)\n");
}

TEST_CASE_FIXTURE(LunaFixture, "include_cycle_is_an_error_with_the_chain_and_include_stack")
{
    file("root.luna", "(include \"a.luna\")");
    file("a.luna", "(include \"b.luna\")");
    file("b.luna", "(include \"a.luna\")");

    run({"root.luna", "out"});
    requireError({
        "b.luna:1:1: error: Include cycle: a.luna -> b.luna -> a.luna",
        "a.luna:1:1: note: included from here",
        "root.luna:1:1: note: included from here",
    });
}

TEST_CASE_FIXTURE(LunaFixture, "self_include_is_a_cycle")
{
    file("root.luna", "(include \"root.luna\")");

    run({"root.luna", "out"});
    requireError({"Include cycle: root.luna -> root.luna"});
}

TEST_CASE_FIXTURE(LunaFixture, "duplicate_definitions_across_files_report_both_locations")
{
    file("root.luna", "(include \"a.luna\")\n(include \"b.luna\")");
    file("a.luna", kRuleA);
    file("b.luna", "(define-rule (rule-a [a :op]) (unsupported!))");

    run({"root.luna", "out"});
    requireError({"b.luna:1:15: error: Duplicate definition of 'rule-a'", "previously defined at a.luna:2:1"});
}

TEST_CASE_FIXTURE(LunaFixture, "errors_in_included_files_show_the_include_stack")
{
    file("root.luna", "\n\n(include \"sub/a.luna\")");
    file("sub/a.luna", "(define-rule (rule-a [a :op])");

    run({"root.luna", "out"});
    requireError({"sub/a.luna:", "error: Expected closing ')'", "root.luna:3:1: note: included from here"});
}

TEST_CASE_FIXTURE(LunaFixture, "include_is_only_allowed_at_the_top_level")
{
    file("root.luna", "(define-rule (rule-a [a :op]) (include \"b.luna\"))");
    file("b.luna", kRuleB);

    run({"root.luna", "out"});
    requireError({"root.luna:1:31: error: include is only allowed at the top level of a file"});
}

TEST_CASE_FIXTURE(LunaFixture, "absolute_include_paths_are_rejected")
{
    file("b.luna", kRuleB);
    file("root.luna", "(include \"" + (dir / "b.luna").generic_string() + "\")");

    run({"root.luna", "out"});
    requireError({"include path must be relative to the including file"});
}

TEST_CASE_FIXTURE(LunaFixture, "includes_may_not_leave_the_source_root")
{
    file("outside.luna", kRuleB);
    file("src/root.luna", "(include \"../outside.luna\")");

    run({"src/root.luna", "out"});
    requireError({"leaves the source root"});
}

TEST_CASE_FIXTURE(LunaFixture, "missing_included_file_is_reported_at_the_include")
{
    file("root.luna", "(include \"nope.luna\")");

    run({"root.luna", "out"});
    requireError({"root.luna:1:10: error: Could not open included file \"nope.luna\""});
}

TEST_CASE_FIXTURE(LunaFixture, "include_needs_a_single_string_path")
{
    file("root.luna", "(include nope)");

    run({"root.luna", "out"});
    requireError({"include expects a single string path"});
}

TEST_CASE_FIXTURE(LunaFixture, "depfile_lists_every_file_once_with_phony_rules")
{
    file("root.luna", "(include \"a.luna\")\n(include \"b.luna\")\n(include \"a.luna\")");
    file("a.luna", kRuleA);
    file("b.luna", kRuleB);

    run({"root.luna", "out", "-MF", "out/gen.d", "-MT", "out/gen.stamp"});
    requireOk();
    CHECK(read("out/gen.d") == "out/gen.stamp: \\\n"
                               "  root.luna \\\n"
                               "  a.luna \\\n"
                               "  b.luna\n"
                               "\n"
                               "root.luna:\n"
                               "\n"
                               "a.luna:\n"
                               "\n"
                               "b.luna:\n");
}

TEST_CASE_FIXTURE(LunaFixture, "unchanged_outputs_are_not_rewritten")
{
    file("root.luna", kRuleA);

    run({"root.luna", "out"});
    requireOk();
    CHECK(out.find("5 output(s) updated") != std::string::npos);

    auto before = fs::last_write_time(dir / "out/LoweringRulesA64.h");

    // Make sure a rewrite would get a different timestamp
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    run({"root.luna", "out"});
    requireOk();
    CHECK(out.find("0 output(s) updated") != std::string::npos);
    CHECK(fs::last_write_time(dir / "out/LoweringRulesA64.h") == before);
}

TEST_CASE_FIXTURE(LunaFixture, "unknown_options_are_rejected")
{
    run({"--bogus"});
    requireError({"unknown option --bogus"});
}

TEST_SUITE_END();

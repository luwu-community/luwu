// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

using namespace LunaTest;

namespace
{

struct DocFixture : LunaFixture
{
};

} // namespace

TEST_SUITE_BEGIN("LunaDocComments");

TEST_CASE_FIXTURE(DocFixture, "tilde_inside_a_string_is_not_a_comment")
{
    file("root.luna", "(include \"a~b.luna\")");
    file("a~b.luna", kCmdA);

    run({"root.luna", "out"});
    requireOk();
}

TEST_CASE_FIXTURE(LunaFixture, "unattached_docs_are_errors")
{
    SUBCASE("before a closing bracket")
    {
        file("root.luna", "(define (rule-a a)\n"
                          "  (unsupported!)\n"
                          "  ~ dangling\n"
                          "  )");
        run({"root.luna", "out"});
        requireError({"root.luna:3:3: error: Doc comment isn't attached to anything"});
    }

    SUBCASE("at the end of a file")
    {
        file("root.luna", std::string(kCmdA) + "\n~ dangling");
        run({"root.luna", "out"});
        requireError({"root.luna:3:1: error: Doc comment isn't attached to anything"});
    }

    SUBCASE("trailing a line of closing brackets")
    {
        file("root.luna", "(define (rule-a a)\n"
                          "  (unsupported!)\n"
                          "  ) ~ dangling");
        run({"root.luna", "out"});
        requireError({"root.luna:3:5: error: Trailing doc comment has no expression on its line"});
    }
}

TEST_CASE_FIXTURE(LunaFixture, "doc_ending_in_a_backslash_is_an_error")
{
    file("root.luna", "~ continues \\\n" + std::string(kCmdA));

    run({"root.luna", "out"});
    requireError({"root.luna:1:1: error: Doc comment can't end with a backslash"});
}

TEST_CASE_FIXTURE(LunaFixture, "doc_on_include_is_an_error")
{
    file("root.luna", "~ the rules\n(include \"a.luna\")");
    file("a.luna", kCmdA);

    run({"root.luna", "out"});
    requireError({"root.luna:2:1: error: Doc comments on include aren't written anywhere"});
}

TEST_SUITE_END();

// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

using namespace LunaTest;

namespace
{

// Compiles root.luna and returns the generated A64 function for rule-a
struct DocFixture : LunaFixture
{
    std::string lowerRuleA(const std::string& header = "out/LoweringRulesA64.h")
    {
        run({"root.luna", "out"});
        requireOk();

        std::string text = read(header);
        size_t start = text.rfind("\n\n", text.find("inline void lower_RULE_A"));
        size_t end = text.find("\n}\n", start);
        REQUIRE(start != std::string::npos);
        REQUIRE(end != std::string::npos);
        return text.substr(start + 2, end + 2 - (start + 2));
    }
};

} // namespace

TEST_SUITE_BEGIN("LunaDocComments");

TEST_CASE_FIXTURE(DocFixture, "rule_docs_go_above_the_generated_function")
{
    file("root.luna", "~ Lowers rule-a\n"
                      "~\n"
                      "~ Mentions regs, index and next\n"
                      "(define-rule (rule-a [a :op])\n"
                      "  ~ so does this statement: regs, index, next, build, function, inst\n"
                      "  (unsupported!))");

    std::string fn = lowerRuleA();
    CHECK(fn.rfind("// Lowers rule-a\n"
                   "//\n"
                   "// Mentions regs, index and next\n"
                   "inline void lower_RULE_A(",
              0) == 0);

    // Words in comments don't count as uses of the function's locals
    CHECK(fn.find("auto& regs") == std::string::npos);
    CHECK(fn.find("auto& build") == std::string::npos);
    CHECK(fn.find("auto& function") == std::string::npos);
    CHECK(fn.find("(void)inst;") != std::string::npos);
    CHECK(fn.find("(void)index;") != std::string::npos);
    CHECK(fn.find("(void)next;") != std::string::npos);
}

TEST_CASE_FIXTURE(DocFixture, "trailing_doc_on_the_rule_header_line_documents_the_rule")
{
    file("root.luna", "(define-rule (rule-a [a :op]) ~ Lowers rule-a\n"
                      "  (unsupported!))");

    CHECK(lowerRuleA().rfind("// Lowers rule-a\ninline void lower_RULE_A(", 0) == 0);
}

TEST_CASE_FIXTURE(DocFixture, "statement_docs")
{
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  ~ leading\n"
                      "  (unsupported!)\n"
                      "  (unsupported!) ~ trailing, one line\n"
                      "  (if (constant? a) ~ trailing, several lines\n"
                      "      (unsupported!)))");

    std::string fn = lowerRuleA();
    CHECK(fn.find("    // leading\n"
                  "    CODEGEN_ASSERT(!\"Unsupported instruction form\");\n"
                  "    CODEGEN_ASSERT(!\"Unsupported instruction form\"); // trailing, one line\n"
                  "    // trailing, several lines\n"
                  "    if (OP_A(inst).kind == IrOpKind::Constant)\n") != std::string::npos);
}

TEST_CASE_FIXTURE(DocFixture, "binding_docs")
{
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  (let ([t (alloc-temp :i32)] ~ trailing\n"
                      "        ~ leading\n"
                      "        [u (alloc-temp :i32)])\n"
                      "    (unsupported!)))");

    std::string fn = lowerRuleA();
    CHECK(fn.find("    RegisterA64 t = regs.allocTemp(KindA64::w); // trailing\n"
                  "    // leading\n"
                  "    RegisterA64 u = regs.allocTemp(KindA64::w);\n") != std::string::npos);
}

TEST_CASE_FIXTURE(DocFixture, "cond_clause_docs_go_before_the_branch")
{
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  (cond\n"
                      "    ~ constant\n"
                      "    [(constant? a) (unsupported!)]\n"
                      "    [else ~ anything else\n"
                      "     (unsupported!)]))");

    std::string fn = lowerRuleA();
    CHECK(fn.find("    // constant\n"
                  "    if (OP_A(inst).kind == IrOpKind::Constant)\n") != std::string::npos);
    CHECK(fn.find("    }\n"
                  "    // anything else\n"
                  "    else\n") != std::string::npos);
}

TEST_CASE_FIXTURE(DocFixture, "target_case_docs_only_reach_their_target")
{
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  (target-case\n"
                      "    ~ x64 branch\n"
                      "    [:x64 (unsupported!)]\n"
                      "    [:a64\n"
                      "     ~ a64 statement\n"
                      "     (unsupported!)]))");

    std::string a64 = lowerRuleA();
    CHECK(a64.find("// a64 statement") != std::string::npos);
    CHECK(a64.find("x64 branch") == std::string::npos);

    std::string x64 = read("out/LoweringRulesX64.h");
    CHECK(x64.find("// x64 branch") != std::string::npos);
    CHECK(x64.find("a64 statement") == std::string::npos);
}

TEST_CASE_FIXTURE(DocFixture, "subrule_docs_are_written_where_the_subrule_is_used")
{
    file("root.luna", "~ helper docs stay in Luna\n"
                      "(define-subrule (helper! [a :op])\n"
                      "  ~ inside helper\n"
                      "  (unsupported!))\n"
                      "(define-rule (rule-a [a :op])\n"
                      "  ~ calls helper\n"
                      "  (helper! a))");

    std::string fn = lowerRuleA();
    CHECK(fn.find("    // calls helper\n"
                  "    // inside helper\n"
                  "    CODEGEN_ASSERT(") != std::string::npos);
    CHECK(read("out/LoweringRulesA64.h").find("helper docs stay in Luna") == std::string::npos);
}

TEST_CASE_FIXTURE(DocFixture, "tilde_inside_a_string_is_not_a_comment")
{
    file("root.luna", "(include \"a~b.luna\")");
    file("a~b.luna", kRuleA);

    run({"root.luna", "out"});
    requireOk();
}

TEST_CASE_FIXTURE(LunaFixture, "docs_on_arguments_are_errors")
{
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  (if\n"
                      "    ~ the test\n"
                      "    (constant? a)\n"
                      "    (unsupported!)))");

    run({"root.luna", "out"});
    requireError({"root.luna:4:5: error: Doc comments (~) can only be attached to statements"});
}

TEST_CASE_FIXTURE(LunaFixture, "trailing_doc_on_a_continuation_line_is_an_error")
{
    // The statement started on an earlier line, so the comment can't be moved up to it
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  (if (constant? a)\n"
                      "      (unsupported!)\n"
                      "      (assert! (constant?\n"
                      "                 a)))) ~ doc\n");

    run({"root.luna", "out"});
    requireError({"root.luna:5:18: error: Doc comments (~) can only be attached to statements"});
}

TEST_CASE_FIXTURE(LunaFixture, "doc_on_a_bound_value_is_an_error")
{
    file("root.luna", "(define-rule (rule-a [a :op])\n"
                      "  (let ([t\n"
                      "         ~ the temp\n"
                      "         (alloc-temp :i32)])\n"
                      "    (unsupported!)))");

    run({"root.luna", "out"});
    requireError({"root.luna:4:10: error: Doc comments (~) can only be attached to statements"});
}

TEST_CASE_FIXTURE(LunaFixture, "unattached_docs_are_errors")
{
    SUBCASE("before a closing bracket")
    {
        file("root.luna", "(define-rule (rule-a [a :op])\n"
                          "  (unsupported!)\n"
                          "  ~ dangling\n"
                          "  )");
        run({"root.luna", "out"});
        requireError({"root.luna:3:3: error: Doc comment isn't attached to anything"});
    }

    SUBCASE("at the end of a file")
    {
        file("root.luna", std::string(kRuleA) + "\n~ dangling");
        run({"root.luna", "out"});
        requireError({"root.luna:2:1: error: Doc comment isn't attached to anything"});
    }

    SUBCASE("trailing a line of closing brackets")
    {
        file("root.luna", "(define-rule (rule-a [a :op])\n"
                          "  (unsupported!)\n"
                          "  ) ~ dangling");
        run({"root.luna", "out"});
        requireError({"root.luna:3:5: error: Trailing doc comment has no expression on its line"});
    }
}

TEST_CASE_FIXTURE(LunaFixture, "doc_ending_in_a_backslash_is_an_error")
{
    file("root.luna", "~ continues \\\n" + std::string(kRuleA));

    run({"root.luna", "out"});
    requireError({"root.luna:1:1: error: Doc comment can't end with a backslash"});
}

TEST_CASE_FIXTURE(LunaFixture, "doc_on_include_is_an_error")
{
    file("root.luna", "~ the rules\n(include \"a.luna\")");
    file("a.luna", kRuleA);

    run({"root.luna", "out"});
    requireError({"root.luna:2:1: error: Doc comments on include aren't written anywhere"});
}

TEST_SUITE_END();

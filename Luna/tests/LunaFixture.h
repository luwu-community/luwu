// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Driver.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace LunaTest
{

namespace fs = std::filesystem;

// IR commands with the rules that lower them
inline const char* kRuleA = "(define-ircmd RULE_A (operands [a :any]) (class :effect) (lowering (rule-a a)))\n"
                            "(define-rule (rule-a [a :op]) (unsupported!))";
inline const char* kRuleB = "(define-ircmd RULE_B (operands [a :any]) (class :effect) (lowering (rule-b a)))\n"
                            "(define-rule (rule-b [a :op]) (unsupported!))";

// Just the command, for tests that write rule-a themselves
inline const char* kCmdA = "(define-ircmd RULE_A (operands [a :any]) (class :effect) (lowering (rule-a a)))\n";

// Fresh temporary directory that is also the working directory for the test, so paths in diagnostics stay short
// and relative (e.g. "root.luna:1:1")
struct LunaFixture
{
    fs::path dir;
    fs::path previousCwd;

    int status = 0;
    std::string out;
    std::string err;

    LunaFixture()
    {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("luna-test-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(dir / "out");
        previousCwd = fs::current_path();
        fs::current_path(dir);
    }

    ~LunaFixture()
    {
        fs::current_path(previousCwd);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void file(const std::string& path, const std::string& contents)
    {
        fs::path p = dir / path;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << contents << "\n";
    }

    std::string read(const std::string& path) const
    {
        std::ifstream in(dir / path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    void run(std::vector<std::string> args)
    {
        std::ostringstream outStream, errStream;
        status = Luwu::Dsl::runDriver(args, outStream, errStream);
        out = outStream.str();
        err = errStream.str();
    }

    void requireOk() const
    {
        INFO("stdout: " << out);
        INFO("stderr: " << err);
        REQUIRE(status == 0);
    }

    // Fails unless the run errored and stderr contains every fragment
    void requireError(const std::vector<std::string>& fragments) const
    {
        INFO("stderr: " << err);
        REQUIRE(status != 0);
        for (const auto& fragment : fragments)
        {
            INFO("missing: " << fragment);
            CHECK(err.find(fragment) != std::string::npos);
        }
    }
};

} // namespace LunaTest

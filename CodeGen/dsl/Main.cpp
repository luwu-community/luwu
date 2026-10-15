// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Ast.h"
#include "Lexer.h"
#include "Parser.h"
#include "Emitter.h"
#include "CompileError.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <input.luna> <output-dir>\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputDir = argv[2];

    // Ensure outputDir ends without trailing slash
    while (!outputDir.empty() && (outputDir.back() == '/' || outputDir.back() == '\\'))
    {
        outputDir.pop_back();
    }

    std::ifstream file(inputPath);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open input file: " << inputPath << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    try
    {
        Luwu::Dsl::Lexer lexer(source, inputPath);
        Luwu::Dsl::Parser parser(lexer);
        std::vector<Luwu::Dsl::SExpr> program = parser.parseProgram();

        std::vector<Luwu::Dsl::Rule> rules = Luwu::Dsl::parseRules(program);
        rules = Luwu::Dsl::expandRules(rules);

        // Emit AArch64
        std::string a64Path = outputDir + "/LoweringRulesA64.h";
        std::ofstream a64File(a64Path);
        if (!a64File.is_open())
        {
            std::cerr << "Error: Could not open output file: " << a64Path << "\n";
            return 1;
        }
        Luwu::Dsl::emitA64(a64File, rules);

        // Emit x86-64
        std::string x64Path = outputDir + "/LoweringRulesX64.h";
        std::ofstream x64File(x64Path);
        if (!x64File.is_open())
        {
            std::cerr << "Error: Could not open output file: " << x64Path << "\n";
            return 1;
        }
        Luwu::Dsl::emitX64(x64File, rules);

        std::cout << "luwu-dsl-compiler: compiled " << rules.size() << " rule(s) from "
                  << inputPath << " -> " << outputDir << "\n";
        return 0;
    }
    catch (const Luwu::Dsl::CompileError& e)
    {
        std::cerr << "luwu-dsl-compiler: " << e.what() << "\n";
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "luwu-dsl-compiler error: " << e.what() << "\n";
        return 1;
    }
}

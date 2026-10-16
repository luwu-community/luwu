// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Ast.h"
#include "Lexer.h"
#include "Parser.h"
#include "Lowering/Emitter.h"
#include "CompileError.h"

#include "Describe.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        std::string arg1 = argv[1];
        if (arg1 == "--describe" || arg1 == "--docs" || arg1 == "--list-forms")
        {
            std::string filter = (argc >= 3) ? argv[2] : "";
            Luwu::Dsl::printDocs(std::cout, filter);
            return 0;
        }
        if (arg1 == "--help" || arg1 == "-h")
        {
            std::cout << "Usage: " << argv[0] << " <input.luna> <output-dir>\n"
                      << "       " << argv[0] << " --describe [all|core|a64|x64]\n\n"
                      << "Luna Metacompiler for Luwu NCG IR Lowering.\n";
            return 0;
        }
    }

    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <input.luna> <output-dir>\n"
                  << "       " << argv[0] << " --describe [all|core|a64|x64]\n";
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


        std::ostringstream a64Out;
        Luwu::Dsl::emitA64(a64Out, rules);

        std::ostringstream x64Out;
        Luwu::Dsl::emitX64(x64Out, rules);

        for (const auto& [path, text] : {std::pair{outputDir + "/LoweringRulesA64.h", a64Out.str()},
                                         std::pair{outputDir + "/LoweringRulesX64.h", x64Out.str()}})
        {
            std::ofstream outFile(path);
            if (!outFile.is_open() || !(outFile << text))
            {
                std::cerr << "Error: Could not write output file: " << path << "\n";
                return 1;
            }
        }

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

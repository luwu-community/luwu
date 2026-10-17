// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Driver.h"

#include "Ast.h"
#include "Loader.h"
#include "Lowering/Emitter.h"
#include "CompileError.h"

#include "Describe.h"

#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const char* kUsage =
    "Usage: luwu-dsl-compiler <root.luna> <output-dir> [-MF <depfile> [-MT <target>]]\n"
    "       luwu-dsl-compiler --list-files <root.luna>\n"
    "       luwu-dsl-compiler --describe [all|core|a64|x64]\n";

// Leaves an up-to-date file untouched, so its timestamp doesn't trigger rebuilds of everything that includes it
static bool writeIfChanged(const std::string& path, const std::string& text, bool& changed)
{
    {
        std::ifstream existing(path, std::ios::binary);
        if (existing)
        {
            std::stringstream buffer;
            buffer << existing.rdbuf();
            if (buffer.str() == text)
            {
                changed = false;
                return true;
            }
        }
    }

    std::ofstream out(path, std::ios::binary);
    changed = true;
    return out.is_open() && bool(out << text);
}

static std::string escapeMakePath(const std::string& path)
{
    std::string result;
    for (char c : path)
    {
        if (c == ' ' || c == '#')
            result += '\\';
        if (c == '$')
            result += '$';
        result += c;
    }
    return result;
}

// Make-style dependency file: the target depends on every loaded .luna file. Each file also gets an empty rule, so
// deleting or renaming one doesn't break the build (like the compiler's -MP).
static std::string makeDepfile(const std::string& target, const std::vector<Luwu::Dsl::SourceFile>& files)
{
    std::string text = escapeMakePath(target) + ":";
    for (const auto& file : files)
        text += " \\\n  " + escapeMakePath(file.path);
    text += "\n";
    for (const auto& file : files)
        text += "\n" + escapeMakePath(file.path) + ":\n";
    return text;
}

static void printIncludeTree(std::ostream& out, const std::vector<Luwu::Dsl::SourceFile>& files, size_t index, int depth, std::vector<bool>& printed)
{
    out << std::string(depth * 2, ' ') << files[index].path;
    if (printed[index])
    {
        out << " (already included)\n";
        return;
    }
    out << "\n";
    printed[index] = true;

    for (size_t child : files[index].includes)
        printIncludeTree(out, files, child, depth + 1, printed);
}

static void reportError(std::ostream& err, const Luwu::Dsl::CompileError& e, const Luwu::Dsl::SourceLoader& loader)
{
    err << "luwu-dsl-compiler: " << e.what() << "\n";
    for (const auto& loc : loader.includeStack(e.loc().file))
        err << "luwu-dsl-compiler: " << loc.toString() << ": note: included from here\n";
}

namespace Luwu::Dsl
{

int runDriver(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
    std::vector<std::string> positional;
    std::string depfilePath;
    std::string depTarget;
    bool listFiles = false;

    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& arg = args[i];
        if (arg == "--describe" || arg == "--docs" || arg == "--list-forms")
        {
            std::string filter = (i + 1 < args.size()) ? args[i + 1] : "";
            Luwu::Dsl::printDocs(out, filter);
            return 0;
        }
        if (arg == "--help" || arg == "-h")
        {
            out << kUsage << "\nLuna Metacompiler for Luwu NCG IR Lowering.\n";
            return 0;
        }
        if (arg == "--list-files")
        {
            listFiles = true;
        }
        else if ((arg == "-MF" || arg == "-MT") && i + 1 < args.size())
        {
            (arg == "-MF" ? depfilePath : depTarget) = args[++i];
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            err << "luwu-dsl-compiler: unknown option " << arg << "\n" << kUsage;
            return 1;
        }
        else
        {
            positional.push_back(arg);
        }
    }

    if (positional.size() != (listFiles ? 1u : 2u) || (!depTarget.empty() && depfilePath.empty()))
    {
        err << kUsage;
        return 1;
    }

    std::string inputPath = positional[0];
    Luwu::Dsl::SourceLoader loader;

    try
    {
        std::vector<Luwu::Dsl::SExpr> program = loader.load(inputPath);

        if (listFiles)
        {
            std::vector<bool> printed(loader.files().size());
            printIncludeTree(out, loader.files(), 0, 0, printed);
            return 0;
        }

        std::string outputDir = positional[1];
        while (!outputDir.empty() && (outputDir.back() == '/' || outputDir.back() == '\\'))
            outputDir.pop_back();

        std::vector<Luwu::Dsl::Rule> rules = Luwu::Dsl::parseRules(program);
        rules = Luwu::Dsl::expandRules(rules);

        std::ostringstream a64Out;
        Luwu::Dsl::emitA64(a64Out, rules);

        std::ostringstream x64Out;
        Luwu::Dsl::emitX64(x64Out, rules);

        std::vector<std::pair<std::string, std::string>> outputs = {
            {outputDir + "/LoweringRulesA64.h", a64Out.str()},
            {outputDir + "/LoweringRulesX64.h", x64Out.str()},
        };

        int updated = 0;
        for (const auto& [path, text] : outputs)
        {
            bool changed = false;
            if (!writeIfChanged(path, text, changed))
            {
                err << "Error: Could not write output file: " << path << "\n";
                return 1;
            }
            updated += changed;
        }

        if (!depfilePath.empty())
        {
            std::string target = depTarget;
            if (target.empty())
            {
                for (const auto& output : outputs)
                    target += (target.empty() ? "" : " ") + output.first;
            }

            bool changed = false;
            if (!writeIfChanged(depfilePath, makeDepfile(target, loader.files()), changed))
            {
                err << "Error: Could not write depfile: " << depfilePath << "\n";
                return 1;
            }
        }

        out << "luwu-dsl-compiler: compiled " << rules.size() << " rule(s) from " << loader.files().size() << " file(s) ("
                  << inputPath << ") -> " << outputDir << ", " << updated << " output(s) updated\n";
        return 0;
    }
    catch (const Luwu::Dsl::CompileError& e)
    {
        reportError(err, e, loader);
        return 1;
    }
    catch (const std::exception& e)
    {
        err << "luwu-dsl-compiler error: " << e.what() << "\n";
        return 1;
    }
}

} // namespace Luwu::Dsl

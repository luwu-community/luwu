// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Driver.h"

#include "Core/Ast.h"
#include "Core/Loader.h"
#include "Core/CompileError.h"

#include "Core/Describe.h"
#include "Core/Evaluator.h"

#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const char* kUsage =
    "Usage: luwu-dsl-compiler <root.luna> <output-dir> [--public-output <dir>] [--lowering-output <dir>] [-MF <depfile> [-MT <target>]]\n"
    "       luwu-dsl-compiler --list-files <root.luna>\n"
    "       luwu-dsl-compiler --eval <file.luna>\n"
    "       luwu-dsl-compiler --describe\n";

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
static std::string makeDepfile(const std::string& target, const std::vector<std::string>& files)
{
    std::string text = escapeMakePath(target) + ":";
    for (const auto& file : files)
        text += " \\\n  " + escapeMakePath(file);
    text += "\n";
    for (const auto& file : files)
        text += "\n" + escapeMakePath(file) + ":\n";
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

std::vector<std::string> installNcg(Evaluator& evaluator)
{
    SourceLoader loader;
    // kept alive with the evaluator: closures point into the forms they were made from
    for (const auto& form : loader.load(std::string(LUNA_DEFS_DIR) + "/ncg/cpp.luna"))
        evaluator.eval(evaluator.keep(form));
    for (const auto& form : loader.load(std::string(LUNA_DEFS_DIR) + "/ncg/ircmds.luna"))
        evaluator.eval(evaluator.keep(form));

    evaluator.resetGensym();

    std::vector<std::string> files;
    for (const auto& f : loader.files())
        files.push_back(f.path);
    return files;
}

int runDriver(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
    std::vector<std::string> positional;
    std::string depfilePath;
    std::string depTarget;
    std::string publicOutputDir;
    std::string loweringOutputDir;
    bool listFiles = false;
    bool evalOnly = false;

    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& arg = args[i];
        if (arg == "--describe" || arg == "--docs" || arg == "--list-forms")
        {
            Luwu::Dsl::Evaluator evaluator;
            Luwu::Dsl::installNcg(evaluator);
            Luwu::Dsl::printDocs(out, evaluator);
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
        else if (arg == "--eval")
        {
            evalOnly = true;
        }
        else if ((arg == "-MF" || arg == "-MT" || arg == "--public-output" || arg == "--lowering-output") && i + 1 < args.size())
        {
            (arg == "-MF" ? depfilePath : arg == "-MT" ? depTarget : arg == "--public-output" ? publicOutputDir : loweringOutputDir) = args[++i];
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

    if (positional.size() != (listFiles || evalOnly ? 1u : 2u) || (!depTarget.empty() && depfilePath.empty()))
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

        // The program is evaluated; its define-ircmd forms register the commands the headers are made from
        Luwu::Dsl::Evaluator evaluator;
        std::vector<std::string> moduleFiles = Luwu::Dsl::installNcg(evaluator);
        Luwu::Dsl::Value last = evaluator.evalProgram(program);

        // --eval prints the program's last value
        if (evalOnly)
        {
            out << Luwu::Dsl::toString(last) << "\n";
            return 0;
        }

        auto trimDir = [](std::string dir) {
            while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
                dir.pop_back();
            return dir;
        };
        std::string outputDir = trimDir(positional[1]);
        // Headers other components include (the IrCmd enum and per-command information); next to the lowering
        // headers unless given
        std::string publicDir = publicOutputDir.empty() ? outputDir : trimDir(publicOutputDir);

        // The headers, from defs/ncg/ircmds.luna: ((:public|:lowering "name" text) ...)
        auto call = [&](const std::string& name, std::vector<Luwu::Dsl::Value> args) {
            const Luwu::Dsl::Value* fn = evaluator.globals()->lookup(name);
            if (!fn)
                throw Luwu::Dsl::CompileError(Luwu::Dsl::SourceLocation{}, name + " isn't defined (defs/ncg/ircmds.luna)");
            return evaluator.apply(*fn, std::move(args), Luwu::Dsl::SExpr{});
        };
        bool lowering = !loweringOutputDir.empty();
        std::vector<Luwu::Dsl::Value> generated;
        Luwu::Dsl::listToVector(call("ncg-outputs", {Luwu::Dsl::Value::boolean(lowering)}), generated);
        std::vector<std::pair<std::string, std::string>> outputs;
        for (const auto& g : generated)
        {
            std::vector<Luwu::Dsl::Value> parts;
            Luwu::Dsl::listToVector(g, parts);
            std::string dir = parts[0].text == ":lowering" ? trimDir(loweringOutputDir) : publicDir;
            outputs.push_back({dir + "/" + parts[1].text, parts[2].text});
        }

        // The micro-op pipeline's lowering, written where asked (CodeGen's LoweringRules headers are frozen until it lowers
        // everything)
        if (lowering)
        {
            std::vector<Luwu::Dsl::Value> notLowered;
            Luwu::Dsl::listToVector(call("ncg-not-lowered", {}), notLowered);
            if (!notLowered.empty())
            {
                out << "luwu-dsl-compiler: " << notLowered.size() << " command(s) not lowered by the micro-op pipeline yet:";
                for (const auto& name : notLowered)
                    out << " " << name.text;
                out << "\n";
            }
        }

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
            std::vector<std::string> files = moduleFiles;
            for (const auto& file : loader.files())
                files.push_back(file.path);
            if (!writeIfChanged(depfilePath, makeDepfile(target, files), changed))
            {
                err << "Error: Could not write depfile: " << depfilePath << "\n";
                return 1;
            }
        }

        out << "luwu-dsl-compiler: compiled " << Luwu::Dsl::toString(call("ncg-command-count", {})) << " IR command(s) from " << loader.files().size() << " file(s) ("
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

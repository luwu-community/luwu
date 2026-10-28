// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/LuwonJsonCommand.h"

#include "Luau/FileUtils.h"
#include "Luau/Luwon.h"

#include <stdio.h>
#include <string.h>

enum class LuwonJsonConversion
{
    ToJson,
    ToLosslessJson,
    ToLuwon,
};

static std::optional<LuwonJsonConversion> conversionOf(const char* arg)
{
    if (strcmp(arg, "--json") == 0 || strcmp(arg, "--luwon-to-json") == 0)
        return LuwonJsonConversion::ToJson;
    if (strcmp(arg, "--json=lossless") == 0 || strcmp(arg, "--luwon-to-json=lossless") == 0)
        return LuwonJsonConversion::ToLosslessJson;
    if (strcmp(arg, "--luwon") == 0 || strcmp(arg, "--json-to-luwon") == 0)
        return LuwonJsonConversion::ToLuwon;
    return std::nullopt;
}

std::optional<int> runLuwonJsonCommand(int argc, char** argv)
{
    std::optional<LuwonJsonConversion> conversion;
    const char* option = nullptr;
    const char* input = nullptr;
    for (int i = 1; i < argc; i++)
    {
        if (std::optional<LuwonJsonConversion> asked = conversionOf(argv[i]))
        {
            conversion = asked;
            option = argv[i];
        }
        else if (!input && (argv[i][0] != '-' || strcmp(argv[i], "-") == 0))
            input = argv[i];
    }

    if (!conversion)
        return std::nullopt;

    if (!input)
    {
        fprintf(stderr, "%s needs a file to read, or - to read stdin\n", option);
        return 1;
    }

    bool fromStdin = strcmp(input, "-") == 0;
    std::optional<std::string> source = fromStdin ? readStdin() : readFile(input);
    if (!source)
    {
        fprintf(stderr, "Couldn't read %s\n", input);
        return 1;
    }

    const char* name = fromStdin ? "stdin" : input;
    bool toLuwon = *conversion == LuwonJsonConversion::ToLuwon;
    Luau::LuwonDocument document = toLuwon ? Luau::readLuwonJson(*source) : Luau::readLuwon(*source);
    if (!document.errors.empty())
    {
        const Luau::LuwonError& error = document.errors.front();
        fprintf(stderr, "%s:%d:%d: %s\n", name, error.location.begin.line + 1, error.location.begin.column + 1, error.message.c_str());
        return 1;
    }

    Luau::LuwonWriteResult written = toLuwon ? Luau::writeLuwon(*document.value)
                                             : Luau::writeLuwonJson(*document.value, *conversion == LuwonJsonConversion::ToLosslessJson);
    if (written.error)
    {
        fprintf(stderr, "%s: %s\n", name, written.error->c_str());
        return 1;
    }

    fwrite(written.text.data(), 1, written.text.size(), stdout);
    return 0;
}

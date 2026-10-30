// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/TypeNaming.h"

#include "Luau/Scope.h"

#include <cctype>

namespace Luau
{

namespace
{

// Plural nouns spelled other than by adding `s` or `es`, as `{singular, plural}`. Only the ones the rules
// below get wrong, in either direction.
const std::vector<std::pair<std::string, std::string>>& irregularNouns()
{
    static const std::vector<std::pair<std::string, std::string>> nouns = {
        {"child", "children"},       {"person", "people"},         {"man", "men"},
        {"woman", "women"},          {"mouse", "mice"},            {"goose", "geese"},
        {"foot", "feet"},            {"tooth", "teeth"},           {"ox", "oxen"},
        {"index", "indices"},        {"vertex", "vertices"},       {"matrix", "matrices"},
        {"appendix", "appendices"},  {"criterion", "criteria"},    {"phenomenon", "phenomena"},
        {"analysis", "analyses"},    {"axis", "axes"},             {"crisis", "crises"},
        {"thesis", "theses"},        {"leaf", "leaves"},           {"wolf", "wolves"},
        {"half", "halves"},          {"knife", "knives"},          {"life", "lives"},
        {"wife", "wives"},           {"self", "selves"},           {"shelf", "shelves"},
        {"thief", "thieves"},        {"loaf", "loaves"},           {"calf", "calves"},
        {"elf", "elves"},            {"hero", "heroes"},           {"potato", "potatoes"},
        {"tomato", "tomatoes"},      {"echo", "echoes"},           {"veto", "vetoes"},
        {"torpedo", "torpedoes"},    {"bus", "buses"},             {"gas", "gases"},
        {"quiz", "quizzes"},         {"alias", "aliases"},         {"bias", "biases"},
        {"canvas", "canvases"},      {"atlas", "atlases"},         {"cactus", "cacti"},
        {"fungus", "fungi"},         {"nucleus", "nuclei"},        {"radius", "radii"},
        {"stimulus", "stimuli"},     {"movie", "movies"},          {"cookie", "cookies"},
        {"zombie", "zombies"},       {"rookie", "rookies"},        {"calorie", "calories"},
        {"pie", "pies"},             {"tie", "ties"},              {"lie", "lies"},
        {"brownie", "brownies"},     {"selfie", "selfies"},        {"hoodie", "hoodies"},
        {"cache", "caches"},         {"headache", "headaches"},    {"ache", "aches"},
        {"niche", "niches"},         {"mustache", "mustaches"},    {"avalanche", "avalanches"},
    };
    return nouns;
}

// Nouns with no separate plural. A list of them is named `DataList`, not `Datas`.
bool isUncountable(const std::string& word)
{
    static const std::unordered_set<std::string> words = {
        "data",     "metadata", "info",  "information", "news",     "series",   "species", "equipment", "sheep",
        "fish",     "deer",     "aircraft", "software", "hardware", "feedback", "media",   "config",    "chaos",
        "physics",  "means",    "offspring", "staff",   "music",    "money",
    };
    return words.count(word) != 0;
}

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isVowel(char c)
{
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

// The singular of a lowercase word, or the word itself when it isn't a plural.
std::string singularWord(const std::string& word)
{
    if (isUncountable(word))
        return word;
    for (const auto& [singular, plural] : irregularNouns())
    {
        if (word == plural)
            return singular;
    }

    // `entries`; `ties` and `pies` are irregular, being too short to tell from `-ies` endings.
    if (endsWith(word, "ies") && word.size() > 4)
        return word.substr(0, word.size() - 3) + "y";
    bool takesEs = endsWith(word, "sses") || endsWith(word, "xes") || endsWith(word, "ches") || endsWith(word, "shes") || endsWith(word, "zzes");
    if (takesEs)
        return word.substr(0, word.size() - 2);
    // A word ending in `ss`, `us` or `is` is already singular: `class`, `status`, `axis`.
    bool singularS = endsWith(word, "ss") || endsWith(word, "us") || endsWith(word, "is");
    if (endsWith(word, "s") && word.size() > 1 && !singularS)
        return word.substr(0, word.size() - 1);
    return word;
}

// The plural of a lowercase singular word, or the word itself when it has none.
std::string pluralWord(const std::string& word)
{
    if (isUncountable(word))
        return word;
    for (const auto& [singular, plural] : irregularNouns())
    {
        if (word == singular)
            return plural;
    }

    if (word.size() > 1 && word.back() == 'y' && !isVowel(word[word.size() - 2]))
        return word.substr(0, word.size() - 1) + "ies";
    bool takesEs = endsWith(word, "s") || endsWith(word, "x") || endsWith(word, "z") || endsWith(word, "ch") || endsWith(word, "sh");
    return word + (takesEs ? "es" : "s");
}

// Where the last word of a PascalCase name starts: `Names` in `CatNames`, `Requests` in `HTTPRequests`.
size_t lastWordStart(const std::string& name)
{
    for (size_t i = name.size(); i-- > 1;)
    {
        bool upper = isupper(static_cast<unsigned char>(name[i]));
        bool afterLower = !isupper(static_cast<unsigned char>(name[i - 1]));
        bool beforeLower = i + 1 < name.size() && islower(static_cast<unsigned char>(name[i + 1]));
        if (upper && (afterLower || beforeLower))
            return i;
    }
    return 0;
}

// `name` with `change` applied to its last word, lowercased, keeping that word's capital letter.
template<typename Change>
std::string changeLastWord(const std::string& name, Change change)
{
    size_t start = lastWordStart(name);
    std::string word = name.substr(start);
    std::string lower = word;
    for (char& c : lower)
        c = char(tolower(static_cast<unsigned char>(c)));

    std::string changed = change(lower);
    if (changed == lower)
        return name;
    if (!word.empty() && isupper(static_cast<unsigned char>(word[0])) && !changed.empty())
        changed[0] = char(toupper(static_cast<unsigned char>(changed[0])));
    return name.substr(0, start) + changed;
}
bool isAllUpper(const std::string& part)
{
    for (char c : part)
    {
        if (islower(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

} // namespace

std::string pascalCase(const std::string& word)
{
    std::string name;
    std::string part;
    auto flush = [&]
    {
        if (part.empty())
            return;
        // An all-caps part (`HTTP` in `HTTP_CODE`) is a shouted word, not an acronym inside a camelCase
        // name (`URLPath`), so it gets lowercased like one.
        bool shouted = part.size() > 1 && isAllUpper(part);
        for (size_t i = 0; i < part.size(); ++i)
        {
            char c = part[i];
            if (i == 0)
                c = char(toupper(static_cast<unsigned char>(c)));
            else if (shouted)
                c = char(tolower(static_cast<unsigned char>(c)));
            name += c;
        }
        part.clear();
    };

    for (char c : word)
    {
        if (!isalnum(static_cast<unsigned char>(c)))
        {
            flush();
            continue;
        }
        // A name can't start with a digit, so leading ones are dropped.
        if (name.empty() && part.empty() && isdigit(static_cast<unsigned char>(c)))
            continue;
        part += c;
    }
    flush();
    return name;
}

std::string singularOf(const std::string& name)
{
    return changeLastWord(name, singularWord);
}

std::string pluralOf(const std::string& name)
{
    return changeLastWord(name, pluralWord);
}

std::string listNameOf(const std::string& element)
{
    std::string plural = pluralOf(element);
    bool pluralAlready = singularOf(element) != element;
    if (plural == element || pluralAlready)
        return element + "List";
    return plural;
}

const std::vector<std::string>& petNames()
{
    static const std::vector<std::string> names = {
        "Nyla",
        "Hina",
        "Taz",
        "Nanuk",
        "Sam",
        "MrPurrsalot",
        "Jasper",
        "Cookie",
        "MrCat",
        "Yuki",
        "Luna",
        "Willow",
    };
    return names;
}

TypeNameAllocator::TypeNameAllocator(const Scope* scope)
    : scope(scope)
{
}

void TypeNameAllocator::reserve(const std::string& name)
{
    used.insert(name);
}

bool TypeNameAllocator::claimAlias(const std::string& alias)
{
    return used.insert(alias).second;
}

bool TypeNameAllocator::isTaken(const std::string& name) const
{
    return used.count(name) || (scope && scope->lookupType(name));
}

std::string TypeNameAllocator::allocate(const std::vector<std::string>& preferred)
{
    std::vector<std::string> names;
    for (const std::string& name : preferred)
    {
        if (!name.empty())
            names.push_back(name);
    }
    names.insert(names.end(), petNames().begin(), petNames().end());

    for (const std::string& name : names)
    {
        if (!isTaken(name))
        {
            used.insert(name);
            return name;
        }
    }
    for (size_t suffix = 2;; ++suffix)
    {
        std::string numbered = names.front() + std::to_string(suffix);
        if (!isTaken(numbered))
        {
            used.insert(numbered);
            return numbered;
        }
    }
}

} // namespace Luau

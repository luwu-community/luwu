// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/MismatchWording.h"

#include "Luau/Common.h"
#include "Luau/Error.h"

#include <algorithm>

namespace Luau
{
namespace MismatchWording
{

DiffPath with(const DiffPath& path, Segment segment)
{
    DiffPath longer = path;
    longer.push_back(std::move(segment));
    return longer;
}

DiffPath onGivenSide(const DiffPath& path)
{
    DiffPath place;
    for (const Segment& segment : path)
    {
        if (segment.kind != Segment::Kind::Member)
            place.push_back(segment);
    }
    return place;
}

std::string Rendering::givenName()
{
    if (givenIsPlaceholder)
        notation |= MismatchNotationGotRoot;
    return givenRoot;
}

std::string Rendering::givenPath(const DiffPath& path, size_t from, size_t to)
{
    return renderPath(givenName(), path, from, to, &notation);
}

std::string Rendering::givenPath(const DiffPath& path)
{
    return givenPath(path, 0, path.size());
}

std::string Rendering::givenMember(size_t number)
{
    notation |= MismatchNotationUnionMember;
    givenMembersShown.insert(number);
    return givenName() + "#" + std::to_string(number);
}

std::string Rendering::expectedMember(size_t number)
{
    notation |= MismatchNotationUnionMember;
    expectedMembersShown.insert(number);
    if (expectedIsPlaceholder)
        notation |= MismatchNotationExpectedRoot;
    return expectedRoot + "#" + std::to_string(number);
}

std::string Rendering::expectedName()
{
    if (expectedIsPlaceholder)
        notation |= MismatchNotationExpectedRoot;
    return quoted(expectedRoot);
}

std::string Rendering::expectedTypeName()
{
    return expectedIsPlaceholder ? "the expected type" : quoted(expectedRoot);
}

std::string quoted(std::string_view s)
{
    std::string result;
    result.reserve(s.size() + 2);
    result += '\'';
    result += s;
    result += '\'';
    return result;
}

std::string ordinal(size_t n)
{
    const char* suffix = "th";
    if (n % 100 < 11 || n % 100 > 13)
    {
        if (n % 10 == 1)
            suffix = "st";
        else if (n % 10 == 2)
            suffix = "nd";
        else if (n % 10 == 3)
            suffix = "rd";
    }
    return std::to_string(n) + suffix;
}

std::string joinedWithAnd(const std::vector<std::string>& items)
{
    std::string result;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            result += i + 1 == items.size() ? " and " : ", ";
        result += items[i];
    }
    return result;
}

std::string joined(const std::vector<std::string>& items, std::string_view separator)
{
    std::string result;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            result += separator;
        result += items[i];
    }
    return result;
}

std::string countedPaths(size_t count)
{
    if (count == 1)
        return "an incompatible path";
    return std::to_string(count) + " incompatible paths";
}

std::string renderPath(const std::string& root, const DiffPath& path, size_t from, size_t to, uint8_t* notation)
{
    std::string s = root;
    for (size_t i = from; i < to && i < path.size(); ++i)
    {
        const Segment& segment = path[i];
        switch (segment.kind)
        {
        case Segment::Kind::Property:
            s += (s.empty() ? "" : ".") + segment.name;
            break;
        case Segment::Kind::Indexer:
            s += "[" + segment.name + "]";
            if (notation)
                *notation |= segment.name == "i" ? MismatchNotationArrayElement : MismatchNotationIndexer;
            break;
        case Segment::Kind::Member:
            s += "#" + std::to_string(segment.index);
            if (notation)
                *notation |= segment.overload ? MismatchNotationOverload : MismatchNotationUnionMember;
            break;
        case Segment::Kind::Parameter:
            s += segment.name.empty() ? "argument " + std::to_string(segment.index + 1) : segment.name;
            break;
        case Segment::Kind::Return:
            s += "return value" + (segment.onlyOne ? std::string() : " " + std::to_string(segment.index + 1));
            break;
        }
    }
    return s;
}

namespace
{

// Whether `rest` continues a path by itself (`[i]`, `#2`) rather than needing a `.` before it.
bool indexesDirectly(const std::string& rest)
{
    return rest.empty() || rest[0] == '[' || rest[0] == '#';
}

// The part of a function a sentence is about, as a noun: `payload[string]`, `self`, `its 2nd
// argument`, `(its return value)[i].chance`.
std::string slotNoun(const DiffPath& path, size_t at, size_t end, uint8_t* notation)
{
    const Segment& slot = path[at];
    std::string rest = renderPath("", path, at + 1, end, notation);

    if (slot.kind == Segment::Kind::Parameter && !slot.name.empty())
        return slot.name + (indexesDirectly(rest) ? rest : "." + rest);

    std::string noun;
    if (slot.kind == Segment::Kind::Parameter)
        noun = "its " + ordinal(slot.index + 1) + " argument";
    else
        noun = slot.onlyOne ? "its return value" : "its " + ordinal(slot.index + 1) + " return value";

    if (rest.empty())
        return noun;

    // A path needs something to index: `[i].chance` on its own refers to nothing. The phrase in
    // parentheses is that something, the way a parameter's name is in `payload[string]`.
    return "(" + noun + ")" + (indexesDirectly(rest) ? rest : "." + rest);
}

// A slot noun in a sentence: a parameter's name is quoted, a phrase like `its 2nd argument` isn't.
std::string slotInProse(const std::string& noun, bool named)
{
    return named ? quoted(noun) : noun;
}

// A value passed to a parameter: `'i: number'` for a named one, `'number' as its 2nd argument` for one
// whose name is lost. `parameterWord` says `parameter 'i: number'`, for a sentence with the signature
// in it.
std::string passedValue(const std::string& noun, bool named, const std::string& type, bool parameterWord)
{
    if (named)
        return (parameterWord ? "parameter " : "") + quoted(noun + ": " + type);
    return quoted(type) + " as " + noun;
}

// Where among a function's returns a difference is. "should return X for its return value" says
// "return" twice, so only a position among several values, or a place inside one, is said.
std::string returnPlace(const DiffPath& path, size_t at, size_t end, uint8_t* notation)
{
    const Segment& slot = path[at];
    if (end == at + 1)
        return slot.onlyOne ? std::string() : " as its " + ordinal(slot.index + 1) + " value";
    return " for " + slotNoun(path, at, end, notation);
}

// The field a `Missing` difference is about, with its type.
std::string missingField(const Difference& d)
{
    return quoted(d.path.back().name + ": " + d.super);
}

// A parameter declared as the non-nil version of what the expected type passes it: says what it should
// be, which is the fix. Why a narrower parameter breaks things goes in a help under the bullets
// (`narrowedParameterHelp`), since it runs against the intuition that a `Player` is fine for a `Player?`.
std::string nonNilVersion(const Difference& d, const std::string& noun, bool named)
{
    std::string should = quoted(d.sub) + (d.subMeaning ? " (which is " + quoted(*d.subMeaning) + ")" : "");
    return slotInProse(noun, named) + " should be " + should + ", not " + quoted(d.super);
}

// The sentence for a difference inside a function. `function` is how the sentence names it: its
// signature when the bullet is only this sentence, or the name on the `name = signature` line above.
// `callee` is who calls it, when that's known and this is the first sentence about it. A collapsed
// sentence has the signature right in it, so it doesn't repeat what the signature already says.
std::string describeInFunction(
    const Difference& d,
    const std::string& function,
    const std::optional<std::string>& callee,
    bool collapsed,
    uint8_t* notation
)
{
    LUAU_ASSERT(d.anchor && d.anchor->at < d.path.size());
    size_t at = d.anchor->at;
    const Segment& slot = d.path[at];
    std::string noun = slotNoun(d.path, at, d.path.size(), notation);

    if (slot.kind == Segment::Kind::Parameter)
    {
        bool named = !slot.name.empty();
        std::string calls = callee ? quoted(*callee) + " can incorrectly call " + quoted(function) : quoted(function) + " can incorrectly be called";
        // What the parameter takes, in the same clause as what it's given. A collapsed sentence has the
        // signature in it already, so it leaves this out.
        std::string expecting = collapsed ? std::string() : ", where " + quoted(d.super) + " is expected";

        switch (d.kind)
        {
        case Difference::Kind::Mismatch:
            return calls + " with " + passedValue(noun, named, d.sub, collapsed) + expecting;
        case Difference::Kind::MismatchExactly:
            return calls + " with " + passedValue(noun, named, d.sub, collapsed) + ", where exactly " + quoted(d.super) + " is expected";
        case Difference::Kind::CouldBeNil:
            return "In " + quoted(function) + ", " + nonNilVersion(d, noun, named);
        case Difference::Kind::NotAMap:
            return calls + " with " + passedValue(noun, named, d.sub, collapsed) + " (named fields but no indexer), where the map " +
                   quoted(d.super) + " is expected";
        case Difference::Kind::Missing:
        {
            std::string parent = slotNoun(d.path, at, d.path.size() - 1, notation);
            return calls + " with " + slotInProse(parent, named) + " missing field " + missingField(d);
        }
        case Difference::Kind::NothingPassed:
            return calls + " without " + slotInProse(noun, named) + (collapsed ? "" : ", which doesn't take 'nil'");
        case Difference::Kind::ExtraPassed:
        {
            std::string has = d.parameterCount == 0 ? "has no arguments"
                                                    : "has " + std::to_string(d.parameterCount) + (d.parameterCount == 1 ? " argument" : " arguments");
            std::string called = callee ? "is incorrectly called by " + quoted(*callee) + " with " : "is incorrectly called with ";
            std::string position = d.parameterCount == 0 ? "" : " as a " + ordinal(slot.index + 1);
            return quoted(function) + " " + has + " but " + called + quoted(d.sub) + position;
        }
        case Difference::Kind::NothingReturned:
            break;
        }
    }
    else
    {
        std::string should = quoted(function) + " should return ";
        // What it returns instead; a collapsed sentence has it in the signature already.
        std::string instead = collapsed ? "" : ", not " + quoted(d.sub);
        std::string here = returnPlace(d.path, at, d.path.size(), notation);

        switch (d.kind)
        {
        case Difference::Kind::Mismatch:
            return should + quoted(d.super) + here + instead;
        case Difference::Kind::MismatchExactly:
            return should + "exactly " + quoted(d.super) + here + instead;
        case Difference::Kind::CouldBeNil:
            return should + quoted(d.super) + here + ", never 'nil'";
        case Difference::Kind::NotAMap:
            return should + "the map " + quoted(d.super) + here + ", not " + quoted(d.sub) + " (named fields but no indexer)";
        case Difference::Kind::Missing:
            return should + "a value with field " + missingField(d) + returnPlace(d.path, at, d.path.size() - 1, notation);
        case Difference::Kind::NothingReturned:
            return should + quoted(d.super) + here + ", but returns nothing";
        case Difference::Kind::NothingPassed:
        case Difference::Kind::ExtraPassed:
            break;
        }
    }

    LUAU_ASSERT(!"unreachable");
    return noun;
}

// A difference inside a function as one item of a list under it ("with incorrect arguments:" / "and
// it should return:"). The function's signature is printed above the list, so an item only says what's
// passed or what should come back, never what the parameter already declares.
std::string describeAsListItem(const Difference& d, uint8_t* notation)
{
    LUAU_ASSERT(d.anchor && d.anchor->at < d.path.size());
    size_t at = d.anchor->at;
    const Segment& slot = d.path[at];
    std::string noun = slotNoun(d.path, at, d.path.size(), notation);

    if (slot.kind == Segment::Kind::Parameter)
    {
        bool named = !slot.name.empty();
        switch (d.kind)
        {
        case Difference::Kind::Mismatch:
            return passedValue(noun, named, d.sub, false);
        case Difference::Kind::MismatchExactly:
            return passedValue(noun, named, d.sub, false) + ", where exactly " + quoted(d.super) + " is expected";
        case Difference::Kind::CouldBeNil:
            return nonNilVersion(d, noun, named);
        case Difference::Kind::NotAMap:
            return passedValue(noun, named, d.sub, false) + " (named fields but no indexer)";
        case Difference::Kind::Missing:
            return slotInProse(slotNoun(d.path, at, d.path.size() - 1, notation), named) + " missing field " + missingField(d);
        case Difference::Kind::NothingPassed:
            return "nothing for " + slotInProse(noun, named);
        case Difference::Kind::ExtraPassed:
            return "an extra " + quoted(d.sub);
        case Difference::Kind::NothingReturned:
            break;
        }
    }
    else
    {
        std::string here = returnPlace(d.path, at, d.path.size(), notation);
        switch (d.kind)
        {
        case Difference::Kind::Mismatch:
        case Difference::Kind::NothingReturned:
            return quoted(d.super) + here;
        case Difference::Kind::MismatchExactly:
            return "exactly " + quoted(d.super) + here;
        case Difference::Kind::CouldBeNil:
            return quoted(d.super) + here + ", never 'nil'";
        case Difference::Kind::NotAMap:
            return "the map " + quoted(d.super) + here + ", not " + quoted(d.sub) + " (named fields but no indexer)";
        case Difference::Kind::Missing:
            return "a value with field " + missingField(d) + returnPlace(d.path, at, d.path.size() - 1, notation);
        case Difference::Kind::NothingPassed:
        case Difference::Kind::ExtraPassed:
            break;
        }
    }

    LUAU_ASSERT(!"unreachable");
    return noun;
}

std::string nestedItem(const std::string& text)
{
    return "  • " + text;
}

std::string moreText(size_t hidden, std::string_view things)
{
    std::string text = "...and " + std::to_string(hidden) + " more";
    if (!things.empty())
        text += " " + std::string(things);
    return text;
}

// `items` as nested bullets of `line`, past `limit` counted as `...and N more <things>`.
void appendNestedItems(Line& line, const std::vector<std::string>& items, size_t limit, std::string_view things)
{
    size_t shown = std::min(items.size(), limit);
    for (size_t i = 0; i < shown; ++i)
        line.text.push_back(nestedItem(items[i]));
    if (items.size() > shown)
        line.text.push_back(nestedItem(moreText(items.size() - shown, things)));
}

} // namespace

std::string anchorLabel(const Difference& d, const std::string& givenRoot, Rendering* rendering)
{
    LUAU_ASSERT(d.anchor && d.anchor->at <= d.path.size());
    size_t at = d.anchor->at;

    // The nearest enclosing function slot, if this function sits inside another one.
    std::optional<size_t> outer;
    for (size_t i = 0; i < at; ++i)
    {
        Segment::Kind kind = d.path[i].kind;
        if (kind == Segment::Kind::Parameter || kind == Segment::Kind::Return)
            outer = i;
    }

    // Members of an expected union say which alternative was compared, which isn't a place on the
    // given side, so they're left out of a label naming something the reader wrote.
    DiffPath given = onGivenSide(DiffPath{d.path.begin() + (outer ? *outer : 0), d.path.begin() + at});

    if (!rendering)
        return renderPath(outer ? "" : givenRoot, given, 0, given.size());
    if (outer)
        return renderPath("", given, 0, given.size(), &rendering->notation);
    return rendering->givenPath(given);
}

std::string describe(const Difference& d, Rendering& rendering)
{
    std::string path = rendering.givenPath(onGivenSide(d.path));

    // An overload isn't named anywhere else, unlike a union member (whose header says which was
    // compared), so the number stays, in words.
    std::string overload;
    for (const Segment& segment : d.path)
    {
        if (segment.kind == Segment::Kind::Member && segment.overload)
        {
            overload = "overload #" + std::to_string(segment.index) + " ";
            rendering.notation |= MismatchNotationOverload;
        }
    }

    switch (d.kind)
    {
    case Difference::Kind::Mismatch:
        return path + ": expected " + overload + quoted(d.super) + ", got " + quoted(d.sub);
    case Difference::Kind::MismatchExactly:
        return path + ": expected exactly " + quoted(d.super) + ", got " + quoted(d.sub);
    case Difference::Kind::Missing:
        // A similarly named field is a typo, which gets a `Help: did you mean` of its own.
        return "missing field " + quoted(path + ": " + d.super);
    case Difference::Kind::CouldBeNil:
        return path + ": expected " + quoted(d.super) + ", but it could be 'nil'";
    case Difference::Kind::NotAMap:
        return path + ": expected the map " + quoted(d.super) + ", got " + quoted(d.sub) + " (named fields but no indexer)";
    case Difference::Kind::NothingReturned:
    case Difference::Kind::NothingPassed:
    case Difference::Kind::ExtraPassed:
        // Only ever recorded inside a function.
        break;
    }

    LUAU_ASSERT(!"unreachable");
    return path;
}

Line functionBullet(const std::vector<const Difference*>& group, const std::optional<std::string>& caller, Rendering& rendering)
{
    LUAU_ASSERT(!group.empty() && group.front()->anchor);
    const Difference& first = *group.front();
    const Anchor& anchor = *first.anchor;

    // The given function itself is named by its signature, inline. An aliased or nested one is
    // introduced by `name = signature` and referred to by name.
    std::string label = anchorLabel(first, rendering.givenRoot);
    bool inlineSignature = !anchor.name && label == rendering.givenRoot;
    bool labelShown = !anchor.name && !inlineSignature;
    std::string name = anchor.name ? *anchor.name : labelShown ? anchorLabel(first, rendering.givenRoot, &rendering) : label;
    std::string function = inlineSignature ? anchor.display : name;

    Line line;
    if (!inlineSignature)
        line.text.push_back(name + " = " + anchor.display);

    // One thing wrong: one sentence. Several: the function once, then each argument and return value
    // as an item under it, so nothing about the function is said twice. A long signature inline pushes
    // the point of the sentence off the end of the line, so a long sentence gets the list layout too.
    if (group.size() == 1)
    {
        std::string sentence = describeInFunction(*group.front(), function, caller, inlineSignature, &rendering.notation);
        bool fitsOnLine = !inlineSignature || sentence.size() <= kLineWidth;
        if (fitsOnLine)
        {
            line.text.push_back(std::move(sentence));
            return line;
        }
    }

    std::vector<std::string> arguments;
    std::vector<std::string> returns;
    for (const Difference* d : group)
    {
        bool parameter = d->path[d->anchor->at].kind == Segment::Kind::Parameter;
        (parameter ? arguments : returns).push_back(describeAsListItem(*d, &rendering.notation));
    }

    if (!arguments.empty())
    {
        bool one = arguments.size() == 1;
        // Without a caller to name, don't invent one: the problem is what the parameters accept.
        std::string intro = caller ? quoted(*caller) + " can call " + quoted(function) + (one ? " with an incorrect argument:" : " with incorrect arguments:")
                                   : quoted(function) + (one ? " can be passed an argument it doesn't accept:" : " can be passed arguments it doesn't accept:");
        line.text.push_back(std::move(intro));
        appendNestedItems(line, arguments, kMaxFunctionItems, "arguments");
    }
    if (!returns.empty())
    {
        line.text.push_back(arguments.empty() ? quoted(function) + " should return:" : "and it should return:");
        appendNestedItems(line, returns, kMaxFunctionItems, "return values");
    }
    return line;
}

Line missingFieldsBullet(const DiffPath& parent, const std::vector<const Difference*>& fields, Rendering& rendering)
{
    // Each missing field on its own line, with the type it should have, as for a literal.
    Line line{true, {"missing fields in " + quoted(rendering.givenPath(parent)) + ":"}};
    std::vector<std::string> items;
    for (const Difference* d : fields)
        items.push_back(missingField(*d));
    appendNestedItems(line, items, kMaxListedFields, "fields");
    return line;
}

Line andMore(size_t hidden, bool bullet, std::string_view things)
{
    return Line{bullet, {moreText(hidden, things)}};
}

std::string narrowedParameterHelp(const std::vector<const Difference*>& narrowed, Rendering& rendering)
{
    if (narrowed.empty())
        return "";

    std::string title;
    std::string passes;
    std::string must;
    if (narrowed.size() == 1)
    {
        const Difference& d = *narrowed.front();
        LUAU_ASSERT(d.anchor && d.anchor->at < d.path.size());
        std::string name = slotNoun(d.path, d.anchor->at, d.path.size(), &rendering.notation);
        // The optional type itself, not an alias of it: "narrowed from 'Slot'" hides what was dropped.
        std::string from = d.subMeaning ? *d.subMeaning : d.sub;
        title = "Help: " + quoted(name) + " can't be narrowed from " + quoted(from) + " to " + quoted(d.super) + ":";
        passes = "code calling this function as the expected type is allowed to pass 'nil' for " + quoted(name);
        must = "this parameter must accept " + quoted(d.sub) + " to match the caller's expected signature";
    }
    else
    {
        title = "Help: these parameters can't drop the 'nil' the expected type allows:";
        passes = "code calling this function as the expected type is allowed to pass 'nil' for any of them";
        must = "each must accept its optional type to match the caller's expected signature";
    }

    return title + "\n" + nestedItem(passes) + "\n" + nestedItem(must);
}

std::string swappedParametersHelp(const std::string& first, const std::string& second, size_t i, size_t j)
{
    if (!first.empty() && !second.empty())
        return "Help: the parameters " + quoted(first) + " and " + quoted(second) + " look like they're in the wrong order.";
    return "Help: the " + ordinal(i + 1) + " and " + ordinal(j + 1) + " parameters look like they're in the wrong order.";
}

std::string swappedReturnsHelp(size_t i, size_t j)
{
    return "Help: the " + ordinal(i + 1) + " and " + ordinal(j + 1) + " return values look like they're in the wrong order.";
}

std::string misspelledStringHelp(const std::string& suggestion)
{
    return "Help: did you mean " + quoted(suggestion) + "?";
}

std::string misspelledFieldHelp(const Difference& d, Rendering& rendering)
{
    LUAU_ASSERT(d.kind == Difference::Kind::Missing && d.similarName);
    DiffPath place = onGivenSide(d.path);
    LUAU_ASSERT(!place.empty());
    std::string parent = rendering.givenPath(place, 0, place.size() - 1);
    return "Help: did you mean " + quoted(parent + "." + d.path.back().name) + " instead of " + quoted(parent + "." + *d.similarName) + "?";
}

std::string misspelledValueHelp(const Difference& d, Rendering& rendering)
{
    LUAU_ASSERT(d.kind == Difference::Kind::Mismatch && d.similarName);
    return "Help: did you mean " + quoted(*d.similarName) + " instead of " + quoted(d.sub) + " for " + quoted(rendering.givenPath(onGivenSide(d.path))) +
           "?";
}

std::string similarHeader(std::string_view arrival, size_t paths)
{
    return "The " + std::string(arrival) + " type is similar, but has " + countedPaths(paths) + ":";
}

std::string pathsHeader(std::string_view arrival, size_t paths)
{
    return "The " + std::string(arrival) + " type has " + countedPaths(paths) + ":";
}

std::string becauseHeader()
{
    return "This is because:";
}

std::string couldBeNilHeader(const std::string& subject)
{
    return "This is because " + quoted(subject) + " could be 'nil'.";
}

std::string unionMembersHeader(bool noneFit, const std::string& unionName, const std::string& target)
{
    return (noneFit ? "No member of " : "Not every member of ") + unionName + " is compatible with " + target + ":";
}

std::string scalarsHeader(const std::vector<std::string>& names, const std::string& expected, bool article)
{
    LUAU_ASSERT(!names.empty());
    std::string wanted = (article ? "a " : "") + quoted(expected);
    std::vector<std::string> shown;
    for (const std::string& name : names)
        shown.push_back(quoted(name));

    if (shown.size() == 1)
        return shown[0] + " isn't " + wanted + ".";
    if (shown.size() == 2)
        return "Neither " + shown[0] + " nor " + shown[1] + " is " + wanted + ".";
    return "None of " + joinedWithAnd(shown) + " is " + wanted + ".";
}

// `label = type` followed by `rest`, the type on a line of its own when it's long. Punctuation in
// `rest` (", but:") stays attached to the type.
static std::string labelled(const std::string& label, const std::string& type, const std::string& rest)
{
    bool punctuation = !rest.empty() && (rest[0] == ',' || rest[0] == ':');
    std::string oneLine = label + " = " + type + (punctuation ? "" : " ") + rest;
    if (oneLine.size() <= kLineWidth)
        return oneLine;
    if (punctuation)
        return label + " =\n    " + type + rest;
    return label + " =\n    " + type + "\n" + rest;
}

std::string closestMatchHeader(const std::string& label, const std::string& memberType)
{
    return labelled("The closest match is " + label, memberType, ", but:");
}

std::string taggedMatchHeader(
    const std::string& field,
    const std::string& value,
    const std::string& label,
    const std::string& memberType,
    Rendering& rendering
)
{
    std::string root = rendering.givenName();
    return labelled(quoted(root + "." + field) + " is " + quoted(value) + ", so " + quoted(root) + " was compared with " + label, memberType, ":");
}

std::string unrelatedSentence(const std::optional<std::string>& givenAlias, const std::string& printedGiven, const std::string& printedExpected)
{
    std::string given = givenAlias ? quoted(*givenAlias) : printedGiven.size() <= kShortTypeWidth ? quoted(printedGiven) : "The given type";
    std::string expected = printedExpected.size() <= kShortTypeWidth ? quoted(printedExpected) : "the expected type";
    return given + " is unrelated to " + expected + ".";
}

std::string memberWithType(const std::string& label, const std::string& type)
{
    return label + " = " + type;
}

Line memberDetailLine(const std::string& label, const std::string& type, bool near, size_t paths)
{
    std::string tail = near ? "is almost what we expected, but:" : "has " + countedPaths(paths) + ":";
    return Line{false, {labelled(label, type, tail)}};
}

Line memberAlsoHasLine(const std::string& label, const std::string& type, size_t paths)
{
    return Line{false, {memberWithType(label, type) + " also has " + countedPaths(paths)}};
}

Line memberNothingInCommonLine(const std::string& label, const std::string& type, Rendering& rendering)
{
    return Line{false, {memberWithType(label, type) + " has no fields in common with " + rendering.expectedName()}};
}

Line otherKindsLine(const std::vector<std::string>& members, const std::optional<std::string>& expectedKinds)
{
    LUAU_ASSERT(!members.empty());
    std::vector<std::string> named{members.begin(), members.begin() + std::min(members.size(), kMaxNamedKinds)};
    if (members.size() > named.size())
        named.push_back(std::to_string(members.size() - named.size()) + " others");
    std::string subject = joinedWithAnd(named);
    if (members.size() == 1)
        return Line{false, {subject + (expectedKinds ? " isn't one of the " + *expectedKinds + " expected" : " isn't the kind of value expected")}};
    return Line{false, {subject + (expectedKinds ? " aren't " + *expectedKinds : " aren't the kind of value expected")}};
}

Line canAlsoBeNilLine(Rendering& rendering)
{
    return Line{false, {quoted(rendering.givenName()) + " can also be 'nil'"}};
}

Line couldBeNilBullet(Rendering& rendering)
{
    return Line{true, {quoted(rendering.givenName()) + " could be 'nil'"}};
}

std::string missingEveryMembersFieldsHeader(const std::string& subject, const std::string& target)
{
    return subject + " is missing the fields of every member of " + target + ":";
}

std::string missingCommonFieldsHeader(const std::string& subject, size_t count, const std::string& target)
{
    std::string fields = count == 1 ? std::string("a field") : std::to_string(count) + " fields";
    return subject + " is missing " + fields + " every member of " + target + " has:";
}

Line fieldBullet(const std::string& name, const std::string& type)
{
    return Line{true, {quoted(name + ": " + type)}};
}

Line someMembersNeedMoreLine()
{
    return Line{false, {"Some members need more:"}};
}

Line memberNeedsBullet(const std::string& who, bool beyondCommon, const std::vector<std::string>& fields)
{
    return Line{true, {who + (beyondCommon ? " also needs " : " needs ") + joinedWithAnd(fields)}};
}

std::string tagLabel(const std::string& field, const std::string& value)
{
    return quoted(field + " = " + value);
}

std::string paragraph(const std::string& text)
{
    return "\n\n" + text;
}

std::string unionSummary(size_t members, bool optional, const std::vector<std::pair<size_t, std::string>>& listed)
{
    std::string text = std::string(optional ? "an optional union of " : "a union of ") + std::to_string(members) + " members, including:";
    for (const auto& [number, type] : listed)
        text += "\n#" + std::to_string(number) + " = " + type;
    if (members > listed.size())
        text += "\n...and " + std::to_string(members - listed.size()) + " others";
    return text;
}

std::string composeReason(
    const std::vector<std::string>& helps,
    const std::string& header,
    const std::vector<Line>& lines,
    const std::string& why,
    Rendering& rendering
)
{
    std::string reason;
    // A help that says what to do comes before the detail that backs it up. Several members making the
    // same mistake give the same help.
    std::vector<std::string> shownHelps;
    for (const std::string& help : helps)
    {
        bool repeated = std::find(shownHelps.begin(), shownHelps.end(), help) != shownHelps.end();
        if (!repeated && shownHelps.size() < kMaxHelps)
            shownHelps.push_back(help);
    }
    for (size_t i = 0; i < shownHelps.size(); ++i)
        reason += (i == 0 ? "\n\n" : "\n") + shownHelps[i];

    reason += paragraph(header);

    // One that explains *why* goes under the bullets it's about.
    std::string tail = why.empty() ? std::string() : paragraph(why);

    auto lineCount = [](const std::string& text)
    {
        return size_t(std::count(text.begin(), text.end(), '\n'));
    };

    // Room for the lines: what the cap leaves after the rest, and a line to count what didn't fit.
    constexpr size_t kSummaryWidth = 40;
    size_t linesLeft = kMaxReasonLines - std::min(kMaxReasonLines, lineCount(reason) + lineCount(tail) + 1);
    size_t charsLeft = kMaxReasonChars - std::min(kMaxReasonChars, reason.size() + tail.size() + kSummaryWidth);

    size_t hiddenLines = 0;
    for (const Line& line : lines)
    {
        std::string rendered;
        for (size_t i = 0; i < line.text.size(); ++i)
        {
            if (!line.bullet)
                rendered += "\n" + line.text[i];
            else if (i == 0)
                rendered += "\n" + nestedItem(line.text[i]);
            else
                rendered += "\n    " + line.text[i];
        }

        size_t count = lineCount(rendered);
        bool fits = hiddenLines == 0 && count <= linesLeft && rendered.size() <= charsLeft;
        if (!fits)
        {
            hiddenLines += count;
            for (size_t number : line.givenMembers)
                rendering.givenMembersShown.erase(number);
            for (size_t number : line.expectedMembers)
                rendering.expectedMembersShown.erase(number);
            continue;
        }
        reason += rendered;
        linesLeft -= count;
        charsLeft -= rendered.size();
    }

    if (hiddenLines > 0)
        reason += "\n" + moreText(hiddenLines, "lines");
    return reason + tail;
}

} // namespace MismatchWording
} // namespace Luau

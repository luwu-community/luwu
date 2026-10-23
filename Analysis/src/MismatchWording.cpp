// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/MismatchWording.h"

#include "Luau/Common.h"
#include "Luau/Error.h"

#include <algorithm>
#include <cctype>

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

// A parameter that doesn't accept what the expected type passes it, said the way a table's bullets say it:
// `payload[string]: expected 'string | number', got 'string'`. Expected is what the expected function type passes,
// got is what the given function declares, so the bullet reads in the same direction as the two types above it. A
// parameter declared as the non-nil version of an optional is said with the optional itself (`slot: expected 'Stack?',
// got 'Stack'`), not an alias that hides the `nil`; why that's wrong goes in a help under the bullets
// (`narrowedParameterHelp`), since it runs against the intuition that a `Stack` is fine for a `Stack?`.
std::string parameterMismatch(const Difference& d, const std::string& noun)
{
    std::string exactly = d.kind == Difference::Kind::MismatchExactly ? "exactly " : "";
    std::string expected = d.subMeaning ? *d.subMeaning : d.subInFull.value_or(d.sub);
    return noun + ": expected " + exactly + quoted(expected) + ", got " + quoted(d.super);
}

// The element type of the array an `ElementOfExpected` difference expected.
std::string elementOf(const Difference& d)
{
    LUAU_ASSERT(d.kind == Difference::Kind::ElementOfExpected && d.element);
    return d.element.value_or("");
}

// What the expected array holds, after `an array of`: `them` when it's exactly the given type, so the
// type is named once and all that's left to see is the braces.
std::string expectedElements(const Difference& d)
{
    std::string element = elementOf(d);
    return element == d.sub ? "them" : quoted(element);
}

// What a given value is, after the value has been shown: `an array where a single 'T' is expected`.
std::string arrayNestingArgument(const Difference& d)
{
    if (d.kind == Difference::Kind::ArrayOfExpected)
        return "an array where a single " + quoted(d.super) + " is expected";
    return "a single value where an array of " + expectedElements(d) + " is expected";
}

// What a function should return: `a single 'T', not an array of them`.
std::string arrayNestingReturn(const Difference& d, const std::string& here)
{
    if (d.kind == Difference::Kind::ArrayOfExpected)
        return "a single " + quoted(d.super) + here + ", not an array of them";
    std::string element = elementOf(d);
    return "an array of " + quoted(element) + here + ", not a single " + (element == d.sub ? "one" : quoted(d.sub));
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
        switch (d.kind)
        {
        case Difference::Kind::Mismatch:
        case Difference::Kind::MismatchExactly:
        case Difference::Kind::CouldBeNil:
            return "In " + quoted(function) + ", " + parameterMismatch(d, noun);
        case Difference::Kind::NotAMap:
            return calls + " with " + passedValue(noun, named, d.sub, collapsed) + ", a record where map " + quoted(d.super) + " is expected";
        case Difference::Kind::ArrayOfExpected:
        case Difference::Kind::ElementOfExpected:
            return calls + " with " + passedValue(noun, named, d.sub, collapsed) + ", " + arrayNestingArgument(d);
        case Difference::Kind::Missing:
        {
            std::string parent = slotNoun(d.path, at, d.path.size() - 1, notation);
            return calls + " with " + slotInProse(parent, named) + " missing property " + missingField(d);
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
            return should + "map " + quoted(d.super) + here + ", not record " + quoted(d.sub);
        case Difference::Kind::ArrayOfExpected:
        case Difference::Kind::ElementOfExpected:
            return should + arrayNestingReturn(d, here);
        case Difference::Kind::Missing:
            return should + "a value with property " + missingField(d) + returnPlace(d.path, at, d.path.size() - 1, notation);
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
// it should return:"): what's passed and what the parameter takes, or what should come back. `sayReturned` adds
// what a mismatched return value is instead, for a list with no signature above it to show that.
std::string describeAsListItem(const Difference& d, bool sayReturned, uint8_t* notation)
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
        case Difference::Kind::MismatchExactly:
        case Difference::Kind::CouldBeNil:
            return parameterMismatch(d, noun);
        case Difference::Kind::NotAMap:
            return noun + ": expected record " + quoted(d.sub) + ", got map " + quoted(d.super);
        case Difference::Kind::ArrayOfExpected:
            return noun + ": expected an array of " + quoted(d.super) + ", got a single one";
        case Difference::Kind::ElementOfExpected:
            return noun + ": expected a single " + quoted(d.sub) + ", got an array of " + expectedElements(d);
        case Difference::Kind::Missing:
            return "missing property " + quoted(noun + ": " + d.super);
        case Difference::Kind::NothingPassed:
            return (named ? quoted(noun + ": " + d.super) : noun) + " is never passed";
        case Difference::Kind::ExtraPassed:
            return "an extra " + quoted(d.sub);
        case Difference::Kind::NothingReturned:
            break;
        }
    }
    else
    {
        std::string here = returnPlace(d.path, at, d.path.size(), notation);
        std::string instead = sayReturned ? ", not " + quoted(d.sub) : "";
        switch (d.kind)
        {
        case Difference::Kind::Mismatch:
            return quoted(d.super) + here + instead;
        case Difference::Kind::NothingReturned:
            return quoted(d.super) + here;
        case Difference::Kind::MismatchExactly:
            return "exactly " + quoted(d.super) + here + instead;
        case Difference::Kind::CouldBeNil:
            return quoted(d.super) + here + ", never 'nil'";
        case Difference::Kind::NotAMap:
            return "map " + quoted(d.super) + here + ", not record " + quoted(d.sub);
        case Difference::Kind::ArrayOfExpected:
        case Difference::Kind::ElementOfExpected:
            return arrayNestingReturn(d, here);
        case Difference::Kind::Missing:
            return "a value with property " + missingField(d) + returnPlace(d.path, at, d.path.size() - 1, notation);
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

// `items` as bullets of their own, then `nested`, past `limit` counted as `...and N more <things>`.
void appendBullets(std::vector<Line>& lines, const std::vector<std::string>& items, const std::vector<Line>& nested, size_t limit, std::string_view things)
{
    size_t total = items.size() + nested.size();
    size_t shown = std::min(total, limit);
    for (size_t i = 0; i < shown; ++i)
        lines.push_back(i < items.size() ? Line{true, {items[i]}} : nested[i - items.size()]);
    if (total > shown)
        lines.push_back(Line{true, {moreText(total - shown, things)}});
}

// A function's differences as list items: what its arguments should be, and what it should return.
void splitFunctionItems(
    const std::vector<const Difference*>& group,
    bool sayReturned,
    uint8_t* notation,
    std::vector<std::string>& arguments,
    std::vector<std::string>& returns
)
{
    for (const Difference* d : group)
    {
        bool parameter = d->path[d->anchor->at].kind == Segment::Kind::Parameter;
        (parameter ? arguments : returns).push_back(describeAsListItem(*d, sayReturned, notation));
    }
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
        return "missing property " + quoted(path + ": " + d.super);
    case Difference::Kind::CouldBeNil:
        return path + ": expected " + quoted(d.super) + ", but it could be 'nil'";
    case Difference::Kind::NotAMap:
        return path + ": expected map " + quoted(d.super) + ", got record " + quoted(d.sub);
    case Difference::Kind::ArrayOfExpected:
        return path + " is " + arrayNestingArgument(d);
    case Difference::Kind::ElementOfExpected:
        return path + " is a single " + quoted(d.sub) + " where an array of " + expectedElements(d) + " is expected";
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
    splitFunctionItems(group, false, &rendering.notation, arguments, returns);

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

// Parameters the given function requires that the expected function type never passes, when nothing else is wrong:
// `'Button.onClick' should only have 2 parameters, but was passed a function with an extra parameter 'modifiers: {string}'`.
std::string extraParametersSentence(const std::vector<const Difference*>& extras, Rendering& rendering)
{
    LUAU_ASSERT(!extras.empty());
    std::vector<std::string> parameters;
    for (const Difference* d : extras)
    {
        const Segment& slot = d->path[d->anchor->at];
        std::string name = slot.name.empty() ? "" : slot.name + ": ";
        parameters.push_back(quoted(name + d->super));
    }

    size_t count = extras.front()->parameterCount;
    std::string should = count == 0 ? "should have no parameters" : "should only have " + std::to_string(count) + (count == 1 ? " parameter" : " parameters");
    std::string extra = parameters.size() == 1 ? "an extra parameter " : "extra parameters ";
    if (rendering.givenIsPlaceholder)
        return "This function " + should + ", but has " + extra + joinedWithAnd(parameters) + ".";
    return quoted(rendering.givenRoot) + " " + should + ", but was passed a function with " + extra + joinedWithAnd(parameters) + ".";
}

FunctionReason givenFunctionReason(
    const std::vector<const Difference*>& group,
    const std::vector<Line>& nestedArguments,
    const std::vector<Line>& nestedReturns,
    Rendering& rendering
)
{
    bool onlyExtraParameters = !group.empty() && nestedArguments.empty() && nestedReturns.empty() &&
                               std::all_of(
                                   group.begin(),
                                   group.end(),
                                   [](const Difference* d)
                                   {
                                       return d->kind == Difference::Kind::NothingPassed;
                                   }
                               );
    if (onlyExtraParameters)
        return FunctionReason{extraParametersSentence(group, rendering), {}, true};

    std::vector<std::string> arguments;
    std::vector<std::string> returns;
    splitFunctionItems(group, true, &rendering.notation, arguments, returns);
    bool anyArguments = !arguments.empty() || !nestedArguments.empty();
    bool anyReturns = !returns.empty() || !nestedReturns.empty();

    FunctionReason reason;
    if (anyArguments)
    {
        bool one = arguments.size() + nestedArguments.size() == 1;
        reason.header = one ? "This function can be passed a value it doesn't accept:" : "This function can be passed values it doesn't accept:";
        appendBullets(reason.lines, arguments, nestedArguments, kMaxFunctionItems, "arguments");
    }
    if (anyReturns)
    {
        if (!anyArguments)
            reason.header = "This function should return:";
        else
            reason.lines.push_back(Line{false, {"It should return:"}});
        appendBullets(reason.lines, returns, nestedReturns, kMaxFunctionItems, "return values");
    }
    return reason;
}

Line missingFieldsBullet(const DiffPath& parent, const std::vector<const Difference*>& fields, Rendering& rendering)
{
    // Each missing field on its own line, with the type it should have, as for a literal.
    Line line{true, {"missing properties in " + quoted(rendering.givenPath(parent)) + ":"}};
    std::vector<std::string> items;
    for (const Difference* d : fields)
        items.push_back(missingField(*d));
    appendNestedItems(line, items, kMaxListedFields, "properties");
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

    if (narrowed.size() == 1)
    {
        const Difference& d = *narrowed.front();
        LUAU_ASSERT(d.anchor && d.anchor->at < d.path.size());
        std::string name = slotNoun(d.path, d.anchor->at, d.path.size(), &rendering.notation);
        // The optional type itself, not an alias of it: "narrowed from 'Slot'" hides what was dropped.
        std::string from = d.subMeaning ? *d.subMeaning : d.sub;
        return rendering.helpifier(
            "narrowed parameter",
            {quoted(name) + " can't be narrowed from " + quoted(from) + " to " + quoted(d.super),
             "Code calling this function as the expected type is allowed to pass 'nil' for " + quoted(name),
             "This parameter must accept " + quoted(from) + " to match the caller's expected signature"}
        );
    }

    return rendering.helpifier(
        "narrowed parameters",
        {"These parameters can't drop the 'nil' the expected type allows",
         "Code calling this function as the expected type is allowed to pass 'nil' for any of them",
         "Each must accept its optional type to match the caller's expected signature"}
    );
}

std::string swappedParametersHelp(const std::string& first, const std::string& second, size_t i, size_t j, const Helpifier& helpifier)
{
    if (!first.empty() && !second.empty())
        return helpifier("parameter order", {"The parameters " + quoted(first) + " and " + quoted(second) + " look like they're in the wrong order"});
    return helpifier("parameter order", {"The " + ordinal(i + 1) + " and " + ordinal(j + 1) + " parameters look like they're in the wrong order"});
}

std::string swappedReturnsHelp(size_t i, size_t j, const Helpifier& helpifier)
{
    return helpifier("return order", {"The " + ordinal(i + 1) + " and " + ordinal(j + 1) + " return values look like they're in the wrong order"});
}

std::string misspelledStringHelp(const std::string& suggestion, const Helpifier& helpifier)
{
    return helpifier("typo", {"Did you mean " + quoted(suggestion) + "?"});
}

std::pair<std::string, std::string> misspelledFieldNames(const Difference& d, Rendering& rendering)
{
    LUAU_ASSERT(d.kind == Difference::Kind::Missing && d.similarName);
    DiffPath place = onGivenSide(d.path);
    LUAU_ASSERT(!place.empty());
    std::string parent = rendering.givenPath(place, 0, place.size() - 1);
    return {parent + "." + d.similarName.value_or(""), parent + "." + d.path.back().name};
}

std::string misspelledFieldHelp(const Difference& d, Rendering& rendering)
{
    auto [written, meant] = misspelledFieldNames(d, rendering);
    return rendering.helpifier("typo", {"Did you mean " + quoted(meant) + " instead of " + quoted(written) + "?"});
}

std::string misspelledValueHelp(const Difference& d, Rendering& rendering)
{
    LUAU_ASSERT(d.kind == Difference::Kind::Mismatch && d.similarName);
    std::string path = rendering.givenPath(onGivenSide(d.path));
    return rendering.helpifier("typo", {"Did you mean " + quoted(*d.similarName) + " instead of " + quoted(d.sub) + " for " + quoted(path) + "?"});
}

std::string expectedPropertyNote(const std::string& name, const std::string& expected)
{
    return "Expected " + quoted(name + ": " + expected) + " because of this";
}

std::string givenPropertyNote(const std::string& name, const std::string& given, const std::string& expected)
{
    return quoted(name) + " is " + quoted(given) + " here, but should be " + quoted(expected);
}

std::string missingPropertyNote(const Difference& d, Rendering& rendering)
{
    LUAU_ASSERT(d.kind == Difference::Kind::Missing && !d.path.empty());

    // The note's path isn't in the message, so it adds nothing to the message's legend
    uint8_t notation = rendering.notation;
    std::string table;
    bool inFunction = d.anchor && d.anchor->at + 1 < d.path.size();
    if (inFunction)
    {
        const Segment& slot = d.path[d.anchor->at];
        bool named = slot.kind == Segment::Kind::Parameter && !slot.name.empty();
        table = slotInProse(slotNoun(d.path, d.anchor->at, d.path.size() - 1, nullptr), named);
        // `its 2nd argument` starts the sentence
        table[0] = char(std::toupper(static_cast<unsigned char>(table[0])));
    }
    else
        table = quoted(rendering.givenPath(onGivenSide(DiffPath{d.path.begin(), d.path.end() - 1})));
    rendering.notation = notation;

    return table + " is missing " + quoted(d.path.back().name + ": " + d.super);
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

std::string propertiesHeader()
{
    return "Some properties are incompatible:";
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

std::string closestMatchHeader(const std::string& label, const std::optional<std::string>& memberType)
{
    std::string subject = "The closest match is " + label;
    return memberType ? labelled(subject, *memberType, ", but:") : subject + ", but:";
}

std::string taggedMatchHeader(
    const std::string& field,
    const std::string& value,
    const std::string& label,
    const std::optional<std::string>& memberType,
    Rendering& rendering
)
{
    std::string root = rendering.givenName();
    std::string subject = quoted(root + "." + field) + " is " + quoted(value) + ", so " + quoted(root) + " was compared with " + label;
    return memberType ? labelled(subject, *memberType, ":") : subject + ":";
}

std::string unrelatedSentence(const std::optional<std::string>& givenAlias, const std::string& printedGiven, const std::string& printedExpected)
{
    std::string given = givenAlias ? quoted(*givenAlias) : printedGiven.size() <= kShortTypeWidth ? quoted(printedGiven) : "The given type";
    std::string expected = printedExpected.size() <= kShortTypeWidth ? quoted(printedExpected) : "the expected type";
    return given + " is unrelated to " + expected + ".";
}

std::string arrayAndMapSentence(const std::optional<std::string>& variable, bool givenIsArray, const std::string& map, const std::string& element)
{
    std::string mapOf = "a map of " + quoted(map);
    std::string arrayOf = "an array of " + quoted(element);
    std::string expectedAndGot = givenIsArray ? mapOf + ", got " + arrayOf : arrayOf + ", got " + mapOf;
    if (variable)
        return *variable + ": expected " + expectedAndGot;
    return "Expected " + expectedAndGot;
}

std::string arrayNestingSentence(const std::string& subject, bool givenIsArray, const std::optional<std::string>& variable)
{
    if (givenIsArray)
        return subject + " is an array where a single value is expected.";
    std::string sentence = subject + " is a single value where an array is expected";
    if (variable)
        sentence += ": pass " + quoted("{ " + *variable + " }") + " instead";
    return sentence + ".";
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
    return Line{false, {memberWithType(label, type) + " has no properties in common with " + rendering.expectedName()}};
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
    return subject + " is missing the properties of every member of " + target + ":";
}

std::string missingCommonFieldsHeader(const std::string& subject, size_t count, const std::string& target)
{
    std::string fields = count == 1 ? std::string("a property") : std::to_string(count) + " properties";
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
    return text.empty() ? text : "\n\n" + text;
}

std::string hiddenMembers(size_t count)
{
    return count == 1 ? "…" : "… (" + std::to_string(count) + " more)";
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
    // same mistake give the same help. A help that is off is empty.
    std::vector<std::string> shownHelps;
    for (const std::string& help : helps)
    {
        bool repeated = std::find(shownHelps.begin(), shownHelps.end(), help) != shownHelps.end();
        if (!help.empty() && !repeated && shownHelps.size() < kMaxHelps)
            shownHelps.push_back(help);
    }
    for (const std::string& help : shownHelps)
        reason += paragraph(help);

    if (!header.empty())
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

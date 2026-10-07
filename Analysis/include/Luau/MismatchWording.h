// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Luau
{

// Luwu (helpful subtyping errors): how `explainMismatch`'s messages read. `MismatchExplanation.cpp`
// decides what to report about a failed `given <: expected` test and builds the `Difference`s below;
// everything that turns them into sentences, bullets and headers lives here.
namespace MismatchWording
{

// Past this many characters, a `label = type` line puts the type on a line of its own, and a sentence
// about a function with its signature inline uses the list layout instead.
constexpr size_t kLineWidth = 100;
// A printed type at most this long is named in prose; a longer one is "the given type".
constexpr size_t kShortTypeWidth = 40;
// From this many fields missing from one table, they are listed under one bullet.
constexpr size_t kGroupedMissingFields = 3;

// Nobody should have to scroll through an explanation: each list is cut off with an "...and N more"
// line, and the whole reason is held to a size that fits in an editor hover.
// Lines and characters of the whole reason, helps and header included.
constexpr size_t kMaxReasonLines = 15;
constexpr size_t kMaxReasonChars = 1500;
// Bullets under one header.
constexpr size_t kMaxBullets = 8;
// Arguments, or return values, listed under one function.
constexpr size_t kMaxFunctionItems = 6;
// Fields listed under one "missing fields in" bullet.
constexpr size_t kMaxListedFields = 8;
// Members of another kind than expected, named in one sentence.
constexpr size_t kMaxNamedKinds = 5;
// Helps above the header.
constexpr size_t kMaxHelps = 3;

// One step of a path from the root of a type down to a place where two types disagree.
struct Segment
{
    enum class Kind
    {
        // `.name`
        Property,
        // `[string]`, or `[i]` for an array element
        Indexer,
        // `#2`: one member of an expected union, or one of several overloads
        Member,
        // one of a function's parameters
        Parameter,
        // one of a function's return values
        Return,
    };

    Kind kind;
    // The property's name, the indexer's key as shown, or the parameter's name (empty when lost,
    // which parameter names often are).
    std::string name;
    // The member's number, or the parameter's or return value's position from 0.
    size_t index = 0;
    // For a return value: whether it's the function's only one, so it can be "its return value".
    bool onlyOne = false;
    // For a member: whether it's one overload of a function rather than a member of a union.
    bool overload = false;
};

using DiffPath = std::vector<Segment>;

DiffPath with(const DiffPath& path, Segment segment);

// `path` as a place in the given value: without the `#n` of an expected union, which says which
// alternative was compared (the header says that) and isn't somewhere in what the reader wrote.
DiffPath onGivenSide(const DiffPath& path);

// The function a difference sits inside: the innermost one whose parameter or return value the path
// goes through. A bullet about a function slot prints this function's type first, so the sentence
// after it has something to be about.
struct Anchor
{
    std::string display;
    // The alias the function's type was declared as (`type Listener = ...`), when it has one.
    std::optional<std::string> name;
    // Where in the path its Parameter or Return segment is.
    size_t at = 0;
};

struct Difference
{
    enum class Kind
    {
        // `sub` is not a `super`.
        Mismatch,
        // Same, but the property is read-write, so it has to be exactly `super`.
        MismatchExactly,
        // `super` requires a property `sub` doesn't have.
        Missing,
        // `sub` is `super` apart from its `nil`.
        CouldBeNil,
        // A parameter that will be passed no value at all, and doesn't take `nil`.
        NothingPassed,
        // A return value that is never returned, where one is expected.
        NothingReturned,
        // An argument passed to a function that has no parameter to take it.
        ExtraPassed,
        // A table with no indexer (named fields, or none at all), where an array or a map is expected: Luau
        // doesn't treat one as the other even when every field would fit.
        NotAMap,
    };

    Kind kind;
    DiffPath path;
    // The type that has to fit, and the type it has to fit into, as printed.
    std::string sub;
    std::string super;
    // For `Missing`, a property the given table has under a similar name; for `Mismatch`, a string
    // the given one was probably a misspelling of.
    std::optional<std::string> similarName;
    // Involves a type that suppresses errors (an unresolved `*error-type*`): the error it stands for
    // has already been reported, so this adds nothing but noise.
    bool suppressing = false;
    std::optional<Anchor> anchor;
    // For `ExtraPassed`, how many parameters the function does have.
    size_t parameterCount = 0;
    // For `CouldBeNil`, what an aliased `sub` stands for (`Slot` is `Stack?`): an alias hides the `nil`
    // the sentence is about.
    std::optional<std::string> subMeaning;
    // For `NotAMap`: the expected table is an array (`{ T }`) rather than a map, and the given table has no fields
    // at all (`table.freeze({})` is typed `{}`, an empty table that can never hold anything).
    bool expectsArray = false;
    bool givenEmpty = false;
};

// The names paths hang off, and what the text rendered so far needs a legend for. Every piece of text
// that names a root goes through here, so the legend matches what was printed.
struct Rendering
{
    // Where given-side paths start: the variable the value was read from, else its type's alias, else
    // the placeholder `given`.
    std::string givenRoot = "given";
    // The expected type's alias, else the placeholder `expected`.
    std::string expectedRoot = "expected";
    // A placeholder root isn't a name from the reader's code, so the legend says what it stands for.
    bool givenIsPlaceholder = true;
    bool expectedIsPlaceholder = true;

    // `MismatchNotation` bits for everything rendered so far.
    uint8_t notation = 0;
    // The `n` of every `root#n` label rendered: an aliased union's members are spelled out only when
    // something counts them, and a long union only those.
    std::set<size_t> givenMembersShown;
    std::set<size_t> expectedMembersShown;

    // The given root as a name in prose.
    std::string givenName();
    // `path[from, to)` rooted at the given root.
    std::string givenPath(const DiffPath& path, size_t from, size_t to);
    std::string givenPath(const DiffPath& path);
    // `Drop#2`: the 2nd member of the given or expected union.
    std::string givenMember(size_t number);
    std::string expectedMember(size_t number);
    // The expected root quoted, even when it's the placeholder.
    std::string expectedName();
    // The expected root quoted, or "the expected type" for the placeholder.
    std::string expectedTypeName();
};

// A line of the explanation: a bullet (first line after the `•`, the rest indented under it), or a
// line of its own that introduces the bullets after it.
struct Line
{
    bool bullet = true;
    std::vector<std::string> text;
    // The `n` of each `root#n` label in `text`, so a line cut off by the size cap stops counting as
    // showing them.
    std::vector<size_t> givenMembers;
    std::vector<size_t> expectedMembers;
};

std::string quoted(std::string_view s);
std::string ordinal(size_t n);
// `a`, `a and b`, `a, b and c`.
std::string joinedWithAnd(const std::vector<std::string>& items);
std::string joined(const std::vector<std::string>& items, std::string_view separator);
// `an incompatible path`, `3 incompatible paths`.
std::string countedPaths(size_t count);

// `path[from, to)` in path notation, appended to `root`. A parameter or return value only ever starts
// a stretch (it's where an anchor's sentence begins), so it's spelled in words there. Adds the
// notation the path uses to `notation` when given one.
std::string renderPath(const std::string& root, const DiffPath& path, size_t from, size_t to, uint8_t* notation = nullptr);

// Where the function a difference is inside lives, on the given side: `given` for the root, `onClick`
// for one in a table, `nextFn` for one passed as another function's parameter. Records its notation
// in `rendering` when given one.
std::string anchorLabel(const Difference& d, const std::string& givenRoot, Rendering* rendering = nullptr);

// A difference outside any function, as one line with its path rooted at the given value: that's
// what's wrong and what the reader edits (`sprite.tint.b`, not `Color.b`).
std::string describe(const Difference& d, Rendering& rendering);

// Every difference inside one function, as one bullet: one sentence for one problem, else the
// function once with each argument and return value listed under it. `caller` is who calls the
// function, when that's known.
Line functionBullet(const std::vector<const Difference*>& group, const std::optional<std::string>& caller, Rendering& rendering);

// Fields missing from one table, one per line: `missing fields in 'a.b':` + `• 'name: type'`.
Line missingFieldsBullet(const DiffPath& parent, const std::vector<const Difference*>& fields, Rendering& rendering);

// `...and 3 more`, or `...and 3 more members` naming what they are, for lines past a limit: a bullet
// under bullets, a line of its own otherwise.
Line andMore(size_t hidden, bool bullet, std::string_view things = "");

// Why a parameter can't drop the `nil` the expected type passes it: the one case where a narrower
// type is wrong, which is the opposite of what a reader has learned from values. `narrowed` holds
// `CouldBeNil` differences on parameters.
std::string narrowedParameterHelp(const std::vector<const Difference*>& narrowed, Rendering& rendering);

// Helps: what to do, printed right after the two types.
std::string swappedParametersHelp(const std::string& first, const std::string& second, size_t i, size_t j);
std::string swappedReturnsHelp(size_t i, size_t j);
std::string misspelledStringHelp(const std::string& suggestion);
// A missing field with a similar name present, at the path of the table holding it.
std::string misspelledFieldHelp(const Difference& d, Rendering& rendering);
// A string one typo from an expected one, inside a table.
std::string misspelledValueHelp(const Difference& d, Rendering& rendering);

// Headers: the line introducing the bullets.
std::string similarHeader(std::string_view arrival, size_t paths);
std::string pathsHeader(std::string_view arrival, size_t paths);
std::string becauseHeader();
std::string couldBeNilHeader(const std::string& subject);
// The whole given value is a table with no indexer where an array or a map is expected (`NotAMap` at the root).
std::string noIndexerSentence(const std::string& subject, const Difference& d);
// For a table whose type says nothing about what it holds, `{}` or `{unknown}` (from `table.create(n)`): the type has
// to be written down. `Help (...)` blocks, which go under the explanation.
std::string emptyTableHelp(const std::string& variable, const std::string& type);
std::string unknownElementsHelp(const std::string& variable, const std::string& type);
std::string unionMembersHeader(bool noneFit, const std::string& unionName, const std::string& target);
// Scalars that aren't the scalar expected: `'string' isn't a 'number'.`, `Neither ... nor ...`,
// `None of ...`. `article` says whether the expected type takes an `a`.
std::string scalarsHeader(const std::vector<std::string>& names, const std::string& expected, bool article);
std::string closestMatchHeader(const std::string& label, const std::string& memberType);
// `'leaving.kind' is '"leave"', so 'leaving' was compared with Event#2 = ...:`.
std::string taggedMatchHeader(
    const std::string& field,
    const std::string& value,
    const std::string& label,
    const std::string& memberType,
    Rendering& rendering
);
// `'Json' is unrelated to 'ServerConfig'.`, naming each type by its alias, or by its printed form
// when that's short.
std::string unrelatedSentence(const std::optional<std::string>& givenAlias, const std::string& printedGiven, const std::string& printedExpected);

// Lines about one member of a given union, labelled `Drop#2 = type`.
Line memberDetailLine(const std::string& label, const std::string& type, bool near, size_t paths);
Line memberAlsoHasLine(const std::string& label, const std::string& type, size_t paths);
Line memberNothingInCommonLine(const std::string& label, const std::string& type, Rendering& rendering);
// Members of another kind than the expected one, `label = type` each, past a few counted as `N
// others`. `expectedKinds` is the expected kind as a plural noun ("tables") when it has one.
Line otherKindsLine(const std::vector<std::string>& members, const std::optional<std::string>& expectedKinds);
std::string memberWithType(const std::string& label, const std::string& type);
Line canAlsoBeNilLine(Rendering& rendering);
Line couldBeNilBullet(Rendering& rendering);

// An empty table where a union of tables is expected.
std::string missingEveryMembersFieldsHeader(const std::string& subject, const std::string& target);
std::string missingCommonFieldsHeader(const std::string& subject, size_t count, const std::string& target);
Line fieldBullet(const std::string& name, const std::string& type);
Line someMembersNeedMoreLine();
// `'kind = "chat"' also needs 'a: number' and 'b: string'`.
Line memberNeedsBullet(const std::string& who, bool beyondCommon, const std::vector<std::string>& fields);
std::string tagLabel(const std::string& field, const std::string& value);

// An aliased union too long to spell out, as the lines after `type X = `: how many members it has,
// then only those `listed`, by number, then how many others there are.
std::string unionSummary(size_t members, bool optional, const std::vector<std::pair<size_t, std::string>>& listed);

// A block of text separated from what comes before it by a blank line.
std::string paragraph(const std::string& text);
// The whole explanation: helps, the header and its lines, then a help explaining why. Held to
// `kMaxReasonLines` and `kMaxReasonChars`: lines past them are counted instead, and taken out of
// `rendering`'s members shown.
std::string composeReason(
    const std::vector<std::string>& helps,
    const std::string& header,
    const std::vector<Line>& lines,
    const std::string& why,
    Rendering& rendering
);

} // namespace MismatchWording
} // namespace Luau

// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Location.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Luau
{

// Luwu object notation: reading `.luwon` data files into values, and writing values back as text. Reading walks the
// file's AST and never runs code, so any tool can read a file it doesn't trust, without a VM.

// How deep tables and objects in a data file's value can nest. It bounds the recursion of everything that walks a value.
inline constexpr size_t kLuwonMaxDepth = 200;

struct LuwonTable;

struct LuwonValue
{
    enum class Kind
    {
        Nil,
        None,
        Boolean,
        Number,
        String,
        Vector,
        Buffer,
        Table,
        // A constructor call the reader builds: `Point(1, 2)`, or `collections.pair(1, 2)` for a builtin class.
        Object,
    };

    Kind kind = Kind::Nil;
    bool boolean = false;
    double number = 0.0;
    // A string's or a buffer's bytes, or an object's class name as the file writes it.
    std::string bytes;
    float vector[4] = {};
    // Every reference to the same const shares it the way code shares a table.
    std::shared_ptr<const LuwonTable> table;
    // An object's constructor arguments.
    std::vector<LuwonValue> args;
};

struct LuwonComment
{
    // The comment's text, `--` included.
    std::string text;
    // Unset for a comment set by code rather than read from a file.
    std::optional<Location> location;
};

// The comments that belong to an item of a table.
struct LuwonComments
{
    // The ones on lines of their own right above the item.
    std::vector<LuwonComment> above;
    // The one after the item on its last line.
    std::optional<LuwonComment> after;
};

// A const a value is written as the name of (`dependencies = dependencies`), and what's written about it.
struct LuwonConstant
{
    std::string name;
    // The declaration, from `const` to the end of its value.
    Location declaration;
    // The comments above the declaration and after it on its last line.
    LuwonComments comments;
};

// Where a value is written in the file it was read from.
struct LuwonSite
{
    // The expression that gives the value, inside any parentheses or `::` cast.
    Location value;
    // A keyed entry's key; equal to `value` for a positional item.
    Location key;
    // The whole item, from its key (or the `[` around it) to its value.
    Location item;
    // The `,` or `;` after the item, when it has one.
    std::optional<Location> separator;
    // When the value is written as a const's name (`threads = num_threads`), where that const's value is written. An edit
    // changes the const there instead of writing a literal over its name.
    std::optional<Location> binding;
    // A `.{name}` field, whose key and value are the one name. A literal written there needs its key spelled out.
    bool shorthand = false;
    // When the value is written as a const's name, that const.
    std::optional<LuwonConstant> constant;
};

struct LuwonEntry
{
    LuwonValue key;
    LuwonValue value;
    // Unset for a table built by code rather than read from a file.
    std::optional<LuwonSite> site;
    // Unset when nothing says what the comments are: writing back into a file then keeps the ones it has.
    std::optional<LuwonComments> comments;
};

struct LuwonTable
{
    // Positional items, the first at index 1. A `nil` item is a hole. Their keys are unset.
    std::vector<LuwonEntry> array;
    // Keyed entries in the order the file wrote them. A key whose value is `nil` isn't stored.
    std::vector<LuwonEntry> entries;
    // The table constructor, from `{` to `}`, when the table was read from a file.
    std::optional<Location> constructor;

    // The value stored under `key`, or nullptr when the table has none.
    const LuwonValue* find(const LuwonValue& key) const;
    const LuwonEntry* findEntry(const LuwonValue& key) const;

    // Adds a keyed entry, or returns false when the table already has the key. A `nil` value only reserves the key.
    // The key must be one `isLuwonKey` accepts.
    bool add(LuwonValue key, LuwonValue value, std::optional<LuwonSite> site = std::nullopt, std::optional<LuwonComments> comments = std::nullopt);

private:
    // Maps an encoded key to its position in `entries`, or to SIZE_MAX for a key whose value is `nil`.
    std::unordered_map<std::string, size_t> keyIndex;
};

// Whether a value can be a table key in a data file: a string, a boolean, or a number other than NaN.
bool isLuwonKey(const LuwonValue& key);

// Whether two values are the same data. Tables compare by contents, and NaN equals NaN.
bool luwonEquals(const LuwonValue& a, const LuwonValue& b);

struct LuwonError
{
    Location location;
    std::string message;
};

struct LuwonDocument
{
    // Empty when there are errors.
    std::optional<LuwonValue> value;
    // Parse errors, or the evaluation error that stopped it.
    std::vector<LuwonError> errors;
    // Where the returned value is written. Its key and item are the value's location too.
    LuwonSite root;
    // Every comment in the file, in order.
    std::vector<LuwonComment> comments;
};

LuwonDocument readLuwon(std::string_view source);

// The byte offset each line of `source` starts at.
std::vector<size_t> luwonLineOffsets(std::string_view source);
size_t luwonOffsetOf(const std::vector<size_t>& lineOffsets, std::string_view source, const Position& position);

// The text a value's type has in error messages: "table", "vector", ...
const char* luwonKindName(LuwonValue::Kind kind);

struct LuwonWriteResult
{
    std::string text;
    // Set when the value can't be written; the text is empty then.
    std::optional<std::string> error;
};

// A data file holding `value`: `return` and its literals.
LuwonWriteResult writeLuwon(const LuwonValue& value);

// `value` as a literal on its own, the way a data file would write it: `Semver(0, 2, 0)`.
std::string writeLuwonLiteral(const LuwonValue& value);

// `source` with the values in it changed to `value`. Only the expressions whose values changed are rewritten, so
// comments, consts, types and layout stay as they were. `source` must read without errors.
LuwonWriteResult editLuwon(std::string_view source, const LuwonValue& value);

// A value as JSON, for tools that read JSON but not data files. Plain JSON approximates what JSON has no type for (a vector
// is an array); lossless JSON writes it as an object with one `$` key, which `readLuwonJson` turns back into the value.
// `LuwonJson.cpp` lists the forms.
LuwonWriteResult writeLuwonJson(const LuwonValue& value, bool lossless);

// JSON as a data file's value, the `$` objects of lossless JSON included. `null` is `none`.
LuwonDocument readLuwonJson(std::string_view json);

// Formats a number the way Luwu's `tostring` does, so string building in a data file gives the runtime's text.
std::string formatLuwonNumber(double number);

} // namespace Luau

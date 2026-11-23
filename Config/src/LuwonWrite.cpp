// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Luwon.h"

#include "Luau/Lexer.h"
#include "Luau/StringUtils.h"

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <ctype.h>
#include <math.h>

namespace Luau
{

// A table whose items fit in this many columns, indentation included, is written on one line.
static constexpr size_t kLuwonLineWidth = 100;

static const char* kLuwonIndent = "    ";

static std::string quoteString(const std::string& bytes)
{
    std::string result = "\"";
    for (char c : bytes)
    {
        unsigned char byte = (unsigned char)c;
        if (c == '"' || c == '\\')
        {
            result += '\\';
            result += c;
        }
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else if (byte < 0x20 || byte == 0x7f)
            result += format("\\%03d", byte);
        else
            result += c;
    }

    return result + "\"";
}

static bool isIdentifier(const std::string& name)
{
    if (name.empty() || isdigit((unsigned char)name[0]))
        return false;

    for (char c : name)
    {
        if (!isalnum((unsigned char)c) && c != '_')
            return false;
    }

    return !Lexer::isReserved(name);
}

// A number as a literal that reads back as the same number. A data file has no `math.huge`, so infinities and NaN are
// written as the divisions that give them.
static std::string numberLiteral(double number)
{
    if (isnan(number))
        return "0 / 0";
    if (isinf(number))
        return number < 0 ? "-1 / 0" : "1 / 0";

    return formatLuwonNumber(number);
}

static bool sameTexts(const std::vector<LuwonComment>& a, const std::vector<LuwonComment>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); i++)
    {
        if (a[i].text != b[i].text)
            return false;
    }

    return true;
}

static bool sameComments(const LuwonComments& a, const LuwonComments& b)
{
    bool sameAfter = a.after.has_value() == b.after.has_value() && (!a.after || a.after->text == b.after->text);
    return sameAfter && sameTexts(a.above, b.above);
}

namespace
{

class LuwonWriter
{
public:
    // `value` as a literal, for an expression on a line indented by `indent`.
    std::string write(const LuwonValue& value, const std::string& indent)
    {
        switch (value.kind)
        {
        case LuwonValue::Kind::Nil:
            return "nil";
        case LuwonValue::Kind::None:
            return "none";
        case LuwonValue::Kind::Boolean:
            return value.boolean ? "true" : "false";
        case LuwonValue::Kind::Number:
            return numberLiteral(value.number);
        case LuwonValue::Kind::String:
            return quoteString(value.bytes);
        case LuwonValue::Kind::Vector:
            return writeVector(value);
        case LuwonValue::Kind::Buffer:
            return "buffer.fromstring(" + quoteString(value.bytes) + ")";
        case LuwonValue::Kind::Table:
            return writeTable(*value.table, indent);
        case LuwonValue::Kind::Object:
            return writeObject(value, indent);
        }

        LUAU_UNREACHABLE();
    }

    std::string writeKey(const LuwonValue& key, const std::string& indent)
    {
        if (key.kind == LuwonValue::Kind::String && isIdentifier(key.bytes))
            return key.bytes;

        return "[" + write(key, indent) + "]";
    }

    // One item of a table: `value` for a positional one, `key = value` otherwise.
    std::string writeItem(const LuwonValue* key, const LuwonValue& value, const std::string& indent)
    {
        if (!key)
            return write(value, indent);

        return writeKey(*key, indent) + " = " + write(value, indent);
    }

    // An item on lines of its own, indented by `indent`: its comments above it, then the item and its separator, then the
    // comment after it.
    static std::string itemLines(const std::string& item, const LuwonComments* comments, const std::string& indent)
    {
        std::string result;
        if (comments)
        {
            for (const LuwonComment& comment : comments->above)
                result += indent + comment.text + "\n";
        }

        result += indent + item + ",";
        if (comments && comments->after)
            result += " " + comments->after->text;

        return result + "\n";
    }

    // An item on a line shared with others, its comments written as block comments so they end where the item starts.
    static std::string itemInline(const std::string& item, const LuwonComments* comments)
    {
        if (!comments)
            return item;

        std::string result;
        for (const LuwonComment& comment : comments->above)
            result += blockComment(comment.text) + " ";

        result += item;
        if (comments->after)
            result += " " + blockComment(comments->after->text);

        return result;
    }

    // A comment as a block comment: `-- note` becomes `--[[ note]]`, with enough `=` to hold any `]]` in it.
    static std::string blockComment(const std::string& text)
    {
        if (text.rfind("--[", 0) == 0 && text.find('\n') == std::string::npos && text.size() > 4 && text.back() == ']')
            return text;

        std::string body = text.rfind("--", 0) == 0 ? text.substr(2) : text;
        std::string level;
        while (body.find("]" + level + "]") != std::string::npos)
            level += "=";

        return "--[" + level + "[" + body + "]" + level + "]";
    }

    struct Item
    {
        std::string text;
        const LuwonComments* comments;
    };

    static bool hasComments(const LuwonComments* comments)
    {
        return comments && (!comments->above.empty() || comments->after);
    }

    // Items that fit on one line are joined there; otherwise each goes on its own line, one level in. Items with comments
    // always go on lines of their own.
    static std::string layout(const std::vector<Item>& items, const std::string& indent)
    {
        size_t width = indent.size() + 4;
        bool multiline = false;
        for (const Item& item : items)
        {
            width += item.text.size() + 2;
            multiline = multiline || item.text.find('\n') != std::string::npos || hasComments(item.comments);
        }

        if (!multiline && width <= kLuwonLineWidth)
        {
            std::vector<std::string> texts;
            for (const Item& item : items)
                texts.push_back(item.text);

            return "{ " + join(texts, ", ") + " }";
        }

        std::string result = "{\n";
        for (const Item& item : items)
            result += itemLines(item.text, item.comments, indent + kLuwonIndent);

        return result + indent + "}";
    }

private:
    std::string writeVector(const LuwonValue& value)
    {
        int count = value.vector[3] == 0.0f ? 3 : 4;

        std::vector<std::string> components;
        for (int i = 0; i < count; i++)
            components.push_back(numberLiteral(value.vector[i]));

        return "vector.create(" + join(components, ", ") + ")";
    }

    std::string writeTable(const LuwonTable& table, const std::string& indent)
    {
        if (table.array.empty() && table.entries.empty())
            return "{}";

        std::string inner = indent + kLuwonIndent;

        std::vector<Item> items;
        for (const LuwonEntry& item : table.array)
            items.push_back({writeItem(nullptr, item.value, inner), item.comments ? &*item.comments : nullptr});
        for (const LuwonEntry& entry : table.entries)
            items.push_back({writeItem(&entry.key, entry.value, inner), entry.comments ? &*entry.comments : nullptr});

        return layout(items, indent);
    }

    // A constructor call. One table argument is written `Point { x = 1 }`, the way the default constructor is called.
    std::string writeObject(const LuwonValue& value, const std::string& indent)
    {
        if (value.args.size() == 1 && value.args[0].kind == LuwonValue::Kind::Table)
            return value.bytes + " " + write(value.args[0], indent);

        std::vector<std::string> args;
        for (const LuwonValue& arg : value.args)
            args.push_back(write(arg, indent));

        return value.bytes + "(" + join(args, ", ") + ")";
    }

    static std::string join(const std::vector<std::string>& parts, const char* separator)
    {
        std::string result;
        for (size_t i = 0; i < parts.size(); i++)
        {
            if (i > 0)
                result += separator;
            result += parts[i];
        }

        return result;
    }
};

struct LuwonEdit
{
    size_t begin;
    size_t end;
    std::string text;
};

// Changes a file by rewriting only the expressions whose values changed.
class LuwonEditor
{
public:
    // A value written as a const's name is changed where the const is declared when `mayEditConst` allows it, given the
    // offset where the const's value starts.
    LuwonEditor(std::string_view source, std::function<bool(size_t)> mayEditConst)
        : source(source)
        , lineOffsets(luwonLineOffsets(source))
        , mayEditConst(std::move(mayEditConst))
    {
    }

    // The offsets of the consts this edit changes.
    std::vector<size_t> editedConstOffsets() const
    {
        std::vector<size_t> offsets;
        for (const auto& [offset, _] : editedConsts)
            offsets.push_back(offset);

        return offsets;
    }

    void diffValue(const LuwonValue& old, const LuwonValue& now, const LuwonSite& site)
    {
        if (luwonEquals(old, now))
            return;

        bool bothTables = old.kind == LuwonValue::Kind::Table && now.kind == LuwonValue::Kind::Table;
        if (!bothTables || !old.table->constructor)
        {
            replace(site, now);
            return;
        }

        // A table a const shares between several places is edited where it's constructed, once. A place whose new
        // value differs from that edit gets a literal of its own.
        auto [it, firstVisit] = edited.try_emplace(old.table.get(), now);
        if (firstVisit)
            diffTable(*old.table, *now.table);
        else if (!luwonEquals(it->second, now))
            replace(site, now);
    }

    // The source with every edit made, or nullopt when two edits overlap.
    std::optional<std::string> apply()
    {
        std::stable_sort(
            edits.begin(),
            edits.end(),
            [](const LuwonEdit& a, const LuwonEdit& b)
            {
                // At one offset, a replacement goes first so text inserted there lands before it
                if (a.begin != b.begin)
                    return a.begin > b.begin;
                return a.end > b.end;
            }
        );

        for (size_t i = 1; i < edits.size(); i++)
        {
            if (edits[i].end > edits[i - 1].begin)
                return std::nullopt;
        }

        std::string result(source);
        for (const LuwonEdit& edit : edits)
            result.replace(edit.begin, edit.end - edit.begin, edit.text);

        return result;
    }

private:
    std::string_view source;
    std::vector<size_t> lineOffsets;
    std::vector<LuwonEdit> edits;
    std::unordered_map<const LuwonTable*, LuwonValue> edited;
    // The new value of each const changed so far, by the offset its value starts at.
    std::unordered_map<size_t, LuwonValue> editedConsts;
    std::function<bool(size_t)> mayEditConst;
    LuwonWriter writer;

    size_t offsetOf(const Position& position) const
    {
        return luwonOffsetOf(lineOffsets, source, position);
    }

    size_t lineStart(unsigned int line) const
    {
        return offsetOf(Position{line, 0});
    }

    // The offset just past the end of `line`, its newline included.
    size_t nextLineStart(unsigned int line) const
    {
        return line + 1 < lineOffsets.size() ? lineOffsets[line + 1] : source.size();
    }

    std::string indentOf(unsigned int line) const
    {
        size_t begin = lineStart(line);
        size_t end = begin;
        while (end < source.size() && (source[end] == ' ' || source[end] == '\t'))
            end++;

        return std::string(source.substr(begin, end - begin));
    }

    bool onlySpaces(size_t begin, size_t end) const
    {
        return source.substr(begin, end - begin).find_first_not_of(" \t\r\n") == std::string_view::npos;
    }

    void replace(const LuwonSite& site, const LuwonValue& now)
    {
        if (site.binding && mayEditConst(offsetOf(site.binding->begin)) && replaceConst(*site.binding, now))
            return;

        std::string text = writer.write(now, indentOf(site.value.begin.line));
        if (!site.shorthand)
        {
            edits.push_back({offsetOf(site.value.begin), offsetOf(site.value.end), std::move(text)});
            return;
        }

        // `.{name}` becomes `.{name = <literal>}`
        size_t end = offsetOf(site.key.end);
        edits.push_back({end, end, " = " + text});
    }

    // Changes the value a const is declared with. Returns false when another place already changed it to something else.
    bool replaceConst(const Location& binding, const LuwonValue& now)
    {
        auto [it, first] = editedConsts.try_emplace(offsetOf(binding.begin), now);
        if (!first)
            return luwonEquals(it->second, now);

        std::string text = writer.write(now, indentOf(binding.begin.line));
        edits.push_back({offsetOf(binding.begin), offsetOf(binding.end), std::move(text)});
        return true;
    }

    // Removes an item and its separator. When nothing else is on its lines, the lines go too. A comment next to it stays.
    void remove(const LuwonSite& site)
    {
        size_t begin = offsetOf(site.item.begin);
        size_t end = offsetOf(site.separator ? site.separator->end : site.item.end);

        size_t lineBegin = lineStart(site.item.begin.line);
        unsigned int lastLine = site.separator ? site.separator->end.line : site.item.end.line;
        size_t lineEnd = nextLineStart(lastLine);

        if (onlySpaces(lineBegin, begin) && onlySpaces(end, lineEnd))
        {
            begin = lineBegin;
            end = lineEnd;
        }
        else
        {
            // The comment that follows moves back to the item's indentation
            while (end < lineEnd && (source[end] == ' ' || source[end] == '\t'))
                end++;
        }

        edits.push_back({begin, end, ""});
    }

    struct Addition
    {
        const LuwonValue* key;
        const LuwonValue* value;
        const LuwonComments* comments;
    };

    static const LuwonComments* commentsOf(const LuwonEntry* entry)
    {
        return entry && entry->comments ? &*entry->comments : nullptr;
    }

    void diffTable(const LuwonTable& old, const LuwonTable& now)
    {
        const LuwonSite* lastKept = nullptr;
        auto keep = [&lastKept](const LuwonSite& site)
        {
            if (!lastKept || lastKept->item.end < site.item.end)
                lastKept = &site;
        };

        for (size_t i = 0; i < old.array.size(); i++)
        {
            const LuwonSite& site = *old.array[i].site;
            if (i < now.array.size())
            {
                diffValue(old.array[i].value, now.array[i].value, site);
                diffComments(site, *old.array[i].comments, commentsOf(&now.array[i]));
                keep(site);
            }
            else
                remove(site);
        }

        for (const LuwonEntry& entry : old.entries)
        {
            const LuwonSite& site = *entry.site;
            if (const LuwonEntry* nowEntry = now.findEntry(entry.key))
            {
                diffValue(entry.value, nowEntry->value, site);
                diffComments(site, *entry.comments, commentsOf(nowEntry));
                keep(site);
            }
            else
                remove(site);
        }

        std::vector<Addition> additions;
        for (size_t i = old.array.size(); i < now.array.size(); i++)
        {
            LuwonValue index{LuwonValue::Kind::Number};
            index.number = double(i + 1);
            if (!old.findEntry(index))
                additions.push_back({nullptr, &now.array[i].value, commentsOf(&now.array[i])});
        }

        for (const LuwonEntry& entry : now.entries)
        {
            if (!old.findEntry(entry.key))
                additions.push_back({&entry.key, &entry.value, commentsOf(&entry)});
        }

        if (!additions.empty())
            insert(*old.constructor, lastKept, additions);
    }

    // Rewrites a kept item's comments when `now` says they're different. Unset `now` keeps them.
    void diffComments(const LuwonSite& site, const LuwonComments& old, const LuwonComments* now)
    {
        if (!now)
            return;

        if (!sameTexts(old.above, now->above))
            editAbove(site, old, *now);

        bool sameAfter = old.after.has_value() == now->after.has_value() && (!old.after || old.after->text == now->after->text);
        if (!sameAfter)
            editAfter(site, old, *now);
    }

    void editAbove(const LuwonSite& site, const LuwonComments& old, const LuwonComments& now)
    {
        size_t itemStart = offsetOf(site.item.begin);
        size_t itemLine = lineStart(site.item.begin.line);

        if (!onlySpaces(itemLine, itemStart))
        {
            // Code before the item on its line: its comments go right before it as block comments
            std::string text;
            for (const LuwonComment& comment : now.above)
                text += LuwonWriter::blockComment(comment.text) + " ";
            edits.push_back({itemStart, itemStart, std::move(text)});
            return;
        }

        std::string indent = indentOf(site.item.begin.line);
        std::string text;
        for (const LuwonComment& comment : now.above)
            text += indent + comment.text + "\n";

        size_t begin = old.above.empty() ? itemLine : lineStart(old.above.front().location->begin.line);
        edits.push_back({begin, itemLine, std::move(text)});
    }

    void editAfter(const LuwonSite& site, const LuwonComments& old, const LuwonComments& now)
    {
        Position itemEnd = site.separator ? site.separator->end : site.item.end;
        size_t from = offsetOf(itemEnd);
        size_t to = old.after ? offsetOf(old.after->location->end) : from;

        if (!now.after)
        {
            edits.push_back({from, to, ""});
            return;
        }

        // A line comment would comment out code that follows on the line, so that takes a block comment
        bool restOfLineIsEmpty = onlySpaces(to, nextLineStart(itemEnd.line));
        std::string comment = restOfLineIsEmpty ? now.after->text : LuwonWriter::blockComment(now.after->text);
        edits.push_back({from, to, " " + comment});
    }

    // Adds items after the last one the table keeps: on lines of their own when the table spans lines and its `}` starts
    // a line, and on the same line otherwise.
    void insert(const Location& constructor, const LuwonSite* lastKept, const std::vector<Addition>& additions)
    {
        Position close{constructor.end.line, constructor.end.column - 1};
        size_t closeOffset = offsetOf(close);
        bool closeStartsLine = onlySpaces(lineStart(close.line), closeOffset);
        bool multiline = constructor.begin.line != constructor.end.line && closeStartsLine;

        if (multiline)
        {
            std::string itemIndent = lastKept ? indentOf(lastKept->item.begin.line) : indentOf(close.line) + kLuwonIndent;

            if (lastKept && !lastKept->separator)
                edits.push_back({offsetOf(lastKept->item.end), offsetOf(lastKept->item.end), ","});

            std::string text;
            for (const Addition& addition : additions)
                text += LuwonWriter::itemLines(writer.writeItem(addition.key, *addition.value, itemIndent), addition.comments, itemIndent);

            size_t at = lineStart(close.line);
            edits.push_back({at, at, std::move(text)});
            return;
        }

        std::string indent = indentOf(constructor.begin.line);
        std::vector<std::string> items;
        for (const Addition& addition : additions)
            items.push_back(LuwonWriter::itemInline(writer.writeItem(addition.key, *addition.value, indent), addition.comments));

        if (lastKept)
        {
            std::string text;
            for (const std::string& item : items)
                text += ", " + item;

            size_t at = offsetOf(lastKept->item.end);
            edits.push_back({at, at, std::move(text)});
            return;
        }

        std::string text;
        for (size_t i = 0; i < items.size(); i++)
            text += (i == 0 ? " " : ", ") + items[i];

        // Past the `{`, or the `.{` of a `.{...}` constructor
        size_t open = offsetOf(constructor.begin);
        size_t at = open + (source[open] == '.' ? 2 : 1);
        edits.push_back({at, at, text + " "});
    }
};

} // namespace

// Whether every comment `expected` sets is the one `actual` has. Entries whose comments `expected` leaves unset match any.
static bool commentsMatch(const LuwonValue& expected, const LuwonValue& actual, std::unordered_set<const LuwonTable*>& visited)
{
    bool bothTables = expected.kind == LuwonValue::Kind::Table && actual.kind == LuwonValue::Kind::Table;
    if (!bothTables || !visited.insert(expected.table.get()).second)
        return true;

    auto entryMatches = [&](const LuwonEntry& entry, const LuwonEntry* other)
    {
        if (!other)
            return true;
        if (entry.comments && (!other->comments || !sameComments(*entry.comments, *other->comments)))
            return false;

        return commentsMatch(entry.value, other->value, visited);
    };

    const LuwonTable& table = *expected.table;
    for (size_t i = 0; i < table.array.size(); i++)
    {
        const LuwonEntry* other = i < actual.table->array.size() ? &actual.table->array[i] : nullptr;
        if (!entryMatches(table.array[i], other))
            return false;
    }

    for (const LuwonEntry& entry : table.entries)
    {
        if (!entryMatches(entry, actual.table->findEntry(entry.key)))
            return false;
    }

    return true;
}

std::string writeLuwonLiteral(const LuwonValue& value)
{
    LuwonWriter writer;
    return writer.write(value, "");
}

LuwonWriteResult writeLuwon(const LuwonValue& value)
{
    return {"return " + writeLuwonLiteral(value) + "\n", std::nullopt};
}

LuwonWriteResult editLuwon(std::string_view source, const LuwonValue& value)
{
    LuwonDocument document = readLuwon(source);
    if (!document.errors.empty())
    {
        const LuwonError& error = document.errors.front();
        return {"", format("the source to edit doesn't read: line %d: %s", error.location.begin.line + 1, error.message.c_str())};
    }

    // Changing a const where it's declared leaves its name in place everywhere it's used, but also changes every value built from it.
    // Each attempt is read back, and the first that gives exactly `value` wins: every const the change goes through, then
    // the consts that are safe to change on their own, then none, which writes literals where the consts were used.
    std::vector<size_t> wanted;
    auto attempt = [&](std::function<bool(size_t)> mayEditConst) -> std::optional<std::string>
    {
        LuwonEditor editor(source, std::move(mayEditConst));
        editor.diffValue(*document.value, value, document.root);
        wanted = editor.editedConstOffsets();

        std::optional<std::string> edited = editor.apply();
        if (!edited)
            return std::nullopt;

        LuwonDocument check = readLuwon(*edited);
        std::unordered_set<const LuwonTable*> visited;
        if (!check.errors.empty() || !luwonEquals(*check.value, value) || !commentsMatch(value, *check.value, visited))
            return std::nullopt;

        return edited;
    };

    if (std::optional<std::string> edited = attempt([](size_t) { return true; }))
        return {std::move(*edited), std::nullopt};

    std::unordered_set<size_t> safe;
    for (size_t candidate : std::vector<size_t>(wanted))
    {
        if (attempt([candidate](size_t offset) { return offset == candidate; }))
            safe.insert(candidate);
    }

    if (std::optional<std::string> edited = attempt([&safe](size_t offset) { return safe.count(offset) > 0; }))
        return {std::move(*edited), std::nullopt};

    if (std::optional<std::string> edited = attempt([](size_t) { return false; }))
        return {std::move(*edited), std::nullopt};

    return {"", "editing the source didn't give the value to write (a bug in luwon's editor)"};
}

} // namespace Luau

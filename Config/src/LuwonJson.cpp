// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Luwon.h"

#include "Luau/StringUtils.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Luwu object notation: converts a data file's value to JSON and back, so tools that only read JSON can use data files.
//
// There are two ways to write JSON. Plain JSON approximates the values JSON has no type for: a vector becomes an array, a
// class call its arguments, `none`, NaN and infinities `null`, and a key that isn't a string its text. Lossless JSON
// writes each of those as an object with a `$` key instead, and reading JSON turns these back into the values:
//
//     { "$vector": [1, 2, 3] }                   { "$class": "Point", "$args": [1, 2] }
//     { "$buffer": "<base64>" }                  { "$string": "<base64>" }    -- a string that isn't UTF-8
//     { "$number": "nan" | "inf" | "-inf" }      { "$nil": true }             -- a hole in a list
//     { "$items": [...], "$entries": [[key, value], ...] }                    -- a table JSON can't write as it is
//
// Comments, consts and layout aren't data, so neither form keeps them.

namespace Luau
{

namespace
{

// A list that fits in this many columns, indentation included, is written on one line
const size_t kJsonLineWidth = 100;

const char* const kBase64Digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        unsigned chunk = unsigned(uint8_t(bytes[i])) << 16;
        if (i + 1 < bytes.size())
            chunk |= unsigned(uint8_t(bytes[i + 1])) << 8;
        if (i + 2 < bytes.size())
            chunk |= unsigned(uint8_t(bytes[i + 2]));

        out += kBase64Digits[(chunk >> 18) & 63];
        out += kBase64Digits[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? kBase64Digits[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kBase64Digits[chunk & 63] : '=';
    }

    return out;
}

std::optional<std::string> base64Decode(std::string_view text)
{
    std::string out;
    unsigned chunk = 0;
    int bits = 0;

    for (char c : text)
    {
        if (c == '=')
            break;

        const char* digit = strchr(kBase64Digits, c);
        if (!digit || c == 0)
            return std::nullopt;

        chunk = (chunk << 6) | unsigned(digit - kBase64Digits);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += char((chunk >> bits) & 0xff);
        }
    }

    return out;
}

// The length of the UTF-8 sequence starting at `text[i]`, or 0 when it isn't a valid one
size_t utf8SequenceLength(std::string_view text, size_t i)
{
    uint8_t lead = uint8_t(text[i]);
    size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 0;
    if (length == 0 || i + length > text.size())
        return 0;

    for (size_t k = 1; k < length; k++)
    {
        if ((uint8_t(text[i + k]) & 0xc0) != 0x80)
            return 0;
    }

    return length;
}

bool isUtf8(std::string_view text)
{
    for (size_t i = 0; i < text.size();)
    {
        size_t length = utf8SequenceLength(text, i);
        if (length == 0)
            return false;
        i += length;
    }

    return true;
}

void appendUtf8(std::string& out, unsigned codepoint)
{
    if (codepoint < 0x80)
        out += char(codepoint);
    else if (codepoint < 0x800)
    {
        out += char(0xc0 | (codepoint >> 6));
        out += char(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint < 0x10000)
    {
        out += char(0xe0 | (codepoint >> 12));
        out += char(0x80 | ((codepoint >> 6) & 0x3f));
        out += char(0x80 | (codepoint & 0x3f));
    }
    else
    {
        out += char(0xf0 | (codepoint >> 18));
        out += char(0x80 | ((codepoint >> 12) & 0x3f));
        out += char(0x80 | ((codepoint >> 6) & 0x3f));
        out += char(0x80 | (codepoint & 0x3f));
    }
}

// A JSON string literal. Bytes that aren't UTF-8 become U+FFFD.
std::string jsonString(std::string_view text)
{
    std::string out = "\"";
    for (size_t i = 0; i < text.size();)
    {
        size_t length = utf8SequenceLength(text, i);
        if (length == 0)
        {
            out += "\xef\xbf\xbd";
            i++;
            continue;
        }

        char c = text[i];
        if (c == '"' || c == '\\')
            out += std::string("\\") + c;
        else if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else if (c == '\r')
            out += "\\r";
        else if (uint8_t(c) < 0x20)
            out += format("\\u%04x", unsigned(uint8_t(c)));
        else
            out.append(text.data() + i, length);

        i += length;
    }

    return out + "\"";
}

std::string keyText(const LuwonValue& key)
{
    if (key.kind == LuwonValue::Kind::String)
        return key.bytes;
    if (key.kind == LuwonValue::Kind::Number)
        return formatLuwonNumber(key.number);

    return key.boolean ? "true" : "false";
}

class JsonWriter
{
public:
    explicit JsonWriter(bool lossless)
        : lossless(lossless)
    {
    }

    std::string out;

    void write(const LuwonValue& value, int depth)
    {
        switch (value.kind)
        {
        case LuwonValue::Kind::Nil:
            out += lossless ? "{ \"$nil\": true }" : "null";
            return;
        case LuwonValue::Kind::None:
            out += "null";
            return;
        case LuwonValue::Kind::Boolean:
            out += value.boolean ? "true" : "false";
            return;
        case LuwonValue::Kind::Number:
            writeNumber(value.number);
            return;
        case LuwonValue::Kind::String:
            if (lossless && !isUtf8(value.bytes))
                out += "{ \"$string\": \"" + base64Encode(value.bytes) + "\" }";
            else
                out += jsonString(value.bytes);
            return;
        case LuwonValue::Kind::Vector:
            out += lossless ? "{ \"$vector\": [" : "[";
            for (int i = 0; i < 3; i++)
            {
                if (i > 0)
                    out += ", ";
                writeNumber(value.vector[i]);
            }
            out += lossless ? "] }" : "]";
            return;
        case LuwonValue::Kind::Buffer:
            out += lossless ? "{ \"$buffer\": \"" + base64Encode(value.bytes) + "\" }" : "\"" + base64Encode(value.bytes) + "\"";
            return;
        case LuwonValue::Kind::Table:
            writeTable(*value.table, depth);
            return;
        case LuwonValue::Kind::Object:
            writeObject(value, depth);
            return;
        }
    }

private:
    bool lossless;

    void newline(int depth)
    {
        out += "\n";
        out.append(size_t(depth) * 4, ' ');
    }

    void writeNumber(double number)
    {
        if (isnan(number))
            out += lossless ? "{ \"$number\": \"nan\" }" : "null";
        else if (isinf(number))
            out += lossless ? (number > 0 ? "{ \"$number\": \"inf\" }" : "{ \"$number\": \"-inf\" }") : "null";
        else
            out += formatLuwonNumber(number);
    }

    // `[a, b]` on one line when it fits and holds nothing written on several, and one item per line otherwise
    void writeList(const std::vector<const LuwonValue*>& items, int depth)
    {
        std::vector<std::string> written;
        size_t width = size_t(depth) * 4 + 2;
        for (const LuwonValue* item : items)
        {
            JsonWriter inner{lossless};
            inner.write(*item, depth + 1);
            width += inner.out.size() + 2;
            written.push_back(std::move(inner.out));
        }

        bool oneLine = width <= kJsonLineWidth;
        for (const std::string& item : written)
            oneLine = oneLine && item.find('\n') == std::string::npos;

        if (oneLine)
        {
            out += "[";
            for (size_t i = 0; i < written.size(); i++)
                out += (i > 0 ? ", " : "") + written[i];
            out += "]";
            return;
        }

        out += "[";
        for (size_t i = 0; i < items.size(); i++)
        {
            newline(depth + 1);
            write(*items[i], depth + 1);
            if (i + 1 < items.size())
                out += ",";
        }
        newline(depth);
        out += "]";
    }

    void writeTable(const LuwonTable& table, int depth)
    {
        std::vector<const LuwonValue*> items;
        for (const LuwonEntry& item : table.array)
            items.push_back(&item.value);

        bool hasHoles = false;
        for (const LuwonValue* item : items)
            hasHoles = hasHoles || item->kind == LuwonValue::Kind::Nil;

        bool stringKeys = true;
        for (const LuwonEntry& entry : table.entries)
        {
            bool plain = entry.key.kind == LuwonValue::Kind::String && (!lossless || entry.key.bytes.empty() || entry.key.bytes[0] != '$');
            stringKeys = stringKeys && plain;
        }

        if (table.entries.empty() && !items.empty() && (!lossless || !hasHoles))
            return writeList(items, depth);
        if (items.empty() && (stringKeys || !lossless))
            return writeEntries(table.entries, depth);
        if (!lossless)
            return writeMixed(table, depth);

        // Lossless: `{ "$items": [...], "$entries": [[key, value], ...] }`, each only when it has something
        out += "{";
        if (!items.empty())
        {
            newline(depth + 1);
            out += "\"$items\": ";
            writeList(items, depth + 1);
            if (table.entries.empty())
            {
                newline(depth);
                out += "}";
                return;
            }
            out += ",";
        }
        newline(depth + 1);
        out += "\"$entries\": [";
        for (size_t i = 0; i < table.entries.size(); i++)
        {
            newline(depth + 2);
            out += "[";
            write(table.entries[i].key, depth + 2);
            out += ", ";
            write(table.entries[i].value, depth + 2);
            out += "]";
            if (i + 1 < table.entries.size())
                out += ",";
        }
        newline(depth + 1);
        out += "]";
        newline(depth);
        out += "}";
    }

    void writeEntries(const std::vector<LuwonEntry>& entries, int depth)
    {
        if (entries.empty())
        {
            out += "{}";
            return;
        }

        out += "{";
        for (size_t i = 0; i < entries.size(); i++)
        {
            newline(depth + 1);
            out += jsonString(keyText(entries[i].key)) + ": ";
            write(entries[i].value, depth + 1);
            if (i + 1 < entries.size())
                out += ",";
        }
        newline(depth);
        out += "}";
    }

    // Plain JSON for a table with both items and keys: one object, the items under their index
    void writeMixed(const LuwonTable& table, int depth)
    {
        std::vector<LuwonEntry> entries;
        for (size_t i = 0; i < table.array.size(); i++)
        {
            LuwonValue key{LuwonValue::Kind::Number};
            key.number = double(i + 1);
            entries.push_back(LuwonEntry{key, table.array[i].value});
        }
        entries.insert(entries.end(), table.entries.begin(), table.entries.end());
        writeEntries(entries, depth);
    }

    void writeObject(const LuwonValue& object, int depth)
    {
        std::vector<const LuwonValue*> args;
        for (const LuwonValue& arg : object.args)
            args.push_back(&arg);

        if (!lossless)
        {
            // A call's arguments: the one, or a list of them
            if (args.size() == 1)
                write(*args[0], depth);
            else
                writeList(args, depth);
            return;
        }

        out += "{";
        newline(depth + 1);
        out += "\"$class\": " + jsonString(object.bytes) + ",";
        newline(depth + 1);
        out += "\"$args\": ";
        writeList(args, depth + 1);
        newline(depth);
        out += "}";
    }
};

class JsonReader
{
public:
    explicit JsonReader(std::string_view text)
        : text(text)
    {
    }

    std::optional<LuwonError> error;

    std::optional<LuwonValue> read()
    {
        std::optional<LuwonValue> value = readValue(0);
        if (value)
        {
            skipSpace();
            if (pos < text.size())
                return fail("expected the end of the file after the value");
        }

        return value;
    }

private:
    std::string_view text;
    size_t pos = 0;

    // A JSON object's members in write order
    using Members = std::vector<std::pair<std::string, LuwonValue>>;

    std::nullopt_t fail(const std::string& message)
    {
        if (error)
            return std::nullopt;

        Position position{0, 0};
        for (size_t i = 0; i < pos && i < text.size(); i++)
        {
            if (text[i] == '\n')
            {
                position.line++;
                position.column = 0;
            }
            else
                position.column++;
        }

        error = LuwonError{Location(position, position), message};
        return std::nullopt;
    }

    void skipSpace()
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            pos++;
    }

    bool consume(std::string_view word)
    {
        if (text.substr(pos, word.size()) != word)
            return false;
        pos += word.size();
        return true;
    }

    std::optional<LuwonValue> readValue(size_t depth)
    {
        if (depth >= kLuwonMaxDepth)
            return fail(format("JSON nested more than %d deep", int(kLuwonMaxDepth)));

        skipSpace();
        if (pos >= text.size())
            return fail("expected a value");

        char c = text[pos];
        if (c == '{')
            return readObject(depth);
        if (c == '[')
            return readArray(depth);
        if (c == '"')
        {
            std::optional<std::string> bytes = readString();
            if (!bytes)
                return std::nullopt;
            LuwonValue value{LuwonValue::Kind::String};
            value.bytes = std::move(*bytes);
            return value;
        }
        if (consume("true") || consume("false"))
        {
            LuwonValue value{LuwonValue::Kind::Boolean};
            value.boolean = text[pos - 4] == 't';
            return value;
        }
        if (consume("null"))
            return LuwonValue{LuwonValue::Kind::None};
        if (c == '-' || (c >= '0' && c <= '9'))
            return readNumber();

        return fail(format("unexpected '%c'", c));
    }

    std::optional<LuwonValue> readNumber()
    {
        size_t start = pos;
        if (text[pos] == '-')
            pos++;
        while (pos < text.size() && (isdigit(uint8_t(text[pos])) || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-'))
            pos++;

        std::string written(text.substr(start, pos - start));
        char* end = nullptr;
        double number = strtod(written.c_str(), &end);
        if (end != written.c_str() + written.size())
        {
            pos = start;
            return fail(format("'%s' isn't a number", written.c_str()));
        }

        LuwonValue value{LuwonValue::Kind::Number};
        value.number = number;
        return value;
    }

    std::optional<unsigned> readHex4()
    {
        if (pos + 4 > text.size())
            return std::nullopt;

        unsigned codepoint = 0;
        for (int i = 0; i < 4; i++)
        {
            char c = text[pos++];
            unsigned digit = isdigit(uint8_t(c)) ? unsigned(c - '0') : (c >= 'a' && c <= 'f') ? unsigned(c - 'a' + 10) : (c >= 'A' && c <= 'F') ? unsigned(c - 'A' + 10) : 16;
            if (digit == 16)
                return std::nullopt;
            codepoint = codepoint * 16 + digit;
        }

        return codepoint;
    }

    std::optional<std::string> readString()
    {
        pos++; // "
        std::string out;
        while (pos < text.size() && text[pos] != '"')
        {
            char c = text[pos++];
            if (c != '\\')
            {
                out += c;
                continue;
            }

            if (pos >= text.size())
                break;

            char escape = text[pos++];
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                std::optional<unsigned> codepoint = readHex4();
                if (!codepoint)
                    return fail("'\\u' needs four hex digits");

                // A surrogate pair is one codepoint
                if (*codepoint >= 0xd800 && *codepoint < 0xdc00 && consume("\\u"))
                {
                    std::optional<unsigned> low = readHex4();
                    if (!low || *low < 0xdc00 || *low >= 0xe000)
                        return fail("a high surrogate needs a low one after it");
                    *codepoint = 0x10000 + ((*codepoint - 0xd800) << 10) + (*low - 0xdc00);
                }

                appendUtf8(out, *codepoint);
                break;
            }
            default:
                pos--;
                return fail(format("unknown escape '\\%c'", escape));
            }
        }

        if (pos >= text.size())
            return fail("unfinished string");

        pos++; // "
        return out;
    }

    std::optional<LuwonValue> readArray(size_t depth)
    {
        pos++; // [
        auto table = std::make_shared<LuwonTable>();

        skipSpace();
        if (consume("]"))
            return tableValue(std::move(table));

        while (true)
        {
            std::optional<LuwonValue> item = readValue(depth + 1);
            if (!item)
                return std::nullopt;
            table->array.push_back(LuwonEntry{LuwonValue{}, std::move(*item)});

            skipSpace();
            if (consume("]"))
                return tableValue(std::move(table));
            if (!consume(","))
                return fail("expected ',' or ']' after an item");
        }
    }

    std::optional<LuwonValue> readObject(size_t depth)
    {
        size_t start = pos;
        pos++; // {
        Members members;

        skipSpace();
        if (!consume("}"))
        {
            while (true)
            {
                skipSpace();
                if (pos >= text.size() || text[pos] != '"')
                    return fail("expected a string key");

                std::optional<std::string> key = readString();
                if (!key)
                    return std::nullopt;

                skipSpace();
                if (!consume(":"))
                    return fail("expected ':' after a key");

                std::optional<LuwonValue> value = readValue(depth + 1);
                if (!value)
                    return std::nullopt;
                members.emplace_back(std::move(*key), std::move(*value));

                skipSpace();
                if (consume("}"))
                    break;
                if (!consume(","))
                    return fail("expected ',' or '}' after a value");
            }
        }

        size_t end = pos;
        pos = start;
        std::optional<LuwonValue> value = fromMembers(members);
        if (value)
            pos = end;
        return value;
    }

    static LuwonValue tableValue(std::shared_ptr<LuwonTable> table)
    {
        LuwonValue value{LuwonValue::Kind::Table};
        value.table = std::move(table);
        return value;
    }

    static const LuwonValue* member(const Members& members, std::string_view key)
    {
        for (const auto& [name, value] : members)
        {
            if (name == key)
                return &value;
        }
        return nullptr;
    }

    // A `$` object, or a plain one when its keys aren't one of the lossless forms
    std::optional<LuwonValue> fromMembers(const Members& members)
    {
        if (std::optional<LuwonValue> tagged = fromTag(members))
            return tagged;
        if (error)
            return std::nullopt;

        auto table = std::make_shared<LuwonTable>();
        for (auto& [key, value] : members)
        {
            LuwonValue keyValue{LuwonValue::Kind::String};
            keyValue.bytes = key;
            if (!table->add(keyValue, value))
                return fail(format("the key '%s' is in this object twice", key.c_str()));
        }

        return tableValue(std::move(table));
    }

    std::optional<LuwonValue> fromTag(const Members& members)
    {
        auto only = [&](std::string_view key) -> const LuwonValue*
        {
            return members.size() == 1 ? member(members, key) : nullptr;
        };
        auto isList = [](const LuwonValue* value)
        {
            return value && value->kind == LuwonValue::Kind::Table && value->table->entries.empty();
        };

        if (const LuwonValue* nil = only("$nil"); nil && nil->kind == LuwonValue::Kind::Boolean)
            return LuwonValue{};

        if (const LuwonValue* vector = only("$vector"); isList(vector))
        {
            const std::vector<LuwonEntry>& components = vector->table->array;
            if (components.size() < 3 || components.size() > 4)
                return fail("'$vector' needs 3 or 4 numbers");

            LuwonValue value{LuwonValue::Kind::Vector};
            for (size_t i = 0; i < components.size(); i++)
            {
                if (components[i].value.kind != LuwonValue::Kind::Number)
                    return fail("'$vector' needs 3 or 4 numbers");
                value.vector[i] = float(components[i].value.number);
            }
            return value;
        }

        for (auto [tag, kind] : {std::pair{"$buffer", LuwonValue::Kind::Buffer}, std::pair{"$string", LuwonValue::Kind::String}})
        {
            const LuwonValue* encoded = only(tag);
            if (!encoded || encoded->kind != LuwonValue::Kind::String)
                continue;

            std::optional<std::string> bytes = base64Decode(encoded->bytes);
            if (!bytes)
                return fail(format("'%s' needs base64 text", tag));

            LuwonValue value{kind};
            value.bytes = std::move(*bytes);
            return value;
        }

        if (const LuwonValue* number = only("$number"); number && number->kind == LuwonValue::Kind::String)
        {
            LuwonValue value{LuwonValue::Kind::Number};
            if (number->bytes == "nan")
                value.number = NAN;
            else if (number->bytes == "inf")
                value.number = HUGE_VAL;
            else if (number->bytes == "-inf")
                value.number = -HUGE_VAL;
            else
                return fail("'$number' is \"nan\", \"inf\" or \"-inf\"");
            return value;
        }

        const LuwonValue* name = member(members, "$class");
        const LuwonValue* args = member(members, "$args");
        if (members.size() == 2 && name && name->kind == LuwonValue::Kind::String && isList(args))
        {
            LuwonValue value{LuwonValue::Kind::Object};
            value.bytes = name->bytes;
            for (const LuwonEntry& arg : args->table->array)
                value.args.push_back(arg.value);
            return value;
        }

        const LuwonValue* items = member(members, "$items");
        const LuwonValue* entries = member(members, "$entries");
        bool tableForm = (items || entries) && members.size() == size_t(items != nullptr) + size_t(entries != nullptr);
        if (tableForm && (!items || isList(items)) && (!entries || isList(entries)))
        {
            auto table = std::make_shared<LuwonTable>();
            if (items)
                table->array = items->table->array;

            for (const LuwonEntry& pair : entries ? entries->table->array : std::vector<LuwonEntry>{})
            {
                bool isPair = isList(&pair.value) && pair.value.table->array.size() == 2;
                if (!isPair)
                    return fail("each of '$entries' is a [key, value] list");

                const LuwonValue& key = pair.value.table->array[0].value;
                if (!isLuwonKey(key))
                    return fail("a key in '$entries' is a string, a boolean or a number other than NaN");
                if (!table->add(key, pair.value.table->array[1].value))
                    return fail(format("the key '%s' is in '$entries' twice", keyText(key).c_str()));
            }

            return tableValue(std::move(table));
        }

        return std::nullopt;
    }
};

} // namespace

LuwonWriteResult writeLuwonJson(const LuwonValue& value, bool lossless)
{
    JsonWriter writer{lossless};
    writer.write(value, 0);
    return LuwonWriteResult{writer.out + "\n", std::nullopt};
}

LuwonDocument readLuwonJson(std::string_view json)
{
    LuwonDocument document;
    JsonReader reader{json};
    std::optional<LuwonValue> value = reader.read();
    if (reader.error)
        document.errors.push_back(*reader.error);
    else
        document.value = std::move(value);
    return document;
}

} // namespace Luau

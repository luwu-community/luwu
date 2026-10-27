// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Luwon.h"

#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/LuwonSyntax.h"
#include "Luau/Parser.h"
#include "Luau/StringUtils.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Luau
{

// How many bytes string building (`..` and interpolation) can produce in one file. Doubling a string through a chain
// of consts grows it exponentially in the file's length.
static constexpr size_t kLuwonMaxBuiltBytes = 256 * 1024 * 1024;

const char* luwonKindName(LuwonValue::Kind kind)
{
    switch (kind)
    {
    case LuwonValue::Kind::Nil:
        return "nil";
    case LuwonValue::Kind::None:
        return "none";
    case LuwonValue::Kind::Boolean:
        return "boolean";
    case LuwonValue::Kind::Number:
        return "number";
    case LuwonValue::Kind::String:
        return "string";
    case LuwonValue::Kind::Vector:
        return "vector";
    case LuwonValue::Kind::Buffer:
        return "buffer";
    case LuwonValue::Kind::Table:
        return "table";
    case LuwonValue::Kind::Object:
        return "object";
    }

    LUAU_UNREACHABLE();
}

// A table key as a string, so a table can index its keys. Returns nullopt for a value that can't be a key.
static std::optional<std::string> encodeKey(const LuwonValue& key)
{
    switch (key.kind)
    {
    case LuwonValue::Kind::String:
        return "s" + key.bytes;
    case LuwonValue::Kind::Boolean:
        return key.boolean ? "b1" : "b0";
    case LuwonValue::Kind::Number:
    {
        if (isnan(key.number))
            return std::nullopt;

        // 0 and -0 are the same key
        double number = key.number == 0.0 ? 0.0 : key.number;
        std::string encoded = "n";
        encoded.append(reinterpret_cast<const char*>(&number), sizeof(number));
        return encoded;
    }
    default:
        return std::nullopt;
    }
}

bool isLuwonKey(const LuwonValue& key)
{
    return encodeKey(key).has_value();
}

// The array position `key` names, or 0 when it names none.
static size_t arrayIndexOf(const LuwonTable& table, const LuwonValue& key)
{
    if (key.kind != LuwonValue::Kind::Number)
        return 0;

    bool inRange = key.number >= 1.0 && key.number <= double(table.array.size());
    if (!inRange || floor(key.number) != key.number)
        return 0;

    return size_t(key.number);
}

const LuwonEntry* LuwonTable::findEntry(const LuwonValue& key) const
{
    if (size_t index = arrayIndexOf(*this, key))
        return &array[index - 1];

    std::optional<std::string> encoded = encodeKey(key);
    if (!encoded)
        return nullptr;

    auto it = keyIndex.find(*encoded);
    if (it == keyIndex.end() || it->second == SIZE_MAX)
        return nullptr;

    return &entries[it->second];
}

const LuwonValue* LuwonTable::find(const LuwonValue& key) const
{
    const LuwonEntry* entry = findEntry(key);
    return entry ? &entry->value : nullptr;
}

bool LuwonTable::add(LuwonValue key, LuwonValue value, std::optional<LuwonSite> site, std::optional<LuwonComments> comments)
{
    std::optional<std::string> encoded = encodeKey(key);
    LUAU_ASSERT(encoded);

    auto [it, inserted] = keyIndex.try_emplace(*encoded, SIZE_MAX);
    if (!inserted)
        return false;

    if (value.kind != LuwonValue::Kind::Nil)
    {
        it->second = entries.size();
        entries.push_back(LuwonEntry{std::move(key), std::move(value), site, std::move(comments)});
    }

    return true;
}

static bool sameNumber(double a, double b)
{
    return (isnan(a) && isnan(b)) || (a == b && signbit(a) == signbit(b));
}

static bool sameItems(const std::vector<LuwonEntry>& a, const std::vector<LuwonEntry>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); i++)
    {
        if (!luwonEquals(a[i].value, b[i].value))
            return false;
    }

    return true;
}

static bool sameTables(const LuwonTable& a, const LuwonTable& b)
{
    if (!sameItems(a.array, b.array) || a.entries.size() != b.entries.size())
        return false;

    for (const LuwonEntry& entry : a.entries)
    {
        const LuwonValue* other = b.find(entry.key);
        if (!other || !luwonEquals(entry.value, *other))
            return false;
    }

    return true;
}

static bool sameArgs(const std::vector<LuwonValue>& a, const std::vector<LuwonValue>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); i++)
    {
        if (!luwonEquals(a[i], b[i]))
            return false;
    }

    return true;
}

bool luwonEquals(const LuwonValue& a, const LuwonValue& b)
{
    if (a.kind != b.kind)
        return false;

    switch (a.kind)
    {
    case LuwonValue::Kind::Nil:
    case LuwonValue::Kind::None:
        return true;
    case LuwonValue::Kind::Boolean:
        return a.boolean == b.boolean;
    case LuwonValue::Kind::Number:
        return sameNumber(a.number, b.number);
    case LuwonValue::Kind::String:
    case LuwonValue::Kind::Buffer:
        return a.bytes == b.bytes;
    case LuwonValue::Kind::Vector:
        return memcmp(a.vector, b.vector, sizeof(a.vector)) == 0;
    case LuwonValue::Kind::Table:
        return a.table == b.table || sameTables(*a.table, *b.table);
    case LuwonValue::Kind::Object:
        return a.bytes == b.bytes && sameArgs(a.args, b.args);
    }

    LUAU_UNREACHABLE();
}

// Lays out a number's digits the way the VM's `luai_num2str` does (`lnumprint.cpp`). The digits come from the shortest
// `%e` precision that reads back as the same number: the shortest digits Schubfach gives the VM.
std::string formatLuwonNumber(double number)
{
    if (isinf(number))
        return number < 0 ? "-inf" : "inf";
    if (isnan(number))
        return "nan";
    if (number == 0.0)
        return signbit(number) ? "-0" : "0";

    char scientific[32];
    for (int precision = 0; precision < 17; precision++)
    {
        snprintf(scientific, sizeof(scientific), "%.*e", precision, number);
        if (strtod(scientific, nullptr) == number)
            break;
    }

    // `scientific` is "-d.ddde+XX": split it into its digits and its exponent
    std::string text = scientific;
    std::string sign = text[0] == '-' ? "-" : "";
    size_t e = text.find('e');
    std::string digits;
    for (size_t i = sign.size(); i < e; i++)
    {
        if (text[i] != '.')
            digits += text[i];
    }
    int exponent = atoi(text.c_str() + e + 1);

    while (digits.size() > 1 && digits.back() == '0')
        digits.pop_back();

    int declen = int(digits.size());
    int dot = exponent + 1;

    if (dot >= -5 && dot <= 21)
    {
        if (dot <= 0)
            return sign + "0." + std::string(-dot, '0') + digits;
        if (dot >= declen)
            return sign + digits + std::string(dot - declen, '0');

        return sign + digits.substr(0, dot) + "." + digits.substr(dot);
    }

    std::string result = sign + digits.substr(0, 1);
    if (declen > 1)
        result += "." + digits.substr(1);

    int printed = dot - 1;
    result += printed < 0 ? "e-" : "e+";
    std::string magnitude = std::to_string(printed < 0 ? -printed : printed);
    if (magnitude.size() < 2)
        magnitude = "0" + magnitude;

    return result + magnitude;
}

// The expression that gives a value, inside any parentheses or `::` cast. An edit replaces this and keeps the cast.
static AstExpr* innermost(AstExpr* expr)
{
    while (true)
    {
        if (AstExprGroup* group = expr->as<AstExprGroup>())
            expr = group->expr;
        else if (AstExprTypeAssertion* assertion = expr->as<AstExprTypeAssertion>())
            expr = assertion->expr;
        else
            return expr;
    }
}

// Whether only whitespace comes before `position` on its line.
static bool startsLine(const std::vector<size_t>& lineOffsets, std::string_view source, const Position& position)
{
    size_t begin = luwonOffsetOf(lineOffsets, source, Position{position.line, 0});
    size_t end = luwonOffsetOf(lineOffsets, source, position);
    return source.substr(begin, end - begin).find_first_not_of(" \t\r") == std::string_view::npos;
}

// The comments that belong to the item at `site`.
static LuwonComments commentsOf(
    const std::vector<LuwonComment>& comments,
    const LuwonSite& site,
    std::string_view source,
    const std::vector<size_t>& lineOffsets
)
{
    LuwonComments result;

    // Above: a run of comments ending on the line before the item, each on lines of its own
    unsigned int nextLine = site.item.begin.line;
    for (size_t i = comments.size(); i-- > 0;)
    {
        const Location& location = *comments[i].location;
        if (location.begin >= site.item.begin)
            continue;
        if (location.end.line + 1 != nextLine || !startsLine(lineOffsets, source, location.begin))
            break;

        result.above.insert(result.above.begin(), comments[i]);
        nextLine = location.begin.line;
    }

    // After: the first comment on the item's last line, past the item and its separator
    Position after = site.separator ? site.separator->end : site.item.end;
    for (const LuwonComment& comment : comments)
    {
        if (comment.location->begin.line == after.line && after <= comment.location->begin)
        {
            result.after = comment;
            break;
        }
    }

    return result;
}

namespace
{

struct LuwonEvaluationError
{
    Location location;
    std::string message;
};

class LuwonEvaluator
{
public:
    LuwonEvaluator(const CstNodeMap& cst, const std::vector<LuwonComment>& comments, std::string_view source)
        : cst(cst)
        , comments(comments)
        , source(source)
        , lineOffsets(luwonLineOffsets(source))
    {
    }

    LuwonValue evaluateChunk(AstStatBlock* root, LuwonSite& rootSite)
    {
        for (AstStat* stat : root->body)
        {
            if (AstStatLocal* local = stat->as<AstStatLocal>())
                bindLocal(local);
            else if (AstStatReturn* ret = stat->as<AstStatReturn>())
            {
                AstExpr* value = ret->list.data[0];
                rootSite.value = innermost(value)->location;
                rootSite.key = rootSite.value;
                rootSite.item = rootSite.value;
                rootSite.binding = bindingOf(value);
                rootSite.constant = constantOf(value);
                return evaluate(ret->list.data[0]);
            }
        }

        // checkLuwonSyntax reports a file that doesn't end in a return
        LUAU_UNREACHABLE();
    }

private:
    const CstNodeMap& cst;
    const std::vector<LuwonComment>& comments;
    std::string_view source;
    std::vector<size_t> lineOffsets;
    std::unordered_map<AstLocal*, LuwonValue> consts;
    // Where each const's value is written, for the consts declared with one.
    std::unordered_map<AstLocal*, Location> bindings;
    std::unordered_map<AstLocal*, LuwonConstant> constants;
    // The nesting depth of each table and object built so far, by the address of its contents.
    std::unordered_map<const void*, size_t> depths;
    size_t builtBytes = 0;

    LUAU_NORETURN void fail(const Location& location, std::string message)
    {
        throw LuwonEvaluationError{location, std::move(message)};
    }

    // Like code, `const a, b = 1, a` reads an outer `a`: every value is evaluated before any is bound.
    void bindLocal(AstStatLocal* local)
    {
        std::vector<LuwonValue> values;
        for (AstExpr* value : local->values)
            values.push_back(evaluate(value));

        // What's written about the declaration, for the places that use its names
        LuwonSite declaration;
        declaration.item = local->location;
        LuwonComments declarationComments = commentsOf(comments, declaration, source, lineOffsets);
        for (AstLocal* var : local->vars)
            constants[var] = LuwonConstant{var->name.value, local->location, declarationComments};

        for (size_t i = 0; i < local->vars.size; i++)
        {
            consts[local->vars.data[i]] = i < values.size() ? values[i] : LuwonValue{};
            // `const a = b` gives `a` the place `b` is written, so an edit through `a` changes `b`'s value
            if (i < local->values.size)
                bindings[local->vars.data[i]] = bindingOf(local->values.data[i]).value_or(innermost(local->values.data[i])->location);
        }
    }

    // The const `expr` names, when `expr` is a const's name.
    std::optional<LuwonConstant> constantOf(AstExpr* expr) const
    {
        const AstExprLocal* local = innermost(expr)->as<AstExprLocal>();
        if (!local)
            return std::nullopt;

        auto it = constants.find(local->local);
        if (it == constants.end())
            return std::nullopt;

        return it->second;
    }

    // Where the const `expr` names has its value written, when `expr` is a const's name.
    std::optional<Location> bindingOf(AstExpr* expr) const
    {
        const AstExprLocal* local = innermost(expr)->as<AstExprLocal>();
        if (!local)
            return std::nullopt;

        auto it = bindings.find(local->local);
        if (it == bindings.end())
            return std::nullopt;

        return it->second;
    }

    LuwonValue evaluate(AstExpr* expr)
    {
        if (expr->is<AstExprConstantNil>())
            return LuwonValue{};
        if (AstExprConstantBool* boolean = expr->as<AstExprConstantBool>())
            return makeBoolean(boolean->value);
        if (AstExprConstantNumber* number = expr->as<AstExprConstantNumber>())
            return makeNumber(number->value);
        if (AstExprConstantString* string = expr->as<AstExprConstantString>())
            return makeString(std::string(string->value.data, string->value.size));
        if (expr->is<AstExprGlobal>())
            return LuwonValue{LuwonValue::Kind::None};
        if (AstExprLocal* local = expr->as<AstExprLocal>())
            return readLocal(local);
        if (AstExprGroup* group = expr->as<AstExprGroup>())
            return evaluate(group->expr);
        if (AstExprTypeAssertion* assertion = expr->as<AstExprTypeAssertion>())
            return evaluate(assertion->expr);
        if (AstExprTable* table = expr->as<AstExprTable>())
            return evaluateTable(table);
        if (AstExprCall* call = expr->as<AstExprCall>())
            return evaluateCall(call);
        if (AstExprIndexName* index = expr->as<AstExprIndexName>())
            return readField(index->expr, makeString(std::string(index->index.value)), index->location);
        if (AstExprIndexExpr* index = expr->as<AstExprIndexExpr>())
            return readField(index->expr, evaluate(index->index), index->location);
        if (AstExprUnary* unary = expr->as<AstExprUnary>())
            return negate(evaluate(unary->expr), unary->location);
        if (AstExprBinary* binary = expr->as<AstExprBinary>())
            return evaluateBinary(binary);
        if (AstExprInterpString* interp = expr->as<AstExprInterpString>())
            return evaluateInterpString(interp);

        // checkLuwonSyntax reports every other expression
        LUAU_UNREACHABLE();
    }

    static LuwonValue makeBoolean(bool boolean)
    {
        LuwonValue value{LuwonValue::Kind::Boolean};
        value.boolean = boolean;
        return value;
    }

    static LuwonValue makeNumber(double number)
    {
        LuwonValue value{LuwonValue::Kind::Number};
        value.number = number;
        return value;
    }

    static LuwonValue makeString(std::string bytes)
    {
        LuwonValue value{LuwonValue::Kind::String};
        value.bytes = std::move(bytes);
        return value;
    }

    LuwonValue makeBuiltString(std::string bytes, const Location& location)
    {
        builtBytes += bytes.size();
        if (builtBytes > kLuwonMaxBuiltBytes)
            fail(location, format("Strings built in a data file can't add up to more than %zu bytes", kLuwonMaxBuiltBytes));

        return makeString(std::move(bytes));
    }

    LuwonValue readLocal(AstExprLocal* local)
    {
        auto it = consts.find(local->local);
        // The parser only resolves a local to a const declared above, and consts are the only bindings.
        LUAU_ASSERT(it != consts.end());
        if (it == consts.end())
            fail(local->location, format("'%s' isn't a const declared above", local->local->name.value));

        return it->second;
    }

    size_t depthOf(const LuwonValue& value) const
    {
        const void* identity = nullptr;
        if (value.kind == LuwonValue::Kind::Table)
            identity = value.table.get();
        else if (value.kind == LuwonValue::Kind::Object)
            identity = value.args.data();
        else
            return 0;

        auto it = depths.find(identity);
        return it == depths.end() ? 1 : it->second;
    }

    void recordDepth(size_t depth, const void* identity, const Location& location)
    {
        if (depth > kLuwonMaxDepth)
            fail(location, format("Tables in a data file can't nest more than %zu deep", kLuwonMaxDepth));

        depths[identity] = depth;
    }

    LuwonSite siteOf(AstExprTable* expr, size_t index)
    {
        const AstExprTable::Item& item = expr->items.data[index];

        LuwonSite site;
        site.value = innermost(item.value)->location;
        site.key = item.key ? item.key->location : site.value;
        site.item = Location(site.key, item.value->location);
        site.binding = bindingOf(item.value);
        site.constant = constantOf(item.value);
        site.shorthand = item.key && item.key->location == item.value->location;

        CstNode* const* cstNode = cst.find(expr);
        const CstExprTable* table = cstNode ? (*cstNode)->as<CstExprTable>() : nullptr;
        if (table && index < table->items.size)
        {
            const CstExprTable::Item& cstItem = table->items.data[index];
            if (item.kind == AstExprTable::Item::Kind::General)
                site.item.begin = cstItem.indexerOpenPosition;
            if (cstItem.separator != CstExprTable::Separator::Missing)
                site.separator = Location(cstItem.separatorPosition, 1);
        }

        return site;
    }

    LuwonValue evaluateTable(AstExprTable* expr)
    {
        std::shared_ptr<LuwonTable> table = std::make_shared<LuwonTable>();
        table->constructor = expr->location;
        size_t depth = 1;

        std::vector<std::pair<Location, LuwonValue>> numberKeys;

        for (size_t i = 0; i < expr->items.size; i++)
        {
            const AstExprTable::Item& item = expr->items.data[i];
            LuwonValue value = evaluate(item.value);
            depth = std::max(depth, depthOf(value) + 1);

            if (item.kind == AstExprTable::Item::Kind::List)
            {
                LuwonSite site = siteOf(expr, i);
                LuwonComments itemComments = commentsOf(comments, site, source, lineOffsets);
                table->array.push_back(LuwonEntry{LuwonValue{}, std::move(value), site, std::move(itemComments)});
                continue;
            }

            LUAU_ASSERT(item.key);
            LuwonValue key = evaluate(item.key);
            addEntry(*table, std::move(key), std::move(value), siteOf(expr, i), numberKeys);
        }

        // `{ [1] = "a", "b" }` writes index 1 twice. Positional items are only all known once the constructor ends.
        for (const auto& [location, key] : numberKeys)
        {
            if (arrayIndexOf(*table, key))
                fail(location, format("Index %s is already a positional item of this table", formatLuwonNumber(key.number).c_str()));
        }

        recordDepth(depth, table.get(), expr->location);

        LuwonValue result{LuwonValue::Kind::Table};
        result.table = std::move(table);
        return result;
    }

    void addEntry(
        LuwonTable& table,
        LuwonValue key,
        LuwonValue value,
        const LuwonSite& site,
        std::vector<std::pair<Location, LuwonValue>>& numberKeys
    )
    {
        if (!isLuwonKey(key))
        {
            if (key.kind == LuwonValue::Kind::Number)
                fail(site.key, "A table key can't be NaN");

            fail(site.key, format("A table key in a data file is a string, number or boolean, not a %s", luwonKindName(key.kind)));
        }

        if (key.kind == LuwonValue::Kind::Number)
            numberKeys.emplace_back(site.key, key);

        if (!table.add(std::move(key), std::move(value), site, commentsOf(comments, site, source, lineOffsets)))
            fail(site.key, "This key is already in the table");
    }

    LuwonValue readField(AstExpr* objectExpr, const LuwonValue& key, const Location& location)
    {
        LuwonValue object = evaluate(objectExpr);

        if (object.kind == LuwonValue::Kind::Vector)
            return readVectorComponent(object, key, location);

        if (object.kind != LuwonValue::Kind::Table)
            fail(location, format("Can't read a field from a %s", luwonKindName(object.kind)));

        const LuwonValue* value = object.table->find(key);
        return value ? *value : LuwonValue{};
    }

    LuwonValue readVectorComponent(const LuwonValue& vector, const LuwonValue& key, const Location& location)
    {
        static const char* kComponents = "xyzw";

        bool isComponent = key.kind == LuwonValue::Kind::String && key.bytes.size() == 1;
        const char* component = isComponent ? strchr(kComponents, tolower((unsigned char)key.bytes[0])) : nullptr;
        if (!component || *component == 0)
            fail(location, "A vector only has the fields 'x', 'y', 'z' and 'w'");

        return makeNumber(vector.vector[component - kComponents]);
    }

    LuwonValue evaluateCall(AstExprCall* call)
    {
        std::vector<LuwonValue> args;
        for (AstExpr* arg : call->args)
            args.push_back(evaluate(arg));

        switch (luwonConstructorOf(call))
        {
        case LuwonConstructor::VectorCreate:
            return createVector(call, args);
        case LuwonConstructor::BufferFromString:
            return createBuffer(call, args);
        case LuwonConstructor::Class:
            return createObject(call, std::move(args));
        case LuwonConstructor::None:
            break;
        }

        // checkLuwonSyntax reports every other call
        LUAU_UNREACHABLE();
    }

    LuwonValue createVector(AstExprCall* call, const std::vector<LuwonValue>& args)
    {
        if (args.size() != 3 && args.size() != 4)
            fail(call->location, format("vector.create takes 3 or 4 numbers, but got %zu", args.size()));

        LuwonValue result{LuwonValue::Kind::Vector};
        for (size_t i = 0; i < args.size(); i++)
        {
            if (args[i].kind != LuwonValue::Kind::Number)
                fail(call->args.data[i]->location, format("vector.create takes numbers, but argument %zu is a %s", i + 1, luwonKindName(args[i].kind)));

            result.vector[i] = float(args[i].number);
        }

        return result;
    }

    LuwonValue createBuffer(AstExprCall* call, const std::vector<LuwonValue>& args)
    {
        if (args.size() != 1 || args[0].kind != LuwonValue::Kind::String)
            fail(call->location, "buffer.fromstring takes one string");

        LuwonValue result{LuwonValue::Kind::Buffer};
        result.bytes = args[0].bytes;
        return result;
    }

    // Whether the file may construct the class is up to whoever builds runtime values from it.
    LuwonValue createObject(AstExprCall* call, std::vector<LuwonValue> args)
    {
        size_t depth = 1;
        for (const LuwonValue& arg : args)
            depth = std::max(depth, depthOf(arg) + 1);

        LuwonValue result{LuwonValue::Kind::Object};
        result.bytes = luwonClassNameOf(call);
        result.args = std::move(args);
        recordDepth(depth, result.args.data(), call->location);
        return result;
    }

    LuwonValue negate(const LuwonValue& operand, const Location& location)
    {
        if (operand.kind == LuwonValue::Kind::Number)
            return makeNumber(-operand.number);

        if (operand.kind == LuwonValue::Kind::Vector)
        {
            LuwonValue result = operand;
            for (float& component : result.vector)
                component = -component;
            return result;
        }

        fail(location, format("'-' needs a number or a vector, but got a %s", luwonKindName(operand.kind)));
    }

    LuwonValue evaluateBinary(AstExprBinary* binary)
    {
        LuwonValue left = evaluate(binary->left);
        LuwonValue right = evaluate(binary->right);

        if (binary->op == AstExprBinary::Concat)
        {
            std::string text = toText(left, "'..'", binary->left->location) + toText(right, "'..'", binary->right->location);
            return makeBuiltString(std::move(text), binary->location);
        }

        return arithmetic(binary->op, left, right, binary->location);
    }

    static double numberArithmetic(AstExprBinary::Op op, double a, double b)
    {
        // The VM's luai_num* macros (`lnumutils.h`)
        switch (op)
        {
        case AstExprBinary::Add:
            return a + b;
        case AstExprBinary::Sub:
            return a - b;
        case AstExprBinary::Mul:
            return a * b;
        case AstExprBinary::Div:
            return a / b;
        case AstExprBinary::FloorDiv:
            return floor(a / b);
        case AstExprBinary::Mod:
            return a - floor(a / b) * b;
        case AstExprBinary::Pow:
            return pow(a, b);
        default:
            LUAU_UNREACHABLE();
        }
    }

    LuwonValue arithmetic(AstExprBinary::Op op, const LuwonValue& left, const LuwonValue& right, const Location& location)
    {
        bool leftNumber = left.kind == LuwonValue::Kind::Number;
        bool rightNumber = right.kind == LuwonValue::Kind::Number;

        if (leftNumber && rightNumber)
            return makeNumber(numberArithmetic(op, left.number, right.number));

        if (std::optional<LuwonValue> vector = vectorArithmetic(op, left, right))
            return *vector;

        fail(
            location,
            format("'%s' needs numbers, but got a %s and a %s", toString(op).c_str(), luwonKindName(left.kind), luwonKindName(right.kind))
        );
    }

    // The VM's vector arithmetic: `+` and `-` between vectors, and `*`, `/` and `//` between vectors or with a number on
    // either side. Returns nullopt for any other mix.
    static std::optional<LuwonValue> vectorArithmetic(AstExprBinary::Op op, const LuwonValue& left, const LuwonValue& right)
    {
        bool leftVector = left.kind == LuwonValue::Kind::Vector;
        bool rightVector = right.kind == LuwonValue::Kind::Vector;
        bool leftScalar = left.kind == LuwonValue::Kind::Number;
        bool rightScalar = right.kind == LuwonValue::Kind::Number;

        bool scales = op == AstExprBinary::Mul || op == AstExprBinary::Div || op == AstExprBinary::FloorDiv;
        bool adds = op == AstExprBinary::Add || op == AstExprBinary::Sub;
        bool bothVectors = leftVector && rightVector;
        bool vectorAndScalar = (leftVector && rightScalar) || (leftScalar && rightVector);

        if (!(bothVectors && (adds || scales)) && !(vectorAndScalar && scales))
            return std::nullopt;

        LuwonValue result{LuwonValue::Kind::Vector};
        for (int i = 0; i < 4; i++)
        {
            double a = leftVector ? left.vector[i] : left.number;
            double b = rightVector ? right.vector[i] : right.number;
            result.vector[i] = float(numberArithmetic(op, float(a), float(b)));
        }

        return result;
    }

    // A string or number as the text `..` and interpolation give it.
    std::string toText(const LuwonValue& value, const char* what, const Location& location)
    {
        if (value.kind == LuwonValue::Kind::String)
            return value.bytes;
        if (value.kind == LuwonValue::Kind::Number)
            return formatLuwonNumber(value.number);

        fail(location, format("%s needs strings or numbers, but got a %s", what, luwonKindName(value.kind)));
    }

    LuwonValue evaluateInterpString(AstExprInterpString* interp)
    {
        std::string text;
        for (size_t i = 0; i < interp->strings.size; i++)
        {
            text.append(interp->strings.data[i].data, interp->strings.data[i].size);

            if (i < interp->expressions.size)
            {
                AstExpr* part = interp->expressions.data[i];
                text += toText(evaluate(part), "An interpolated string", part->location);
            }
        }

        return makeBuiltString(std::move(text), interp->location);
    }
};

} // namespace

std::vector<size_t> luwonLineOffsets(std::string_view source)
{
    std::vector<size_t> offsets{0};
    for (size_t i = 0; i < source.size(); i++)
    {
        if (source[i] == '\n')
            offsets.push_back(i + 1);
    }

    return offsets;
}

size_t luwonOffsetOf(const std::vector<size_t>& lineOffsets, std::string_view source, const Position& position)
{
    if (position.line >= lineOffsets.size())
        return source.size();

    return std::min(lineOffsets[position.line] + position.column, source.size());
}

LuwonDocument readLuwon(std::string_view source)
{
    Allocator allocator;
    AstNameTable names(allocator);

    ParseOptions options;
    options.luwon = true;
    options.captureComments = true;
    options.storeCstData = true;

    ParseResult parsed = Parser::parse(source.data(), source.size(), names, allocator, options);

    LuwonDocument document;
    for (const ParseError& error : parsed.errors)
        document.errors.push_back({error.getLocation(), error.getMessage()});

    if (!document.errors.empty() || !parsed.root)
        return document;

    std::vector<size_t> lineOffsets = luwonLineOffsets(source);
    for (const Comment& comment : parsed.commentLocations)
    {
        size_t begin = luwonOffsetOf(lineOffsets, source, comment.location.begin);
        size_t end = luwonOffsetOf(lineOffsets, source, comment.location.end);
        document.comments.push_back({std::string(source.substr(begin, end - begin)), comment.location});
    }

    try
    {
        document.value = LuwonEvaluator(parsed.cstNodeMap, document.comments, source).evaluateChunk(parsed.root, document.root);
    }
    catch (const LuwonEvaluationError& error)
    {
        document.errors.push_back({error.location, error.message});
    }

    return document;
}

} // namespace Luau

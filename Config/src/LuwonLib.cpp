// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Luwon.h"

#include "luwon.h"

#include "Luau/LuwonSyntax.h"
#include "Luau/StringUtils.h"

#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <string.h>

LUAU_FASTFLAG(LuwuNonePrimitive)

// Luwu object notation: the `luwon` library, and `luwon_load` for `require`. Values cross between Lua and the
// notation's value tree here.
//
// A table read from a file carries what the file said about it in a metatable luwon owns, so the comments and locations
// go wherever the table goes, and code can set comments on a table it built itself. The metatable has no metamethods,
// so the table behaves like any other:
//
//     {
//         __luwon = true,
//         location = Location,          -- the table's constructor, from `{` to `}`
//         fields = { [key] = Info },    -- positional items under their index
//         source = string,              -- only on the table a file returns: the file's text
//         comments = { Comment },       -- only on the table a file returns: every comment in the file
//     }
//
//     Info = { above = { Comment }, after = Comment?, location = Location?, keylocation = Location? }
//     Comment = { text = string, location = Location? }
//     Location = { begin = vector, end = vector }   -- line, column (both from 1) and byte offset, as `ast` gives them

namespace Luau
{

// Calls what the caller was given (a class, an `args` function) on the stack. A Lua error becomes `error` instead of
// unwinding through C++ frames that hold the stack.
static bool callProtected(lua_State* L, int nargs, int nresults, std::optional<std::string>& error)
{
    if (lua_pcall(L, nargs, nresults, 0) == LUA_OK)
        return true;

    const char* message = lua_tostring(L, -1);
    error = message ? message : "an error that isn't a string";
    lua_pop(L, 1);
    return false;
}

// Pushes a table key, which is a string, number or boolean.
static void pushKey(lua_State* L, const LuwonValue& key)
{
    if (key.kind == LuwonValue::Kind::String)
        lua_pushlstring(L, key.bytes.data(), key.bytes.size());
    else if (key.kind == LuwonValue::Kind::Number)
        lua_pushnumber(L, key.number);
    else
        lua_pushboolean(L, key.boolean);
}

// A class's entry in the `classes` table `luwon.decode` and `luwon.encode` take: the class itself, or
// `{ class = Point, args = function(point) return point.x, point.y end }` to write instances too.
static void pushEntryClass(lua_State* L, int entry)
{
    if (lua_istable(L, entry))
        lua_getfield(L, entry, "class");
    else
        lua_pushvalue(L, entry);
}

// The builtin class a data file names `library.name`, if there is one.
static std::optional<LuwonBuiltinClass> builtinNamed(const std::string& name)
{
    for (const LuwonBuiltinClass& builtin : luwonBuiltinClasses())
    {
        if (name == std::string(builtin.library) + "." + builtin.name)
            return builtin;
    }

    return std::nullopt;
}

// Pushes a builtin class from the registry's `_LOADED` copy of its library, which a global of the same name can't
// replace. Pushes nil when the library isn't open.
static void pushBuiltinClass(lua_State* L, const LuwonBuiltinClass& builtin)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, builtin.library);
    lua_remove(L, -2);

    if (lua_istable(L, -1))
        lua_getfield(L, -1, builtin.name);
    else
        lua_pushnil(L);
    lua_remove(L, -2);
}

// A position as the `ast` library pushes one: a vector of its line, its column (both from 1) and its byte offset.
static void pushPosition(lua_State* L, const std::vector<size_t>& lineOffsets, std::string_view source, const Position& position)
{
    float offset = float(luwonOffsetOf(lineOffsets, source, position));
#if LUA_VECTOR_SIZE == 4
    lua_pushvector(L, float(position.line + 1), float(position.column + 1), offset, 0.0f);
#else
    lua_pushvector(L, float(position.line + 1), float(position.column + 1), offset);
#endif
}

static void pushLocation(lua_State* L, const std::vector<size_t>& lineOffsets, std::string_view source, const Location& location)
{
    lua_createtable(L, 0, 2);
    pushPosition(L, lineOffsets, source, location.begin);
    lua_setfield(L, -2, "begin");
    pushPosition(L, lineOffsets, source, location.end);
    lua_setfield(L, -2, "end");
    lua_setreadonly(L, -1, true);
}

// Pushes the luwon metatable of the table at `idx` and returns true, or returns false with nothing pushed when the table
// has none. Sets `foreign` when the table has a metatable that isn't luwon's.
static bool pushLuwonMetatable(lua_State* L, int idx, bool& foreign)
{
    foreign = false;
    if (!lua_getmetatable(L, idx))
        return false;

    lua_rawgetfield(L, -1, "__luwon");
    bool isLuwon = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    if (!isLuwon)
    {
        lua_pop(L, 1);
        foreign = true;
        return false;
    }

    return true;
}

namespace
{

// Where a file's values are written, for the metatables of the tables read from it.
struct LuwonOrigin
{
    std::string_view source;
    std::vector<size_t> lineOffsets;
};

class LuwonPusher
{
public:
    // `classes` is the stack index of the table naming the classes the file may construct, or 0 for none. With `origin`,
    // tables read from the file get metatables holding their comments and locations.
    LuwonPusher(lua_State* L, int classes, const LuwonOrigin* origin)
        : L(L)
        , classes(classes)
        , origin(origin)
    {
        lua_newtable(L);
        cache = lua_gettop(L);
    }

    ~LuwonPusher()
    {
        lua_remove(L, cache);
    }

    // Pushes `value`, or sets `error` and returns false. The stack holds partial results then.
    bool push(const LuwonValue& value)
    {
        switch (value.kind)
        {
        case LuwonValue::Kind::Nil:
            lua_pushnil(L);
            return true;
        case LuwonValue::Kind::None:
            if (!FFlag::LuwuNonePrimitive)
                return fail("'none' isn't available in this runtime (FFlag LuwuNonePrimitive)");
            lua_pushsymnone(L);
            return true;
        case LuwonValue::Kind::Boolean:
            lua_pushboolean(L, value.boolean);
            return true;
        case LuwonValue::Kind::Number:
            lua_pushnumber(L, value.number);
            return true;
        case LuwonValue::Kind::String:
            lua_pushlstring(L, value.bytes.data(), value.bytes.size());
            return true;
        case LuwonValue::Kind::Vector:
#if LUA_VECTOR_SIZE == 4
            lua_pushvector(L, value.vector[0], value.vector[1], value.vector[2], value.vector[3]);
#else
            lua_pushvector(L, value.vector[0], value.vector[1], value.vector[2]);
#endif
            return true;
        case LuwonValue::Kind::Buffer:
        {
            void* data = lua_newbuffer(L, value.bytes.size());
            if (!value.bytes.empty())
                memcpy(data, value.bytes.data(), value.bytes.size());
            return true;
        }
        case LuwonValue::Kind::Table:
            return pushTable(*value.table);
        case LuwonValue::Kind::Object:
            return pushObject(value);
        }

        LUAU_UNREACHABLE();
    }

    // Puts the file's source and comments on the metatable of the table on top of the stack, when it has one.
    void markRoot(const std::vector<LuwonComment>& comments)
    {
        bool foreign = false;
        if (!origin || !lua_istable(L, -1) || !pushLuwonMetatable(L, -1, foreign))
            return;

        lua_pushlstring(L, origin->source.data(), origin->source.size());
        lua_setfield(L, -2, "source");

        lua_createtable(L, int(comments.size()), 0);
        for (size_t i = 0; i < comments.size(); i++)
        {
            pushComment(comments[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        lua_setfield(L, -2, "comments");

        lua_pop(L, 1);
    }

    std::optional<std::string> error;

private:
    lua_State* L;
    int classes = 0;
    const LuwonOrigin* origin = nullptr;
    int cache = 0;

    bool fail(std::string message)
    {
        error = std::move(message);
        return false;
    }

    void pushLocationOf(const Location& location)
    {
        pushLocation(L, origin->lineOffsets, origin->source, location);
    }

    void pushComment(const LuwonComment& comment)
    {
        lua_createtable(L, 0, 2);
        lua_pushlstring(L, comment.text.data(), comment.text.size());
        lua_setfield(L, -2, "text");
        if (comment.location)
        {
            pushLocationOf(*comment.location);
            lua_setfield(L, -2, "location");
        }
    }

    // `{ above, after?, location, keylocation? }` for an item of a table read from the file.
    void pushInfo(const LuwonEntry& entry, bool keyed)
    {
        lua_createtable(L, 0, 4);

        const std::vector<LuwonComment>& above = entry.comments ? entry.comments->above : std::vector<LuwonComment>{};
        lua_createtable(L, int(above.size()), 0);
        for (size_t i = 0; i < above.size(); i++)
        {
            pushComment(above[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        lua_setfield(L, -2, "above");

        if (entry.comments && entry.comments->after)
        {
            pushComment(*entry.comments->after);
            lua_setfield(L, -2, "after");
        }

        if (entry.site)
        {
            pushLocationOf(entry.site->value);
            lua_setfield(L, -2, "location");
            if (keyed)
            {
                pushLocationOf(entry.site->key);
                lua_setfield(L, -2, "keylocation");
            }
        }
    }

    // Sets the luwon metatable of the table on top of the stack.
    void setMetatable(const LuwonTable& table)
    {
        lua_createtable(L, 0, 3);
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "__luwon");

        pushLocationOf(*table.constructor);
        lua_setfield(L, -2, "location");

        lua_createtable(L, int(table.array.size()), int(table.entries.size()));
        for (size_t i = 0; i < table.array.size(); i++)
        {
            pushInfo(table.array[i], false);
            lua_rawseti(L, -2, int(i + 1));
        }
        for (const LuwonEntry& entry : table.entries)
        {
            pushKey(L, entry.key);
            pushInfo(entry, true);
            lua_rawset(L, -3);
        }
        lua_setfield(L, -2, "fields");

        lua_setmetatable(L, -2);
    }

    bool pushTable(const LuwonTable& table)
    {
        lua_pushlightuserdata(L, const_cast<LuwonTable*>(&table));
        if (lua_rawget(L, cache) != LUA_TNIL)
            return true;
        lua_pop(L, 1);

        lua_createtable(L, int(table.array.size()), int(table.entries.size()));

        for (size_t i = 0; i < table.array.size(); i++)
        {
            if (!push(table.array[i].value))
                return false;
            lua_rawseti(L, -2, int(i + 1));
        }

        for (const LuwonEntry& entry : table.entries)
        {
            pushKey(L, entry.key);
            if (!push(entry.value))
                return false;
            lua_rawset(L, -3);
        }

        if (origin && table.constructor)
            setMetatable(table);

        lua_pushlightuserdata(L, const_cast<LuwonTable*>(&table));
        lua_pushvalue(L, -2);
        lua_rawset(L, cache);
        return true;
    }

    bool pushBuiltinObject(const LuwonBuiltinClass& builtin, const LuwonValue& object)
    {
        if (object.args.size() != builtin.fields.size())
            return fail(format("%s takes %zu values, but got %zu", object.bytes.c_str(), builtin.fields.size(), object.args.size()));

        pushBuiltinClass(L, builtin);
        if (lua_type(L, -1) != LUA_TCLASS)
            return fail(format("%s isn't available in this runtime", object.bytes.c_str()));

        return pushArgsAndCall(object);
    }

    bool pushArgsAndCall(const LuwonValue& object)
    {
        for (const LuwonValue& arg : object.args)
        {
            if (!push(arg))
                return false;
        }

        return callProtected(L, int(object.args.size()), 1, error);
    }

    // Calls the class the reader allows under the object's name with its arguments. The call runs the class's own
    // constructor: only classes the reader names are ever built.
    bool pushObject(const LuwonValue& object)
    {
        if (std::optional<LuwonBuiltinClass> builtin = builtinNamed(object.bytes))
            return pushBuiltinObject(*builtin, object);

        bool allowed = false;
        if (classes != 0)
        {
            lua_getfield(L, classes, object.bytes.c_str());
            if (!lua_isnil(L, -1))
            {
                pushEntryClass(L, lua_gettop(L));
                lua_remove(L, -2);
                allowed = lua_type(L, -1) == LUA_TCLASS;
            }
        }

        if (!allowed)
            return fail(format("'%s' isn't a class this file may construct; pass it in luwon.decode's classes", object.bytes.c_str()));

        return pushArgsAndCall(object);
    }
};

// The order keys of a table written from Lua take, so the text doesn't depend on hash order: numbers, then strings,
// then booleans.
int keyRank(LuwonValue::Kind kind)
{
    if (kind == LuwonValue::Kind::Number)
        return 0;
    if (kind == LuwonValue::Kind::String)
        return 1;
    return 2;
}

bool keyBefore(const LuwonValue& a, const LuwonValue& b)
{
    if (a.kind != b.kind)
        return keyRank(a.kind) < keyRank(b.kind);

    if (a.kind == LuwonValue::Kind::Number)
        return a.number < b.number;
    if (a.kind == LuwonValue::Kind::String)
        return a.bytes < b.bytes;

    return !a.boolean && b.boolean;
}

// A comment's text as a file holds it: a string that doesn't start with `--` becomes a line comment.
std::string commentText(const char* text, size_t length)
{
    std::string result(text, length);
    if (result.rfind("--", 0) == 0)
        return result;

    return "-- " + result;
}

// Reads one comment, a string or a `{ text }` table, from the stack. Returns false when it's neither.
bool readComment(lua_State* L, int idx, LuwonComment& out)
{
    idx = lua_absindex(L, idx);
    if (lua_istable(L, idx))
    {
        lua_rawgetfield(L, idx, "text");
        bool isString = lua_type(L, -1) == LUA_TSTRING;
        if (isString)
        {
            size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            out.text = commentText(text, length);
        }
        lua_pop(L, 1);
        return isString;
    }

    if (lua_type(L, idx) != LUA_TSTRING)
        return false;

    size_t length = 0;
    const char* text = lua_tolstring(L, idx, &length);
    out.text = commentText(text, length);
    return true;
}

// Reads an Info table (see the top of this file) into the comments it sets. Returns false when its shape is wrong.
bool readInfo(lua_State* L, int idx, LuwonComments& out)
{
    idx = lua_absindex(L, idx);

    lua_rawgetfield(L, idx, "above");
    if (lua_istable(L, -1))
    {
        int above = lua_gettop(L);
        for (int i = 1;; i++)
        {
            lua_rawgeti(L, above, i);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                break;
            }

            LuwonComment comment;
            bool read = readComment(L, -1, comment);
            lua_pop(L, 1);
            if (!read)
            {
                lua_pop(L, 1);
                return false;
            }
            out.above.push_back(std::move(comment));
        }
    }
    else if (!lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);

    lua_rawgetfield(L, idx, "after");
    bool afterOk = true;
    if (!lua_isnil(L, -1))
    {
        LuwonComment comment;
        afterOk = readComment(L, -1, comment) && comment.text.find('\n') == std::string::npos;
        if (afterOk)
            out.after = std::move(comment);
    }
    lua_pop(L, 1);

    return afterOk;
}

// Converts a Lua value to the notation's value tree, for writing.
class LuwonFromLua
{
public:
    LuwonFromLua(lua_State* L, int classes)
        : L(L)
        , classes(classes)
    {
    }

    // Converts the value at `idx`, or sets `error` and returns false.
    bool convert(int idx, LuwonValue& out, const std::string& path)
    {
        if (depth >= kLuwonMaxDepth)
            return fail(format("tables nest more than %zu deep at %s", kLuwonMaxDepth, path.c_str()));

        // Each level holds a table's metatable and fields, a key, a value and a call's results at most
        if (!lua_checkstack(L, 8))
            return fail("stack overflow");

        idx = lua_absindex(L, idx);
        switch (lua_type(L, idx))
        {
        case LUA_TNIL:
            out = LuwonValue{};
            return true;
        case LUA_TSYMNONE:
            out = LuwonValue{LuwonValue::Kind::None};
            return true;
        case LUA_TBOOLEAN:
            out = LuwonValue{LuwonValue::Kind::Boolean};
            out.boolean = lua_toboolean(L, idx);
            return true;
        case LUA_TNUMBER:
            out = LuwonValue{LuwonValue::Kind::Number};
            out.number = lua_tonumber(L, idx);
            return true;
        case LUA_TSTRING:
        {
            size_t length = 0;
            const char* data = lua_tolstring(L, idx, &length);
            out = LuwonValue{LuwonValue::Kind::String};
            out.bytes.assign(data, length);
            return true;
        }
        case LUA_TVECTOR:
        {
            const float* vector = lua_tovector(L, idx);
            out = LuwonValue{LuwonValue::Kind::Vector};
            for (int i = 0; i < LUA_VECTOR_SIZE; i++)
                out.vector[i] = vector[i];
            return true;
        }
        case LUA_TBUFFER:
        {
            size_t length = 0;
            const char* data = static_cast<const char*>(lua_tobuffer(L, idx, &length));
            out = LuwonValue{LuwonValue::Kind::Buffer};
            out.bytes.assign(data, length);
            return true;
        }
        case LUA_TTABLE:
            return convertTable(idx, out, path);
        case LUA_TOBJECT:
            return convertObject(idx, out, path);
        default:
            return fail(format("a data file can't hold a %s, at %s", luaL_typename(L, idx), path.c_str()));
        }
    }

    std::optional<std::string> error;

private:
    lua_State* L;
    int classes = 0;
    size_t depth = 0;
    // Tables being converted (one met again is a cycle), and tables already converted (one met again stays shared).
    std::vector<const void*> inProgress;
    std::unordered_map<const void*, std::shared_ptr<const LuwonTable>> done;

    bool fail(std::string message)
    {
        error = std::move(message);
        return false;
    }

    bool convertTable(int idx, LuwonValue& out, const std::string& path)
    {
        const void* identity = lua_topointer(L, idx);
        out = LuwonValue{LuwonValue::Kind::Table};

        if (auto it = done.find(identity); it != done.end())
        {
            out.table = it->second;
            return true;
        }

        if (std::find(inProgress.begin(), inProgress.end(), identity) != inProgress.end())
            return fail(format("a data file can't hold a table that contains itself, at %s", path.c_str()));

        // The comments the table carries, when it has a luwon metatable
        bool foreign = false;
        int top = lua_gettop(L);
        int fields = 0;
        if (pushLuwonMetatable(L, idx, foreign))
        {
            lua_rawgetfield(L, -1, "fields");
            if (lua_istable(L, -1))
                fields = lua_gettop(L);
        }
        else if (foreign)
            return fail(format("a data file can't hold a table with a metatable, at %s", path.c_str()));

        inProgress.push_back(identity);
        depth++;
        std::shared_ptr<LuwonTable> table = std::make_shared<LuwonTable>();
        bool converted = convertItems(idx, fields, *table, path) && convertEntries(idx, fields, *table, path);
        depth--;
        inProgress.pop_back();
        lua_settop(L, top);

        if (!converted)
            return false;

        done[identity] = table;
        out.table = std::move(table);
        return true;
    }

    // The comments `fields` (a stack index, or 0) holds for the key on top of the stack, which it pops.
    bool commentsFor(int fields, std::optional<LuwonComments>& out, const std::string& path)
    {
        if (fields == 0)
        {
            lua_pop(L, 1);
            return true;
        }

        lua_rawget(L, fields);
        bool ok = true;
        if (lua_istable(L, -1))
        {
            LuwonComments comments;
            ok = readInfo(L, -1, comments);
            if (ok)
                out = std::move(comments);
        }
        lua_pop(L, 1);

        if (!ok)
            return fail(format("the comments luwon keeps for %s aren't { above = { string }, after = string? }", path.c_str()));

        return true;
    }

    // The positional items: 1, 2, ... up to the first index that's absent.
    bool convertItems(int idx, int fields, LuwonTable& table, const std::string& path)
    {
        for (int i = 1;; i++)
        {
            lua_rawgeti(L, idx, i);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                return true;
            }

            std::string itemPath = format("%s[%d]", path.c_str(), i);
            LuwonValue value;
            bool converted = convert(-1, value, itemPath);
            lua_pop(L, 1);
            if (!converted)
                return false;

            std::optional<LuwonComments> comments;
            lua_pushinteger(L, i);
            if (!commentsFor(fields, comments, itemPath))
                return false;

            table.array.push_back(LuwonEntry{LuwonValue{}, std::move(value), std::nullopt, std::move(comments)});
        }
    }

    struct ConvertedEntry
    {
        LuwonValue key;
        LuwonValue value;
        std::optional<LuwonComments> comments;
    };

    bool convertEntries(int idx, int fields, LuwonTable& table, const std::string& path)
    {
        std::vector<ConvertedEntry> entries;

        lua_pushnil(L);
        while (lua_next(L, idx))
        {
            // Positional items are already converted
            bool positional = lua_type(L, -2) == LUA_TNUMBER && isArrayIndex(lua_tonumber(L, -2), table.array.size());
            if (positional)
            {
                lua_pop(L, 1);
                continue;
            }

            ConvertedEntry entry;
            bool converted = convertKey(-2, entry.key, path) && convert(-1, entry.value, path + keyPath(entry.key));
            lua_pop(L, 1);
            if (converted)
            {
                lua_pushvalue(L, -1);
                converted = commentsFor(fields, entry.comments, path + keyPath(entry.key));
            }

            if (!converted)
            {
                lua_pop(L, 1);
                return false;
            }

            entries.push_back(std::move(entry));
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const ConvertedEntry& a, const ConvertedEntry& b)
            {
                return keyBefore(a.key, b.key);
            }
        );

        for (ConvertedEntry& entry : entries)
            table.add(std::move(entry.key), std::move(entry.value), std::nullopt, std::move(entry.comments));

        return true;
    }

    static bool isArrayIndex(double key, size_t size)
    {
        return key >= 1.0 && key <= double(size) && double(size_t(key)) == key;
    }

    bool convertKey(int idx, LuwonValue& key, const std::string& path)
    {
        int type = lua_type(L, idx);
        if (type != LUA_TSTRING && type != LUA_TNUMBER && type != LUA_TBOOLEAN)
            return fail(format("a table key in a data file is a string, number or boolean, not a %s, at %s", luaL_typename(L, idx), path.c_str()));

        // A string key converts in place; lua_tolstring on a number key would turn it into a string and break lua_next
        lua_pushvalue(L, idx);
        bool converted = convert(-1, key, path);
        lua_pop(L, 1);
        return converted;
    }

    static std::string keyPath(const LuwonValue& key)
    {
        if (key.kind == LuwonValue::Kind::String)
            return "." + key.bytes;
        if (key.kind == LuwonValue::Kind::Number)
            return "[" + formatLuwonNumber(key.number) + "]";

        return key.boolean ? "[true]" : "[false]";
    }

    // An object of a class the writer is given, as its constructor call with the arguments the class's `args` returns.
    bool convertObject(int idx, LuwonValue& out, const std::string& path)
    {
        if (std::optional<bool> converted = convertBuiltinObject(idx, out, path))
            return *converted;

        std::optional<std::string> name = findClassEntry(idx);
        if (error)
            return false;
        if (!name)
            return fail(format("a data file can't hold an object of class '%s' unless luwon.encode's classes has it, at %s", lua_getclassname(L, idx), path.c_str()));

        // The entry is on the stack. A class on its own only lets a file construct it.
        bool hasArgs = false;
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "args");
            hasArgs = lua_isfunction(L, -1);
            lua_remove(L, -2);
        }

        if (!hasArgs)
        {
            lua_pop(L, 1);
            return fail(format("'%s' needs an 'args' function in classes to be written: { class = %s, args = function(object) ... end }", name->c_str(), name->c_str()));
        }

        int top = lua_gettop(L) - 1;
        lua_pushvalue(L, idx);
        if (!callProtected(L, 1, LUA_MULTRET, error))
            return false;

        out = LuwonValue{LuwonValue::Kind::Object};
        out.bytes = *name;
        depth++;
        for (int i = top + 1; i <= lua_gettop(L); i++)
        {
            LuwonValue arg;
            if (!convert(i, arg, format("%s(argument %d)", path.c_str(), i - top)))
            {
                depth--;
                lua_settop(L, top);
                return false;
            }
            out.args.push_back(std::move(arg));
        }
        depth--;

        lua_settop(L, top);
        return true;
    }

    // An instance of a builtin class, as its constructor call with its fields. Returns nullopt for any other object.
    std::optional<bool> convertBuiltinObject(int idx, LuwonValue& out, const std::string& path)
    {
        std::vector<LuwonBuiltinClass> builtins = luwonBuiltinClasses();
        if (builtins.empty())
            return std::nullopt;
        if (!pushClassOf(idx))
            return error ? std::optional<bool>(false) : std::nullopt;

        for (const LuwonBuiltinClass& builtin : builtins)
        {
            pushBuiltinClass(L, builtin);
            bool matches = lua_rawequal(L, -1, -2);
            lua_pop(L, 1);
            if (!matches)
                continue;

            lua_pop(L, 1);
            out = LuwonValue{LuwonValue::Kind::Object};
            out.bytes = std::string(builtin.library) + "." + builtin.name;
            return convertFields(idx, builtin, out, path);
        }

        lua_pop(L, 1);
        return std::nullopt;
    }

    bool convertFields(int idx, const LuwonBuiltinClass& builtin, LuwonValue& out, const std::string& path)
    {
        depth++;
        for (const char* field : builtin.fields)
        {
            lua_getfield(L, idx, field);
            LuwonValue arg;
            bool converted = convert(-1, arg, path + "." + field);
            lua_pop(L, 1);
            if (!converted)
            {
                depth--;
                return false;
            }
            out.args.push_back(std::move(arg));
        }
        depth--;

        return true;
    }

    // Finds the entry of `classes` for the object's class, and leaves it on the stack. Returns its name, or nullopt with
    // nothing pushed.
    std::optional<std::string> findClassEntry(int object)
    {
        if (classes == 0 || !pushClassOf(object))
            return std::nullopt;

        int objectClass = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, classes))
        {
            pushEntryClass(L, lua_gettop(L));
            bool matches = lua_rawequal(L, -1, objectClass) && lua_type(L, -3) == LUA_TSTRING;
            lua_pop(L, 1);

            if (matches)
            {
                std::string name = lua_tostring(L, -2);
                lua_remove(L, -2);
                lua_remove(L, objectClass);
                return name;
            }

            lua_pop(L, 1);
        }

        lua_pop(L, 1);
        return std::nullopt;
    }

    // Pushes the object's class through `class.of`, read from the registry so a global named `class` can't change it.
    bool pushClassOf(int object)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
        lua_getfield(L, -1, "class");
        lua_remove(L, -2);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }

        lua_getfield(L, -1, "of");
        lua_remove(L, -2);
        lua_pushvalue(L, object);
        return callProtected(L, 1, 1, error);
    }
};

// The chunk's name as error messages show it, without the '@' or '=' prefix.
std::string displayName(const char* chunkname)
{
    if (chunkname[0] == '@' || chunkname[0] == '=')
        return chunkname + 1;
    return chunkname;
}

// `classes`, the optional table of classes, as a stack index, or 0 when it's absent.
int classesArgument(lua_State* L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return 0;

    luaL_checktype(L, idx, LUA_TTABLE);
    return idx;
}

// Reads `source` and pushes its value, with metatables carrying its comments and locations. Returns false with `error`
// set when it doesn't read or the value can't be built.
bool readAndPush(lua_State* L, std::string_view source, int classes, std::optional<std::string>& error, std::optional<LuwonError>& located)
{
    LuwonDocument document = readLuwon(source);
    if (!document.errors.empty())
    {
        located = document.errors.front();
        return false;
    }

    // Each nested table holds a table, its metatable, a key and a value on the stack while it's built
    if (!lua_checkstack(L, int(kLuwonMaxDepth * 6 + 16)))
    {
        error = "stack overflow loading a data file";
        return false;
    }

    LuwonOrigin origin{source, luwonLineOffsets(source)};
    LuwonPusher pusher(L, classes, &origin);
    if (!pusher.push(*document.value))
    {
        error = pusher.error;
        return false;
    }

    pusher.markRoot(document.comments);
    return true;
}

// luwon.decode(text, classes?): the value a data file returns
int luwonDecode(lua_State* L)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    int classes = classesArgument(L, 2);
    int top = lua_gettop(L);

    std::optional<std::string> error;
    {
        std::optional<LuwonError> located;
        if (!readAndPush(L, std::string_view(text, length), classes, error, located) && located)
            error = format("line %d: %s", located->location.begin.line + 1, located->message.c_str());
    }

    if (error)
    {
        lua_settop(L, top);
        luaL_error(L, "luwon.decode: %s", error->c_str());
    }

    return 1;
}

// luwon.encode(value, classes?): the text of a data file. A table a file returned is written back into that file's
// source, keeping its comments, consts, types and layout; anything else is written anew. Comments tables carry are
// written either way.
int luwonEncode(lua_State* L)
{
    luaL_checkany(L, 1);
    int classes = classesArgument(L, 2);

    // The source a returned table was read from
    std::optional<std::string> source;
    bool foreign = false;
    if (lua_istable(L, 1) && pushLuwonMetatable(L, 1, foreign))
    {
        lua_rawgetfield(L, -1, "source");
        if (lua_type(L, -1) == LUA_TSTRING)
        {
            size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            source.emplace(text, length);
        }
        lua_pop(L, 2);
    }

    std::optional<std::string> error;
    {
        LuwonFromLua converter(L, classes);
        LuwonValue value;
        if (!converter.convert(1, value, "value"))
            error = converter.error;
        else
        {
            LuwonWriteResult written = source ? editLuwon(*source, value) : writeLuwon(value);
            if (written.error)
                error = written.error;
            else
                lua_pushlstring(L, written.text.data(), written.text.size());
        }
    }

    if (error)
        luaL_error(L, "luwon.encode: %s", error->c_str());

    return 1;
}

// Pushes a copy of a comment or Info table's list, so changing what luwon.comments returns changes nothing until
// luwon.setcomments.
void pushCopy(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    lua_createtable(L, 0, 4);
    lua_pushnil(L);
    while (lua_next(L, idx))
    {
        if (lua_istable(L, -1) && !lua_getreadonly(L, -1))
        {
            pushCopy(L, -1);
            lua_remove(L, -2);
        }
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_rawset(L, -4);
    }
}

// luwon.comments(t, key?): the comments and locations of `t[key]`, as
// `{ above = { Comment }, after = Comment?, location = Location?, keylocation = Location? }`, or nil when there are
// none. Without `key`: every comment in the file `t` was read from, when `t` is the table a file returned.
int luwonComments(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    bool withKey = lua_gettop(L) >= 2;

    bool foreign = false;
    if (!pushLuwonMetatable(L, 1, foreign))
    {
        lua_pushnil(L);
        return 1;
    }

    if (!withKey)
    {
        lua_rawgetfield(L, -1, "comments");
        if (lua_istable(L, -1))
            pushCopy(L, -1);
        else
            lua_pushnil(L);
        return 1;
    }

    lua_rawgetfield(L, -1, "fields");
    if (!lua_istable(L, -1))
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_istable(L, -1))
        pushCopy(L, -1);
    else
        lua_pushnil(L);
    return 1;
}

// Builds the comments `luwon.setcomments` was given as an Info table, keeping the locations the old one had. Returns
// false when they aren't `{ above = { string }?, after = string? }`.
bool pushNewInfo(lua_State* L, int given, int old)
{
    LuwonComments comments;
    if (!readInfo(L, given, comments))
        return false;

    lua_createtable(L, 0, 4);

    // `above` takes one comment per line
    lua_createtable(L, int(comments.above.size()), 0);
    int line = 0;
    for (const LuwonComment& comment : comments.above)
    {
        // A block comment keeps its lines
        bool block = comment.text.rfind("--[", 0) == 0;
        size_t start = 0;
        while (start <= comment.text.size())
        {
            size_t end = block ? std::string::npos : comment.text.find('\n', start);
            if (end == std::string::npos)
                end = comment.text.size();

            std::string text = start == 0 ? comment.text.substr(0, end) : commentText(comment.text.data() + start, end - start);
            lua_createtable(L, 0, 1);
            lua_pushlstring(L, text.data(), text.size());
            lua_setfield(L, -2, "text");
            lua_rawseti(L, -2, ++line);
            start = end + 1;
        }
    }
    lua_setfield(L, -2, "above");

    if (comments.after)
    {
        lua_createtable(L, 0, 1);
        lua_pushlstring(L, comments.after->text.data(), comments.after->text.size());
        lua_setfield(L, -2, "text");
        lua_setfield(L, -2, "after");
    }

    if (old != 0)
    {
        lua_rawgetfield(L, old, "location");
        lua_setfield(L, -2, "location");
        lua_rawgetfield(L, old, "keylocation");
        lua_setfield(L, -2, "keylocation");
    }

    return true;
}

// luwon.setcomments(t, key, { above = { string }?, after = string? }): replaces the comments of `t[key]`. A string that
// doesn't start with `--` becomes a line comment. A table without a luwon metatable gets one.
int luwonSetComments(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    int keyType = lua_type(L, 2);
    bool validKey = keyType == LUA_TSTRING || keyType == LUA_TBOOLEAN || (keyType == LUA_TNUMBER && lua_tonumber(L, 2) == lua_tonumber(L, 2));
    if (!validKey)
        luaL_argerror(L, 2, "a key in a data file is a string, a number other than NaN, or a boolean");

    bool foreign = false;
    if (!pushLuwonMetatable(L, 1, foreign))
    {
        if (foreign)
            luaL_error(L, "luwon.setcomments: this table has a metatable of its own, so it can't carry luwon's comments");

        lua_createtable(L, 0, 2);
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "__luwon");
        lua_pushvalue(L, -1);
        lua_setmetatable(L, 1);
    }
    int metatable = lua_gettop(L);

    lua_rawgetfield(L, metatable, "fields");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, metatable, "fields");
    }
    int fields = lua_gettop(L);

    lua_pushvalue(L, 2);
    lua_rawget(L, fields);
    int old = lua_istable(L, -1) ? lua_gettop(L) : 0;

    if (!pushNewInfo(L, 3, old))
        luaL_argerror(L, 3, "comments are { above = { string }?, after = string? }, and the one after fits on one line");

    lua_pushvalue(L, 2);
    lua_insert(L, -2);
    lua_rawset(L, fields);
    return 0;
}

// luwon.source(t): the text of the file `t` was returned from, or nil.
int luwonSource(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    bool foreign = false;
    if (!pushLuwonMetatable(L, 1, foreign))
    {
        lua_pushnil(L);
        return 1;
    }

    lua_rawgetfield(L, -1, "source");
    return 1;
}

const luaL_Reg kLuwonLib[] = {
    {"decode", luwonDecode},
    {"encode", luwonEncode},
    {"comments", luwonComments},
    {"setcomments", luwonSetComments},
    {"source", luwonSource},
    {nullptr, nullptr},
};

} // namespace

} // namespace Luau

int luaopen_luwon(lua_State* L)
{
    luaL_register(L, "luwon", Luau::kLuwonLib);
    return 1;
}

int luwon_load(lua_State* L, const char* chunkname, const char* source, size_t size)
{
    using namespace Luau;

    int top = lua_gettop(L);
    std::optional<std::string> error;
    std::optional<LuwonError> located;
    bool pushed = readAndPush(L, std::string_view(source, size), 0, error, located);
    if (pushed)
        return 0;

    lua_settop(L, top);
    std::string message;
    if (located)
        message = format("%s:%d: %s", displayName(chunkname).c_str(), located->location.begin.line + 1, located->message.c_str());
    else
        message = displayName(chunkname) + ": " + error.value_or("");

    lua_pushlstring(L, message.data(), message.size());
    return 1;
}

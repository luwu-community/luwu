// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Luwon.h"

#include "luwon.h"

#include "Luau/Compiler.h"
#include "Luau/LuwonSyntax.h"
#include "Luau/StringUtils.h"

#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <string.h>

LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(LuwuClasses)

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
//     Info = { above = { Comment }, after = Comment?, location = Location?, keylocation = Location?, binding = Binding? }
//     Binding = { name = string, location = Location, above = { Comment }, after = Comment? }
//         -- the const a value is written as the name of (`dependencies = dependencies`), and its declaration's comments
//     Comment = { text = string, location = Location? }
//     Location = { begin = vector, end = vector }   -- line, column (both from 1) and byte offset, as `ast` gives them

namespace Luau
{

static int errorText(lua_State* L)
{
    luaL_tolstring(L, 1, nullptr);
    return 1;
}

// Calls what the caller was given (a class, an `args` function) on the stack. A Lua error becomes `error` instead of
// unwinding through C++ frames that hold the stack.
static bool callProtected(lua_State* L, int nargs, int nresults, std::optional<std::string>& error)
{
    if (lua_pcall(L, nargs, nresults, 0) == LUA_OK)
        return true;

    // A runtime's error objects (seal's) say what they are through `__tostring`, which may raise too
    if (!lua_isstring(L, -1))
    {
        lua_pushcfunction(L, errorText, "errortext");
        lua_insert(L, -2);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK || !lua_isstring(L, -1))
        {
            lua_pop(L, 1);
            lua_pushliteral(L, "an error whose __tostring failed");
        }
    }

    size_t length = 0;
    const char* message = lua_tolstring(L, -1, &length);
    error = std::string(message, length);
    lua_pop(L, 1);
    return false;
}

// Pushes the object's class through `class.of`, read from the registry so a global named `class` can't change it. Returns
// false with nothing pushed when the class library isn't open, or when `class.of` raised, which sets `error`.
static bool pushClassOf(lua_State* L, int object, std::optional<std::string>& error)
{
    object = lua_absindex(L, object);
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

// The registry keys of the set of classes that implement `luwon.data`, which the trait's `__implemented` hook fills, of
// the class `luwon.tagged`, and of the metatable readers share.
static const char* kDataClassesKey = "luwon_dataclasses";
static const char* kTaggedClassKey = "luwon_tagged";
static const char* kReaderMetatableKey = "luwon_reader";

// Pushes the set of classes that implement `luwon.data`, creating it the first time. It's keyed by the class itself and
// holds it weakly, so it never decides what a name means.
static void pushDataClasses(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kDataClassesKey);
    if (lua_istable(L, -1))
        return;

    lua_pop(L, 1);
    lua_newtable(L);
    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "k");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kDataClassesKey);
}

// Whether the class at `idx` implements `luwon.data`, which lets a data file construct it whatever it holds.
static bool implementsData(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    pushDataClasses(L);
    lua_pushvalue(L, idx);
    lua_rawget(L, -2);
    bool implements = lua_toboolean(L, -1) != 0;
    lua_pop(L, 2);
    return implements;
}

// Why a data file can't construct the class at `idx`, or nullopt when it can: the class has no methods and a constructor
// that only sets its fields, or it implements `luwon.data`.
static std::optional<std::string> refusalOf(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    if (implementsData(L, idx))
        return std::nullopt;

    int top = lua_gettop(L);
    int hasmethods = 0;
    int shape = lua_getclassdatashape(L, idx, &hasmethods);
    lua_settop(L, top);

    const char* name = lua_getclassname(L, idx);
    if (hasmethods)
        return format("'%s' has methods, so a data file can't construct it unless it implements luwon.data", name);
    if (shape == LUA_CLASSDATA_NONE)
        return format("'%s' has a constructor that runs code, so a data file can't construct it unless it implements luwon.data", name);

    return std::nullopt;
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
    // `classes` is the stack index of a reader's table of the classes the file may construct by name, or 0 for none. With `origin`,
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
    // Sets `above` and `after` on the table on top of the stack.
    void setComments(const LuwonComments* comments)
    {
        size_t count = comments ? comments->above.size() : 0;
        lua_createtable(L, int(count), 0);
        for (size_t i = 0; i < count; i++)
        {
            pushComment(comments->above[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        lua_setfield(L, -2, "above");

        if (comments && comments->after)
        {
            pushComment(*comments->after);
            lua_setfield(L, -2, "after");
        }
    }

    // `{ above, after?, location, keylocation?, binding? }` for an item of a table read from the file.
    void pushInfo(const LuwonEntry& entry, bool keyed)
    {
        lua_createtable(L, 0, 5);
        setComments(entry.comments ? &*entry.comments : nullptr);

        if (!entry.site)
            return;

        pushLocationOf(entry.site->value);
        lua_setfield(L, -2, "location");
        if (keyed)
        {
            pushLocationOf(entry.site->key);
            lua_setfield(L, -2, "keylocation");
        }

        // The const the value is written as the name of: `{ name, location, above, after? }`
        if (const std::optional<LuwonConstant>& constant = entry.site->constant)
        {
            lua_createtable(L, 0, 4);
            lua_pushlstring(L, constant->name.data(), constant->name.size());
            lua_setfield(L, -2, "name");
            pushLocationOf(constant->declaration);
            lua_setfield(L, -2, "location");
            setComments(&constant->comments);
            lua_setfield(L, -2, "binding");
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

    // Builds an object with the `__fromdata` of the class on top of the stack, which replaces it, and checks it built one
    // of that class
    bool pushFromData(const LuwonValue& object)
    {
        int cls = lua_gettop(L);
        lua_getfield(L, cls, "__fromdata");
        for (const LuwonValue& arg : object.args)
        {
            if (!push(arg))
                return false;
        }

        if (!callProtected(L, int(object.args.size()), 1, error))
            return false;

        bool built = false;
        if (lua_isobject(L, -1))
        {
            // Without the class library there's nothing to check the class with
            if (pushClassOf(L, -1, error))
            {
                built = lua_rawequal(L, -1, cls);
                lua_pop(L, 1);
            }
            else if (error)
                return false;
            else
                built = true;
        }

        if (!built)
        {
            const char* got = lua_isobject(L, -1) ? lua_getclassname(L, -1) : luaL_typename(L, -1);
            return fail(format("'%s.__fromdata' must return a %s, but returned a %s", object.bytes.c_str(), object.bytes.c_str(), got));
        }

        lua_remove(L, cls);
        return true;
    }

    // Pushes the class the reader has for a name and returns true, or returns false with nothing pushed
    bool pushNamedClass(const std::string& name)
    {
        if (classes == 0)
            return false;

        lua_rawgetfield(L, classes, name.c_str());
        if (lua_type(L, -1) == LUA_TCLASS)
            return true;

        lua_pop(L, 1);
        return false;
    }

    // Builds an object with its class's own constructor, when the class is known and a data file may construct it, and
    // otherwise a `luwon.tagged` holding the name and the arguments, so a file reads without knowing its classes.
    bool pushObject(const LuwonValue& object)
    {
        if (std::optional<LuwonBuiltinClass> builtin = builtinNamed(object.bytes))
            return pushBuiltinObject(*builtin, object);

        if (pushNamedClass(object.bytes))
        {
            if (std::optional<std::string> refusal = refusalOf(L, -1))
                return fail(*refusal);
            if (implementsData(L, -1) && lua_getmemberaccess(L, -1, "__fromdata") != LUA_MEMBERMISSING)
                return pushFromData(object);
            return pushArgsAndCall(object);
        }

        lua_getfield(L, LUA_REGISTRYINDEX, kTaggedClassKey);
        if (lua_type(L, -1) != LUA_TCLASS)
            return fail(format("'%s' isn't a class this file may construct; read it with a luwon.reader that has it", object.bytes.c_str()));

        lua_pushlstring(L, object.bytes.data(), object.bytes.size());
        lua_createtable(L, int(object.args.size()), 0);
        for (size_t i = 0; i < object.args.size(); i++)
        {
            if (!push(object.args[i]))
                return false;
            lua_rawseti(L, -2, int(i + 1));
        }

        return callProtected(L, 2, 1, error);
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
    explicit LuwonFromLua(lua_State* L)
        : L(L)
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

    // An object as its constructor call. A builtin class's and a `luwon.tagged`'s are written as they were read, and any
    // other one a data file may construct from what its `__todata` returns or else from its fields.
    bool convertObject(int idx, LuwonValue& out, const std::string& path)
    {
        if (std::optional<bool> converted = convertBuiltinObject(idx, out, path))
            return *converted;
        if (std::optional<bool> converted = convertTagged(idx, out, path))
            return *converted;

        return convertByFields(idx, out, path);
    }

    // The object at `idx` as a call to `name` with the values its `__todata`, on top of the stack, returns for it.
    bool convertCall(int idx, const std::string& name, LuwonValue& out, const std::string& path)
    {
        int top = lua_gettop(L) - 1;
        lua_pushvalue(L, idx);
        if (!callProtected(L, 1, LUA_MULTRET, error))
            return false;

        out = LuwonValue{LuwonValue::Kind::Object};
        out.bytes = name;
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

    // A `luwon.tagged`, as the constructor call it was read from. Returns nullopt for any other object.
    std::optional<bool> convertTagged(int idx, LuwonValue& out, const std::string& path)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, kTaggedClassKey);
        if (lua_type(L, -1) != LUA_TCLASS || !pushClassOf(idx))
        {
            lua_pop(L, 1);
            return error ? std::optional<bool>(false) : std::nullopt;
        }

        bool tagged = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (!tagged)
            return std::nullopt;

        lua_getfield(L, idx, "name");
        lua_getfield(L, idx, "args");
        bool shaped = lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1);
        if (!shaped)
        {
            lua_pop(L, 2);
            return fail(format("a luwon.tagged needs a string name and a table of args, at %s", path.c_str()));
        }

        out = LuwonValue{LuwonValue::Kind::Object};
        out.bytes = lua_tostring(L, -2);
        int args = lua_gettop(L);
        depth++;
        for (int i = 1;; i++)
        {
            lua_rawgeti(L, args, i);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                break;
            }

            LuwonValue arg;
            bool converted = convert(-1, arg, format("%s(argument %d)", path.c_str(), i));
            lua_pop(L, 1);
            if (!converted)
            {
                depth--;
                lua_pop(L, 2);
                return false;
            }
            out.args.push_back(std::move(arg));
        }
        depth--;

        lua_pop(L, 2);
        return true;
    }

    // An object of a class a data file may construct, written from its fields the way its constructor takes them:
    // positionally for a primary constructor, as a table for the default one.
    bool convertByFields(int idx, LuwonValue& out, const std::string& path)
    {
        const char* className = lua_getclassname(L, idx);
        if (!pushClassOf(idx))
            return error ? false : fail(format("a data file can't hold an object of class '%s' without the class library, at %s", className, path.c_str()));

        int top = lua_gettop(L) - 1;
        int cls = lua_gettop(L);
        if (std::optional<std::string> refusal = refusalOf(L, cls))
        {
            lua_settop(L, top);
            return fail(format("%s, at %s", refusal->c_str(), path.c_str()));
        }

        // A class that implements `luwon.data` may say what its call holds
        if (implementsData(L, cls) && lua_getmemberaccess(L, cls, "__todata") != LUA_MEMBERMISSING)
        {
            lua_getfield(L, idx, "__todata");
            bool converted = convertCall(idx, className, out, path);
            lua_settop(L, top);
            return converted;
        }

        int hasmethods = 0;
        int shape = lua_getclassdatashape(L, cls, &hasmethods);
        if (shape == LUA_CLASSDATA_NONE)
        {
            lua_settop(L, top);
            return fail(format(
                "'%s' has a constructor that takes something other than its fields; give it a '__todata' that says what its "
                "call holds, at %s",
                className,
                path.c_str()
            ));
        }

        int names = lua_gettop(L);
        out = LuwonValue{LuwonValue::Kind::Object};
        out.bytes = className;

        std::shared_ptr<LuwonTable> fields = std::make_shared<LuwonTable>();
        depth++;
        for (int i = 1;; i++)
        {
            lua_rawgeti(L, names, i);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                break;
            }

            LuwonValue key{LuwonValue::Kind::String};
            key.bytes = lua_tostring(L, -1);
            lua_gettable(L, idx);

            LuwonValue value;
            bool converted = convert(-1, value, path + "." + key.bytes);
            lua_pop(L, 1);
            if (!converted)
            {
                depth--;
                lua_settop(L, top);
                return false;
            }

            if (shape == LUA_CLASSDATA_POSITIONAL)
                out.args.push_back(std::move(value));
            else
                fields->add(std::move(key), std::move(value));
        }
        depth--;

        if (shape == LUA_CLASSDATA_TABLE)
        {
            LuwonValue table{LuwonValue::Kind::Table};
            table.table = std::move(fields);
            out.args.push_back(std::move(table));
        }

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

    bool pushClassOf(int object)
    {
        return Luau::pushClassOf(L, object, error);
    }
};

// The chunk's name as error messages show it, without the '@' or '=' prefix.
std::string displayName(const char* chunkname)
{
    if (chunkname[0] == '@' || chunkname[0] == '=')
        return chunkname + 1;
    return chunkname;
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

// Decodes the text at `textIdx` with the classes table at `classes` (0 for none), raising errors as `caller`'s
int decodeWith(lua_State* L, int textIdx, int classes, const char* caller)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, textIdx, &length);
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
        luaL_error(L, "%s: %s", caller, error->c_str());
    }

    return 1;
}

// luwon.decode(text): the value a data file returns. Every constructor call in it reads as a `luwon.tagged`.
int luwonDecode(lua_State* L)
{
    return decodeWith(L, 1, 0, "luwon.decode");
}

// reader:decode(text): the value a data file returns, building the classes the reader has
int luwonReaderDecode(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kReaderMetatableKey);
    bool isReader = lua_istable(L, 1) && lua_getmetatable(L, 1) && lua_rawequal(L, -1, -2);
    if (!isReader)
        luaL_typeerror(L, 1, "luwon.reader");
    lua_settop(L, 2);

    lua_rawgetfield(L, 1, "classes");
    return decodeWith(L, 2, lua_gettop(L), "reader:decode");
}

// luwon.reader(classes): a reader whose `decode` builds the given classes from the calls that name them. A data file
// names a class by its name, so two classes with one name can't be in one reader.
int luwonReader(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_newtable(L);
    int byName = lua_gettop(L);

    for (int i = 1;; i++)
    {
        lua_rawgeti(L, 1, i);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            break;
        }

        if (lua_type(L, -1) != LUA_TCLASS)
            luaL_error(L, "luwon.reader: item %d is a %s, not a class", i, luaL_typename(L, -1));
        if (std::optional<std::string> refusal = refusalOf(L, -1))
            luaL_error(L, "luwon.reader: %s", refusal->c_str());

        const char* name = lua_getclassname(L, -1);
        lua_rawgetfield(L, byName, name);
        if (!lua_isnil(L, -1))
            luaL_error(L, "luwon.reader: two classes are named '%s', so a data file couldn't tell them apart", name);
        lua_pop(L, 1);

        lua_rawsetfield(L, byName, name);
    }
    lua_setreadonly(L, byName, true);

    lua_createtable(L, 0, 1);
    lua_pushvalue(L, byName);
    lua_rawsetfield(L, -2, "classes");

    lua_getfield(L, LUA_REGISTRYINDEX, kReaderMetatableKey);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_createtable(L, 0, 2);
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, luwonReaderDecode, "decode");
        lua_rawsetfield(L, -2, "decode");
        lua_setreadonly(L, -1, true);
        lua_rawsetfield(L, -2, "__index");
        lua_pushliteral(L, "luwon.reader");
        lua_rawsetfield(L, -2, "__type");
        lua_setreadonly(L, -1, true);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kReaderMetatableKey);
    }
    lua_setmetatable(L, -2);
    lua_setreadonly(L, -1, true);
    return 1;
}

// luwon.encode(value): the text of a data file. A table a file returned is written back into that file's
// source, keeping its comments, consts, types and layout; anything else is written anew. Comments tables carry are
// written either way.
int luwonEncode(lua_State* L)
{
    luaL_checkany(L, 1);

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
        LuwonFromLua converter(L);
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
        lua_rawgetfield(L, old, "binding");
        lua_setfield(L, -2, "binding");
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

// A `luwon.tagged` as the constructor call it holds, for its `__tostring`.
int describeTagged(lua_State* L)
{
    std::optional<std::string> error;
    {
        LuwonFromLua converter(L);
        LuwonValue value;
        if (converter.convert(1, value, "value"))
        {
            std::string text = writeLuwonLiteral(value);
            lua_pushlstring(L, text.data(), text.size());
        }
        else
            error = converter.error;
    }

    if (error)
        luaL_error(L, "%s", error->c_str());

    return 1;
}

// `luwon.tagged`, what a data file's constructor call reads to when no class is known for its name. It's a class written
// in Luwu: compiled and run here, it pushes the class and remembers it in the registry.
const char* const kTaggedClassSource = R"(
const describe = ...

class tagged(public name: string, public args: { unknown })
    public function __tostring(self): string
        return describe(self)
    end
end

return tagged
)";

const luaL_Reg kLuwonLib[] = {
    {"decode", luwonDecode},
    {"reader", luwonReader},
    {"encode", luwonEncode},
    {"comments", luwonComments},
    {"setcomments", luwonSetComments},
    {"source", luwonSource},
    {nullptr, nullptr},
};

} // namespace

// Pushes the class `luwon.tagged`, compiling it from kTaggedClassSource, and remembers it in the registry.
static void openTaggedClass(lua_State* L)
{
    CompileOptions options;
    options.language = int(Language::Luwu);
    std::string bytecode = compile(kTaggedClassSource, options);

    int loaded = luau_load(L, "=luwon.tagged", bytecode.data(), bytecode.size(), 0);
    LUAU_ASSERT(loaded == 0);
    if (loaded != 0)
        lua_error(L);

    lua_pushcfunction(L, describeTagged, "describe");
    lua_call(L, 1, 1);

    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kTaggedClassKey);
}

} // namespace Luau

int luaopen_luwon(lua_State* L)
{
    luaL_register(L, "luwon", Luau::kLuwonLib);

    if (FFlag::LuwuClasses)
    {
        Luau::openTaggedClass(L);
        lua_setfield(L, -2, "tagged");
    }

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

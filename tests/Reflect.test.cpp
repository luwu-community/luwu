// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Reflect.h"
#include "Luau/ReflectCommon.h"

#include "ScopedFlags.h"
#include "doctest.h"
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include <memory>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

LUAU_FASTFLAG(LuauDirectFieldGet)
LUAU_FASTFLAG(LuwuManagedReferences2)
LUAU_FASTFLAG(LuauUdataMetatablePinned)
LUAU_FASTFLAG(LuauCstAttr)
LUAU_FASTFLAG(LuwuDefaultArguments)

static int dostring(lua_State* L, const char* code)
{
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code, strlen(code), nullptr, &bytecodeSize);
    if (!bytecode)
        return -1;
    int result = luau_load(L, "=chunk", bytecode, bytecodeSize, 0);
    free(bytecode);
    if (result == 0)
    {
        int status = lua_pcall(L, 0, 0, 0);
        if (status != 0)
        {
            fprintf(stderr, "Lua runtime error: %s\n", lua_tostring(L, -1));
        }
        return status;
    }
    return result;
}

TEST_SUITE_BEGIN("Reflect");

TEST_CASE("LazyAstTypeof")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("local x = 1")
        assert(typeof(doc) == "AstDocument")
        assert(typeof(doc:allocator()) == "AstAllocator")
        assert(doc:allocator() == arena)

        local root = doc:root()
        assert(typeof(root) == "AstItem")
        assert(root.kind == "AstStatBlock")
        assert(root.category == "stat")

        local stat = root:body()[1]
        assert(typeof(stat) == "AstItem")
        assert(stat.kind == "AstStatLocal")
        assert(stat.category == "stat")

        local val = stat:values()[1]
        assert(typeof(val) == "AstItem")
        assert(val.kind == "AstExprConstantNumber")
        assert(val.category == "expr")

        local localItem = stat:vars()[1]
        assert(typeof(localItem) == "AstItem")
        assert(localItem.kind == "AstLocal")
        assert(localItem:name() == "x")

        local loc = stat:origlocation()
        assert(typeof(loc) == "table")
        assert(typeof(loc.begin) == "vector")
        assert(typeof(loc["end"]) == "vector")
        assert(loc.begin.x == 1)
        assert(loc.begin.y == 1)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstProperties")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse([[
            if x > 0 then
                print("positive")
            else
                print("non-positive")
            end
        ]])

        local root = doc:root()
        local ifStat = root:body()[1]
        assert(ifStat.kind == "AstStatIf")
        assert(ifStat.category == "stat")

        local cond = ifStat:condition()
        assert(cond.kind == "AstExprBinary")
        assert(cond.category == "expr")
        assert(cond:op() == ">")
        assert(cond:left():name() == "x")
        assert(cond:right():value() == 0)

        local thenBody = ifStat:thenbody()
        assert(thenBody.kind == "AstStatBlock")
        assert(thenBody.category == "stat")
        assert(#thenBody:body() == 1)

        local callExpr = thenBody:body()[1]:expr()
        assert(callExpr.kind == "AstExprCall")
        assert(callExpr.category == "expr")
        assert(callExpr:func():name() == "print")
        assert(#callExpr:args() == 1)
        assert(callExpr:args()[1]:value() == "positive")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstChildrenAndWalk")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("local a = 1; local b = 2; return a + b")
        
        -- Test children() on block
        local root = doc:root()
        local children = root:children()
        assert(#children == 3)
        assert(children[1].kind == "AstStatLocal")
        assert(children[2].kind == "AstStatLocal")
        assert(children[3].kind == "AstStatReturn")

        -- Test root:walk()
        local nodeKinds = {}
        root:walk(function(node)
            table.insert(nodeKinds, node.kind)
            return true
        end)
        assert(#nodeKinds > 5)

        -- Test root:walk with filter
        local locals = {}
        root:walk(function(node)
            table.insert(locals, node)
        end, reflect.filter("AstStatLocal"))
        assert(#locals == 2)
        assert(locals[1]:vars()[1]:name() == "a")
        assert(locals[2]:vars()[1]:name() == "b")

        -- Test node:walk with filter
        local nodeLocals = {}
        root:walk(function(node)
            table.insert(nodeLocals, node)
        end, reflect.filter("AstStatLocal"))
        assert(#nodeLocals == 2)
        assert(nodeLocals[1]:vars()[1]:name() == "a")
        assert(nodeLocals[2]:vars()[1]:name() == "b")

        local returns = {}
        root:walk(function(node)
            table.insert(returns, node)
        end, reflect.filter("AstStatReturn"))
        assert(#returns == 1)

        -- Test walk pruning (return false)
        local topOnly = {}
        doc:root():walk(function(node)
            table.insert(topOnly, node.kind)
            return false -- do not descend
        end)
        assert(#topOnly == 1)
        assert(topOnly[1] == "AstStatBlock")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstParseExpr")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local expr = arena:parseexpr("a + b * 2")
        assert(expr ~= nil)
        assert(typeof(expr) == "AstItem")
        assert(expr.kind == "AstExprBinary")
        assert(expr.category == "expr")
        assert(expr:op() == "+")
        assert(expr:left().kind == "AstExprGlobal")
        assert(expr:left():name() == "a")

        local right = expr:right()
        assert(typeof(right) == "AstItem")
        assert(right.kind == "AstExprBinary")
        assert(right.category == "expr")
        assert(right:op() == "*")
        assert(right:left():name() == "b")
        assert(right:right():value() == 2)
        assert(expr:prettyprint() == "a + b * 2")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstErrors")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("local x = \n-- a comment")
        local errors = doc:errors()
        assert(#errors > 0)
        assert(errors[1].message ~= nil)
        assert(errors[1].location ~= nil)
        assert(errors[1].location.begin.x == 2)
        local comments = doc:comments()
        assert(#comments > 0)
        local lineOffsets = doc:lineOffsets()
        assert(#lineOffsets > 0)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyCstData")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("local str = 'hello world'\nfoo(1, 2)", true)
        local stats = doc:root():body()

        -- Check local stat CST
        local locStat = stats[1]
        local locStatCst = locStat:cst()
        assert(locStatCst ~= nil)
        assert(typeof(locStatCst) == "AstItem")
        assert(locStatCst.kind == "CstStatLocal")
        assert(locStatCst.category == "cst")

        -- Check string CST quote style
        local strExpr = locStat:values()[1]
        local strExprCst = strExpr:cst()
        assert(strExprCst ~= nil)
        assert(typeof(strExprCst) == "AstItem")
        assert(strExprCst.kind == "CstExprConstantString")
        assert(strExprCst.category == "cst")
        assert(strExprCst:quoteStyle() == "single")
        assert(strExprCst:sourceString() == "hello world")

        -- Check call CST parens and comma positions
        local callStat = stats[2]
        local callExpr = callStat:expr()
        local callExprCst = callExpr:cst()
        assert(callExprCst ~= nil)
        assert(callExprCst.kind == "CstExprCall")
        assert(typeof(callExprCst:openParens()) == "vector")
        assert(callExprCst:openParens().x == 2)
        assert(callExprCst:openParens().z > 0)
        assert(#doc:lineOffsets() >= 2)
        assert(doc:lineOffsets()[1] == 0)
        assert(#callExprCst:commaPositions() == 1)
        assert(typeof(callExprCst:commaPositions()[1]) == "vector")

        -- Check type function CST and AST category
        local typeDoc = arena:parse("type Callback = (a: number, b: string) -> boolean", true)
        local aliasStat = typeDoc:root():body()[1]
        assert(aliasStat.category == "stat")
        local aliasStatCst = aliasStat:cst()
        assert(aliasStatCst ~= nil)
        assert(aliasStatCst.kind == "CstStatTypeAlias")
        assert(aliasStatCst:typeKeywordPosition().x == 1)

        local typeFunc = aliasStat:type()
        assert(typeFunc.category == "type")
        local typeFuncCst = typeFunc:cst()
        assert(typeFuncCst ~= nil)
        assert(typeFuncCst.kind == "CstTypeFunction")
        assert(typeFuncCst:returnArrowPosition().x == 1)
        assert(#typeFuncCst:argumentsCommaPositions() == 1)
        assert(#typeFuncCst:argumentNameColonPositions() == 2)

        -- Check that includeCst defaults to false / omitted results in nil cst()
        local noCstDoc = arena:parse("local x = 1")
        assert(noCstDoc:root():body()[1]:cst() == nil)
        local noCstExpr = arena:parseexpr("1 + 2")
        assert(noCstExpr:cst() == nil)
        local cstExpr = arena:parseexpr("1 + 2", true)
        assert(cstExpr:cst() ~= nil)
        assert(cstExpr:cst().kind == "CstExprOp")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstComments")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("-- single comment\nlocal x = 1\n--[[\nblock comment\n]]\n")
        local comments = doc:comments()
        assert(#comments == 2)

        local c1 = comments[1]
        assert(typeof(c1) == "AstItem")
        assert(c1.kind == "AstComment")
        assert(c1:type() == "single")
        assert(c1:origlocation().begin.x == 1)
        assert(c1:text() == "-- single comment")

        local c2 = comments[2]
        assert(typeof(c2) == "AstItem")
        assert(c2.kind == "AstComment")
        assert(c2:type() == "block")
        assert(c2:text() == "--[[\nblock comment\n]]")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstTableItems")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse([[
            local tbl = {
                "hello",             
                foo = 123,           
                ["key" .. 2] = true 
            }
        ]], true)

        local stat = doc:root():body()[1]
        assert(stat.kind == "AstStatLocal")

        local tableExpr = stat:values()[1]
        assert(tableExpr.kind == "AstExprTable")

        local items = tableExpr:items()
        assert(#items == 3)

        -- List item
        local item1 = items[1]
        assert(typeof(item1) == "AstItem")
        assert(item1.kind == "AstTableItem")
        assert(item1:itemKind() == "list")
        assert(item1:key() == nil)
        assert(item1:value().kind == "AstExprConstantString")
        assert(item1:value():value() == "hello")

        -- Record item
        local item2 = items[2]
        assert(typeof(item2) == "AstItem")
        assert(item2.kind == "AstTableItem")
        assert(item2:itemKind() == "record")
        assert(item2:key() ~= nil)
        assert(item2:key().kind == "AstExprConstantString")
        assert(item2:key():value() == "foo")
        assert(item2:value().kind == "AstExprConstantInteger" or item2:value().kind == "AstExprConstantNumber")
        assert(item2:value():value() == 123)

        -- General item
        local item3 = items[3]
        assert(typeof(item3) == "AstItem")
        assert(item3.kind == "AstTableItem")
        assert(item3:itemKind() == "general")
        assert(item3:key() ~= nil)
        assert(item3:key().kind == "AstExprBinary")
        assert(item3:key():op() == "..")
        assert(item3:value().kind == "AstExprConstantBool")
        assert(item3:value():value() == true)

        -- CST table items
        local tableCst = tableExpr:cst()
        assert(tableCst ~= nil)
        local cstItems = tableCst:items()
        assert(#cstItems == 3)
        assert(typeof(cstItems[1]) == "AstItem")
        assert(typeof(cstItems[2]) == "AstItem")
        assert(typeof(cstItems[3]) == "AstItem")
        assert(cstItems[2]:equalsPosition() ~= nil)
        assert(cstItems[3]:indexerOpenPosition() ~= nil)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("AstTypeTableAndItems")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse([[
            type MyTable = {
                read foo: string,
                bar: number,
                [string]: boolean,
            }
        ]])

        local stat = doc:root():body()[1]
        assert(stat.kind == "AstStatTypeAlias")
        assert(stat:name() == "MyTable")

        local ty = stat:type()
        assert(ty.kind == "AstTypeTable")
        assert(ty.category == "type")

        local props = ty:props()
        assert(#props == 2)

        local prop1 = props[1]
        assert(prop1.kind == "AstTableProp")
        assert(prop1:name() == "foo")
        assert(prop1:access() == "read")
        assert(prop1:type().kind == "AstTypeReference")
        assert(prop1:type():name() == "string")

        local prop2 = props[2]
        assert(prop2.kind == "AstTableProp")
        assert(prop2:name() == "bar")
        assert(prop2:access() == "readwrite")
        assert(prop2:type().kind == "AstTypeReference")
        assert(prop2:type():name() == "number")

        local indexer = ty:indexer()
        assert(indexer ~= nil)
        assert(indexer.kind == "AstTableIndexer")
        assert(indexer:access() == "readwrite")
        assert(indexer:indexType().kind == "AstTypeReference")
        assert(indexer:indexType():name() == "string")
        assert(indexer:resultType().kind == "AstTypeReference")
        assert(indexer:resultType():name() == "boolean")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("TestFields")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("local a = 1\nfoo(1, 2)", true)
        local stat1 = doc:root():body()[1]
        local loc = stat1:origlocation()

        -- Test fields on AstLocation table
        assert(typeof(loc) == "table")
        assert(table.isfrozen(loc) == true)
        assert(typeof(loc.begin) == "vector")
        assert(typeof(loc["end"]) == "vector")
        assert(loc.begin.x == 1)
        assert(loc.begin.y == 1)
        assert(loc.begin.z == 0)
        assert(loc["end"].x == 1)
        assert(loc["end"].y == 12)
        assert(loc["end"].z == 11)

        -- Test CST positions as vectors
        local call = doc:root():body()[2]:expr()
        local pos = call:cst():openParens()
        assert(typeof(pos) == "vector")
        assert(pos.x == 2)
        assert(pos.y == 4)
        assert(pos.z == 15)

        -- Test dot indexing a method (stat1.origlocation(stat1))
        local bodyFn = stat1.origlocation
        assert(typeof(bodyFn) == "function")
        local loc2 = bodyFn(stat1)
        assert(loc2.begin.x == 1)

        -- Test dot indexing a CST method
        local cstFn = call:cst().openParens
        assert(typeof(cstFn) == "function")
        local pos2 = cstFn(call:cst())
        assert(pos2.x == 2)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ParseReflectTypesFile")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::ifstream file("Reflect/types.luau");
    if (!file.is_open())
        file.open("../Reflect/types.luau");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    lua_pushlstring(L, content.data(), content.size());
    lua_setglobal(L, "sourceCode");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse(sourceCode)
        local errors = doc:errors()
        assert(#errors == 0, "Parse errors in Reflect/types.luau: " .. (#errors > 0 and errors[1].message or ""))
        assert(doc:root() ~= nil)
        assert(#doc:root():body() > 0)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectUseAtoms")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local src = "-- hello\nlocal x: number = 42\nprint(x)\n"
        local arena = reflect.allocator()
        local doc = arena:parse(src, true)
        assert(doc:root() ~= nil)
        assert(doc:source() == src)

        -- Test non-string and invalid key indexing returns nil
        assert(doc[123] == nil)
        assert(doc[true] == nil)
        assert(doc["nonexistentProp"] == nil)
        assert(doc:root()[123] == nil)
        assert(doc:root()[false] == nil)
        assert(doc:root()["nonexistentProp"] == nil)

        -- Test comments & lineOffsets methods
        local comments = doc:comments()
        assert(#comments == 1)
        local c = comments[1]
        assert(c:type() == "single")
        assert(c:text() == "-- hello")
        assert(c:origlocation().begin.x == 1)
        assert(c[123] == nil)

        -- Test root & statements
        local root = doc:root()
        assert(root.kind == "AstStatBlock")
        assert(root.category == "stat")
        local stats = root:body()
        assert(#stats == 2)

        local stat1 = stats[1]
        assert(stat1.kind == "AstStatLocal")
        local vars = stat1:vars()
        assert(#vars == 1)
        local var = vars[1]
        assert(var:name() == "x")
        assert(var:isConst() == false)
        assert(var[99] == nil)

        local vals = stat1:values()
        assert(#vals == 1)
        local val = vals[1]
        assert(val.kind == "AstExprConstantNumber")
        assert(val:value() == 42)

        -- Test walk method via namecall
        local foundCount = 0
        doc:root():walk(function(node)
            foundCount = foundCount + 1
        end)
        assert(foundCount > 0)

        local found = {}
        doc:root():walk(function(node)
            table.insert(found, node)
        end, reflect.filter("AstStatLocal"))
        assert(#found == 1)

        -- Test CST access via atoms
        local cst = stat1:cst()
        assert(cst.kind == "CstStatLocal")
        assert(cst[123] == nil)
        local colons = cst:varsAnnotationColonPositions()
        assert(#colons == 1)
        assert(colons[1].x == 2)
        assert(colons[1].y == 8)
        assert(typeof(colons[1]) == "vector")

        -- Test location table
        local loc = stat1:origlocation()
        assert(typeof(loc) == "table")
        assert(loc.begin.x == 2)
        assert(loc.begin.y == 1)
        assert(loc["end"].x == 2)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectUserdataId")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("local a = 1; local b = 2", true)
        assert(doc.id ~= nil)
        assert(typeof(doc.id) == "Id")

        local root = doc:root()
        local stats1 = root:body()
        local stats2 = root:body()

        local statA1 = stats1[1]
        local statA2 = stats2[1]
        local statB1 = stats1[2]

        -- Verify that different userdata materializations of the same node have identical .id
        assert(statA1.id ~= nil)
        assert(typeof(statA1.id) == "Id")
        assert(statA1.id == statA2.id)

        -- Distinct nodes have distinct IDs
        assert(statA1.id ~= statB1.id)

        -- Verify lightuserdata can be used as table key and hashes identically across materializations
        local visited = {}
        visited[statA1.id] = "first_statement"
        visited[statB1.id] = "second_statement"
        assert(visited[statA2.id] == "first_statement")

        -- Verify CST node IDs
        local cstA1 = statA1:cst()
        local cstA2 = statA2:cst()
        assert(cstA1.id ~= nil)
        assert(typeof(cstA1.id) == "Id")
        assert(cstA1.id == cstA2.id)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectUserdataProtectedMetatable")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("-- comment\ndo end", true)
        local root = doc:root()
        local stat = root:body()[1]
        local loc = stat:origlocation()
        local cst = stat:cst()
        local pos = cst:endPosition()
        local comment = doc:comments()[1]
        local flt = reflect.filter("AstStatLocal")

        assert(getmetatable(doc) == false)
        assert(getmetatable(root) == false)
        assert(getmetatable(stat) == false)
        assert(typeof(loc) == "table")
        assert(getmetatable(cst) == false)
        assert(typeof(pos) == "vector")
        assert(getmetatable(comment) == false)
        assert(getmetatable(flt) == false)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectAstFilter")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse([[
            local x = 1
            local function foo(a, b)
                print(a + b)
                return a * b
            end
            foo(x, 2)
        ]])

        -- Filter properties and types
        local callFilter = reflect.filter("AstExprCall")
        assert(callFilter.kind == "AstFilter")
        assert(typeof(callFilter.id) == "Id")

        -- Matches method
        local root = doc:root()
        assert(callFilter:match(root) == false)

        -- Walk with filter
        local calls = {}
        root:walk(function(node)
            table.insert(calls, node)
        end, callFilter)

        assert(#calls == 2) -- print(...) and foo(x, 2)
        assert(calls[1].kind == "AstExprCall")
        assert(calls[2].kind == "AstExprCall")
        assert(callFilter:match(calls[1]) == true)
        assert(callFilter:match(calls[2]) == true)

        -- Category filter: stat
        local statFilter = reflect.filter("stat")
        local stats = {}
        root:walk(function(node)
            table.insert(stats, node)
        end, statFilter)

        assert(#stats > 0)
        for _, s in stats do
            assert(s.category == "stat")
            assert(statFilter:match(s) == true)
        end

        -- Multi-kind filter
        local multiFilter = reflect.filter("AstStatLocal", "AstStatLocalFunction")
        local localsAndFuncs = {}
        root:walk(function(node)
            table.insert(localsAndFuncs, node)
        end, multiFilter)

        assert(#localsAndFuncs == 2)
        assert(localsAndFuncs[1].kind == "AstStatLocal")
        assert(localsAndFuncs[2].kind == "AstStatLocalFunction")

        -- Multi-kind from table using unpack
        local tableFilter = reflect.filter(unpack({"AstStatLocal", "AstStatLocalFunction"}))
        assert(multiFilter == tableFilter)

        -- Node:walk with filter
        local funcStat = localsAndFuncs[2]
        local innerCalls = {}
        funcStat:walk(function(node)
            table.insert(innerCalls, node)
        end, callFilter)
        assert(#innerCalls == 1) -- print(a + b)

        -- Error on unrecognized kind or category
        local ok, err = pcall(reflect.filter, "NonExistentKind")
        assert(not ok)

        local ok2, err2 = pcall(reflect.filter, {"AstStatLocal", "InvalidCategory"})
        assert(not ok2)

        local ok3, err3 = pcall(reflect.filter, 123)
        assert(not ok3)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectAstMutations")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local src = "if x then a() else b() end\nlocal num = 10\nwhile cond do work() end\nlocal bin = 1 + 2\n"
        local doc = arena:parse(src)
        local root = doc:root()
        local stats = root:body()

        -- Test AstStatIf mutations and chaining
        local ifStat = stats[1]
        assert(ifStat.kind == "AstStatIf")
        local newCond = stats[2]:values()[1] -- number 10 expr
        assert(ifStat:setCondition(newCond) == ifStat)
        assert(ifStat:condition() == newCond)

        -- Test AstStatWhile mutations and chaining
        local whileStat = stats[3]
        assert(whileStat.kind == "AstStatWhile")
        assert(whileStat:hasDo() == true)
        assert(whileStat:setHasDo(false):setCondition(newCond) == whileStat)
        assert(whileStat:hasDo() == false)
        assert(whileStat:condition() == newCond)

        -- Test AstExprBinary mutations
        local binStat = stats[4]
        local binExpr = binStat:vars()[1] -- local bin
        local binVal = binStat:values()[1] -- 1 + 2
        assert(binVal.kind == "AstExprBinary")
        local left = binVal:left()
        local right = binVal:right()
        assert(left.kind == "AstExprConstantNumber")
        assert(left:value() == 1)
        assert(left:setValue(99) == left)
        assert(left:value() == 99)

        -- Swap left and right on binary expr
        assert(binVal:setLeft(right):setRight(left) == binVal)
        assert(binVal:left() == right)
        assert(binVal:right() == left)

        -- Test AstStatLocal isConst and exported
        local locStat = stats[2]
        assert(locStat:isConst() == false)
        assert(locStat:setIsConst(true):setExported(true) == locStat)
        assert(locStat:isConst() == true)
        assert(locStat:exported() == true)

        -- Test subtype mismatch raises error
        local okBad, errBad = pcall(function()
            ifStat:setCondition(locStat) -- locStat is an AstStat, not an AstExpr!
        end)
        assert(not okBad)

        local okBad2, errBad2 = pcall(function()
            ifStat:setThenBody(binVal) -- binVal is an AstExpr, not an AstStatBlock!
        end)
        assert(not okBad2)

        -- Test compound assign, function, type alias, string quote style, and attribute mutations
        local src2 = [[
            x += 1
            function foo(a: number): string return "hello" end
            type MyType = number
            @native
            local function bar() end
            local tbl = { k = 1 }
        ]]
        local doc2 = arena:parse(src2)
        local stats2 = doc2:root():body()

        -- AstStatCompoundAssign
        local compStat = stats2[1]
        assert(compStat.kind == "AstStatCompoundAssign")
        assert(compStat:op() == "+")
        assert(compStat:setOp("-") == compStat)
        assert(compStat:op() == "-")

        -- AstStatFunction
        local fnStat = stats2[2]
        assert(fnStat.kind == "AstStatFunction")
        local fnExpr = fnStat:func()
        assert(fnExpr.kind == "AstExprFunction")
        assert(fnExpr:setVararg(true):setDebugName("newName") == fnExpr)
        assert(fnExpr:vararg() == true)
        assert(fnExpr:debugname() == "newName")

        -- AstStatTypeAlias
        local aliasStat = stats2[3]
        assert(aliasStat.kind == "AstStatTypeAlias")
        assert(aliasStat:name() == "MyType")
        assert(aliasStat:setName("CustomType"):setExported(true) == aliasStat)
        assert(aliasStat:name() == "CustomType")
        assert(aliasStat:exported() == true)

        -- AstStatLocalFunction & AstAttr
        local locFnStat = stats2[4]
        assert(locFnStat.kind == "AstStatLocalFunction")
        assert(locFnStat:setIsConst(true) == locFnStat)
        assert(locFnStat:isConst() == true)
        local locFnExpr = locFnStat:func()
        local attrs = locFnExpr:attributes()
        assert(#attrs == 1)
        local attr = attrs[1]
        assert(attr.kind == "AstAttr")
        assert(attr:type() == "native")
        assert(attr:setType("checked"):setName("newAttr") == attr)
        assert(attr:type() == "checked")
        assert(attr:name() == "newAttr")

        -- AstExprTable & AstTableItem mutations
        local tblStat = stats2[5]
        local tblExpr = tblStat:values()[1]
        assert(tblExpr.kind == "AstExprTable")
        local items = tblExpr:items()
        assert(#items == 1)
        local itm = items[1]
        assert(itm.kind == "AstTableItem")
        assert(itm:itemKind() == "record")
        assert(itm:properties().kind == "AstTableItem")
        assert(itm:properties().itemKind == "record")
        assert(itm:setItemKind("list"):setValue(tblStat:values()[1]) == itm)
        assert(itm:itemKind() == "list")
        assert(tblExpr:setItems({}) == tblExpr)
        assert(#tblExpr:items() == 0)
        assert(tblExpr:setItems(items) == tblExpr)
        assert(#tblExpr:items() == 1)

        -- AstLocal mutations
        local locVar = tblStat:vars()[1] -- local tbl
        assert(locVar:name() == "tbl")
        assert(locVar:setName("myTbl"):setIsConst(true):setDepth(4) == locVar)
        assert(locVar:name() == "myTbl")
        assert(locVar:isConst() == true)
        assert(tostring(locVar:depth()) == "4")

        -- AstNode base metadata (origlocation, setHasSemicolon)
        local origLoc = tblStat:origlocation()
        assert(tblStat:setHasSemicolon(true) == tblStat)
        assert(tblStat:hasSemicolon() == true)
        assert(origLoc ~= nil)

        -- AstTableProp and AstTableIndexer mutations
        local doc3 = arena:parse("type T = { read foo: string, [number]: boolean }")
        local tyTable = doc3:root():body()[1]:type()
        local tprop = tyTable:props()[1]
        assert(tprop:name() == "foo")
        assert(tprop:setName("bar"):setAccess("write") == tprop)
        assert(tprop:name() == "bar")
        assert(tprop:access() == "write")

        local tindexer = tyTable:indexer()
        assert(tindexer:access() == "readwrite")
        assert(tindexer:setAccess("read") == tindexer)
        assert(tindexer:access() == "read")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectProperties")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);
    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()
        local doc = arena:parse("-- comment\nlocal x = 1\nif x then print(x) else print(2) end", true)
        assert(doc:allocator() == arena)
        local docProps = doc:properties()
        assert(docProps.id ~= nil)
        assert(docProps.allocator ~= nil)
        assert(docProps.allocator == arena)
        assert(docProps.root ~= nil)
        assert(type(docProps.source) == "string")
        assert(#docProps.comments == 1)
        assert(#docProps.errors == 0)
        assert(#docProps.lineOffsets >= 1)

        -- Comment aux properties
        local comment = docProps.comments[1]
        local commentProps = comment:properties()
        assert(commentProps.id ~= nil)
        assert(commentProps.kind == "AstComment")
        assert(commentProps.type == "single")
        assert(commentProps.origlocation ~= nil)
        assert(commentProps.text == "-- comment")

        -- AstStatLocal & AstLocal properties
        local root = docProps.root
        local statLocal = root:body()[1]
        local statProps = statLocal:properties()
        assert(statProps.id ~= nil)
        assert(statProps.kind == "AstStatLocal")
        assert(statProps.category == "stat")
        assert(statProps.hasSemicolon == false)
        assert(#statProps.vars == 1)
        assert(#statProps.values == 1)
        assert(statProps.origlocation ~= nil)
        assert(statLocal:prettyprint() == "local x = 1")

        local localVar = statProps.vars[1]
        local localProps = localVar:properties()
        assert(localProps.id ~= nil)
        assert(localProps.kind == "AstLocal")
        assert(localProps.name == "x")
        assert(localProps.origlocation ~= nil)
        assert(localProps.isConst == false)
        assert(tostring(localProps.depth) == "0")

        -- AstStatIf properties
        local statIf = root:body()[2]
        local ifProps = statIf:properties()
        assert(ifProps.id ~= nil)
        assert(ifProps.kind == "AstStatIf")
        assert(ifProps.category == "stat")
        assert(ifProps.condition ~= nil)
        assert(ifProps.thenbody ~= nil)
        assert(ifProps.elsebody ~= nil)

        -- CST properties
        local cst = statLocal:cst()
        if cst then
            local cstProps = cst:properties()
            assert(cstProps.id ~= nil)
            assert(cstProps.category == "cst")
        end
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("AstAllocator")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);
    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena1 = reflect.allocator()
        assert(typeof(arena1) == "AstAllocator")
        assert(arena1:properties().id ~= nil)

        local docA = arena1:parse("local a = 1")
        local docB = arena1:parse("local b = 2")
        assert(typeof(docA) == "AstDocument")
        assert(typeof(docB) == "AstDocument")
        assert(docA:allocator() == arena1)
        assert(docB:allocator() == arena1)

        -- This is safe
        local valB = docB:root():body()[1]:values()[1]
        docA:root():body()[1]:setValues({ valB })
        assert(docA:root():body()[1]:values()[1]:value() == 2)

        -- Cross-allocator mutation must throw an error
        local arena2 = reflect.allocator()
        local docC = arena2:parse("local c = 3")
        local valC = docC:root():body()[1]:values()[1]
        local ok, err = pcall(function()
            docA:root():body()[1]:setValues({ valC })
        end)
        assert(not ok)
        assert(string.find(err, "cross-allocator", 1, true) ~= nil)

        -- Cross-allocator mutation must throw an error
        local localC = docC:root():body()[1]:vars()[1]
        local okLocal, errLocal = pcall(function()
            docA:root():body()[1]:setVars({ localC })
        end)
        assert(not okLocal)
        assert(string.find(errLocal, "cross-allocator", 1, true) ~= nil)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("AstAllocatorDefaultNode")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()

        -- Ast Node Creation
        local numExpr = alloc:defaultnode("AstExprConstantNumber")
        assert(typeof(numExpr) == "AstItem")
        assert(numExpr.kind == "AstExprConstantNumber")
        assert(numExpr.category == "expr")
        assert(numExpr:value() == 0)
        assert(numExpr:setValue(42) == numExpr)
        assert(numExpr:value() == 42)

        local localStat = alloc:defaultnode("AstStatLocal")
        assert(typeof(localStat) == "AstItem")
        assert(localStat.kind == "AstStatLocal")
        assert(localStat.category == "stat")
        assert(#localStat:vars() == 0)
        assert(#localStat:values() == 0)
        assert(localStat:setValues({ numExpr }) == localStat)
        assert(#localStat:values() == 1)
        assert(localStat:values()[1] == numExpr)

        local genType = alloc:defaultnode("AstGenericType")
        assert(typeof(genType) == "AstItem")
        assert(genType.kind == "AstGenericType")
        assert(genType:setName("T") == genType)
        assert(genType:name() == "T")

        local genTypePack = alloc:defaultnode("AstGenericTypePack")
        assert(typeof(genTypePack) == "AstItem")
        assert(genTypePack.kind == "AstGenericTypePack")
        assert(genTypePack:setName("U...") == genTypePack)
        assert(genTypePack:name() == "U...")

        local attr = alloc:defaultnode("AstAttr")
        assert(typeof(attr) == "AstItem")
        assert(attr.kind == "AstAttr")

        -- Cst Node Creation
        local cstLocal = alloc:defaultnode("CstStatLocal")
        assert(typeof(cstLocal) == "AstItem")
        assert(cstLocal.kind == "CstStatLocal")
        assert(cstLocal.category == "cst")

        -- AstLocal Creation (dedicated userdata)
        local astLocal = alloc:defaultnode("AstLocal")
        assert(typeof(astLocal) == "AstItem")
        assert(astLocal.kind == "AstLocal")
        assert(astLocal:setName("myVar"):setIsConst(true) == astLocal)
        assert(astLocal:name() == "myVar")
        assert(astLocal:isConst() == true)

        -- Aux Creation
        local astComment = alloc:defaultnode("AstComment")
        assert(typeof(astComment) == "AstItem")
        assert(astComment.kind == "AstComment")

        local astTableProp = alloc:defaultnode("AstTableProp")
        assert(typeof(astTableProp) == "AstItem")
        assert(astTableProp.kind == "AstTableProp")
        assert(astTableProp:setName("myProp"):setAccess("read") == astTableProp)
        assert(astTableProp:name() == "myProp")
        assert(astTableProp:access() == "read")

        -- Attach newly constructed default nodes into a parsed document on the same allocator
        local doc = alloc:parse("local x = 1")
        local root = doc:root()
        root:body()[1]:setVars({ astLocal })
        root:body()[1]:setValues({ numExpr })
        assert(root:body()[1]:vars()[1] == astLocal)
        assert(root:body()[1]:values()[1] == numExpr)

        -- Cross-allocator safety check
        local alloc2 = reflect.allocator()
        local nodeOther = alloc2:defaultnode("AstExprConstantNumber")
        local okCross, errCross = pcall(function()
            root:body()[1]:setValues({ nodeOther })
        end)
        assert(not okCross)
        assert(string.find(errCross, "cross-allocator", 1, true) ~= nil)

        -- Error handling on unknown kind
        local okUnknown, errUnknown = pcall(function()
            alloc:defaultnode("NonExistentKind")
        end)
        assert(not okUnknown)
        assert(string.find(errUnknown, "unknown node kind 'NonExistentKind'", 1, true) ~= nil)

        -- Error handling on invalid argument types
        local okNil, errNil = pcall(function()
            (alloc :: any):defaultnode(123)
        end)
        assert(not okNil)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectCommentTracking")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);
    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local arena = reflect.allocator()

        -- Fast parse without processComments: leadingComments and trailingComments are empty
        local docNoComments = arena:parse("-- leading\nlocal x = 1 -- trailing\n")
        local statNo = docNoComments:root():body()[1]
        assert(#statNo:leadingComments() == 0)
        assert(#statNo:trailingComments() == 0)

        -- Parse with processComments: comments classified into leading and trailing
        local docWithComments = arena:parse("-- leading\nlocal x = 1 -- trailing\n", true, true)
        local root = docWithComments:root()
        local stat = root:body()[1]
        local leading = stat:leadingComments()
        local trailing = stat:trailingComments()
        assert(#leading == 1)
        assert(leading[1]:text() == "-- leading")
        assert(leading[1]:type() == "single")
        assert(leading[1]:origlocation().begin.x == 1)
        assert(#trailing == 1)
        assert(trailing[1]:text() == "-- trailing")
        assert(trailing[1]:type() == "single")
        assert(trailing[1]:origlocation().begin.x == 2)

        -- Check properties table contains leadingComments and trailingComments
        local props = stat:properties()
        assert(props.leadingComments ~= nil)
        assert(#props.leadingComments == 1)
        assert(props.leadingComments[1]:text() == "-- leading")
        assert(props.trailingComments ~= nil)
        assert(#props.trailingComments == 1)
        assert(props.trailingComments[1]:text() == "-- trailing")

        -- Mutating comments with AstComment userdata
        local newLeading = arena:defaultnode("AstComment"):setText("-- new leading"):setType("single")
        stat:setLeadingComments({ newLeading })
        assert(#stat:leadingComments() == 1)
        assert(stat:leadingComments()[1]:text() == "-- new leading")

        local customComment = arena:defaultnode("AstComment"):setText("-- custom trailing"):setType("single")
        stat:setTrailingComments({ customComment })
        assert(#stat:trailingComments() == 1)
        assert(stat:trailingComments()[1]:text() == "-- custom trailing")
        assert(stat:trailingComments()[1]:type() == "single")

        -- Setting string directly should fail
        local okString = pcall(function()
            stat:setLeadingComments({ "-- raw string" })
        end)
        assert(not okString)

        -- ParseExpr with processComments
        local expr = arena:parseexpr("1 + 2 -- expr comment\n", true, true)
        local exprTrailing = expr:trailingComments()
        assert(#exprTrailing == 1)
        assert(exprTrailing[1]:text() == "-- expr comment")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectRequiresPinnedUserdataMetatables")
{
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, false};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    lua_pushcfunction(L, Luau::luaopen_reflect, "luaopen_reflect");
    CHECK_NE(lua_pcall(L, 0, 1, 0), 0);
    CHECK(std::string(lua_tostring(L, -1)).find("LuauUdataMetatablePinned") != std::string::npos);
}

TEST_CASE("ReflectMetatablesSurviveCollection")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    lua_gc(L, LUA_GCCOLLECT, 0);
    lua_gc(L, LUA_GCCOLLECT, 0);

    const char* script = R"LUA(
        local root = reflect.allocator():parse("local x = 1"):root()
        assert(root.kind == "AstStatBlock")
        assert(root:body()[1]:vars()[1]:name() == "x")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectMalformedTreesRaiseErrors")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local alloc = reflect.allocator()
        local function visitAll(node)
            node:walk(function() return true end)
        end

        local ifStat = alloc:defaultnode("AstStatIf")
        fails(function() visitAll(ifStat) end, "cannot walk a tainted AST")
        fails(function() return ifStat:prettyprint() end, "cannot prettyprint a tainted AST")
        fails(function() return ifStat:untaint() end, "AstStatIf.condition is nil")
        fails(function() return ifStat:children() end, "AstStatIf.condition is nil")

        ifStat:setCondition(alloc:parseexpr("x")):setThenBody(alloc:defaultnode("AstStatBlock"))
        assert(#ifStat:children() == 2)
        fails(function() visitAll(ifStat) end, "tainted AST")
        assert(ifStat:untaint() == ifStat)
        visitAll(ifStat)
        visitAll(ifStat:thenbody())
        assert(ifStat:untaint() == ifStat)
        assert(ifStat:elsebody() == nil)

        local parsedIf = alloc:parse("if x then y() end"):root():body()[1]
        visitAll(parsedIf)
        parsedIf:setCondition(nil)
        fails(function() return parsedIf:children() end, "AstStatIf.condition is nil")
        fails(function() visitAll(parsedIf) end, "tainted AST")
        fails(function() return parsedIf:untaint() end, "AstStatIf.condition is nil")

        local doc = alloc:parse("while true do if x then y() end end")
        doc:root():body()[1]:body():body()[1]:setThenBody(nil)
        fails(function() visitAll(doc:root()) end, "tainted AST")
        fails(function() return doc:root():prettyprint() end, "tainted AST")
        fails(function() return doc:root():untaint() end, "AstStatIf.thenbody is nil")

        local failed = alloc:parse("local a = 1"):root()
        fails(function() failed:setBody({ reflect.allocator():parse("b()"):root():body()[1] }) end, "cross-allocator")
        fails(function() failed:setBody({ failed:body()[1]:vars()[1] }) end, "AstNode")
        visitAll(failed)
        assert(failed:prettyprint() == "local a = 1")

        local sameAllocator = alloc:parse("local keep = 1")
        local otherAllocator = reflect.allocator():parse("local far = 1")
        failed:setBody({})
        fails(function() visitAll(failed) end, "tainted AST")
        fails(function() visitAll(sameAllocator:root()) end, "tainted AST")
        visitAll(sameAllocator:root():untaint())
        visitAll(otherAllocator:root())
        visitAll(alloc:parse("local later = 1"):root())

        local global = alloc:defaultnode("AstExprGlobal")
        fails(function() visitAll(global) end, "tainted AST")
        visitAll(global:untaint())
        fails(function() return global:prettyprint() end, "AstExprGlobal.name is nil")
        assert(global:setName("g"):prettyprint() == "g")
        fails(function() return alloc:defaultnode("AstExprLocal"):untaint() end, "AstExprLocal.local is nil")

        fails(function() return global:setName("") end, "a name cannot be an empty string")
        assert(global:name() == "g")

        local renamed = alloc:parse("local x = y"):root()
        renamed:body()[1]:values()[1]:setName("z")
        renamed:body()[1]:setHasSemicolon(true)
        assert(renamed:prettyprint() == "local x = z;")
        visitAll(renamed)

        local ref = alloc:parse("local v: Foo<number, string, boolean> = nil"):root():body()[1]:vars()[1]:annotation()
        local params = ref:parameters()
        ref:setParameters({ params[1], nil, params[3] })
        fails(function() return ref:untaint() end, "AstTypeReference.parameters[2] is nil")

        local tbl = alloc:parseexpr("{ x = 1 }")
        local item = tbl:items()[1]
        item:setKey(alloc:parseexpr("1 + 2"))
        tbl:setItems({ item })
        fails(function() visitAll(tbl) end, "tainted AST")
        visitAll(tbl:untaint())
        fails(function() return tbl:prettyprint() end, "is a record item whose key is not a string constant")
        item:setItemKind("general")
        fails(function() return tbl:prettyprint() end, "tainted AST")
        tbl:setItems({ item })
        assert(string.gsub(tbl:untaint():prettyprint(), "%s", "") == "{[1+2]=1}")

        local keyless = alloc:parseexpr("{ 1 }")
        keyless:items()[1]:setItemKind("record")
        fails(function() return keyless:untaint() end, "has no key")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectCyclesAndDeepNesting")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local alloc = reflect.allocator()

        local root = alloc:parse("do end"):root()
        local block = root:body()[1]
        block:setBody({ block })
        fails(function() return root:untaint() end, "AstStatBlock is nested inside itself")
        fails(function() return root:prettyprint() end, "tainted AST")
        fails(function() root:walk(function() return true end) end, "tainted AST")
        assert(#root:children() == 1)

        local bin = alloc:parseexpr("a + b")
        local group = alloc:defaultnode("AstExprGroup"):setExpr(bin)
        bin:setLeft(group)
        fails(function() return group:untaint() end, "is nested inside itself")
        fails(function() return bin:untaint() end, "is nested inside itself")

        local x = alloc:parseexpr("x")
        local sum = alloc:parseexpr("a + b"):setLeft(x):setRight(x)
        assert(string.gsub(sum:untaint():prettyprint(), "%s", "") == "x+x")
        local shared = x
        for i = 1, 64 do
            shared = alloc:parseexpr("a + b"):setLeft(shared):setRight(shared)
        end
        assert(#shared:children() == 2)
        assert(shared:untaint() == shared)

        local deepRoot = alloc:parse("do end"):root()
        local deepBlock = deepRoot:body()[1]
        deepRoot:untaint()
        local tall = alloc:parse("x()"):root():body()[1]
        for i = 1, 500 do
            tall = alloc:defaultnode("AstStatBlock"):setBody({ tall })
        end
        deepBlock:setBody({ tall })
        assert(deepBlock:untaint() == deepBlock)
        deepBlock:walk(function() return true end)
        fails(function() deepRoot:walk(function() return true end) end, "tainted AST")
        deepRoot:untaint():walk(function() return true end)

        local function nest(depth)
            local expr = alloc:parseexpr("1")
            for i = 1, depth do
                expr = alloc:defaultnode("AstExprGroup"):setExpr(expr)
            end
            return expr
        end
        assert(#nest(100):untaint():prettyprint() == 201)
        fails(function() return nest(5000):untaint() end, "nesting is deeper than the limit")
        fails(function() nest(5000):walk(function() return true end) end, "tainted AST")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectCrossAllocatorAuxValues")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local a = reflect.allocator()
        local b = reflect.allocator()

        local tblA = a:parseexpr("{ 1 }")
        local tblB = b:parseexpr("{ x = 'from b' }")
        fails(function() tblA:setItems(tblB:items()) end, "cross-allocator")
        assert(string.gsub(tblA:prettyprint(), "%s", "") == "{1}")

        local refA = a:parse("local v: Foo<number> = nil"):root():body()[1]:vars()[1]:annotation()
        local refB = b:parse("local w: Bar<string, boolean> = nil"):root():body()[1]:vars()[1]:annotation()
        fails(function() refA:setParameters(refB:parameters()) end, "cross-allocator")

        local typeA = a:parse("type T = { x: number, [string]: boolean }"):root():body()[1]:type()
        local typeB = b:parse("type U = { y: string, [number]: string }"):root():body()[1]:type()
        fails(function() typeA:setProps(typeB:props()) end, "cross-allocator")
        fails(function() typeA:setIndexer(typeB:indexer()) end, "cross-allocator")
        local fnA = a:parse("type F = (number) -> string"):root():body()[1]:type()
        local fnB = b:parse("type G = (boolean, boolean) -> ()"):root():body()[1]:type()
        fails(function() fnA:setArgTypes(fnB:argTypes()) end, "cross-allocator")

        local tblA2 = a:parseexpr("{ y = 2 }")
        tblA:setItems(tblA2:items())
        assert(string.gsub(tblA:untaint():prettyprint(), "%s", "") == "{y=2}")
        local typeA2 = a:parse("type V = { z: boolean }"):root():body()[1]:type()
        typeA:setProps(typeA2:props())
        assert(typeA:props()[1]:name() == "z")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectNoStructuralEditsDuringWalk")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local alloc = reflect.allocator()
        local root = alloc:parse("if x then y = 1 end"):root()
        local ifStat = root:body()[1]

        local renamed = 0
        root:walk(function(node)
            if node.kind == "AstExprGlobal" then
                node:setName(node:name() .. "_")
                renamed += 1
            end
            return true
        end)
        assert(renamed == 2)
        assert(ifStat:condition():name() == "x_")

        fails(function()
            root:walk(function(node)
                if node.kind == "AstStatIf" then
                    node:setCondition(alloc:parseexpr("z"))
                end
                return true
            end)
        end, "cannot change the structure of an AST while it is being walked")
        assert(ifStat:condition():name() == "x_")

        ifStat:setCondition(alloc:parseexpr("z"))
        assert(ifStat:condition():name() == "z")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectDefaultNodesThatNeedAFlag")
{
    for (bool enabled : {false, true})
    {
        ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
        ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
        ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
        ScopedFastFlag sffClasses{FFlag::LuwuClasses, enabled};
        ScopedFastFlag sffCstAttr{FFlag::LuauCstAttr, enabled};
        std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
        lua_State* L = globalState.get();
        luaL_openlibs(L);

        Luau::luaopen_reflect(L);
        lua_setglobal(L, "reflect");

        lua_pushboolean(L, enabled);
        lua_setglobal(L, "enabled");

        const char* script = R"LUA(
            local alloc = reflect.allocator()
            for kind, flag in { AstStatClass = "LuwuClasses", CstAttr = "LuauCstAttr", CstParametrizedAttr = "LuauCstAttr", CstAttrList = "LuauCstAttr" } do
                local ok, res = pcall(function() return alloc:defaultnode(kind) end)
                assert(ok == enabled, kind)
                if not ok then
                    assert(string.find(res, "cannot create a default " .. kind .. ": FFlag " .. flag .. " is not enabled", 1, true) ~= nil, res)
                end
            end

            if enabled then
                local class = alloc:defaultnode("AstStatClass")
                local prop = alloc:defaultnode("AstClassProperty"):setName("p")
                local method = alloc:defaultnode("AstClassMethod"):setName("m")
                class:setMembers({ prop, method })
                assert(#class:members() == 2)
                assert(class:members()[1]:name() == "p")
                assert(class:members()[2]:name() == "m")

                class:setName(alloc:defaultnode("AstLocal"):setName("C"))
                local ok, err = pcall(function() return class:children() end)
                assert(not ok and string.find(err, "AstStatClass.members[2] has no function", 1, true) ~= nil, err)
            end
        )LUA";

        CHECK_EQ(dostring(L, script), 0);
    }
}

TEST_CASE("ReflectEveryDefaultNodeIsSafeToUse")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    lua_newtable(L);
    for (int i = 0; i < int(Luau::ReflectAtom::Count); i++)
    {
        lua_pushstring(L, Luau::getAtomString(Luau::ReflectAtom(i)));
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, "atoms");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        local made = 0

        for _, name in atoms do
            local ok, node = pcall(function() return alloc:defaultnode(name) end)
            if not ok then
                continue
            end
            made += 1

            pcall(function() return tostring(node) end)
            pcall(function() return node:properties() end)
            if typeof(node) == "AstItem" then
                pcall(function() return node:children() end)
                pcall(function() node:walk(function() return true end) end)
                pcall(function() return node:prettyprint() end)
                pcall(function() return node:untaint() end)
                pcall(function() node:walk(function() return true end) end)
                pcall(function() return node:prettyprint() end)
                local parent = alloc:defaultnode("AstStatExpr")
                pcall(function() parent:setExpr(node) return parent:untaint():prettyprint() end)
                local group = alloc:defaultnode("AstTypeGroup")
                pcall(function() group:setType(node) return group:untaint():prettyprint() end)
            end
        end

        assert(made > 100)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectOpenTwice")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "first");
    Luau::luaopen_reflect(L);
    lua_setglobal(L, "second");

    const char* script = R"LUA(
        local doc = first.allocator():parse("local x = 1")
        assert(second.filter("stat"):match(doc:root()))
        assert(second.allocator():parse("return 1"):root().kind == "AstStatBlock")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectUseAtomsWithEmbedderAtoms")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    lua_callbacks(L)->useratom = [](lua_State*, const char*, size_t l) -> int16_t {
        return int16_t(l);
    };

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.allocator():parse("local x = 1")
        local root = doc:root()
        assert(root.kind == "AstStatBlock")
        assert(doc:source() == "local x = 1")
        assert(root:body()[1]:vars()[1]:name() == "x")
        assert(#root:children() == 1)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectPrettyprintRoot")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        assert(alloc:parse("local x = 1 + 2"):root():prettyprint() == "local x = 1 + 2")
        assert(alloc:parse("local x = 1\nif y then z() end"):root():prettyprint() == "local x = 1\nif y then\n    z()\nend")
        assert(alloc:parse(""):root():prettyprint() == "")

        local doc = alloc:parse("if y then z() end")
        assert(doc:root():body()[1]:prettyprint() == "if y then\n    z()\nend")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectEmptyFilter")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local root = reflect.allocator():parse("local x = 1"):root()
        local empty = reflect.filter()
        assert(not empty:match(root))

        local visited = 0
        root:walk(function()
            visited += 1
            return true
        end, empty)
        assert(visited == 0)

        root:walk(function()
            visited += 1
            return true
        end)
        assert(visited == 3)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectPrettyprintDeclarations")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()

        local exact = {
            "declare function foo(x: number, ...: string): boolean",
            "declare function baz(): (number, string)",
            "declare function quux(a: string)",
            "declare extern type Empty with\nend",
            "declare extern type Point extends Base with\n    x: number\n    read y: number\n    write z: number\n    function dot(self, other: Point): number\n    function scale(self, ...: number)\n    [number]: string\nend",
            "local before = 1\ndeclare extern type Point with\n    x: number\nend\nlocal after = 2",
            "type T = number?",
        }
        for _, source in exact do
            local doc = alloc:parse(source)
            assert(#doc:errors() == 0, source)
            assert(doc:root():prettyprint() == source, doc:root():prettyprint())
        end

        local quoted = alloc:parse("declare extern type Named with\n    [\"two words\"]: string\nend")
        local printed = quoted:root():prettyprint()
        local reparsed = alloc:parse(printed)
        assert(#reparsed:errors() == 0, printed)
        assert(reparsed:root():body()[1]:props()[1]:name() == "two words")

        local optional
        alloc:parse("type T = number?"):root():walk(function(node)
            if node.kind == "AstTypeOptional" then
                optional = node
            end
            return true
        end)
        assert(optional:prettyprint() == "?")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectFunctionArgDefaults")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sffDefaults{FFlag::LuwuDefaultArguments, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        local doc = alloc:parse("local function f(x, y = 2, z = x + 1) end")
        assert(#doc:errors() == 0)

        local fn = doc:root():body()[1]:func()
        local defaults = fn:argsDefaults()
        assert(#defaults == 3)
        assert(defaults[1] == false)
        assert(defaults[2].kind == "AstExprConstantNumber")
        assert(defaults[3].kind == "AstExprBinary")
        assert(doc:root():prettyprint() == "local function f(x, y = 2, z = x + 1)\nend")

        local args = fn:args()
        fn:setArgs({ args[1], args[2] })
        assert(#fn:argsDefaults() == 2)
        assert(doc:root():untaint():prettyprint() == "local function f(x, y = 2)\nend")

        fn:setArgs({ args[2], args[1], args[3] })
        assert(#fn:argsDefaults() == 3)
        assert(fn:argsDefaults()[1] == defaults[2])
        assert(fn:argsDefaults()[3] == false)
        assert(doc:root():untaint():prettyprint() == "local function f(y = 2, x, z)\nend")

        fn:setArgs({ args[1], args[2] })
        assert(fn:setArgsDefaults({ false, defaults[3] }) == fn)
        assert(fn:argsDefaults()[1] == false)
        assert(fn:argsDefaults()[2] == defaults[3])
        assert(string.gsub(doc:root():untaint():prettyprint(), "%s", "") == "localfunctionf(x,y=x+1)end")

        fn:setArgsDefaults({ defaults[2] })
        assert(#fn:argsDefaults() == 2)
        assert(fn:argsDefaults()[2] == false)
        assert(doc:root():untaint():prettyprint() == "local function f(x = 2, y)\nend")

        local okLong, errLong = pcall(function() fn:setArgsDefaults({ false, false, false }) end)
        assert(not okLong and string.find(errLong, "argsDefaults has more entries than args", 1, true) ~= nil, errLong)

        fn:setArgsDefaults({})
        assert(#fn:argsDefaults() == 0)
        assert(doc:root():untaint():prettyprint() == "local function f(x, y)\nend")
        fn:setArgsDefaults({ false, defaults[3] })

        assert(not pcall(function() fn:setArgsDefaults({ false, doc:root() }) end))
        local okCross, errCross = pcall(function() fn:setArgsDefaults({ false, reflect.allocator():parseexpr("1") }) end)
        assert(not okCross)
        assert(string.find(errCross, "cross-allocator", 1, true) ~= nil)

        local plain = alloc:parse("local function g(a, b) end"):root():body()[1]:func()
        assert(#plain:argsDefaults() == 2)
        assert(plain:argsDefaults()[1] == false and plain:argsDefaults()[2] == false)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectItemCategories")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local alloc = reflect.allocator()
        local doc = alloc:parse("local t: { x: number } = { 1 }", true)
        assert(typeof(alloc) == "AstAllocator")
        assert(typeof(doc) == "AstDocument")

        local stat = doc:root():body()[1]
        assert(typeof(stat) == "AstItem")
        assert(stat.kind == "AstStatLocal")
        assert(stat.category == "stat")
        assert(tostring(stat) == "AstNode(AstStatLocal)")

        local loc = stat:vars()[1]
        assert(typeof(loc) == "AstItem")
        assert(loc.kind == "AstLocal")
        assert(loc.category == "local")
        assert(tostring(loc) == "AstLocal(t)")

        local prop = loc:annotation():props()[1]
        assert(typeof(prop) == "AstItem")
        assert(prop.kind == "AstTableProp")
        assert(prop.category == "aux")
        assert(tostring(prop) == "AstTableProp(x)")

        local item = stat:values()[1]:items()[1]
        assert(item.kind == "AstTableItem")
        assert(item:itemKind() == "list")
        assert(item.category == "aux")
        assert(tostring(item) == "AstTableItem")
        assert(item ~= prop)
        assert(item == stat:values()[1]:items()[1])

        local cst = stat:cst()
        assert(typeof(cst) == "AstItem")
        assert(cst.kind == "CstStatLocal")
        assert(cst.category == "cst")
        assert(tostring(cst) == "CstNode(CstStatLocal)")

        assert(stat ~= loc)
        assert(loc ~= prop)
        assert(stat == doc:root():body()[1])
        assert(loc == stat:vars()[1])
        assert(cst == stat:cst())

        fails(function() stat:setVars({ prop }) end, "AstLocal")
        fails(function() loc:annotation():setProps({ loc }) end, "AstTableProp")
        fails(function() loc:annotation():setProps({ item }) end, "AstTableProp")
        fails(function() stat:values()[1]:setItems({ prop }) end, "AstTableItem")
        fails(function() return prop:walk(function() end) end, "walk is not a valid method of AstTableProp")
        fails(function() stat:setValues({ loc }) end, "AstNode")
        fails(function() return reflect.filter("stat"):match(loc) end, "AstNode")
        fails(function() return loc:walk(function() end) end, "walk is not a valid method of AstLocal")
        fails(function() return cst:setName("x") end, "setName is not a valid method of CstNode")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectAuxWriteThrough")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local alloc = reflect.allocator()
        local doc = alloc:parse("type T = { x: number, y: string }")
        local tbl = doc:root():body()[1]:type()

        local prop = tbl:props()[1]
        prop:setName("renamed")
        assert(tbl:props()[1]:name() == "renamed")
        assert(string.gsub(doc:root():prettyprint(), "%s", "") == "typeT={renamed:number,y:string}")

        assert(tbl:props()[1] == prop)
        assert(tbl:props()[1].id == prop.id)
        assert(tbl:props()[1] ~= tbl:props()[2])

        local fresh = alloc:defaultnode("AstTableProp")
        fresh:setName("z"):setType(alloc:parse("type U = boolean"):root():body()[1]:type())
        tbl:setProps({ prop, fresh })
        fresh:setName("w")
        assert(tbl:props()[2]:name() == "w")
        assert(tbl:props()[2] == fresh)
        prop:setName("first")
        assert(tbl:props()[1]:name() == "first")

        local old = tbl:props()[1]
        tbl:setProps({ fresh })
        old:setName("detached")
        assert(#tbl:props() == 1)
        assert(tbl:props()[1]:name() == "w")
        assert(old:name() == "detached")

        local expr = alloc:parseexpr("{ a = 1 }")
        expr:items()[1]:setValue(alloc:parseexpr("2"))
        assert(string.gsub(expr:untaint():prettyprint(), "%s", "") == "{a=2}")

        local indexed = alloc:parse("type I = { [string]: number }"):root():body()[1]:type()
        indexed:indexer():setAccess("read")
        assert(indexed:indexer():access() == "read")
        assert(indexed:indexer() == indexed:indexer())

        local fn = alloc:parse("type F = (number) -> string"):root():body()[1]:type()
        fn:argTypes():setTypes({})
        assert(#fn:argTypes():types() == 0)

        fails(function() doc:root():walk(function() return true end) end, "tainted AST")
        doc:root():untaint()

        fails(function()
            doc:root():walk(function(node)
                if node.kind == "AstTypeTable" then
                    node:props()[1]:setType(nil)
                end
                return true
            end)
        end, "cannot change the structure of an AST while it is being walked")

        doc:root():walk(function(node)
            if node.kind == "AstTypeTable" then
                node:props()[1]:setName("inwalk")
            end
            return true
        end)
        assert(tbl:props()[1]:name() == "inwalk")

        tbl:props()[1]:setType(nil)
        fails(function() return doc:root():prettyprint() end, "tainted AST")
        fails(function() return doc:root():untaint() end, "AstTypeTable.props[1] has no type")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectCommentsAreShared")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end

        local arena = reflect.allocator()
        local doc = arena:parse("-- lead\nlocal x = 1 -- trail\n", false, true)
        local stat = doc:root():body()[1]

        local lead = stat:leadingComments()[1]
        assert(lead:text() == "-- lead")
        lead:setText("-- edited")
        assert(stat:leadingComments()[1]:text() == "-- edited")
        assert(stat:leadingComments()[1] == lead)
        assert(doc:comments()[1] == lead)
        assert(doc:comments()[1]:text() == "-- edited")
        assert(doc:comments()[2] == stat:trailingComments()[1])

        local added = arena:defaultnode("AstComment"):setText("-- added")
        stat:setLeadingComments({ lead, added })
        added:setType("block")
        assert(stat:leadingComments()[2]:type() == "block")
        assert(stat:leadingComments()[2] == added)

        fails(function() stat:setLeadingComments({ reflect.allocator():defaultnode("AstComment") }) end, "cross-allocator")
        fails(function() stat:setLeadingComments({ stat }) end, "expected AstComment at index 1")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectPrettyprintComments")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()

        local exact = {
            "-- header\nlocal x = 1 -- trailing\n-- before\nlocal y = 2\n-- last",
            "--[=[ block\n     comment ]=]\nlocal x = 1",
            "if x then\n    -- inside\n    y = 2 -- same line\nend",
            "if x then\n    y()\n    -- about the next branch\nelseif z then\n    w()\nend",
            "local function f()\n    -- first\n    -- second\n    return 1\nend",
            "while x do\n    -- only a comment\nend",
        }
        for _, source in exact do
            for _, processed in { true, false } do
                local doc = alloc:parse(source, false, processed)
                assert(doc:root():prettyprint() == source, doc:root():prettyprint())
            end
        end

        local moved = alloc:parse("local t = { 1, -- inside\n  2 }\nlocal after = 3\n")
        assert(moved:root():prettyprint() == "local t = { 1, 2 }\n-- inside\nlocal after = 3")

        local doc = alloc:parse("-- lead\nlocal x = 1 -- trail\nif x then\n    -- inner\n    y()\nend", false, true)
        local first = doc:root():body()[1]
        assert(first:prettyprint() == "local x = 1")
        assert(doc:root():body()[2]:prettyprint() == "if x then\n    -- inner\n    y()\nend")

        first:leadingComments()[1]:setText("-- edited")
        first:setTrailingComments({})
        assert(doc:root():prettyprint() == "-- edited\nlocal x = 1\nif x then\n    -- inner\n    y()\nend")

        local tail = alloc:parse("while x do\n    y()\n    -- tail\nend -- after\nz()", false, true)
        local loop = tail:root():body()[1]
        assert(#loop:body():trailingComments() == 1)
        assert(loop:body():trailingComments()[1]:text() == "-- tail")
        assert(#loop:trailingComments() == 1)
        assert(loop:trailingComments()[1]:text() == "-- after")
        assert(#tail:root():body()[2]:leadingComments() == 0)
        assert(tail:root():prettyprint() == "while x do\n    y()\n    -- tail\nend -- after\nz()")

        local lines = alloc:parse("foo()"):root()
        lines:body()[1]:setTrailingComments({
            alloc:defaultnode("AstComment"):setText("-- a"),
            alloc:defaultnode("AstComment"):setText("-- b"),
        })
        assert(lines:prettyprint() == "foo() -- a\n-- b")
        assert(#alloc:parse(lines:prettyprint()):comments() == 2)

        local block = alloc:parse(""):root()
        local one = alloc:parse("foo()"):root():body()[1]
        local two = alloc:parse("bar()"):root():body()[1]
        one:setLeadingComments({ alloc:defaultnode("AstComment"):setText("-- one") })
        one:setTrailingComments({ alloc:defaultnode("AstComment"):setText("-- two") })
        two:setLeadingComments({ alloc:defaultnode("AstComment"):setText("--[[ three ]]"):setType("block") })
        block:setBody({ one, two })
        assert(#block:body()[1]:leadingComments() == 1)
        assert(block:untaint():prettyprint() == "-- one\nfoo() -- two\n--[[ three ]]\nbar()")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectPrettyprintFormatting")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        local function format(source)
            local doc = alloc:parse(source)
            assert(#doc:errors() == 0, source)
            return doc:root():prettyprint()
        end

        assert(format("local   x,y = 1,  'two'") == 'local x, y = 1, "two"')
        assert(format("if x then y() elseif z then w() else q() end") == "if x then\n    y()\nelseif z then\n    w()\nelse\n    q()\nend")
        assert(format("for i=1,10,2 do print(i) end") == "for i = 1, 10, 2 do\n    print(i)\nend")
        assert(format("for k,v in pairs(t) do continue end") == "for k, v in pairs(t) do\n    continue\nend")
        assert(format("repeat x+=1 until x>10") == "repeat\n    x += 1\nuntil x > 10")
        assert(format("do local inner = 1 end") == "do\n    local inner = 1\nend")
        assert(format("function M.sub:method<T>(a: T, ...: number): T return a end") == "function M.sub:method<T>(a: T, ...: number): T\n    return a\nend")
        assert(format("local t = {1,key='v',['not plain']=true,[1+2]=f}") == 'local t = {\n    1,\n    key = "v",\n    ["not plain"] = true,\n    [1 + 2] = f,\n}')
        assert(format("local t = {1,2}") == "local t = { 1, 2 }")
        assert(format("local t = {}") == "local t = {}")
        assert(format("type P<K, V = string> = { read key: K, value: V, [number]: boolean }") == "type P<K, V = string> = { read key: K, value: V, [number]: boolean }")
        assert(format("type F = (name: string, ...number) -> (boolean, string)") == "type F = (name: string, ...number) -> (boolean, string)")
        assert(format("type O = (() -> ())?") == "type O = (() -> ())?")
        assert(format("export type A = {number}") == "export type A = { number }")
        assert(format("local v = -x^2+(a or b) and not c") == "local v = -x ^ 2 + (a or b) and not c")
        assert(format("local n = - -x") == "local n = - -x")
        assert(format("local w = f\n;(g or h)()") == "local w = f;\n(g or h)()")
        assert(format("local s = `a {b} c`") == "local s = `a {b} c`")

        local source = "local a = 1\nlocal function f(x)\n    return x\nend\nreturn f(a)"
        assert(format(source) == source)
        assert(format(format(source)) == source)

        local mul = alloc:parseexpr("x * y")
        mul:setLeft(alloc:parseexpr("a + b"))
        assert(mul:untaint():prettyprint() == "(a + b) * y")

        local pow = alloc:parseexpr("x ^ y")
        pow:setLeft(alloc:parseexpr("-a"))
        assert(pow:untaint():prettyprint() == "(-a) ^ y")

        local neg = alloc:parseexpr("-x")
        neg:setExpr(alloc:parseexpr("-y"))
        assert(neg:untaint():prettyprint() == "- -y")

        local call = alloc:parseexpr("f()")
        call:setFunc(alloc:parseexpr("function() end"))
        assert(call:untaint():prettyprint() == "(function()\nend)()")

        local block = alloc:parse(""):root()
        block:setBody({
            alloc:parse("local a = f"):root():body()[1],
            alloc:parse("(g)()"):root():body()[1],
            alloc:parse("bar()"):root():body()[1],
        })
        assert(block:untaint():prettyprint() == "local a = f;\n(g)()\nbar()")
        assert(#alloc:parse(block:prettyprint()):errors() == 0)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectPrettyprintClasses")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sffClasses{FFlag::LuwuClasses, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        local function format(source)
            local doc = alloc:parse(source)
            assert(#doc:errors() == 0, source)
            local out = doc:root():prettyprint()
            local again = alloc:parse(out)
            assert(#again:errors() == 0, out)
            assert(again:root():prettyprint() == out, out)
            return out
        end

        assert(format("class P x: number = 0 function len(self): number return self.x end end") == "class P\n    x: number = 0\n    function len(self): number\n        return self.x\n    end\nend")
        assert(format("class A public x: number public function f(self) end end") == "class A\n    public x: number\n    public function f(self)\n    end\nend")
        assert(format("class B private x: number public const y: number = 1 end") == "class B\n    private x: number\n    public const y: number = 1\nend")
        assert(format("class C private (public const key: string, private secret: string) end") == "class C private (public const key: string, private secret: string)\nend")
        assert(format("class D(text: string, public other: number) private text: string end") == "class D(text: string, public other: number)\n    private text: string\nend")
        assert(format("class F() end") == "class F()\nend")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectClassVisibility")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sffClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag sffDefaults{FFlag::LuwuDefaultArguments, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        local function class(source)
            local doc = alloc:parse(source)
            assert(#doc:errors() == 0, source)
            return doc:root():body()[1]
        end
        local function printed(cls)
            local out = cls:untaint():prettyprint()
            assert(#alloc:parse(out):errors() == 0, out)
            return out
        end

        local a = class("class A public x: number public function f(self) end end")
        assert(a:explicitVisibility() == true)
        assert(a:primaryConstructor() == nil)
        assert(a:properties().explicitVisibility == true)
        assert(printed(a) == "class A\n    public x: number\n    public function f(self)\n    end\nend")
        assert(a:setExplicitVisibility(false) == a)
        assert(printed(a) == "class A\n    x: number\n    function f(self)\n    end\nend")

        local b = class("class B x: number y: number end")
        assert(b:explicitVisibility() == false)
        local members = b:members()
        assert(members[1]:visibility() == "public")
        assert(members[1]:isConst() == false)
        assert(members[1]:properties().visibility == "public")
        members[1]:setVisibility("private")
        members[2]:setIsConst(true)
        assert(printed(b) == "class B\n    private x: number\n    public const y: number\nend")
        assert(not pcall(function() members[1]:setVisibility("protected") end))

        local m = class("class M private function f(self) end public function g(self) end end")
        assert(m:members()[1]:visibility() == "private")
        m:members()[1]:setVisibility("public")
        assert(printed(m) == "class M\n    public function f(self)\n    end\n    public function g(self)\n    end\nend")
        m:setExplicitVisibility(false)
        assert(printed(m) == "class M\n    function f(self)\n    end\n    function g(self)\n    end\nend")

        local c = class("class C public (public const key: string, public secret: string = 'a') end")
        local ctor = c:primaryConstructor()
        assert(ctor.kind == "AstClassPrimaryConstructor")
        assert(ctor.category == "aux")
        assert(ctor:visibility() == "public")
        assert(ctor:explicitVisibility() == true)
        assert(#ctor:args() == 2 and ctor:args()[1]:name() == "key")
        assert(ctor:argsDefaults()[1] == false)
        assert(ctor:argsDefaults()[2].kind == "AstExprConstantString")
        local qualifiers = ctor:argsQualifiers()
        assert(qualifiers[1].kind == "AstClassPrimaryConstructorParamQualifiers")
        assert(qualifiers[1]:visibility() == "public")
        assert(qualifiers[1]:isConst() == true)
        assert(qualifiers[1]:explicitVisibility() == true)
        assert(qualifiers[2]:isConst() == false)
        assert(printed(c) == 'class C public (public const key: string, public secret: string = "a")\nend')

        ctor:setExplicitVisibility(false)
        assert(printed(c) == 'class C(public const key: string, public secret: string = "a")\nend')
        ctor:setVisibility("private")
        qualifiers[2]:setVisibility("private")
        qualifiers[1]:setIsConst(false)
        assert(printed(c) == 'class C private (public key: string, private secret: string = "a")\nend')

        local d = class("class D(public text: string) public text: string private n: number end")
        assert(printed(d) == "class D(public text: string)\n    public text: string\n    private n: number\nend")
        d:primaryConstructor():argsQualifiers()[1]:setExplicitVisibility(false)
        assert(printed(d) == "class D(text: string)\n    public text: string\n    private n: number\nend")

        local e = class("class E(text: string, public other: number) private text: string end")
        assert(e:primaryConstructor():argsQualifiers()[1]:explicitVisibility() == false)
        assert(e:primaryConstructor():argsQualifiers()[2]:explicitVisibility() == true)
        assert(printed(e) == "class E(text: string, public other: number)\n    private text: string\nend")

        local g = class("class G public (public a: number) end")
        g:primaryConstructor():setArgsQualifiers(g:primaryConstructor():argsQualifiers())
        g:setPrimaryConstructor(g:primaryConstructor())
        assert(g:primaryConstructor():explicitVisibility() == true)
        assert(g:primaryConstructor():argsQualifiers()[1]:explicitVisibility() == true)
        assert(printed(g) == "class G public (public a: number)\nend")

        local f = class("class F(private a: number = 1, public const b: string) end")
        local fc = f:primaryConstructor()
        fc:setArgs({ fc:args()[2], fc:args()[1] })
        assert(printed(f) == "class F(public const b: string, private a: number = 1)\nend")
        assert(fc:argsQualifiers()[1]:isConst() == true)
        assert(fc:argsQualifiers()[1]:explicitVisibility() == true)
        assert(fc:argsDefaults()[1] == false)

        fc:setArgs({ fc:args()[2], alloc:defaultnode("AstLocal"):setName("c") })
        assert(#fc:argsDefaults() == 2 and #fc:argsQualifiers() == 2)
        assert(fc:argsDefaults()[2] == false)
        assert(fc:argsQualifiers()[2]:visibility() == "public")
        assert(printed(f) == "class F(private a: number = 1, public c)\nend")

        fc:setArgsQualifiers({ fc:argsQualifiers()[1]:setIsConst(true) })
        assert(#fc:argsQualifiers() == 2)
        assert(fc:argsQualifiers()[1]:explicitVisibility() == true)
        local ok, err = pcall(function() fc:setArgsDefaults({ false, false, false }) end)
        assert(not ok and string.find(err, "argsDefaults has more entries than args", 1, true))
        ok, err = pcall(function() fc:setArgsQualifiers({ fc:argsQualifiers()[1], fc:argsQualifiers()[2], fc:argsQualifiers()[1] }) end)
        assert(not ok and string.find(err, "argsQualifiers has more entries than args", 1, true))
        fc:setArgsDefaults({})
        assert(printed(f) == "class F(private const a: number, public c)\nend")

        fc:setArgs({})
        assert(#fc:argsDefaults() == 0 and #fc:argsQualifiers() == 0)
        assert(printed(f) == "class F()\nend")

        local other = reflect.allocator()
        local foreign = other:parse("class X(a) end"):root():body()[1]:primaryConstructor()
        assert(not pcall(function() f:setPrimaryConstructor(foreign) end))
        assert(not pcall(function() fc:setArgsQualifiers(foreign:argsQualifiers()) end))

        local h = alloc:defaultnode("AstStatClass")
        h:setName(alloc:defaultnode("AstLocal"):setName("H"))
        local hc = alloc:defaultnode("AstClassPrimaryConstructor")
        hc:setArgs({ alloc:defaultnode("AstLocal"):setName("x") })
        hc:setArgsDefaults({ false })
        hc:setArgsQualifiers({ alloc:defaultnode("AstClassPrimaryConstructorParamQualifiers"):setVisibility("private"):setIsConst(true) })
        h:setPrimaryConstructor(hc)
        h:setMembers({ alloc:defaultnode("AstClassProperty"):setName("y") })
        assert(h:explicitVisibility() == false)
        assert(printed(h) == "class H(private const x)\n    public y\nend")
        h:setPrimaryConstructor(nil)
        assert(h:primaryConstructor() == nil)
        assert(printed(h) == "class H\n    y\nend")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectTaintAndDocuments")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sffClasses{FFlag::LuwuClasses, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local function fails(f, message)
            local ok, err = pcall(f)
            assert(not ok, "expected an error containing: " .. message)
            assert(string.find(tostring(err), message, 1, true) ~= nil, tostring(err))
        end
        local function visitAll(node)
            local count = 0
            node:walk(function()
                count += 1
                return true
            end)
            return count
        end

        local alloc = reflect.allocator()

        local parsed = alloc:parse("local a = 1\nlocal b = 2"):root()
        assert(parsed:tainted() == false)
        assert(visitAll(parsed) == 5)
        assert(parsed:untaint() == parsed)
        assert(visitAll(parsed:body()[1]) == 2)

        local fresh = alloc:defaultnode("AstStatBlock")
        assert(fresh:tainted() == true)
        assert(fresh:untaint():tainted() == false)
        assert(alloc:defaultnode("AstStatIf"):tainted() == true)
        fails(function() return alloc:defaultnode("AstStatIf"):untaint() end, "AstStatIf.condition is nil")
        fails(function() return alloc:defaultnode("AstLocal"):tainted() end, "tainted is not a valid method of AstLocal")

        local flagged = alloc:parse("local c = d\nlocal e = f"):root()
        local firstStat, secondStat = flagged:body()[1], flagged:body()[2]
        firstStat:values()[1]:setName("renamed")
        firstStat:setHasSemicolon(true)
        assert(flagged:tainted() == false)
        firstStat:setValues({})
        assert(flagged:tainted() == true)
        assert(firstStat:tainted() == true)
        assert(secondStat:tainted() == true)
        assert(secondStat:untaint():tainted() == false)
        assert(secondStat:values()[1]:tainted() == false)
        assert(firstStat:tainted() == true)
        assert(flagged:tainted() == true)
        assert(flagged:untaint():tainted() == false)
        assert(firstStat:tainted() == false)
        fails(function() flagged:setBody({ flagged:body()[1]:vars()[1] }) end, "AstNode")
        assert(flagged:tainted() == false)

        local first = alloc:parse("while x do y() end"):root()
        local second = alloc:parse("z()"):root()
        local separate = reflect.allocator():parse("w()"):root()
        fails(function()
            first:walk(function(node)
                if node.kind == "AstStatWhile" then
                    second:setBody({})
                end
                return true
            end)
        end, "cannot change the structure of an AST while it is being walked")
        first:walk(function(node)
            if node.kind == "AstStatWhile" then
                separate:setBody({})
            end
            return true
        end)
        fails(function() return visitAll(separate) end, "tainted AST")
        assert(visitAll(first) == 7)
        assert(visitAll(second) == 4)
        assert(visitAll(separate:untaint()) == 1)

        local host = alloc:parse("do end"):root()
        local guest = alloc:parse("local n = 1"):root()
        local moved = guest:body()[1]
        host:body()[1]:setBody({ moved })
        fails(function() return visitAll(host) end, "tainted AST")
        fails(function() return visitAll(guest) end, "tainted AST")
        assert(visitAll(host:untaint()) == 4)
        assert(visitAll(moved) == 2)
        fails(function() return visitAll(guest) end, "tainted AST")
        assert(visitAll(guest:untaint()) == 3)
        assert(host:prettyprint() == "do\n    local n = 1\nend")

        moved:setValues({})
        fails(function() return visitAll(host) end, "tainted AST")
        fails(function() return host:prettyprint() end, "tainted AST")
        assert(host:untaint():prettyprint() == "do\n    local n\nend")

        moved:setValues({ nil })
        moved:vars()[1]:setName("renamed")
        assert(host:untaint():prettyprint() == "do\n    local renamed\nend")

        fails(function()
            host:walk(function(node)
                if node.kind == "AstStatLocal" then
                    guest:setBody({})
                end
                return true
            end)
        end, "cannot change the structure of an AST while it is being walked")
        guest:setBody({})

        local walked = alloc:parse("local p = q"):root()
        fails(function()
            walked:walk(function(node)
                if node.kind == "AstStatLocal" then
                    error("stop")
                end
                return true
            end)
        end, "stop")
        walked:setBody({})
        assert(visitAll(walked:untaint()) == 1)

        local inside = alloc:parse("local r = s"):root()
        local seen = 0
        inside:walk(function(node)
            assert(node:untaint() == node)
            seen += visitAll(node)
            return true
        end)
        assert(seen == 3 + 2 + 1)

        local commented = alloc:parse("foo()"):root()
        local source = alloc:parse("-- carried\nbar()", false, true):root()
        local carried = source:body()[1]
        assert(#carried:leadingComments() == 1)
        commented:setBody({ commented:body()[1], carried })
        assert(#commented:body()[2]:leadingComments() == 1)
        assert(commented:untaint():prettyprint() == "foo()\n-- carried\nbar()")

        local plain = alloc:parse("baz()"):root()
        plain:body()[1]:setLeadingComments({ alloc:defaultnode("AstComment"):setText("-- attached") })
        assert(plain:prettyprint() == "-- attached\nbaz()")
        assert(visitAll(plain) == 4)

        local owner = alloc:parse("class A(x) end"):root():body()[1]
        local donor = alloc:parse("class B public (public y: number) end"):root():body()[1]
        owner:setPrimaryConstructor(donor:primaryConstructor())
        assert(owner:primaryConstructor():explicitVisibility() == true)
        assert(owner:primaryConstructor():argsQualifiers()[1]:explicitVisibility() == true)
        assert(owner:untaint():prettyprint() == "class A public (y: number)\nend")
        owner:setExplicitVisibility(true)
        assert(owner:prettyprint() == "class A public (public y: number)\nend")

        local built = alloc:defaultnode("AstStatBlock")
        local statements = {}
        for i = 1, 50 do
            statements[i] = alloc:parse("f" .. i .. "()"):root():body()[1]
        end
        built:setBody(statements)
        fails(function() return built:prettyprint() end, "tainted AST")
        assert(visitAll(built:untaint()) == 1 + 50 * 3)
        assert(visitAll(statements[50]) == 3)
        statements[1]:setExpr(alloc:parseexpr("g()"))
        fails(function() return visitAll(statements[50]) end, "tainted AST")
        assert(visitAll(statements[50]:untaint()) == 3)
        assert(visitAll(statements[49]:untaint()) == 3)
        assert(visitAll(statements[50]) == 3)
        fails(function() return visitAll(built) end, "tainted AST")
        assert(visitAll(built:untaint()) == 1 + 50 * 3)

        local left = alloc:parse("-- left\none()", false, true):root()
        local right = alloc:parse("-- right\ntwo()", false, true):root()
        left:setBody({ left:body()[1], right:body()[1] })
        assert(left:untaint():prettyprint() == "-- left\none()\n-- right\ntwo()")
        assert(right:untaint():prettyprint() == "-- right\ntwo()")

        local chained = alloc:parseexpr("1")
        local handles = {}
        for i = 1, 200000 do
            local outer = alloc:defaultnode("AstExprGroup")
            outer:setExpr(chained)
            handles[i] = chained
            chained = outer
        end
        assert(handles[1]:tainted() == true)
        assert(handles[100000]:tainted() == true)
        chained = nil
        handles = nil

        local gathered = alloc:parse(""):root()
        local pieces = {}
        for i = 1, 3000 do
            pieces[i] = alloc:parse("-- note " .. i .. "\ncall" .. i .. "()", false, true):root():body()[1]
        end
        gathered:setBody(pieces)
        local gatheredText = gathered:untaint():prettyprint()
        assert(string.sub(gatheredText, 1, 9) == "-- note 1")
        assert(string.find(gatheredText, "-- note 1500\ncall1500()", 1, true) ~= nil)
        assert(string.find(gatheredText, "-- note 3000\ncall3000()", 1, true) ~= nil)
        assert(#pieces[3000]:leadingComments() == 1)

        local many = alloc:parse("-- a\none()\n-- b\ntwo()\n-- c\nthree()", false, true):root()
        local more = alloc:parse("-- d\nfour()\n-- e\nfive()\n-- f\nsix()\n-- g\nseven()", false, true):root()
        local combined = many:body()
        for _, stat in more:body() do
            table.insert(combined, stat)
        end
        many:setBody(combined)
        assert(many:untaint():prettyprint() == "-- a\none()\n-- b\ntwo()\n-- c\nthree()\n-- d\nfour()\n-- e\nfive()\n-- f\nsix()\n-- g\nseven()")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
    lua_gc(L, LUA_GCCOLLECT, 0);
}

TEST_CASE("ReflectExposesEveryMember")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};
    ScopedFastFlag sffClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag sffCstAttr{FFlag::LuauCstAttr, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()
        local function span(l)
            return l.begin.x .. ":" .. l.begin.y .. "-" .. l["end"].x .. ":" .. l["end"].y
        end

        local doc = alloc:parse("--!strict\nlocal x = 0x10\nif x then y() else z() end\nlocal v = if a then b else c\nfunction T:m(a, ...: number) end\ntype F = (name: string, number) -> ()\ntype R = Mod.Thing\n")
        assert(#doc:errors() == 0)
        assert(doc:lines() == 7)
        assert(#doc:hotcomments() == 1)
        assert(doc:hotcomments()[1].header == true)
        assert(doc:hotcomments()[1].content == "strict")
        assert(span(doc:hotcomments()[1].location) == "1:1-1:10")
        assert(doc:properties().lines == 7)
        assert(#doc:properties().hotcomments == 1)

        local body = doc:root():body()

        local statLocal = body[1]
        assert(span(statLocal:keywordLocation()) == "2:1-2:6")
        assert(span(statLocal:equalsSignLocation()) == "2:9-2:10")
        assert(span(statLocal:properties().keywordLocation) == "2:1-2:6")
        assert(not pcall(function() statLocal:setKeywordLocation(nil) end))
        assert(statLocal:luwuOnly() == false)
        assert(statLocal:setLuwuOnly(true):luwuOnly() == true)
        assert(tostring(statLocal:vars()[1]:loopDepth()) == "0")
        assert(tostring(statLocal:vars()[1]:setLoopDepth(2):loopDepth()) == "2")

        local number = statLocal:values()[1]
        assert(number:parseResult() == "ok")
        assert(not pcall(function() number:setParseResult("bogus") end))
        assert(number:setParseResult("imprecise"):parseResult() == "imprecise")

        local statIf = body[2]
        assert(span(statIf:ifLocation()) == "3:1-3:3")
        assert(span(statIf:thenLocation()) == "3:6-3:10")
        assert(span(statIf:elseLocation()) == "3:15-3:19")
        assert(alloc:parse("if x then end"):root():body()[1]:elseLocation() == nil)

        local ifElse = body[3]:values()[1]
        assert(ifElse:hasThen() == true)
        assert(span(ifElse:ifLocation()) == "4:11-4:13")
        assert(span(ifElse:thenLocation()) == "4:16-4:20")
        assert(span(ifElse:elseLocation()) == "4:23-4:27")
        assert(ifElse:setHasThen(false):hasThen() == false)

        local statFunction = body[4]
        local fn = statFunction:func()
        assert(span(statFunction:functionLocation()) == "5:1-5:9")
        assert(fn:self():name() == "self")
        assert(tostring(fn:functionDepth()) == "1")
        assert(tostring(fn:setFunctionDepth(3):functionDepth()) == "3")
        assert(span(fn:varargLocation()) == "5:17-5:20")
        assert(span(fn:argLocation()) == "5:13-5:29")
        assert(fn:varargAnnotation().kind == "AstTypePackVariadic")
        fn:setVarargAnnotation(nil)
        assert(statFunction:untaint():prettyprint() == "function T:m(a, ...)\nend")
        assert(fn:setSelf(nil):self() == nil)
        doc:root():untaint()

        local alias = body[5]
        local typeFunction = alias:type()
        assert(span(alias:nameLocation()) == "6:6-6:7")
        assert(span(alias:typeLocation()) == "6:1-6:5")
        assert(typeFunction:argNames()[1] == "name")
        assert(typeFunction:argNames()[2] == false)
        assert(span(typeFunction:argNameLocations()[1]) == "6:11-6:15")
        assert(typeFunction:argNameLocations()[2] == false)
        typeFunction:setArgNames({ false, "count" })
        assert(alias:prettyprint() == "type F = (string, count: number) -> ()")
        typeFunction:setArgNames({ "only" })
        assert(alias:prettyprint() == "type F = (only: string, number) -> ()")
        typeFunction:setArgNames({ "a", "b", "c", "d" })
        assert(alias:prettyprint() == "type F = (a: string, b: number) -> ()")
        typeFunction:setArgNames({})
        assert(alias:prettyprint() == "type F = (string, number) -> ()")

        local reference = body[6]:type()
        assert(span(reference:prefixLocation()) == "7:10-7:13")
        assert(span(reference:nameLocation()) == "7:14-7:19")
        assert(reference:prefixLocal() == nil)

        local declarations = alloc:parse("declare function foo(a: number, b: string): boolean\ndeclare extern type E extends B with\n  p: number\nend"):root():body()
        local declared = declarations[1]
        assert(declared:paramNames()[1] == "a" and declared:paramNames()[2] == "b")
        assert(span(declared:paramNameLocations()[1]) == "1:22-1:23")
        assert(span(declared:nameLocation()) == "1:18-1:21")
        assert(span(declared:declareLocation()) == "1:1-1:8")
        assert(span(declared:functionLocation()) == "1:9-1:17")
        declared:setParamNames({ "first" })
        assert(declared:prettyprint() == "declare function foo(first: number, string): boolean")
        local extern = declarations[2]
        assert(span(extern:declareLocation()) == "2:1-2:8")
        assert(span(extern:classLocation()) == "2:16-2:20")
        assert(span(extern:extendsLocation()) == "2:23-2:30")
        assert(span(extern:props()[1]:nameLocation()) == "3:3-3:4")

        local withCst = alloc:parse("@[native, checked] local function g() end\nlocal s = `a{1}b`\ntype T = { x: number, [\"y\"]: string }\nlocal t: { read a: number }\n", true):root():body()
        local attrLists = withCst[1]:cst():attrLists()
        assert(#attrLists == 1)
        assert(attrLists[1].kind == "CstAttrList")
        assert(attrLists[1].category == "aux")
        assert(attrLists[1]:atBracketPosition() ~= nil)
        assert(#attrLists[1]:commaPositions() == 1)
        assert(not pcall(function() attrLists[1]:setCommaPositions({}) end))
        local sourceStrings = withCst[2]:values()[1]:cst():sourceStrings()
        assert(#sourceStrings == 2 and sourceStrings[1] == "a" and sourceStrings[2] == "b")
        local items = withCst[3]:type():cst():items()
        assert(#items == 2)
        assert(items[1].kind == "CstTypeTableItem" and items[1]:itemKind() == "property")
        assert(items[2].kind == "CstTypeTableItem" and items[2]:itemKind() == "stringProperty")
        assert(items[2]:stringInfo().kind == "CstExprConstantString")
        assert(items[1]:stringInfo() == nil)
        assert(items[1]:separator() == "comma")
        assert(items[1]:colonPosition() ~= nil)
        assert(span(withCst[4]:vars()[1]:annotation():props()[1]:accessLocation()) == "4:12-4:16")

        local class = alloc:parse("class K\n    public a: number = 1;\n    public function f(self) end\nend"):root():body()[1]
        assert(class:hasEnd() == true)
        assert(span(class:keywordLocation()) == "1:1-1:6")
        assert(#class:generics() == 0 and #class:genericPacks() == 0)
        local property, method = class:members()[1], class:members()[2]
        assert(property:hasSemicolon() == true)
        assert(method:hasSemicolon() == false)
        assert(property:defaultValue().kind == "AstExprConstantNumber")
        assert(span(property:qualifierLocation()) == "2:5-2:11")
        assert(span(property:nameLocation()) == "2:12-2:13")
        assert(property:constLocation() == nil)
        assert(span(method:keywordLocation()) == "3:12-3:20")
        assert(class:prettyprint() == "class K\n    public a: number = 1;\n    public function f(self)\n    end\nend")
        property:setDefaultValue(nil):setHasSemicolon(false)
        assert(class:untaint():prettyprint() == "class K\n    public a: number\n    public function f(self)\n    end\nend")
        assert(class:setHasEnd(false):hasEnd() == false)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

using ReflectMemberSet = std::map<std::string, std::set<std::string>>;

static std::optional<std::string> readReflectSourceFile(const std::string& relative)
{
    std::string prefix;
    for (int i = 0; i < 8; i++)
    {
        std::ifstream file(prefix + relative);
        if (file)
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
        prefix += "../";
    }
    return std::nullopt;
}

TEST_CASE("ReflectNestedWalksAreBounded")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuwuManagedReferences2, true};
    ScopedFastFlag sffPinned{FFlag::LuauUdataMetatablePinned, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local alloc = reflect.allocator()

        local nested = alloc:parse("local function a() local function b() local function c() local function d() return 1 end end end end"):root()
        local level, deepest = 0, 0
        local function walkFunctions(block)
            level += 1
            deepest = math.max(deepest, level)
            block:walk(function(node)
                if node.kind == "AstExprFunction" then
                    walkFunctions(node:body())
                    return false
                end
                return true
            end)
            level -= 1
        end
        walkFunctions(nested)
        assert(deepest == 5)

        local deep = alloc:parse(string.rep("do ", 900) .. "x()" .. string.rep(" end", 900)):root()
        local count = 0
        deep:walk(function()
            count += 1
            return true
        end)
        assert(count == 904)

        local restarts = 0
        local function walkAgain()
            restarts += 1
            deep:walk(function(node)
                if node.kind == "AstExprGlobal" then
                    walkAgain()
                end
                return true
            end)
        end
        local ok, err = pcall(walkAgain)
        assert(not ok and string.find(tostring(err), "C stack overflow", 1, true) ~= nil, tostring(err))
        assert(restarts > 50)

        count = 0
        deep:walk(function()
            count += 1
            return true
        end)
        assert(count == 904)

        local function pcallChain(n)
            if n == 0 then
                return true
            end
            return select(2, pcall(pcallChain, n - 1))
        end
        assert(pcallChain(150) == true)

        local descents = 0
        local function descend(node)
            descents += 1
            node:walk(function(child)
                if child ~= node and child.kind == "AstStatBlock" then
                    descend(child)
                    return false
                end
                return true
            end)
        end
        local moderate = alloc:parse(string.rep("do ", 60) .. "x()" .. string.rep(" end", 60)):root()
        descend(moderate)
        assert(descents == 61)

        local printed, untainted, printErr
        deep:walk(function(node)
            if node.kind == "AstExprGlobal" then
                printed, printErr = pcall(function() return deep:prettyprint() end)
                untainted = pcall(function() return node:untaint() end)
            end
            return true
        end)
        assert(printed == true, tostring(printErr))
        assert(untainted == true)
        assert(#deep:prettyprint() > 1000)

        local viaCoroutine, coroutineErr
        deep:walk(function(node)
            if node.kind == "AstExprGlobal" then
                coroutine.wrap(function()
                    viaCoroutine, coroutineErr = pcall(function()
                        deep:walk(function() return true end)
                    end)
                end)()
            end
            return true
        end)
        assert(viaCoroutine == true, tostring(coroutineErr))

        local nestedLevels, printedAtBottom, printErrAtBottom = 0, nil, nil
        local function nestUntilRefused()
            nestedLevels += 1
            deep:walk(function(node)
                if node.kind == "AstExprGlobal" then
                    local nestedOk = pcall(nestUntilRefused)
                    if not nestedOk and printedAtBottom == nil then
                        printedAtBottom, printErrAtBottom = pcall(function() return deep:prettyprint() end)
                    end
                end
                return true
            end)
        end
        pcall(nestUntilRefused)
        assert(nestedLevels > 50)
        assert(printedAtBottom == false and string.find(tostring(printErrAtBottom), "C stack overflow", 1, true) ~= nil, tostring(printErrAtBottom))

        local shallow = alloc:parse("local a = 1"):root()
        local shallowRestarts = 0
        local function shallowAgain()
            shallowRestarts += 1
            shallow:walk(function(node)
                if node.kind == "AstExprConstantNumber" and shallowRestarts < 40 then
                    shallowAgain()
                end
                return true
            end)
        end
        shallowAgain()
        assert(shallowRestarts == 40)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
    CHECK(lua_cstackdepth(L) == 0);
}

TEST_CASE("ReflectWalkMatchesTheCoreVisitor")
{
    ScopedFastFlag sffClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag sffDefaults{FFlag::LuwuDefaultArguments, true};

    Luau::initializeAstNodeClasses();

    struct Recorder : Luau::AstVisitor
    {
        std::vector<Luau::AstNode*> seen;

        bool visit(Luau::AstNode* node) override
        {
            seen.push_back(node);
            return true;
        }

        bool visit(Luau::AstType* node) override
        {
            return visit(static_cast<Luau::AstNode*>(node));
        }

        bool visit(Luau::AstTypePack* node) override
        {
            return visit(static_cast<Luau::AstNode*>(node));
        }
    };

    size_t nodes = 0;
    std::set<std::string> kinds;

    auto check = [&](const std::string& label, const std::string& source)
    {
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::ParseOptions options;
        options.allowDeclarationSyntax = true;

        Luau::ParseResult result = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
        REQUIRE_MESSAGE(result.root, label);

        Recorder recorder;
        result.root->visit(&recorder);

        std::vector<Luau::AstNode*> walked;
        std::vector<Luau::AstNode*> pending{result.root};
        std::vector<Luau::AstNode*> children;
        while (!pending.empty())
        {
            Luau::AstNode* node = pending.back();
            pending.pop_back();
            walked.push_back(node);
            kinds.insert(Luau::getNodeKind(node));

            children.clear();
            Luau::collectAstChildren(node, children);
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }

        CHECK_MESSAGE(walked == recorder.seen, label);
        nodes += walked.size();
    };

    check("statements and expressions", R"LUA(
        local a, b: number = 1, "two"
        local t = { 1, k = 2, [3] = function(x: number, y: string = "d", ...: string): (number, string) return x, ... end }
        if a then b() elseif c then d() else e() end
        for i: number = 1, 10, 2 do print(i) end
        for k: string, v in pairs(t) do continue end
        while true do break end
        repeat a += 1 until a > 10
        do local inner = `x{a}y{b}z` end
        function M.sub:method<T>(p: T, q: number?): T return if p then p else q :: any end
        local function lf<T...>(...: T...): ...number return ... end
        local v = -a ^ 2 + (a or b) and not c
        a.b, c[1] = f(1, 2), g "s"
        return a
    )LUA");

    check("types", R"LUA(
        type Pair<K, V = string, P... = ...number> = { read key: K, value: V, [number]: boolean }
        type Fn = <T>(name: string, ...number) -> (boolean, string)
        type U = "a" | "b" | nil | true
        type I = { a: number } & { b: string }
        type O = (() -> ())?
        type Q = typeof(a.b) | Mod.Thing<number, ...string>
        export type Arr = { number }
        type function tf(x) return x end
    )LUA");

    check("declarations", R"LUA(
        declare function foo<T>(a: number, b: string, ...: T): boolean
        declare x: number
        declare extern type E extends B with
            p: number
            [string]: number
            function m(self, q: string): number
        end
    )LUA");

    check("classes", R"LUA(
        class P<T> private (public const key: string, private secret: T = nil)
            public count: number = 0
            private function bump(self) self.count += 1 end
            public @native function get(self): string return self.key end
        end
        @checked local function g(x, y = 2) return x + y end
    )LUA");

    check("errors", "local = 1\nif then end\nlocal x: = \nf(,)\ntype T = { a: }\n");

    for (const char* file : {"Reflect/types.luau", "tests/conformance/basic.luau", "tests/conformance/types.luau", "tests/conformance/tables.luau"})
    {
        std::optional<std::string> source = readReflectSourceFile(file);
        REQUIRE_MESSAGE(source, "cannot find " << std::string(file) << " from the working directory");
        check(file, *source);
    }

    CHECK(nodes > 15000);
    CHECK(kinds.size() > 50);
}

static void collectDeclaredMembers(const std::string& source, ReflectMemberSet& members)
{
    static const std::regex definition(R"(^(?:class|struct)\s+(\w+)[^;]*$)");
    static const std::regex identifier(R"([A-Za-z_]\w*)");
    static const char* const skipped[] = {"static ", "using ", "friend ", "typedef ", "template", "enum ", "class ", "struct ", "return "};

    struct Scope
    {
        std::string name;
        int depth;
    };

    std::vector<Scope> scopes;
    std::string pending;
    int depth = 0;

    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line))
    {
        if (size_t comment = line.find("//"); comment != std::string::npos)
            line.erase(comment);

        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);

        bool isSkipped = false;
        for (const char* prefix : skipped)
            isSkipped = isSkipped || line.rfind(prefix, 0) == 0;

        std::smatch match;
        if (std::regex_match(line, match, definition))
        {
            pending = scopes.empty() ? match[1].str() : scopes.back().name + "::" + match[1].str();
            members[pending];
        }
        else if (!scopes.empty() && depth == scopes.back().depth + 1 && line.back() == ';' && !isSkipped &&
                 line.find_first_of("()") == std::string::npos)
        {
            std::string declaration = line.substr(0, line.find_first_of("={;"));
            std::vector<std::string> words(
                std::sregex_token_iterator(declaration.begin(), declaration.end(), identifier),
                std::sregex_token_iterator()
            );
            if (words.size() >= 2)
                members[scopes.back().name].insert(words.back());
        }

        for (char c : line)
        {
            if (c == '{')
            {
                if (!pending.empty())
                {
                    scopes.push_back({pending, depth});
                    pending.clear();
                }
                depth++;
            }
            else if (c == '}')
            {
                depth--;
                if (!scopes.empty() && scopes.back().depth == depth)
                    scopes.pop_back();
            }
        }
    }
}

static void collectSchemaMembers(const std::string& source, ReflectMemberSet& members)
{
    static const std::regex owner(R"rx("schema type of (\w+)\.)rx");
    static const std::regex member(R"(\bn(?:->|\.)(\w+))");

    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line))
    {
        std::smatch match;
        if (!std::regex_search(line, match, owner))
            continue;

        std::string expression = line.substr(0, size_t(match.position(0)));
        std::set<std::string>& names = members[match[1].str()];
        for (std::sregex_iterator it(expression.begin(), expression.end(), member), end; it != end; ++it)
            names.insert((*it)[1].str());
    }
}

TEST_CASE("ReflectSchemaCoversEveryAstMember")
{
    ReflectMemberSet declared;
    for (const char* header : {"Ast/include/Luau/Ast.h", "Ast/include/Luau/Cst.h"})
    {
        std::optional<std::string> source = readReflectSourceFile(header);
        REQUIRE_MESSAGE(source, "cannot find " << std::string(header) << " from the working directory");
        collectDeclaredMembers(*source, declared);
    }

    std::vector<std::string> generatedFiles = {"Reflect/src/AstNodes.gen.inl", "Reflect/src/CstNodes.gen.inl"};
    {
        std::optional<std::string> index = readReflectSourceFile("Reflect/src/AstItems.gen.inl");
        REQUIRE_MESSAGE(index, "cannot find Reflect/src/AstItems.gen.inl from the working directory");

        static const std::regex include(R"rx(#include "(\w+\.gen\.inl)")rx");
        for (std::sregex_iterator it(index->begin(), index->end(), include), end; it != end; ++it)
            generatedFiles.push_back("Reflect/src/" + (*it)[1].str());
    }
    CHECK(generatedFiles.size() > 10);

    ReflectMemberSet exposed;
    std::string generated;
    for (const std::string& file : generatedFiles)
    {
        std::optional<std::string> source = readReflectSourceFile(file);
        REQUIRE_MESSAGE(source, "cannot find " << file << " from the working directory");
        collectSchemaMembers(*source, exposed);
        generated += *source;
    }

    std::set<std::string> exposedAnywhere;
    for (const auto& [owner, names] : exposed)
        exposedAnywhere.insert(names.begin(), names.end());

    const std::set<std::string> baseClasses = {"AstNode", "AstExpr", "AstStat", "AstType", "AstTypePack", "CstNode"};
    const std::set<std::string> notReflected = {"AstName", "AstArray", "AstTypeOrPack", "AstVisitor", "AstRtti", "CstRtti", "AstAttr::DeprecatedInfo"};
    const std::set<std::string> internal = {"AstNode.classIndex", "CstNode.classIndex", "AstStatRepeat.DEPRECATED_hasUntil"};
    const std::map<std::string, std::string> schemaNames = {
        {"AstExprTable::Item", "AstTableItem"},
        {"CstExprTable::Item", "CstTableItem"},
        {"CstTypeTable::Item", "CstTypeTableItem"},
    };

    std::string missing;
    size_t checked = 0;

    for (const auto& [owner, names] : declared)
    {
        bool isAst = owner.rfind("Ast", 0) == 0 || owner.rfind("Cst", 0) == 0;
        if (!isAst || notReflected.count(owner))
            continue;

        auto renamed = schemaNames.find(owner);
        const std::string& schemaName = renamed != schemaNames.end() ? renamed->second : owner;
        bool isBase = baseClasses.count(owner) > 0;

        if (!isBase && generated.find("\"" + schemaName + "\"") == std::string::npos)
            missing += owner + " ";

        auto entry = exposed.find(schemaName);
        for (const std::string& name : names)
        {
            checked++;
            if (internal.count(owner + "." + name))
                continue;

            bool found = isBase ? exposedAnywhere.count(name) > 0 : entry != exposed.end() && entry->second.count(name) > 0;
            if (!found)
                missing += owner + "." + name + " ";
        }
    }

    CHECK_MESSAGE(missing.empty(), "AST or CST declarations with no Reflect schema entry: " << missing);
    CHECK(checked > 300);
}

TEST_SUITE_END();

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
#include <string>

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
        assert(typeof(root) == "AstNode")
        assert(root.kind == "AstStatBlock")
        assert(root.category == "stat")

        local stat = root:body()[1]
        assert(typeof(stat) == "AstNode")
        assert(stat.kind == "AstStatLocal")
        assert(stat.category == "stat")

        local val = stat:values()[1]
        assert(typeof(val) == "AstNode")
        assert(val.kind == "AstExprConstantNumber")
        assert(val.category == "expr")

        local localItem = stat:vars()[1]
        assert(typeof(localItem) == "AstLocal")
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
        assert(typeof(expr) == "AstNode")
        assert(expr.kind == "AstExprBinary")
        assert(expr.category == "expr")
        assert(expr:op() == "+")
        assert(expr:left().kind == "AstExprGlobal")
        assert(expr:left():name() == "a")

        local right = expr:right()
        assert(typeof(right) == "AstNode")
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
        assert(typeof(locStatCst) == "CstNode")
        assert(locStatCst.kind == "CstStatLocal")
        assert(locStatCst.category == "generic")

        -- Check string CST quote style
        local strExpr = locStat:values()[1]
        local strExprCst = strExpr:cst()
        assert(strExprCst ~= nil)
        assert(typeof(strExprCst) == "CstNode")
        assert(strExprCst.kind == "CstExprConstantString")
        assert(strExprCst.category == "generic")
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
        assert(typeof(c1) == "AstAux")
        assert(c1.kind == "AstComment")
        assert(c1:type() == "single")
        assert(c1:origlocation().begin.x == 1)
        assert(c1:text() == "-- single comment")

        local c2 = comments[2]
        assert(typeof(c2) == "AstAux")
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
        assert(typeof(item1) == "AstAux")
        assert(item1.kind == "list")
        assert(item1:key() == nil)
        assert(item1:value().kind == "AstExprConstantString")
        assert(item1:value():value() == "hello")

        -- Record item
        local item2 = items[2]
        assert(typeof(item2) == "AstAux")
        assert(item2.kind == "record")
        assert(item2:key() ~= nil)
        assert(item2:key().kind == "AstExprConstantString")
        assert(item2:key():value() == "foo")
        assert(item2:value().kind == "AstExprConstantInteger" or item2:value().kind == "AstExprConstantNumber")
        assert(item2:value():value() == 123)

        -- General item
        local item3 = items[3]
        assert(typeof(item3) == "AstAux")
        assert(item3.kind == "general")
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
        assert(typeof(cstItems[1]) == "AstAux")
        assert(typeof(cstItems[2]) == "AstAux")
        assert(typeof(cstItems[3]) == "AstAux")
        assert(cstItems[2]:equalsPosition() ~= nil)
        assert(cstItems[3]:indexerOpenPosition() ~= nil)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("AstTypeTableAndAstAux")
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
        assert(itm.kind == "record")
        assert(itm:setKind("list"):setValue(tblStat:values()[1]) == itm)
        assert(itm:kind() == "list")
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
            assert(cstProps.category == "generic")
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
        assert(typeof(numExpr) == "AstNode")
        assert(numExpr.kind == "AstExprConstantNumber")
        assert(numExpr.category == "expr")
        assert(numExpr:value() == 0)
        assert(numExpr:setValue(42) == numExpr)
        assert(numExpr:value() == 42)

        local localStat = alloc:defaultnode("AstStatLocal")
        assert(typeof(localStat) == "AstNode")
        assert(localStat.kind == "AstStatLocal")
        assert(localStat.category == "stat")
        assert(#localStat:vars() == 0)
        assert(#localStat:values() == 0)
        assert(localStat:setValues({ numExpr }) == localStat)
        assert(#localStat:values() == 1)
        assert(localStat:values()[1] == numExpr)

        local genType = alloc:defaultnode("AstGenericType")
        assert(typeof(genType) == "AstNode")
        assert(genType.kind == "AstGenericType")
        assert(genType:setName("T") == genType)
        assert(genType:name() == "T")

        local genTypePack = alloc:defaultnode("AstGenericTypePack")
        assert(typeof(genTypePack) == "AstNode")
        assert(genTypePack.kind == "AstGenericTypePack")
        assert(genTypePack:setName("U...") == genTypePack)
        assert(genTypePack:name() == "U...")

        local attr = alloc:defaultnode("AstAttr")
        assert(typeof(attr) == "AstNode")
        assert(attr.kind == "AstAttr")

        -- Cst Node Creation
        local cstLocal = alloc:defaultnode("CstStatLocal")
        assert(typeof(cstLocal) == "CstNode")
        assert(cstLocal.kind == "CstStatLocal")
        assert(cstLocal.category == "generic")

        -- AstLocal Creation (dedicated userdata)
        local astLocal = alloc:defaultnode("AstLocal")
        assert(typeof(astLocal) == "AstLocal")
        assert(astLocal.kind == "AstLocal")
        assert(astLocal:setName("myVar"):setIsConst(true) == astLocal)
        assert(astLocal:name() == "myVar")
        assert(astLocal:isConst() == true)

        -- Aux Creation
        local astComment = alloc:defaultnode("AstComment")
        assert(typeof(astComment) == "AstAux")
        assert(astComment.kind == "AstComment")

        local astTableProp = alloc:defaultnode("AstTableProp")
        assert(typeof(astTableProp) == "AstAux")
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
        fails(function() visitAll(ifStat) end, "AstStatIf.condition is nil")
        fails(function() return ifStat:children() end, "AstStatIf.condition is nil")
        fails(function() return ifStat:prettyprint() end, "AstStatIf.condition is nil")

        ifStat:setCondition(alloc:parseexpr("x")):setThenBody(alloc:defaultnode("AstStatBlock"))
        assert(#ifStat:children() == 2)
        visitAll(ifStat)
        assert(ifStat:elsebody() == nil)

        local parsedIf = alloc:parse("if x then y() end"):root():body()[1]
        parsedIf:setCondition(nil)
        fails(function() return parsedIf:children() end, "AstStatIf.condition is nil")
        fails(function() visitAll(parsedIf) end, "AstStatIf.condition is nil")

        local doc = alloc:parse("while true do if x then y() end end")
        doc:root():body()[1]:body():body()[1]:setThenBody(nil)
        fails(function() visitAll(doc:root()) end, "AstStatIf.thenbody is nil")
        fails(function() return doc:root():prettyprint() end, "AstStatIf.thenbody is nil")

        local global = alloc:defaultnode("AstExprGlobal")
        visitAll(global)
        fails(function() return global:prettyprint() end, "AstExprGlobal.name is nil")
        assert(global:setName("g"):prettyprint() == "g")
        fails(function() return alloc:defaultnode("AstExprLocal"):prettyprint() end, "AstExprLocal.local is nil")

        local ref = alloc:parse("local v: Foo<number, string, boolean> = nil"):root():body()[1]:vars()[1]:annotation()
        local params = ref:parameters()
        ref:setParameters({ params[1], nil, params[3] })
        fails(function() return ref:prettyprint() end, "AstTypeReference.parameters[2] is nil")

        local tbl = alloc:parseexpr("{ x = 1 }")
        local item = tbl:items()[1]
        item:setKey(alloc:parseexpr("1 + 2"))
        tbl:setItems({ item })
        visitAll(tbl)
        fails(function() return tbl:prettyprint() end, "is a record item whose key is not a string constant")
        item:setKind("general")
        tbl:setItems({ item })
        assert(string.gsub(tbl:prettyprint(), "%s", "") == "{[1+2]=1}")
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
        fails(function() return root:prettyprint() end, "AstStatBlock is nested inside itself")
        fails(function() root:walk(function() return true end) end, "AstStatBlock is nested inside itself")
        assert(#root:children() == 1)

        local bin = alloc:parseexpr("a + b")
        local group = alloc:defaultnode("AstExprGroup"):setExpr(bin)
        bin:setLeft(group)
        fails(function() return group:prettyprint() end, "is nested inside itself")

        local x = alloc:parseexpr("x")
        local sum = alloc:parseexpr("a + b"):setLeft(x):setRight(x)
        assert(string.gsub(sum:prettyprint(), "%s", "") == "x+x")
        local shared = x
        for i = 1, 64 do
            shared = alloc:parseexpr("a + b"):setLeft(shared):setRight(shared)
        end
        assert(#shared:children() == 2)

        local function nest(depth)
            local expr = alloc:parseexpr("1")
            for i = 1, depth do
                expr = alloc:defaultnode("AstExprGroup"):setExpr(expr)
            end
            return expr
        end
        assert(#nest(100):prettyprint() == 201)
        fails(function() return nest(5000):prettyprint() end, "nesting is deeper than the limit")
        fails(function() nest(5000):walk(function() return true end) end, "nesting is deeper than the limit")
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
        assert(string.gsub(tblA:prettyprint(), "%s", "") == "{y=2}")
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
            for kind, flag in { AstStatClass = "LuwuClasses", CstAttr = "LuauCstAttr", CstParametrizedAttr = "LuauCstAttr" } do
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
            if typeof(node) == "AstNode" then
                pcall(function() return node:children() end)
                pcall(function() node:walk(function() return true end) end)
                pcall(function() return node:prettyprint() end)
                local parent = alloc:defaultnode("AstStatExpr")
                pcall(function() parent:setExpr(node) return parent:prettyprint() end)
                local group = alloc:defaultnode("AstTypeGroup")
                pcall(function() group:setType(node) return group:prettyprint() end)
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
        assert(alloc:parse("local x = 1\nif y then z() end"):root():prettyprint() == "local x = 1\nif y then z() end")
        assert(alloc:parse(""):root():prettyprint() == "")

        local doc = alloc:parse("if y then z() end")
        assert(doc:root():body()[1]:prettyprint() == "if y then z() end")
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
        assert(doc:root():prettyprint() == "local function f(x, y = 2, z = x + 1) end")

        local args = fn:args()
        fn:setArgs({ args[1], args[2] })
        local ok, err = pcall(function() return doc:root():prettyprint() end)
        assert(not ok)
        assert(string.find(err, "AstExprFunction.args has a different length", 1, true) ~= nil, err)

        assert(fn:setArgsDefaults({ false, defaults[3] }) == fn)
        assert(fn:argsDefaults()[1] == false)
        assert(fn:argsDefaults()[2] == defaults[3])
        assert(string.gsub(doc:root():prettyprint(), "%s", "") == "localfunctionf(x,y=x+1)end")

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

TEST_SUITE_END();

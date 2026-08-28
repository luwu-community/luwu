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
LUAU_FASTFLAG(LuauManagedReferences2)

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
            printf("Lua runtime error: %s\n", lua_tostring(L, -1));
        }
        return status;
    }
    return result;
}

TEST_SUITE_BEGIN("Reflect");

TEST_CASE("LazyAstTypeof")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local x = 1")
        assert(typeof(doc) == "AstDocument")

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
        assert(typeof(localItem) == "AstAux")
        assert(localItem.kind == "AstLocal")
        assert(localItem.name == "x")

        local loc = stat:location()
        assert(typeof(loc) == "AstLocation")
        assert(loc.beginLine == 1)
        assert(loc.beginColumn == 1)
        assert(loc.text == "local x = 1")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstProperties")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse([[
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
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local a = 1; local b = 2; return a + b")
        
        -- Test children() on block
        local root = doc:root()
        local children = root:children()
        assert(#children == 3)
        assert(children[1].kind == "AstStatLocal")
        assert(children[2].kind == "AstStatLocal")
        assert(children[3].kind == "AstStatReturn")

        -- Test doc:walk()
        local nodeKinds = {}
        doc:walk(function(node)
            table.insert(nodeKinds, node.kind)
            return true
        end)
        assert(#nodeKinds > 5)

        -- Test doc:find()
        local locals = doc:find("AstStatLocal")
        assert(#locals == 2)
        assert(locals[1]:vars()[1].name == "a")
        assert(locals[2]:vars()[1].name == "b")

        -- Test node:find()
        local nodeLocals = root:find("AstStatLocal")
        assert(#nodeLocals == 2)
        assert(nodeLocals[1]:vars()[1].name == "a")
        assert(nodeLocals[2]:vars()[1].name == "b")

        local returns = root:find("AstStatReturn")
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
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local expr = reflect.parseExpr("a + b * 2")
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
        assert(expr:text() == "a + b * 2")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstErrors")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local x = \n-- a comment")
        local errors = doc:errors()
        assert(#errors > 0)
        assert(errors[1].message ~= nil)
        assert(errors[1].location ~= nil)
        assert(errors[1].location.beginLine == 2)
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
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local str = 'hello world'\nfoo(1, 2)", true)
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
        assert(typeof(callExprCst:openParens()) == "AstPosition")
        assert(callExprCst:openParens().line == 2)
        assert(callExprCst:openParens().computedOffset > 0)
        assert(#doc:lineOffsets() >= 2)
        assert(doc:lineOffsets()[1] == 0)
        assert(#callExprCst:commaPositions() == 1)
        assert(typeof(callExprCst:commaPositions()[1]) == "AstPosition")

        -- Check type function CST and AST category
        local typeDoc = reflect.parse("type Callback = (a: number, b: string) -> boolean", true)
        local aliasStat = typeDoc:root():body()[1]
        assert(aliasStat.category == "stat")
        local aliasStatCst = aliasStat:cst()
        assert(aliasStatCst ~= nil)
        assert(aliasStatCst.kind == "CstStatTypeAlias")
        assert(aliasStatCst:typeKeywordPosition().line == 1)

        local typeFunc = aliasStat:type()
        assert(typeFunc.category == "type")
        local typeFuncCst = typeFunc:cst()
        assert(typeFuncCst ~= nil)
        assert(typeFuncCst.kind == "CstTypeFunction")
        assert(typeFuncCst:returnArrowPosition().line == 1)
        assert(#typeFuncCst:argumentsCommaPositions() == 1)
        assert(#typeFuncCst:argumentNameColonPositions() == 2)

        -- Check that includeCst defaults to false / omitted results in nil cst()
        local noCstDoc = reflect.parse("local x = 1")
        assert(noCstDoc:root():body()[1]:cst() == nil)
        local noCstExpr = reflect.parseExpr("1 + 2")
        assert(noCstExpr:cst() == nil)
        local cstExpr = reflect.parseExpr("1 + 2", true)
        assert(cstExpr:cst() ~= nil)
        assert(cstExpr:cst().kind == "CstExprOp")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstComments")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("-- single comment\nlocal x = 1\n--[[\nblock comment\n]]\n")
        local comments = doc:comments()
        assert(#comments == 2)

        local c1 = comments[1]
        assert(typeof(c1) == "AstAux")
        assert(c1.kind == "AstComment")
        assert(c1.type == "single")
        assert(c1.location.beginLine == 1)
        assert(c1.text == "-- single comment")

        local c2 = comments[2]
        assert(typeof(c2) == "AstAux")
        assert(c2.kind == "AstComment")
        assert(c2.type == "block")
        assert(c2.text == "--[[\nblock comment\n]]")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstTableItems")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse([[
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
        assert(item1.key == nil)
        assert(item1.value.kind == "AstExprConstantString")
        assert(item1.value:value() == "hello")

        -- Record item
        local item2 = items[2]
        assert(typeof(item2) == "AstAux")
        assert(item2.kind == "record")
        assert(item2.key ~= nil)
        assert(item2.key.kind == "AstExprConstantString")
        assert(item2.key:value() == "foo")
        assert(item2.value.kind == "AstExprConstantInteger" or item2.value.kind == "AstExprConstantNumber")
        assert(item2.value:value() == 123)

        -- General item
        local item3 = items[3]
        assert(typeof(item3) == "AstAux")
        assert(item3.kind == "general")
        assert(item3.key ~= nil)
        assert(item3.key.kind == "AstExprBinary")
        assert(item3.key:op() == "..")
        assert(item3.value.kind == "AstExprConstantBool")
        assert(item3.value:value() == true)

        -- CST table items
        local tableCst = tableExpr:cst()
        assert(tableCst ~= nil)
        local cstItems = tableCst:items()
        assert(#cstItems == 3)
        assert(typeof(cstItems[1]) == "AstAux")
        assert(typeof(cstItems[2]) == "AstAux")
        assert(typeof(cstItems[3]) == "AstAux")
        assert(cstItems[2].equalsPosition ~= nil)
        assert(cstItems[3].indexerOpenPosition ~= nil)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("AstTypeTableAndAstAux")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse([[
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
        assert(prop1.name == "foo")
        assert(prop1.access == "read")
        assert(prop1.type.kind == "AstTypeReference")
        assert(prop1.type:name() == "string")

        local prop2 = props[2]
        assert(prop2.kind == "AstTableProp")
        assert(prop2.name == "bar")
        assert(prop2.access == "readwrite")
        assert(prop2.type.kind == "AstTypeReference")
        assert(prop2.type:name() == "number")

        local indexer = ty:indexer()
        assert(indexer ~= nil)
        assert(indexer.kind == "AstTableIndexer")
        assert(indexer.access == "readwrite")
        assert(indexer.indexType.kind == "AstTypeReference")
        assert(indexer.indexType:name() == "string")
        assert(indexer.resultType.kind == "AstTypeReference")
        assert(indexer.resultType:name() == "boolean")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("TestFields")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local a = 1\nfoo(1, 2)", true)
        local stat1 = doc:root():body()[1]
        local loc = stat1:location()

        -- Test direct field access on AstLocation
        assert(loc.beginLine == 1)
        assert(loc.beginColumn == 1)
        assert(loc.endLine == 1)
        assert(loc.endColumn == 12)
        assert(loc.startOffset == 0)
        assert(loc.endOffset == 11)
        assert(loc.text == "local a = 1")

        -- Test direct field access on AstPosition
        local call = doc:root():body()[2]:expr()
        local pos = call:cst():openParens()
        assert(pos.line == 2)
        assert(pos.column == 4)
        assert(pos.computedOffset == 15)

        -- Test dot indexing a method (stat1.location(stat1))
        local bodyFn = stat1.location
        assert(typeof(bodyFn) == "function")
        local loc2 = bodyFn(stat1)
        assert(loc2.beginLine == 1)

        -- Test dot indexing a CST method
        local cstFn = call:cst().openParens
        assert(typeof(cstFn) == "function")
        local pos2 = cstFn(call:cst())
        assert(pos2.line == 2)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ParseReflectTypesFile")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};

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
        local doc = reflect.parse(sourceCode)
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
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local src = "-- hello\nlocal x: number = 42\nprint(x)\n"
        local doc = reflect.parse(src, true)
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
        assert(c.type == "single")
        assert(c.text == "-- hello")
        assert(c.location.beginLine == 1)
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
        assert(var.name == "x")
        assert(var.isConst == false)
        assert(var[99] == nil)

        local vals = stat1:values()
        assert(#vals == 1)
        local val = vals[1]
        assert(val.kind == "AstExprConstantNumber")
        assert(val:value() == 42)

        -- Test walk & find methods via namecall
        local foundCount = 0
        doc:walk(function(node)
            foundCount = foundCount + 1
        end)
        assert(foundCount > 0)

        local found = doc:find("AstStatLocal")
        assert(#found == 1)

        -- Test CST access via atoms
        local cst = stat1:cst()
        assert(cst.kind == "CstStatLocal")
        assert(cst[123] == nil)
        local colons = cst:varsAnnotationColonPositions()
        assert(#colons == 1)
        assert(colons[1].line == 2)
        assert(colons[1].column == 8)
        assert(colons[1][123] == nil)

        -- Test location atoms
        local loc = stat1:location()
        assert(loc.beginLine == 2)
        assert(loc.beginColumn == 1)
        assert(loc.endLine == 2)
        assert(loc[123] == nil)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("ReflectUserdataId")
{
    ScopedFastFlag sff1{FFlag::LuauDirectFieldGet, true};
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local a = 1; local b = 2", true)
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
    ScopedFastFlag sff2{FFlag::LuauManagedReferences2, true};
    ScopedFastFlag sff3{FFlag::OptLuwuReflectUseAtoms, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("-- comment\ndo end", true)
        local root = doc:root()
        local stat = root:body()[1]
        local loc = stat:location()
        local cst = stat:cst()
        local pos = cst:endPosition()
        local comment = doc:comments()[1]

        assert(getmetatable(doc) == false)
        assert(getmetatable(root) == false)
        assert(getmetatable(stat) == false)
        assert(getmetatable(loc) == false)
        assert(getmetatable(cst) == false)
        assert(getmetatable(pos) == false)
        assert(getmetatable(comment) == false)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_SUITE_END();

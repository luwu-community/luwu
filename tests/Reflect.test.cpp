// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Reflect.h"
#include "Luau/ReflectCommon.h"

#include "doctest.h"
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include <memory>
#include <cstring>

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
    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local x = 1")
        assert(typeof(doc) == "AstDocument")

        local root = doc.root
        assert(typeof(root) == "AstNode")
        assert(root.kind == "AstStatBlock")
        assert(root.category == "stat")

        local stat = root.body[1]
        assert(typeof(stat) == "AstNode")
        assert(stat.kind == "AstStatLocal")
        assert(stat.category == "stat")

        local val = stat.values[1]
        assert(typeof(val) == "AstNode")
        assert(val.kind == "AstExprConstantNumber")
        assert(val.category == "expr")

        local localItem = stat.vars[1]
        assert(typeof(localItem) == "AstLocal")
        assert(localItem.name == "x")

        local loc = stat.location
        assert(typeof(loc) == "AstLocation")
        assert(loc.beginLine == 1)
        assert(loc.beginColumn == 1)
        assert(loc.text == "local x = 1")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstProperties")
{
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

        local root = doc.root
        local ifStat = root.body[1]
        assert(ifStat.kind == "AstStatIf")
        assert(ifStat.category == "stat")

        local cond = ifStat.condition
        assert(cond.kind == "AstExprBinary")
        assert(cond.category == "expr")
        assert(cond.op == ">")
        assert(cond.left.name == "x")
        assert(cond.right.value == 0)

        local thenBody = ifStat.thenbody
        assert(thenBody.kind == "AstStatBlock")
        assert(thenBody.category == "stat")
        assert(#thenBody.body == 1)

        local callExpr = thenBody.body[1].expr
        assert(callExpr.kind == "AstExprCall")
        assert(callExpr.category == "expr")
        assert(callExpr.func.name == "print")
        assert(#callExpr.args == 1)
        assert(callExpr.args[1].value == "positive")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstChildrenAndWalk")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local a = 1; local b = 2; return a + b")
        
        -- Test children() on block
        local root = doc.root
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
        assert(locals[1].vars[1].name == "a")
        assert(locals[2].vars[1].name == "b")

        -- Test walk pruning (return false)
        local topOnly = {}
        doc.root:walk(function(node)
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
        assert(expr.op == "+")
        assert(expr.left.kind == "AstExprGlobal")
        assert(expr.left.name == "a")

        local right = expr.right
        assert(typeof(right) == "AstNode")
        assert(right.kind == "AstExprBinary")
        assert(right.category == "expr")
        assert(right.op == "*")
        assert(right.left.name == "b")
        assert(right.right.value == 2)
        assert(expr.text == "a + b * 2")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstErrors")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local x = ")
        assert(#doc.errors > 0)
        assert(doc.errors[1].message ~= nil)
        assert(doc.errors[1].beginLine == 1)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyCstData")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("local str = 'hello world'\nfoo(1, 2)")
        local stats = doc.root.body

        -- Check local stat CST
        local locStat = stats[1]
        assert(locStat.cst ~= nil)
        assert(typeof(locStat.cst) == "CstNode")
        assert(locStat.cst.kind == "CstStatLocal")

        -- Check string CST quote style
        local strExpr = locStat.values[1]
        assert(strExpr.cst ~= nil)
        assert(typeof(strExpr.cst) == "CstNode")
        assert(strExpr.cst.kind == "CstExprConstantString")
        assert(strExpr.cst.quoteStyle == "single")
        assert(strExpr.cst.sourceString == "hello world")

        -- Check call CST parens and comma positions
        local callStat = stats[2]
        local callExpr = callStat.expr
        assert(callExpr.cst ~= nil)
        assert(callExpr.cst.kind == "CstExprCall")
        assert(typeof(callExpr.cst.openParens) == "AstPosition")
        assert(callExpr.cst.openParens.line == 2)
        assert(callExpr.cst.openParens.column == 4)
        assert(callExpr.cst.openParens.offset > 0)
        assert(#callExpr.cst.commaPositions == 1)
        assert(typeof(callExpr.cst.commaPositions[1]) == "AstPosition")

        -- Check type function CST and AST category
        local typeDoc = reflect.parse("type Callback = (a: number, b: string) -> boolean")
        local aliasStat = typeDoc.root.body[1]
        assert(aliasStat.category == "stat")
        assert(aliasStat.cst ~= nil)
        assert(aliasStat.cst.kind == "CstStatTypeAlias")
        assert(aliasStat.cst.typeKeywordPosition.line == 1)

        local typeFunc = aliasStat.type
        assert(typeFunc.category == "type")
        assert(typeFunc.cst ~= nil)
        assert(typeFunc.cst.kind == "CstTypeFunction")
        assert(typeFunc.cst.returnArrowPosition.line == 1)
        assert(#typeFunc.cst.argumentsCommaPositions == 1)
        assert(#typeFunc.cst.argumentNameColonPositions == 2)
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_CASE("LazyAstComments")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();
    luaL_openlibs(L);

    Luau::luaopen_reflect(L);
    lua_setglobal(L, "reflect");

    const char* script = R"LUA(
        local doc = reflect.parse("-- single comment\nlocal x = 1\n--[[\nblock comment\n]]\n")
        assert(#doc.comments == 2)

        local c1 = doc.comments[1]
        assert(typeof(c1) == "AstComment")
        assert(c1.type == "single")
        assert(c1.location.beginLine == 1)
        assert(c1.text == "-- single comment")

        local c2 = doc.comments[2]
        assert(typeof(c2) == "AstComment")
        assert(c2.type == "block")
        assert(c2.text == "--[[\nblock comment\n]]")
    )LUA";

    CHECK_EQ(dostring(L, script), 0);
}

TEST_SUITE_END();

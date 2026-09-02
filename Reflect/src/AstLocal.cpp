// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushAstLocal, checkAstLocal, astLocalDtor, AstLocalData, Luau::AstLocal*, TagLocal, "AstLocal")

static int astLocalProperties(lua_State* L, AstLocalData& handle)
{
    lua_createtable(L, 0, 8);
    lua_pushlightuserdatatagged(L, (void*)handle.local, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, "AstLocal");
    lua_setfield(L, -2, "kind");

    pushReflectValue(L, handle.doc, handle.local->name);
    lua_setfield(L, -2, "name");

    pushLocation(L, handle.doc, handle.local->location);
    lua_setfield(L, -2, "location");

    pushReflectValue(L, handle.doc, handle.local->shadow);
    lua_setfield(L, -2, "shadow");

    pushReflectValue(L, handle.doc, handle.local->functionDepth);
    lua_setfield(L, -2, "depth");

    pushReflectValue(L, handle.doc, handle.local->isConst);
    lua_setfield(L, -2, "isConst");

    pushReflectValue(L, handle.doc, handle.local->isExported);
    lua_setfield(L, -2, "exported");

    pushReflectValue(L, handle.doc, handle.local->annotation);
    lua_setfield(L, -2, "annotation");

    return 1;
}

static int dispatchAstLocalMethod(lua_State* L, AstLocalData& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Properties:
        return astLocalProperties(L, handle);

    case ReflectAtom::Name:
        pushReflectValue(L, handle.doc, handle.local->name);
        return 1;

    case ReflectAtom::SetName:
        readReflectValue(L, handle.doc, 2, handle.local->name);
        lua_pushvalue(L, 1);
        return 1;

    case ReflectAtom::Location:
        pushLocation(L, handle.doc, handle.local->location);
        return 1;

    case ReflectAtom::SetLocation:
        readReflectValue(L, handle.doc, 2, handle.local->location);
        lua_pushvalue(L, 1);
        return 1;

    case ReflectAtom::Shadow:
        pushReflectValue(L, handle.doc, handle.local->shadow);
        return 1;

    case ReflectAtom::SetShadow:
        readReflectValue(L, handle.doc, 2, handle.local->shadow);
        lua_pushvalue(L, 1);
        return 1;

    case ReflectAtom::Depth:
        pushReflectValue(L, handle.doc, handle.local->functionDepth);
        return 1;

    case ReflectAtom::SetDepth:
        readReflectValue(L, handle.doc, 2, handle.local->functionDepth);
        lua_pushvalue(L, 1);
        return 1;

    case ReflectAtom::IsConst:
        pushReflectValue(L, handle.doc, handle.local->isConst);
        return 1;

    case ReflectAtom::SetIsConst:
        readReflectValue(L, handle.doc, 2, handle.local->isConst);
        lua_pushvalue(L, 1);
        return 1;

    case ReflectAtom::Exported:
        pushReflectValue(L, handle.doc, handle.local->isExported);
        return 1;

    case ReflectAtom::SetExported:
        readReflectValue(L, handle.doc, 2, handle.local->isExported);
        lua_pushvalue(L, 1);
        return 1;

    case ReflectAtom::Annotation:
        pushReflectValue(L, handle.doc, handle.local->annotation);
        return 1;

    case ReflectAtom::SetAnnotation:
        readReflectValue(L, handle.doc, 2, handle.local->annotation);
        lua_pushvalue(L, 1);
        return 1;

    default:
        break;
    }

    luaL_error(L, "%.*s is not a valid method of AstLocal", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astLocalMethodTrampoline, checkAstLocal, dispatchAstLocalMethod)
LUAU_REFLECT_NAMECALL(astLocalNamecall, checkAstLocal, dispatchAstLocalMethod)

static int astLocalIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstLocal);

    switch (atom)
    {
    case ReflectAtom::Id:
        lua_pushlightuserdatatagged(L, (void*)handle.local, TagId);
        return 1;

    case ReflectAtom::Kind:
        lua_pushstring(L, "AstLocal");
        return 1;

    default:
        break;
    }

    if (atom != ReflectAtom::Unknown)
        return pushCachedUserdataMethod(L, TagLocal, keyStr, astLocalMethodTrampoline);

    lua_pushnil(L);
    return 1;
}

static int astLocalToString(lua_State* L)
{
    auto& handle = checkAstLocal(L, 1);
    if (handle.local && handle.local->name.value)
        lua_pushfstring(L, "AstLocal(%s)", handle.local->name.value);
    else
        lua_pushstring(L, "AstLocal");
    return 1;
}

LUAU_REFLECT_DEFINE_EQ(astLocalEq, TagLocal, checkAstLocal, a.local == b.local)

Luau::AstLocal* createDefaultAstLocal(std::string_view kind, Luau::Allocator& alloc)
{
    if (kind == "AstLocal")
        return alloc.alloc<Luau::AstLocal>(Luau::AstName(), Luau::Location(), nullptr, 0, 0, nullptr, false);
    return nullptr;
}

void registerAstLocal(lua_State* L)
{
    registerUserdataType(L, TagLocal, "AstLocal", astLocalDtor, astLocalIndex, astLocalToString, astLocalEq, astLocalNamecall);
}

} // namespace Luau

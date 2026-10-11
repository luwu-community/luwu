// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/PrettyPrinter.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "AstNodes.gen.inl"

LUAU_FASTINT(LuauRecursionLimit)

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushAstNode, checkAstNode, astNodeDtor, AstNodeData, Luau::AstNode*, TagNode, "AstNode")

NodeCategory getNodeCategory(Luau::AstNode* node)
{
    if (!node)
        return NodeCategory::Unknown;
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()))
        return s_nodeClassTable[idx].categoryEnum;
    return NodeCategory::Unknown;
}

bool canNodeHoldComments(Luau::AstNode* node)
{
    if (!node)
        return false;
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()))
        return s_nodeClassTable[idx].canHoldComments;
    return false;
}

int getNodeClassIndexByKind(std::string_view kind)
{
    for (size_t i = 0; i < s_nodeClassTable.size(); i++)
    {
        if (s_nodeClassTable[i].kind && kind == s_nodeClassTable[i].kind)
            return int(i);
    }
    return -1;
}

LUAU_REFLECT_GET_NODE_KIND(getNodeKind, Luau::AstNode*, s_nodeClassTable, "AstNode")
LUAU_REFLECT_GET_NODE_CATEGORY(getAstNodeCategory, Luau::AstNode*, s_nodeClassTable, "unknown")

void AstValidator::node(Luau::AstNode* node)
{
    if (const int* known = heights.find(node))
    {
        if (*known < 0)
            luaL_error(L, "malformed AST: %s is nested inside itself", getNodeKind(node));
        if (depth + *known > maxDepth)
            luaL_error(L, "malformed AST: nesting is deeper than the limit of %d", maxDepth);
        tallestChild = std::max(tallestChild, *known);
        return;
    }

    if (depth >= maxDepth)
        luaL_error(L, "malformed AST: nesting is deeper than the limit of %d", maxDepth);

    heights[node] = -1;

    const char* outerKind = kind;
    int outerTallestChild = tallestChild;
    kind = getNodeKind(node);
    tallestChild = 0;
    depth++;

    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].validator)
        s_nodeClassTable[idx].validator(*this, node);

    depth--;
    int height = tallestChild + 1;
    heights[node] = height;
    kind = outerKind;
    tallestChild = std::max(outerTallestChild, height);
}

void AstValidator::fail(const char* field, int index, const char* problem)
{
    if (index >= 0)
        luaL_error(L, "malformed AST: %s.%s[%d] %s", kind, field, index + 1, problem);
    luaL_error(L, "malformed AST: %s.%s %s", kind, field, problem);
}

void AstValidator::child(Luau::AstNode* node, const char* field, int index, const char* missing)
{
    if (!node)
    {
        if (missing)
            fail(field, index, missing);
        return;
    }
    if (deep)
        this->node(node);
}

void AstValidator::local(Luau::AstLocal* local, const char* field, int index, const char* missing)
{
    if (!local)
    {
        if (missing)
            fail(field, index, missing);
        return;
    }
    if (forPrint && !local->name.value)
        fail(field, index, "has no name");
    child(local->annotation, field, index, nullptr);
}

void AstValidator::name(const Luau::AstName& name, const char* field, int index, const char* missing)
{
    if (forPrint && missing && !name.value)
        fail(field, index, missing);
}

void validateAstNode(lua_State* L, const AstNodeData& handle, AstValidation mode)
{
    if (!handle.node || !handle.doc || !handle.doc->arena || !handle.doc->arena->mutated)
        return;

    AstValidator validator{L, mode != AstValidation::Node, mode == AstValidation::Print, int(FInt::LuauRecursionLimit)};
    validator.node(handle.node);
}

struct DirectChildCollector : public Luau::AstVisitor
{
    std::vector<Luau::AstNode*> children;
    bool isRoot = true;

    bool visit(Luau::AstNode* node) override
    {
        if (isRoot)
        {
            isRoot = false;
            return true;
        }
        children.push_back(node);
        return false;
    }

    // By default visiting type annotations is disabled; we override this so visitor inspects these nodes
    bool visit(Luau::AstType* node) override
    {
        return visit(static_cast<Luau::AstNode*>(node));
    }

    bool visit(Luau::AstTypePack* node) override
    {
        return visit(static_cast<Luau::AstNode*>(node));
    }
};

static void initializeDispatchTables()
{
    static const bool initialized = []() {
        registerAstNodeClasses();
        return true;
    }();
    (void)initialized;
}

static int astNodeChildren(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    validateAstNode(L, handle, AstValidation::Node);
    DirectChildCollector collector;
    handle.node->visit(&collector);
    pushArray(L, collector.children.size(), [&](size_t i) {
        pushAstNode(L, handle.doc, collector.children[i]);
    });
    return 1;
}

struct CallbackVisitor : public Luau::AstVisitor
{
    lua_State* L;
    std::shared_ptr<AstDocumentState> doc;
    int callbackIndex;
    AstFilterData filter;
    bool hasFilter = false;
    bool errorOccurred = false;

    CallbackVisitor(lua_State* L, std::shared_ptr<AstDocumentState> doc, int callbackIndex, const AstFilterData& filter, bool hasFilter)
        : L(L)
        , doc(doc)
        , callbackIndex(callbackIndex)
        , filter(filter)
        , hasFilter(hasFilter)
    {
    }

    bool visit(Luau::AstNode* node) override
    {
        if (errorOccurred || !node)
            return false;

        if (hasFilter && !filter.matches(node))
            return true;

        lua_pushvalue(L, callbackIndex);
        pushAstNode(L, doc, node);

        int status = lua_pcall(L, 1, 1, 0);
        if (status != 0)
        {
            errorOccurred = true;
            return false;
        }

        if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }

        lua_pop(L, 1);
        return true;
    }

    // By default visiting type annotations is disabled; we override this so visitor inspects these nodes
    bool visit(Luau::AstType* node) override
    {
        return visit(static_cast<Luau::AstNode*>(node));
    }

    bool visit(Luau::AstTypePack* node) override
    {
        return visit(static_cast<Luau::AstNode*>(node));
    }
};

static int astNodeWalk(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    AstFilterData filter = extractAstFilter(L, 3);

    validateAstNode(L, handle, AstValidation::Walk);

    struct WalkScope
    {
        AstAllocatorState& arena;

        explicit WalkScope(AstAllocatorState& arena)
            : arena(arena)
        {
            arena.activeWalks++;
        }

        ~WalkScope()
        {
            arena.activeWalks--;
        }
    } walkScope(*handle.doc->arena);

    CallbackVisitor visitor(L, handle.doc, 2, filter, !lua_isnoneornil(L, 3));
    handle.node->visit(&visitor);

    if (visitor.errorOccurred)
        lua_error(L);

    return 0;
}

static int astNodeProperties(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.node)
    {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, 0, 10);

    lua_pushlightuserdatatagged(L, (void*)handle.node, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, getNodeKind(handle.node));
    lua_setfield(L, -2, "kind");

    lua_pushstring(L, getAstNodeCategory(handle.node));
    lua_setfield(L, -2, "category");

    int idx = handle.node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].propCollector)
    {
        s_nodeClassTable[idx].propCollector(L, handle);
    }

    auto pushCommentList = [&](const char* field, const std::vector<ReflectComment>* list) {
        if (list && !list->empty())
        {
            pushArray(L, list->size(), [&](size_t i) {
                pushAstAux(L, handle.doc, (*list)[i]);
            });
        }
        else
        {
            lua_newtable(L);
        }
        lua_setfield(L, -2, field);
    };

    const auto* comments = (handle.doc && handle.node) ? handle.doc->nodeComments.find(handle.node) : nullptr;
    pushCommentList("leadingComments", comments ? &comments->leading : nullptr);
    pushCommentList("trailingComments", comments ? &comments->trailing : nullptr);

    return 1;
}

static int astNodePrettyprint(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.node)
    {
        lua_pushliteral(L, "");
        return 1;
    }
    validateAstNode(L, handle, AstValidation::Print);
    Luau::AstStatBlock* root = handle.doc ? handle.doc->parseResult.root : nullptr;
    std::string s = handle.node == root ? Luau::prettyPrintWithTypes(*root) : Luau::toString(handle.node);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

struct CommentAttacher : public Luau::AstVisitor
{
    AstDocumentState& doc;
    const std::vector<Luau::Comment>& comments;
    size_t cursor = 0;

    CommentAttacher(AstDocumentState& doc)
        : doc(doc)
        , comments(doc.parseResult.commentLocations)
    {
    }

    ReflectComment extractComment(const Luau::Comment& c)
    {
        return extractReflectComment(doc, c);
    }

    bool visit(Luau::AstNode* node) override
    {
        if (!canNodeHoldComments(node))
            return true;

        if (auto block = node->as<AstStatBlock>())
        {
            if (block->body.size > 0)
                return true;
        }

        while (cursor < comments.size() && comments[cursor].location.end <= node->location.begin)
        {
            doc.nodeComments[node].leading.push_back(extractComment(comments[cursor++]));
        }

        if (cursor < comments.size() && comments[cursor].location.begin.line == node->location.end.line)
        {
            doc.nodeComments[node].trailing.push_back(extractComment(comments[cursor++]));
        }

        return true;
    }
};

void attachCommentsToAst(AstDocumentState& doc, Luau::AstNode* rootNode)
{
    const auto& comments = doc.parseResult.commentLocations;
    Luau::AstNode* root = rootNode ? rootNode : static_cast<Luau::AstNode*>(doc.parseResult.root);
    if (!root || comments.empty())
        return;

    CommentAttacher attacher(doc);
    root->visit(&attacher);

    while (attacher.cursor < comments.size())
    {
        doc.nodeComments[root].trailing.push_back(attacher.extractComment(comments[attacher.cursor++]));
    }
}

static int astNodeGetComments(lua_State* L, bool trailing)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.doc || !handle.node)
    {
        lua_newtable(L);
        return 1;
    }
    const auto* comments = handle.doc->nodeComments.find(handle.node);
    if (!comments)
    {
        lua_newtable(L);
        return 1;
    }
    const auto& list = trailing ? comments->trailing : comments->leading;
    pushArray(L, list.size(), [&](size_t i) {
        pushAstAux(L, handle.doc, list[i]);
    });
    return 1;
}

static int astNodeLeadingComments(lua_State* L)
{
    return astNodeGetComments(L, false);
}

static int astNodeTrailingComments(lua_State* L)
{
    return astNodeGetComments(L, true);
}

static void parseCommentArray(lua_State* L, int argIdx, std::vector<ReflectComment>& out)
{
    luaL_checktype(L, argIdx, LUA_TTABLE);
    int len = int(lua_objlen(L, argIdx));
    out.clear();
    out.reserve(len);
    for (int i = 1; i <= len; ++i)
    {
        lua_rawgeti(L, argIdx, i);
        if (lua_userdatatag(L, -1) == TagAux)
        {
            auto& aux = *static_cast<AstAuxData*>(lua_touserdata(L, -1));
            if (aux.kind == Aux_Comment)
                out.push_back(aux.comment);
            else
                luaL_error(L, "expected AstComment at index %d in comment array", i);
        }
        else
        {
            luaL_error(L, "expected AstComment at index %d in comment array", i);
        }
        lua_pop(L, 1);
    }
}

static int astNodeSetComments(lua_State* L, bool trailing)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.doc || !handle.node)
        luaL_error(L, "cannot set comments on null node");
    auto& target = trailing ? handle.doc->nodeComments[handle.node].trailing : handle.doc->nodeComments[handle.node].leading;
    parseCommentArray(L, 2, target);
    lua_pushvalue(L, 1);
    return 1;
}

static int astNodeSetLeadingComments(lua_State* L)
{
    return astNodeSetComments(L, false);
}

static int astNodeSetTrailingComments(lua_State* L)
{
    return astNodeSetComments(L, true);
}

static int dispatchAstNodeMethod(lua_State* L, AstNodeData& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Children:            return astNodeChildren(L);
    case ReflectAtom::Walk:                return astNodeWalk(L);
    case ReflectAtom::Properties:          return astNodeProperties(L);
    case ReflectAtom::Prettyprint:         return astNodePrettyprint(L);
    case ReflectAtom::LeadingComments:     return astNodeLeadingComments(L);
    case ReflectAtom::SetLeadingComments:  return astNodeSetLeadingComments(L);
    case ReflectAtom::TrailingComments:    return astNodeTrailingComments(L);
    case ReflectAtom::SetTrailingComments: return astNodeSetTrailingComments(L);
    case ReflectAtom::Comments:            return astNodeLeadingComments(L);
    default: break;
    }

    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].methodHandler)
    {
        if (s_nodeClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }
    luaL_error(L, "%.*s is not a valid method of AstNode", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astNodeMethodTrampoline, checkAstNode, dispatchAstNodeMethod)
LUAU_REFLECT_NAMECALL(astNodeNamecall, checkAstNode, dispatchAstNodeMethod)
LUAU_REFLECT_INDEX(astNodeIndex, checkAstNode, getNodeKind, getAstNodeCategory, TagNode, astNodeMethodTrampoline)

static int astNodeToString(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    lua_pushfstring(L, "AstNode(%s)", getNodeKind(handle.node));
    return 1;
}

LUAU_REFLECT_DEFINE_EQ(astNodeEq, TagNode, checkAstNode, a.node == b.node)

void registerAstNode(lua_State* L)
{
    initializeDispatchTables();
    registerUserdataType(L, TagNode, "AstNode", astNodeDtor, astNodeIndex, astNodeToString, astNodeEq, astNodeNamecall);
}

} // namespace Luau

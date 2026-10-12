// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "AstNodes.gen.inl"

LUAU_FASTINT(LuauRecursionLimit)

namespace Luau
{

static const AstItemClass s_unknownNodeClass = {"AstNode", "unknown", ItemFamily::Node};

const AstItemClass* astNodeClass(Luau::AstNode* node)
{
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()))
        return &s_nodeClassTable[idx];
    return &s_unknownNodeClass;
}

void pushAstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstNode* node)
{
    if (!node)
    {
        lua_pushnil(L);
        return;
    }
    pushAstItem(L, doc, astNodeClass(node), node);
}

NodeCategory getNodeCategory(Luau::AstNode* node)
{
    return node ? astNodeClass(node)->categoryEnum : NodeCategory::Unknown;
}

bool canNodeHoldComments(Luau::AstNode* node)
{
    return node && astNodeClass(node)->canHoldComments;
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

const char* getNodeKind(Luau::AstNode* node)
{
    return node ? astNodeClass(node)->kind : "nil";
}

static const int kRecursionLevelsPerCStackUnit = 4;
static const int kFreeCStackUnits = 16;

static int availableDepth(lua_State* L)
{
    int limit = int(FInt::LuauRecursionLimit);
    int charged = std::max(0, lua_cstackdepth(L) - kFreeCStackUnits) * kRecursionLevelsPerCStackUnit;
    return std::max(0, limit - charged);
}

void AstValidator::tooDeep()
{
    if (maxDepth < int(FInt::LuauRecursionLimit))
        luaL_error(L, "C stack overflow");
    luaL_error(L, "malformed AST: nesting is deeper than the limit of %d", maxDepth);
}

void AstValidator::node(Luau::AstNode* node)
{
    const int* known = heights.find(node);
    if (!known && untainted)
        known = untainted->find(node);

    if (known)
    {
        if (*known < 0)
            luaL_error(L, "malformed AST: %s is nested inside itself", getNodeKind(node));
        if (depth + *known > maxDepth)
            tooDeep();
        tallestChild = std::max(tallestChild, *known);
        return;
    }

    if (depth >= maxDepth)
        tooDeep();

    heights[node] = -1;

    const char* outerKind = kind;
    int outerTallestChild = tallestChild;
    kind = getNodeKind(node);
    tallestChild = 0;
    depth++;

    if (auto validator = astNodeClass(node)->validator)
        validator(*this, node);

    depth--;
    int height = tallestChild + 1;
    heights[node] = height;
    order.push_back(node);
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

static void requireUntainted(lua_State* L, const AstItem& handle, const char* action)
{
    if (isTainted(*handle.doc, handle.node()))
        luaL_error(L, "cannot %s a tainted AST; call :untaint() on it first", action);
}

static int astNodeUntaint(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    AstTreeState& tree = treeOf(*handle.doc);

    if (isTainted(*handle.doc, handle.node()))
    {
        AstValidator validator{L, true, false, availableDepth(L)};
        validator.untainted = &tree.untainted;
        validator.node(handle.node());

        if (tree.untainted.empty())
            tree.untainted = std::move(validator.heights);
        else
        {
            for (const Luau::AstNode* visited : validator.order)
                tree.untainted[visited] = *validator.heights.find(visited);
        }
    }

    lua_pushvalue(L, 1);
    return 1;
}

static int astNodeTainted(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    lua_pushboolean(L, isTainted(*handle.doc, handle.node()));
    return 1;
}

void collectAstChildren(Luau::AstNode* node, std::vector<Luau::AstNode*>& out)
{
    if (auto children = astNodeClass(node)->children)
        children(node, out);
}

void initializeAstNodeClasses()
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
    if (isTainted(*handle.doc, handle.node()))
    {
        AstValidator validator{L, false, false, int(FInt::LuauRecursionLimit)};
        validator.node(handle.node());
    }
    std::vector<Luau::AstNode*> children;
    collectAstChildren(handle.node(), children);
    pushArray(L, children.size(), [&](size_t i) {
        pushAstNode(L, handle.doc, children[i]);
    });
    return 1;
}

static int astNodeWalk(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    AstFilterData filter = extractAstFilter(L, 3);
    bool hasFilter = !lua_isnoneornil(L, 3);

    requireUntainted(L, handle, "walk");

    struct WalkScope
    {
        std::shared_ptr<AstTreeState> tree;

        explicit WalkScope(std::shared_ptr<AstTreeState> tree)
            : tree(std::move(tree))
        {
            this->tree->activeWalks++;
        }

        ~WalkScope()
        {
            tree->activeWalks--;
        }
    } walkScope(handle.doc->tree);

    std::vector<Luau::AstNode*> pending;
    pending.push_back(handle.node());

    bool errorOccurred = false;

    while (!pending.empty())
    {
        Luau::AstNode* node = pending.back();
        pending.pop_back();

        if (!hasFilter || filter.matches(node))
        {
            lua_pushvalue(L, 2);
            pushAstNode(L, handle.doc, node);

            if (lua_pcall(L, 1, 1, 0) != 0)
            {
                errorOccurred = true;
                break;
            }

            bool descend = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
            lua_pop(L, 1);
            if (!descend)
                continue;
        }

        size_t first = pending.size();
        collectAstChildren(node, pending);
        std::reverse(pending.begin() + first, pending.end());
    }

    if (errorOccurred)
        lua_error(L);

    return 0;
}

static void pushCommentList(lua_State* L, AstItem& handle, const std::vector<ReflectComment*>* list)
{
    if (!list)
    {
        lua_newtable(L);
        return;
    }
    pushArray(L, list->size(), [&](size_t i) {
        pushItem(L, handle.doc, (*list)[i]);
    });
}

static int astNodeProperties(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);

    lua_createtable(L, 0, 10);

    lua_pushlightuserdatatagged(L, handle.target, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, handle.cls->kind);
    lua_setfield(L, -2, "kind");

    lua_pushstring(L, handle.cls->category);
    lua_setfield(L, -2, "category");

    if (handle.cls->propCollector)
        handle.cls->propCollector(L, handle);

    const auto* comments = treeOf(*handle.doc).nodeComments.find(handle.node());
    pushCommentList(L, handle, comments ? &comments->leading : nullptr);
    lua_setfield(L, -2, "leadingComments");
    pushCommentList(L, handle, comments ? &comments->trailing : nullptr);
    lua_setfield(L, -2, "trailingComments");

    return 1;
}

static int astNodePrettyprint(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    requireUntainted(L, handle, "prettyprint");

    AstTreeState& tree = treeOf(*handle.doc);
    Luau::AstNode* node = handle.node();

    int maxDepth = availableDepth(L);
    if (isEdited(*handle.doc) || maxDepth < int(FInt::LuauRecursionLimit))
    {
        AstValidator validator{L, true, true, maxDepth};
        validator.node(node);
    }

    std::string error;
    std::string s = reflectPrettyPrint(node, node == handle.doc->parseResult.root, tree, error);
    if (!error.empty())
        luaL_error(L, "%s", error.c_str());

    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

struct CommentAttacher
{
    const std::vector<ReflectComment*>& comments;
    NodeCommentMap& out;
    size_t cursor = 0;
    std::vector<Luau::AstNode*> scratch;

    CommentAttacher(const std::vector<ReflectComment*>& comments, NodeCommentMap& out)
        : comments(comments)
        , out(out)
    {
    }

    void attach(Luau::AstNode* node, bool statementBlock)
    {
        NodeCategory category = getNodeCategory(node);
        if (category == NodeCategory::Type || category == NodeCategory::TypePack)
            return;

        auto block = node->as<AstStatBlock>();

        if (canNodeHoldComments(node) && !(block && block->body.size > 0))
        {
            while (cursor < comments.size() && comments[cursor]->location.end <= node->location.begin)
            {
                out[node].leading.push_back(comments[cursor++]);
            }

            if (cursor < comments.size() && comments[cursor]->location.begin.line == node->location.end.line)
            {
                out[node].trailing.push_back(comments[cursor++]);
            }
        }

        if (block)
        {
            for (Luau::AstStat* stat : block->body)
            {
                attach(stat, stat->is<AstStatBlock>());

                while (cursor < comments.size() && stat->location.end <= comments[cursor]->location.begin &&
                       comments[cursor]->location.begin.line == stat->location.end.line)
                {
                    out[stat].trailing.push_back(comments[cursor++]);
                }
            }

            if (!statementBlock)
            {
                while (cursor < comments.size() && comments[cursor]->location.end <= block->location.end)
                {
                    out[block].trailing.push_back(comments[cursor++]);
                }
            }

            return;
        }

        size_t base = scratch.size();
        collectAstChildren(node, scratch);
        size_t end = scratch.size();
        for (size_t i = base; i < end; i++)
            attach(scratch[i], false);
        scratch.resize(base);
    }
};

static void collectNodeComments(AstDocumentState& doc, Luau::AstNode* rootNode, NodeCommentMap& out)
{
    Luau::AstNode* root = rootNode ? rootNode : static_cast<Luau::AstNode*>(doc.parseResult.root);
    const std::vector<ReflectComment*>& comments = documentComments(doc);
    if (!root || comments.empty())
        return;

    CommentAttacher attacher(comments, out);
    attacher.attach(root, false);

    while (attacher.cursor < comments.size())
    {
        out[root].trailing.push_back(comments[attacher.cursor++]);
    }
}

void attachCommentsToAst(AstDocumentState& doc, Luau::AstNode* rootNode)
{
    collectNodeComments(doc, rootNode, treeOf(doc).nodeComments);
}

void recordParsedComments(AstDocumentState& doc, Luau::AstNode* rootNode)
{
    collectNodeComments(doc, rootNode, treeOf(doc).parsedComments);
}

void recordExplicitVisibility(AstDocumentState& doc)
{
    Luau::AstStatBlock* root = doc.parseResult.root;
    if (!root)
        return;

    DenseHashMap2<const void*, bool>& written = treeOf(doc).explicitVisibility;

    for (Luau::AstStat* stat : root->body)
    {
        Luau::AstStatClass* cls = stat->as<Luau::AstStatClass>();
        if (!cls)
            continue;

        bool qualified = false;

        for (const Luau::AstClassMember& member : cls->members)
        {
            if (const auto* prop = Luau::get_if<Luau::AstClassProperty>(&member))
                qualified = qualified || prop->qualifierLocation.has_value();
            else if (const auto* method = Luau::get_if<Luau::AstClassMethod>(&member))
                qualified = qualified || method->qualifierLocation.has_value();
        }

        if (Luau::AstClassPrimaryConstructor* constructor = cls->primaryConstructor)
        {
            if (constructor->qualifierLocation.has_value())
                written[constructor] = true;

            for (const Luau::AstClassPrimaryConstructorParamQualifiers& qualifiers : constructor->argsQualifiers)
            {
                if (qualifiers.qualifierLocation.has_value())
                {
                    written[&qualifiers] = true;
                    qualified = true;
                }
            }
        }

        if (qualified)
            written[cls] = true;
    }
}

static int astNodeGetComments(lua_State* L, bool trailing)
{
    auto& handle = checkAstNode(L, 1);
    const auto* comments = treeOf(*handle.doc).nodeComments.find(handle.node());
    pushCommentList(L, handle, comments ? (trailing ? &comments->trailing : &comments->leading) : nullptr);
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

static void parseCommentArray(lua_State* L, AstItem& handle, int argIdx, std::vector<ReflectComment*>& out)
{
    luaL_checktype(L, argIdx, LUA_TTABLE);
    int len = int(lua_objlen(L, argIdx));
    std::vector<ReflectComment*> comments;
    comments.reserve(len);
    for (int i = 1; i <= len; ++i)
    {
        lua_rawgeti(L, argIdx, i);
        AstItem* item = static_cast<AstItem*>(lua_touserdatatagged(L, -1, TagItem));
        if (!item || item->cls != itemClass<ReflectComment>())
            luaL_error(L, "expected AstComment at index %d in comment array", i);
        checkSameAllocator(L, handle.doc, item->doc, "AstComment");
        comments.push_back(static_cast<ReflectComment*>(item->target));
        lua_pop(L, 1);
    }
    out = std::move(comments);
}

static int astNodeSetComments(lua_State* L, bool trailing)
{
    auto& handle = checkAstNode(L, 1);
    std::vector<ReflectComment*> list;
    parseCommentArray(L, handle, 2, list);

    AstTreeState& tree = treeOf(*handle.doc);
    tree.edits++;
    ClassifiedNodeComments& comments = tree.nodeComments[handle.node()];
    (trailing ? comments.trailing : comments.leading) = std::move(list);
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

int dispatchAstNodeMethod(lua_State* L, AstItem& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Children:            return astNodeChildren(L);
    case ReflectAtom::Walk:                return astNodeWalk(L);
    case ReflectAtom::Properties:          return astNodeProperties(L);
    case ReflectAtom::Prettyprint:         return astNodePrettyprint(L);
    case ReflectAtom::Untaint:             return astNodeUntaint(L);
    case ReflectAtom::Tainted:             return astNodeTainted(L);
    case ReflectAtom::LeadingComments:     return astNodeLeadingComments(L);
    case ReflectAtom::SetLeadingComments:  return astNodeSetLeadingComments(L);
    case ReflectAtom::TrailingComments:    return astNodeTrailingComments(L);
    case ReflectAtom::SetTrailingComments: return astNodeSetTrailingComments(L);
    case ReflectAtom::Comments:            return astNodeLeadingComments(L);
    default: break;
    }

    if (handle.cls->methodHandler && handle.cls->methodHandler(L, handle, atom))
        return 1;

    luaL_error(L, "%.*s is not a valid method of AstNode", int(len), str);
}

} // namespace Luau

// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/ReflectCommon.h"
#include "Luau/StringUtils.h"

#include <limits>
#include <math.h>

LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuauExportValueSyntax)
LUAU_FASTFLAG(LuwuDefaultArguments)

namespace Luau
{

namespace
{

bool isIdentifierStartChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool isIdentifierChar(char c)
{
    return isIdentifierStartChar(c) || (c >= '0' && c <= '9');
}

const char* const kKeywords[] = {"and",   "break", "do",  "else", "elseif", "end",    "false", "for",  "function", "if",   "in",
                                 "local", "nil",   "not", "or",   "repeat", "return", "then",  "true", "until",    "while"};

bool isPlainName(std::string_view s)
{
    if (s.empty() || !isIdentifierStartChar(s[0]))
        return false;

    for (char c : s)
    {
        if (!isIdentifierChar(c))
            return false;
    }

    for (const char* keyword : kKeywords)
    {
        if (s == keyword)
            return false;
    }

    return true;
}

int binaryPrecedence(AstExprBinary::Op op)
{
    switch (op)
    {
    case AstExprBinary::Or:
        return 1;
    case AstExprBinary::And:
        return 2;
    case AstExprBinary::CompareNe:
    case AstExprBinary::CompareEq:
    case AstExprBinary::CompareLt:
    case AstExprBinary::CompareLe:
    case AstExprBinary::CompareGt:
    case AstExprBinary::CompareGe:
        return 3;
    case AstExprBinary::Concat:
        return 5;
    case AstExprBinary::Add:
    case AstExprBinary::Sub:
        return 6;
    case AstExprBinary::Mul:
    case AstExprBinary::Div:
    case AstExprBinary::FloorDiv:
    case AstExprBinary::Mod:
        return 7;
    case AstExprBinary::Pow:
        return 10;
    default:
        return 0;
    }
}

const int kUnaryPrecedence = 8;
const size_t kMaxInlineItems = 3;

bool isRightAssociative(AstExprBinary::Op op)
{
    return op == AstExprBinary::Pow || op == AstExprBinary::Concat;
}

bool endsOpen(AstExpr& expr)
{
    if (expr.is<AstExprIfElse>())
        return true;
    if (auto binary = expr.as<AstExprBinary>())
        return endsOpen(*binary->right);
    if (auto unary = expr.as<AstExprUnary>())
        return endsOpen(*unary->expr);
    return false;
}

bool operandNeedsParens(AstExpr& child, AstExprBinary::Op parentOp, bool isRight)
{
    if (auto binary = child.as<AstExprBinary>())
    {
        int childPrecedence = binaryPrecedence(binary->op);
        int parentPrecedence = binaryPrecedence(parentOp);
        if (childPrecedence < parentPrecedence)
            return true;
        if (childPrecedence == parentPrecedence && isRight != isRightAssociative(parentOp))
            return true;
    }
    else if (child.is<AstExprUnary>())
    {
        if (parentOp == AstExprBinary::Pow && !isRight)
            return true;
    }

    return !isRight && endsOpen(child);
}

bool unaryOperandNeedsParens(AstExpr& child)
{
    if (auto binary = child.as<AstExprBinary>())
        return binaryPrecedence(binary->op) < kUnaryPrecedence;
    return false;
}

bool assertionOperandNeedsParens(AstExpr& child)
{
    return child.is<AstExprBinary>() || child.is<AstExprUnary>() || child.is<AstExprIfElse>() || child.is<AstExprTypeAssertion>();
}

bool prefixNeedsParens(AstExpr& expr)
{
    return !(
        expr.is<AstExprLocal>() || expr.is<AstExprGlobal>() || expr.is<AstExprIndexName>() || expr.is<AstExprIndexExpr>() || expr.is<AstExprCall>() ||
        expr.is<AstExprGroup>() || expr.is<AstExprInstantiate>()
    );
}

bool startsWithParen(AstExpr* expr)
{
    for (;;)
    {
        if (expr->is<AstExprGroup>())
            return true;

        AstExpr* next = nullptr;
        if (auto call = expr->as<AstExprCall>())
            next = call->func;
        else if (auto indexName = expr->as<AstExprIndexName>())
            next = indexName->expr;
        else if (auto indexExpr = expr->as<AstExprIndexExpr>())
            next = indexExpr->expr;
        else if (auto instantiate = expr->as<AstExprInstantiate>())
            next = instantiate->expr;

        if (next)
        {
            if (prefixNeedsParens(*next))
                return true;
            expr = next;
            continue;
        }

        if (auto binary = expr->as<AstExprBinary>())
        {
            if (operandNeedsParens(*binary->left, binary->op, false))
                return true;
            expr = binary->left;
        }
        else if (auto assertion = expr->as<AstExprTypeAssertion>())
        {
            if (assertionOperandNeedsParens(*assertion->expr))
                return true;
            expr = assertion->expr;
        }
        else
            return false;
    }
}

bool startsWithParen(AstStat& stat)
{
    if (auto statExpr = stat.as<AstStatExpr>())
        return startsWithParen(statExpr->expr);
    if (auto assign = stat.as<AstStatAssign>())
        return assign->vars.size > 0 && startsWithParen(assign->vars.data[0]);
    if (auto compound = stat.as<AstStatCompoundAssign>())
        return startsWithParen(compound->var);
    return false;
}

bool endsWithPrefixExpr(AstExpr* expr)
{
    for (;;)
    {
        if (auto binary = expr->as<AstExprBinary>())
            expr = binary->right;
        else if (auto unary = expr->as<AstExprUnary>())
            expr = unary->expr;
        else if (auto ifElse = expr->as<AstExprIfElse>())
            expr = ifElse->falseExpr;
        else
            return !prefixNeedsParens(*expr);
    }
}

bool endsWithPrefixExpr(AstStat& stat)
{
    if (auto statExpr = stat.as<AstStatExpr>())
        return endsWithPrefixExpr(statExpr->expr);
    if (auto statLocal = stat.as<AstStatLocal>())
        return statLocal->values.size > 0 && endsWithPrefixExpr(statLocal->values.data[statLocal->values.size - 1]);
    if (auto assign = stat.as<AstStatAssign>())
        return assign->values.size > 0 && endsWithPrefixExpr(assign->values.data[assign->values.size - 1]);
    if (auto compound = stat.as<AstStatCompoundAssign>())
        return endsWithPrefixExpr(compound->value);
    if (auto repeat = stat.as<AstStatRepeat>())
        return endsWithPrefixExpr(repeat->condition);
    return false;
}

bool startsWithTable(AstExpr* expr)
{
    for (;;)
    {
        if (expr->is<AstExprTable>())
            return true;
        if (auto binary = expr->as<AstExprBinary>())
            expr = binary->left;
        else if (auto assertion = expr->as<AstExprTypeAssertion>())
            expr = assertion->expr;
        else
            return false;
    }
}

bool startsWithMinus(AstExpr& expr)
{
    if (auto unary = expr.as<AstExprUnary>())
        return unary->op == AstExprUnary::Op::Minus;
    if (auto number = expr.as<AstExprConstantNumber>())
        return number->value < 0;
    return false;
}

bool isBlockComment(const ReflectComment& comment)
{
    std::string_view text(comment.text.data ? comment.text.data : "", comment.text.size);
    if (text.substr(0, 3) != "--[")
        return false;

    size_t i = 3;
    while (i < text.size() && text[i] == '=')
        i++;

    return i < text.size() && text[i] == '[';
}

struct ReflectFormatter
{
    ReflectFormatter(const AstTreeState& tree)
        : tree(tree)
        , comments(&tree.nodeComments)
        , fallbackComments(&tree.parsedComments)
    {
    }

    const AstTreeState& tree;

    std::string out;
    int indent = 0;
    bool lineStart = true;
    const NodeCommentMap* comments;
    const NodeCommentMap* fallbackComments;
    std::string error;

    void write(std::string_view s)
    {
        if (s.empty())
            return;

        if (lineStart)
        {
            out.append(size_t(indent) * 4, ' ');
            lineStart = false;
        }

        out.append(s.data(), s.size());
    }

    void newline()
    {
        out += '\n';
        lineStart = true;
    }

    void unsupported(AstNode* node)
    {
        if (error.empty())
            error = std::string("cannot print ") + getNodeKind(node);
    }

    const ClassifiedNodeComments* commentsFor(const AstNode* node)
    {
        if (comments)
        {
            if (const ClassifiedNodeComments* found = comments->find(node))
                return found;
        }
        return fallbackComments ? fallbackComments->find(node) : nullptr;
    }

    void writeComment(const ReflectComment& comment)
    {
        write(std::string_view(comment.text.data ? comment.text.data : "", comment.text.size));
    }

    void ownLineComments(const std::vector<ReflectComment*>& list)
    {
        for (const ReflectComment* comment : list)
        {
            writeComment(*comment);
            newline();
        }
    }

    void sameLineComments(const std::vector<ReflectComment*>& list)
    {
        bool lineOpen = true;

        for (const ReflectComment* comment : list)
        {
            if (lineOpen)
                write(" ");
            else
                newline();

            writeComment(*comment);
            lineOpen = isBlockComment(*comment);
        }
    }

    void name(const AstName& value)
    {
        write(value.value ? value.value : "");
    }

    void quoted(std::string_view s)
    {
        write("\"");
        write(escape(s));
        write("\"");
    }

    void propertyName(const AstName& value)
    {
        std::string_view text = value.value ? value.value : "";
        if (isPlainName(text))
            write(text);
        else
        {
            write("[");
            quoted(text);
            write("]");
        }
    }

    void access(AstTableAccess value)
    {
        if (value == AstTableAccess::Read)
            write("read ");
        else if (value == AstTableAccess::Write)
            write("write ");
    }

    void local(const AstLocal& value)
    {
        name(value.name);
        if (value.annotation)
        {
            write(": ");
            type(*value.annotation);
        }
    }

    void attributes(const AstArray<AstAttr*>& list)
    {
        for (AstAttr* attr : list)
        {
            attribute(*attr);
            write(" ");
        }
    }

    void attribute(AstAttr& attr)
    {
        if (attr.args.size == 0)
        {
            write("@");
            name(attr.name);
            return;
        }

        write("@[");
        name(attr.name);
        write("(");
        expressions(attr.args);
        write(")]");
    }

    void expressions(const AstArray<AstExpr*>& list)
    {
        for (size_t i = 0; i < list.size; i++)
        {
            if (i > 0)
                write(", ");
            expr(*list.data[i]);
        }
    }

    void typeOrPacks(const AstArray<AstTypeOrPack>& list)
    {
        for (size_t i = 0; i < list.size; i++)
        {
            if (i > 0)
                write(", ");
            if (list.data[i].type)
                type(*list.data[i].type);
            else
                typePack(*list.data[i].typePack, false);
        }
    }

    void genericNames(const AstArray<AstGenericType*>& generics, const AstArray<AstGenericTypePack*>& genericPacks, bool withDefaults)
    {
        if (generics.size == 0 && genericPacks.size == 0)
            return;

        write("<");
        bool first = true;

        for (AstGenericType* generic : generics)
        {
            if (!first)
                write(", ");
            first = false;

            name(generic->name);
            if (withDefaults && generic->defaultValue)
            {
                write(" = ");
                type(*generic->defaultValue);
            }
        }

        for (AstGenericTypePack* genericPack : genericPacks)
        {
            if (!first)
                write(", ");
            first = false;

            name(genericPack->name);
            write("...");
            if (withDefaults && genericPack->defaultValue)
            {
                write(" = ");
                typePack(*genericPack->defaultValue, false);
            }
        }

        write(">");
    }

    void typePack(AstTypePack& pack, bool forVarArg, bool asReturn = false)
    {
        if (auto variadic = pack.as<AstTypePackVariadic>())
        {
            if (!forVarArg)
                write("...");
            type(*variadic->variadicType);
        }
        else if (auto generic = pack.as<AstTypePackGeneric>())
        {
            name(generic->genericName);
            write("...");
        }
        else if (auto explicitPack = pack.as<AstTypePackExplicit>())
        {
            const AstTypeList& list = explicitPack->typeList;
            if (asReturn && list.types.size == 1 && !list.tailType)
                type(*list.types.data[0]);
            else
                typeList(list, nullptr);
        }
        else
        {
            unsupported(&pack);
        }
    }

    void typeList(const AstTypeList& list, const AstArray<std::optional<AstArgumentName>>* argNames)
    {
        write("(");

        for (size_t i = 0; i < list.types.size; i++)
        {
            if (i > 0)
                write(", ");

            const AstArgumentName* argName = argNames && i < argNames->size ? argumentName(argNames->data[i]) : nullptr;
            if (argName)
            {
                name(argName->first);
                write(": ");
            }

            type(*list.types.data[i]);
        }

        if (list.tailType)
        {
            if (list.types.size > 0)
                write(", ");
            typePack(*list.tailType, false);
        }

        write(")");
    }

    void typeInUnion(AstType& value, bool inIntersection)
    {
        bool wrap = value.is<AstTypeFunction>() || (inIntersection ? value.is<AstTypeUnion>() : value.is<AstTypeIntersection>());

        if (wrap)
            write("(");
        type(value);
        if (wrap)
            write(")");
    }

    void type(AstType& value)
    {
        if (auto reference = value.as<AstTypeReference>())
        {
            if (reference->prefix)
            {
                name(*reference->prefix);
                write(".");
            }

            name(reference->name);
            if (reference->parameters.size > 0 || reference->hasParameterList)
            {
                write("<");
                typeOrPacks(reference->parameters);
                write(">");
            }
        }
        else if (auto function = value.as<AstTypeFunction>())
        {
            genericNames(function->generics, function->genericPacks, false);
            typeList(function->argTypes, &function->argNames);
            write(" -> ");
            typePack(*function->returnTypes, false, true);
        }
        else if (auto table = value.as<AstTypeTable>())
        {
            typeTable(*table);
        }
        else if (auto typeOf = value.as<AstTypeTypeof>())
        {
            write("typeof(");
            expr(*typeOf->expr);
            write(")");
        }
        else if (auto unionType = value.as<AstTypeUnion>())
        {
            bool first = true;
            for (AstType* member : unionType->types)
            {
                if (member->is<AstTypeOptional>())
                {
                    write("?");
                    continue;
                }

                if (!first)
                    write(" | ");
                first = false;

                typeInUnion(*member, false);
            }
        }
        else if (auto intersection = value.as<AstTypeIntersection>())
        {
            for (size_t i = 0; i < intersection->types.size; i++)
            {
                if (i > 0)
                    write(" & ");
                typeInUnion(*intersection->types.data[i], true);
            }
        }
        else if (auto group = value.as<AstTypeGroup>())
        {
            write("(");
            type(*group->type);
            write(")");
        }
        else if (auto singletonBool = value.as<AstTypeSingletonBool>())
        {
            write(singletonBool->value ? "true" : "false");
        }
        else if (auto singletonString = value.as<AstTypeSingletonString>())
        {
            quoted(std::string_view(singletonString->value.data, singletonString->value.size));
        }
        else if (value.is<AstTypeError>())
        {
            write("%error-type%");
        }
        else if (value.is<AstTypeOptional>())
        {
            write("?");
        }
        else
        {
            unsupported(&value);
        }
    }

    void typeTable(AstTypeTable& table)
    {
        AstTableIndexer* indexer = table.indexer;

        if (table.props.size == 0 && !indexer)
        {
            write("{}");
            return;
        }

        if (table.props.size == 0 && indexer->access == AstTableAccess::ReadWrite)
        {
            AstTypeReference* indexType = indexer->indexType->as<AstTypeReference>();
            if (indexType && !indexType->prefix && indexType->parameters.size == 0 && indexType->name == "number")
            {
                write("{ ");
                type(*indexer->resultType);
                write(" }");
                return;
            }
        }

        size_t count = table.props.size + (indexer ? 1 : 0);
        bool multiline = count > kMaxInlineItems;

        write(multiline ? "{" : "{ ");
        if (multiline)
        {
            newline();
            indent++;
        }

        size_t written = 0;
        auto separate = [&]()
        {
            if (multiline)
            {
                write(",");
                newline();
            }
            else if (++written < count)
                write(", ");
        };

        for (const AstTableProp& prop : table.props)
        {
            access(prop.access);
            propertyName(prop.name);
            if (prop.type)
            {
                write(": ");
                type(*prop.type);
            }
            separate();
        }

        if (indexer)
        {
            access(indexer->access);
            write("[");
            type(*indexer->indexType);
            write("]: ");
            type(*indexer->resultType);
            separate();
        }

        if (multiline)
            indent--;
        write(multiline ? "}" : " }");
    }

    void prefix(AstExpr& value)
    {
        bool wrap = prefixNeedsParens(value);

        if (wrap)
            write("(");
        expr(value);
        if (wrap)
            write(")");
    }

    void wrapped(AstExpr& value, bool wrap)
    {
        if (wrap)
            write("(");
        expr(value);
        if (wrap)
            write(")");
    }

    void number(double value)
    {
        if (isinf(value))
            write(value > 0 ? "1e500" : "-1e500");
        else if (isnan(value))
            write("0/0");
        else if (value <= std::numeric_limits<int>::max() && value >= std::numeric_limits<int>::min() && double(int(value)) == value &&
                 !(value == 0.0 && signbit(value)))
            write(std::to_string(int(value)));
        else
        {
            char buffer[100];
            size_t len = snprintf(buffer, sizeof(buffer), "%.17g", value);
            write(std::string_view{buffer, len});
        }
    }

    void tableItem(const AstExprTable::Item& item)
    {
        switch (item.kind)
        {
        case AstExprTable::Item::Kind::List:
            break;

        case AstExprTable::Item::Kind::Record:
        {
            const AstArray<char>& key = item.key->as<AstExprConstantString>()->value;
            std::string_view text(key.data, key.size);
            if (isPlainName(text))
                write(text);
            else
            {
                write("[");
                quoted(text);
                write("]");
            }
            write(" = ");
        }
        break;

        case AstExprTable::Item::Kind::General:
            write("[");
            expr(*item.key);
            write("] = ");
            break;
        }

        expr(*item.value);
    }

    void table(AstExprTable& value)
    {
        if (value.items.size == 0)
        {
            write("{}");
            return;
        }

        bool multiline = value.items.size > kMaxInlineItems;
        for (const AstExprTable::Item& item : value.items)
        {
            if (item.value->is<AstExprFunction>())
                multiline = true;
            else if (auto nested = item.value->as<AstExprTable>())
                multiline = multiline || nested->items.size > 0;
        }

        if (!multiline)
        {
            write("{ ");
            for (size_t i = 0; i < value.items.size; i++)
            {
                if (i > 0)
                    write(", ");
                tableItem(value.items.data[i]);
            }
            write(" }");
            return;
        }

        write("{");
        newline();
        indent++;
        for (const AstExprTable::Item& item : value.items)
        {
            tableItem(item);
            write(",");
            newline();
        }
        indent--;
        write("}");
    }

    void ifElse(AstExprIfElse& value)
    {
        expr(*value.condition);
        write(" then ");
        expr(*value.trueExpr);

        if (auto chained = value.falseExpr->as<AstExprIfElse>())
        {
            write(" elseif ");
            ifElse(*chained);
        }
        else
        {
            write(" else ");
            expr(*value.falseExpr);
        }
    }

    void expr(AstExpr& value)
    {
        if (auto group = value.as<AstExprGroup>())
        {
            write("(");
            expr(*group->expr);
            write(")");
        }
        else if (value.is<AstExprConstantNil>())
        {
            write("nil");
        }
        else if (auto constantBool = value.as<AstExprConstantBool>())
        {
            write(constantBool->value ? "true" : "false");
        }
        else if (auto constantNumber = value.as<AstExprConstantNumber>())
        {
            number(constantNumber->value);
        }
        else if (auto constantInteger = value.as<AstExprConstantInteger>())
        {
            char buffer[100];
            size_t len = 0;
            if (constantInteger->value >= 0)
                len = snprintf(buffer, sizeof(buffer), "%lldi", (long long)constantInteger->value);
            else
                len = snprintf(buffer, sizeof(buffer), "0x%llxi", (unsigned long long)constantInteger->value);
            write(std::string_view{buffer, len});
        }
        else if (auto constantString = value.as<AstExprConstantString>())
        {
            quoted(std::string_view(constantString->value.data, constantString->value.size));
        }
        else if (auto exprLocal = value.as<AstExprLocal>())
        {
            name(exprLocal->local->name);
        }
        else if (auto global = value.as<AstExprGlobal>())
        {
            name(global->name);
        }
        else if (value.is<AstExprVarargs>())
        {
            write("...");
        }
        else if (auto call = value.as<AstExprCall>())
        {
            prefix(*call->func);

            if (call->typeArguments.size > 0)
            {
                write("<<");
                typeOrPacks(call->typeArguments);
                write(">>");
            }

            write("(");
            expressions(call->args);
            write(")");
        }
        else if (auto indexName = value.as<AstExprIndexName>())
        {
            prefix(*indexName->expr);
            write(std::string(1, indexName->op));
            name(indexName->index);
        }
        else if (auto indexExpr = value.as<AstExprIndexExpr>())
        {
            prefix(*indexExpr->expr);
            write("[");
            expr(*indexExpr->index);
            write("]");
        }
        else if (auto function = value.as<AstExprFunction>())
        {
            attributes(function->attributes);
            write("function");
            functionBody(*function);
        }
        else if (auto exprTable = value.as<AstExprTable>())
        {
            table(*exprTable);
        }
        else if (auto unary = value.as<AstExprUnary>())
        {
            switch (unary->op)
            {
            case AstExprUnary::Op::Not:
                write("not ");
                break;
            case AstExprUnary::Op::Minus:
                write(startsWithMinus(*unary->expr) ? "- " : "-");
                break;
            case AstExprUnary::Op::Len:
                write("#");
                break;
            }
            wrapped(*unary->expr, unaryOperandNeedsParens(*unary->expr));
        }
        else if (auto binary = value.as<AstExprBinary>())
        {
            wrapped(*binary->left, operandNeedsParens(*binary->left, binary->op, false));
            write(" ");
            write(toString(binary->op));
            write(" ");
            wrapped(*binary->right, operandNeedsParens(*binary->right, binary->op, true));
        }
        else if (auto assertion = value.as<AstExprTypeAssertion>())
        {
            wrapped(*assertion->expr, assertionOperandNeedsParens(*assertion->expr));
            write(" :: ");
            type(*assertion->annotation);
        }
        else if (auto exprIfElse = value.as<AstExprIfElse>())
        {
            write("if ");
            ifElse(*exprIfElse);
        }
        else if (auto interp = value.as<AstExprInterpString>())
        {
            write("`");

            for (size_t i = 0; i < interp->strings.size; i++)
            {
                const AstArray<char>& piece = interp->strings.data[i];
                write(escape(std::string_view(piece.data, piece.size), true));

                if (i < interp->expressions.size)
                {
                    bool pad = startsWithTable(interp->expressions.data[i]);
                    write(pad ? "{ " : "{");
                    expr(*interp->expressions.data[i]);
                    write(pad ? " }" : "}");
                }
            }

            write("`");
        }
        else if (auto exprError = value.as<AstExprError>())
        {
            write("(error-expr");
            for (size_t i = 0; i < exprError->expressions.size; i++)
            {
                write(i == 0 ? ": " : ", ");
                expr(*exprError->expressions.data[i]);
            }
            write(")");
        }
        else if (auto instantiate = value.as<AstExprInstantiate>())
        {
            prefix(*instantiate->expr);
            write("<<");
            typeOrPacks(instantiate->typeArguments);
            write(">>");
        }
        else
        {
            unsupported(&value);
        }
    }

    void functionBody(AstExprFunction& function)
    {
        genericNames(function.generics, function.genericPacks, false);

        write("(");
        for (size_t i = 0; i < function.args.size; i++)
        {
            if (i > 0)
                write(", ");

            local(*function.args.data[i]);

            if (FFlag::LuwuDefaultArguments && i < function.argsDefaults.size && function.argsDefaults.data[i])
            {
                write(" = ");
                expr(*function.argsDefaults.data[i]);
            }
        }

        if (function.vararg)
        {
            if (function.args.size > 0)
                write(", ");
            write("...");

            if (function.varargAnnotation)
            {
                write(": ");
                typePack(*function.varargAnnotation, true);
            }
        }
        write(")");

        if (function.returnAnnotation)
        {
            write(": ");
            typePack(*function.returnAnnotation, false, true);
        }

        newline();
        body(*function.body);
        write("end");
    }

    template<typename NameAt>
    void declaredSignature(const AstTypeList& params, bool hasSelf, AstTypePack& retTypes, NameAt nameAt)
    {
        write("(");
        bool first = true;

        if (hasSelf)
        {
            write("self");
            first = false;
        }

        for (size_t i = 0; i < params.types.size; i++)
        {
            if (!first)
                write(", ");
            first = false;

            if (const AstArgumentName* argName = nameAt(i))
            {
                name(argName->first);
                write(": ");
            }

            type(*params.types.data[i]);
        }

        if (params.tailType)
        {
            if (!first)
                write(", ");
            write("...: ");
            typePack(*params.tailType, true);
        }

        write(")");

        const AstTypePackExplicit* explicitReturn = retTypes.as<AstTypePackExplicit>();
        if (explicitReturn && explicitReturn->typeList.types.size == 0 && !explicitReturn->typeList.tailType)
            return;

        write(": ");
        typePack(retTypes, false, true);
    }

    void ifStatement(AstStatIf& value)
    {
        expr(*value.condition);
        write(" then");
        newline();
        body(*value.thenbody);

        if (!value.elsebody)
        {
            write("end");
        }
        else if (auto chained = value.elsebody->as<AstStatIf>())
        {
            const ClassifiedNodeComments* attached = commentsFor(chained);

            if (attached)
            {
                indent++;
                ownLineComments(attached->leading);
                indent--;
            }

            write("elseif ");
            ifStatement(*chained);

            if (attached)
                sameLineComments(attached->trailing);
        }
        else if (auto elseBlock = value.elsebody->as<AstStatBlock>())
        {
            write("else");
            newline();
            body(*elseBlock);
            write("end");
        }
        else
        {
            unsupported(value.elsebody);
        }
    }

    void localKeyword(bool exported, bool isConst)
    {
        if (FFlag::LuauExportValueSyntax && exported)
            write(isConst ? "export const" : "export local");
        else
            write(isConst ? "const" : "local");
    }

    void classQualifier(bool qualified, AstClassMemberVisibility visibility)
    {
        if (visibility == AstClassMemberVisibility::Private)
            write("private ");
        else if (qualified)
            write("public ");
    }

    bool classIsQualified(AstStatClass& value)
    {
        if (hasExplicitVisibility(tree, &value))
            return true;

        if (const AstClassPrimaryConstructor* constructor = value.primaryConstructor)
        {
            for (const AstClassPrimaryConstructorParamQualifiers& qualifiers : constructor->argsQualifiers)
            {
                if (qualifiers.visibility == AstClassMemberVisibility::Private)
                    return true;
            }
        }

        for (const AstClassMember& member : value.members)
        {
            if (const AstClassProperty* property = get_if<AstClassProperty>(&member))
            {
                if (property->visibility == AstClassMemberVisibility::Private)
                    return true;
            }
            else if (const AstClassMethod* method = get_if<AstClassMethod>(&member))
            {
                if (method->visibility == AstClassMemberVisibility::Private)
                    return true;
            }
        }

        return false;
    }

    const AstClassProperty* classRestatedField(AstStatClass& value, const AstName& fieldName)
    {
        for (const AstClassMember& member : value.members)
        {
            const AstClassProperty* property = get_if<AstClassProperty>(&member);
            if (property && property->name == fieldName)
                return property;
        }

        return nullptr;
    }

    void classParameterQualifier(AstStatClass& value, const AstName& fieldName, const AstClassPrimaryConstructorParamQualifiers& qualifiers, bool qualified)
    {
        const AstClassProperty* restated = classRestatedField(value, fieldName);

        if (restated && restated->visibility != qualifiers.visibility)
            return;

        if (qualifiers.visibility == AstClassMemberVisibility::Private)
            write("private ");
        else if (qualified && (!restated || hasExplicitVisibility(tree, &qualifiers)))
            write("public ");
    }

    void classStatement(AstStatClass& value)
    {
        if (value.exported)
            write("export ");

        write("class ");
        name(value.name->name);
        genericNames(value.generics, value.genericPacks, true);

        bool qualified = classIsQualified(value);

        if (const AstClassPrimaryConstructor* constructor = value.primaryConstructor)
        {
            if (constructor->visibility == AstClassMemberVisibility::Private)
                write(" private ");
            else if (hasExplicitVisibility(tree, constructor))
                write(" public ");

            write("(");
            for (size_t i = 0; i < constructor->args.size; i++)
            {
                if (i > 0)
                    write(", ");

                if (i < constructor->argsQualifiers.size)
                {
                    const AstClassPrimaryConstructorParamQualifiers& qualifiers = constructor->argsQualifiers.data[i];
                    classParameterQualifier(value, constructor->args.data[i]->name, qualifiers, qualified);
                    if (qualifiers.isConst)
                        write("const ");
                }

                local(*constructor->args.data[i]);

                if (i < constructor->argsDefaults.size && constructor->argsDefaults.data[i])
                {
                    write(" = ");
                    expr(*constructor->argsDefaults.data[i]);
                }
            }
            write(")");
        }

        newline();
        indent++;

        for (const AstClassMember& member : value.members)
        {
            if (const AstClassProperty* property = get_if<AstClassProperty>(&member))
            {
                classQualifier(qualified, property->visibility);
                if (property->isConst)
                    write("const ");
                name(property->name);
                if (property->ty)
                {
                    write(": ");
                    type(*property->ty);
                }
                if (property->defaultValue)
                {
                    write(" = ");
                    expr(*property->defaultValue);
                }
                if (property->hasSemicolon)
                    write(";");
            }
            else if (const AstClassMethod* method = get_if<AstClassMethod>(&member))
            {
                classQualifier(qualified, method->visibility);
                attributes(method->function->attributes);
                write("function ");
                name(method->functionName);
                functionBody(*method->function);
                if (method->hasSemicolon)
                    write(";");
            }
            newline();
        }

        indent--;
        write("end");
    }

    void externTypeStatement(AstStatDeclareExternType& value)
    {
        write("declare extern type ");
        name(value.name);
        genericNames(value.generics, value.genericPacks, true);

        if (value.superName)
        {
            write(" extends ");
            name(*value.superName);
        }

        write(" with");
        newline();
        indent++;

        for (const AstDeclaredExternTypeProperty& prop : value.props)
        {
            const AstTypeFunction* method = prop.isMethod ? prop.ty->as<AstTypeFunction>() : nullptr;

            if (method)
            {
                attributes(method->attributes);
                write("function ");
                name(prop.name);
                genericNames(method->generics, method->genericPacks, false);
                declaredSignature(
                    method->argTypes,
                    true,
                    *method->returnTypes,
                    [&](size_t i) -> const AstArgumentName*
                    {
                        return i < method->argNames.size ? argumentName(method->argNames.data[i]) : nullptr;
                    }
                );
            }
            else
            {
                access(prop.access);
                propertyName(prop.name);
                write(": ");
                type(*prop.ty);
            }
            newline();
        }

        if (value.indexer)
        {
            write("[");
            type(*value.indexer->indexType);
            write("]: ");
            type(*value.indexer->resultType);
            newline();
        }

        indent--;
        write("end");
    }

    void statement(AstStat& value)
    {
        if (auto block = value.as<AstStatBlock>())
        {
            write("do");
            newline();
            body(*block, false);
            write("end");
        }
        else if (auto statIf = value.as<AstStatIf>())
        {
            write("if ");
            ifStatement(*statIf);
        }
        else if (auto statWhile = value.as<AstStatWhile>())
        {
            write("while ");
            expr(*statWhile->condition);
            write(" do");
            newline();
            body(*statWhile->body);
            write("end");
        }
        else if (auto repeat = value.as<AstStatRepeat>())
        {
            write("repeat");
            newline();
            body(*repeat->body);
            write("until ");
            expr(*repeat->condition);
        }
        else if (value.is<AstStatBreak>())
        {
            write("break");
        }
        else if (value.is<AstStatContinue>())
        {
            write("continue");
        }
        else if (auto statReturn = value.as<AstStatReturn>())
        {
            write("return");
            if (statReturn->list.size > 0)
            {
                write(" ");
                expressions(statReturn->list);
            }
        }
        else if (auto statExpr = value.as<AstStatExpr>())
        {
            expr(*statExpr->expr);
        }
        else if (auto statLocal = value.as<AstStatLocal>())
        {
            localKeyword(statLocal->isExported, statLocal->isConst);
            write(" ");

            for (size_t i = 0; i < statLocal->vars.size; i++)
            {
                if (i > 0)
                    write(", ");
                local(*statLocal->vars.data[i]);
            }

            if (statLocal->values.size > 0)
            {
                write(" = ");
                expressions(statLocal->values);
            }
        }
        else if (auto statFor = value.as<AstStatFor>())
        {
            write("for ");
            local(*statFor->var);
            write(" = ");
            expr(*statFor->from);
            write(", ");
            expr(*statFor->to);
            if (statFor->step)
            {
                write(", ");
                expr(*statFor->step);
            }
            write(" do");
            newline();
            body(*statFor->body);
            write("end");
        }
        else if (auto forIn = value.as<AstStatForIn>())
        {
            write("for ");
            for (size_t i = 0; i < forIn->vars.size; i++)
            {
                if (i > 0)
                    write(", ");
                local(*forIn->vars.data[i]);
            }
            write(" in ");
            expressions(forIn->values);
            write(" do");
            newline();
            body(*forIn->body);
            write("end");
        }
        else if (auto assign = value.as<AstStatAssign>())
        {
            expressions(assign->vars);
            write(" = ");
            expressions(assign->values);
        }
        else if (auto compound = value.as<AstStatCompoundAssign>())
        {
            expr(*compound->var);

            switch (compound->op)
            {
            case AstExprBinary::Add:
                write(" += ");
                break;
            case AstExprBinary::Sub:
                write(" -= ");
                break;
            case AstExprBinary::Mul:
                write(" *= ");
                break;
            case AstExprBinary::Div:
                write(" /= ");
                break;
            case AstExprBinary::FloorDiv:
                write(" //= ");
                break;
            case AstExprBinary::Mod:
                write(" %= ");
                break;
            case AstExprBinary::Pow:
                write(" ^= ");
                break;
            case AstExprBinary::Concat:
                write(" ..= ");
                break;
            default:
                unsupported(&value);
                break;
            }

            expr(*compound->value);
        }
        else if (auto function = value.as<AstStatFunction>())
        {
            attributes(function->func->attributes);
            write("function ");
            expr(*function->name);
            functionBody(*function->func);
        }
        else if (auto localFunction = value.as<AstStatLocalFunction>())
        {
            attributes(localFunction->func->attributes);

            if (FFlag::LuauExportValueSyntax && localFunction->name->isExported)
                write("export");
            else
                write(localFunction->name->isConst ? "const" : "local");

            write(" function ");
            name(localFunction->name->name);
            functionBody(*localFunction->func);
        }
        else if (auto alias = value.as<AstStatTypeAlias>())
        {
            if (alias->exported)
                write("export ");
            write("type ");
            name(alias->name);
            genericNames(alias->generics, alias->genericPacks, true);
            write(" = ");
            type(*alias->type);
        }
        else if (auto typeFunction = value.as<AstStatTypeFunction>())
        {
            if (typeFunction->exported)
                write("export ");
            write("type function ");
            name(typeFunction->name);
            functionBody(*typeFunction->body);
        }
        else if (auto statError = value.as<AstStatError>())
        {
            write("(error-stat");

            bool first = true;
            for (AstExpr* expression : statError->expressions)
            {
                write(first ? ": " : ", ");
                first = false;
                expr(*expression);
            }

            for (AstStat* inner : statError->statements)
            {
                write(first ? ": " : ", ");
                first = false;
                statement(*inner);
            }

            write(")");
        }
        else if (auto declareGlobal = value.as<AstStatDeclareGlobal>())
        {
            write("declare ");
            name(declareGlobal->name);
            write(": ");
            type(*declareGlobal->type);
        }
        else if (auto declareFunction = value.as<AstStatDeclareFunction>())
        {
            attributes(declareFunction->attributes);
            write("declare function ");
            name(declareFunction->name);
            genericNames(declareFunction->generics, declareFunction->genericPacks, false);
            declaredSignature(
                declareFunction->params,
                false,
                *declareFunction->retTypes,
                [&](size_t i) -> const AstArgumentName*
                {
                    return i < declareFunction->paramNames.size ? argumentName(declareFunction->paramNames.data[i]) : nullptr;
                }
            );
        }
        else if (auto externType = value.as<AstStatDeclareExternType>())
        {
            externTypeStatement(*externType);
        }
        else if (auto statClass = value.as<AstStatClass>(); statClass && FFlag::LuwuClasses)
        {
            classStatement(*statClass);
        }
        else
        {
            unsupported(&value);
        }
    }

    void statementWithComments(AstStat& value, bool separate)
    {
        const ClassifiedNodeComments* attached = commentsFor(&value);

        if (attached)
            ownLineComments(attached->leading);

        statement(value);

        if (value.hasSemicolon || separate)
            write(";");

        if (attached)
            sameLineComments(attached->trailing);
    }

    void statements(AstStatBlock& block, bool withBlockComments)
    {
        const ClassifiedNodeComments* attached = withBlockComments ? commentsFor(&block) : nullptr;

        if (attached)
            ownLineComments(attached->leading);

        for (size_t i = 0; i < block.body.size; i++)
        {
            bool separate = i + 1 < block.body.size && endsWithPrefixExpr(*block.body.data[i]) && startsWithParen(*block.body.data[i + 1]);
            statementWithComments(*block.body.data[i], separate);
            newline();
        }

        if (attached)
            ownLineComments(attached->trailing);
    }

    void body(AstStatBlock& block, bool withBlockComments = true)
    {
        indent++;
        statements(block, withBlockComments);
        indent--;
    }
};

} // namespace

std::string reflectPrettyPrint(Luau::AstNode* node, bool asProgram, const AstTreeState& tree, std::string& error)
{
    ReflectFormatter formatter(tree);

    AstStatBlock* program = asProgram ? node->as<AstStatBlock>() : nullptr;
    if (program)
    {
        formatter.statements(*program, true);
    }
    else if (auto stat = node->asStat())
    {
        formatter.statement(*stat);
        if (stat->hasSemicolon)
            formatter.write(";");
    }
    else if (auto expr = node->asExpr())
        formatter.expr(*expr);
    else if (auto type = node->asType())
        formatter.type(*type);
    else if (auto attr = node->asAttr())
        formatter.attribute(*attr);
    else if (auto generic = node->as<AstGenericType>())
    {
        formatter.name(generic->name);
        if (generic->defaultValue)
        {
            formatter.write(" = ");
            formatter.type(*generic->defaultValue);
        }
    }
    else if (auto genericPack = node->as<AstGenericTypePack>())
    {
        formatter.name(genericPack->name);
        formatter.write("...");
        if (genericPack->defaultValue)
        {
            formatter.write(" = ");
            formatter.typePack(*genericPack->defaultValue, false);
        }
    }
    else if (node->is<AstTypePackExplicit>() || node->is<AstTypePackVariadic>() || node->is<AstTypePackGeneric>())
        formatter.typePack(*static_cast<AstTypePack*>(node), false);
    else
        formatter.unsupported(node);

    while (!formatter.out.empty() && formatter.out.back() == '\n')
        formatter.out.pop_back();

    error = formatter.error;
    return formatter.out;
}

} // namespace Luau

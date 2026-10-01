// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Common.h"
#include "Luau/Location.h"
#include "Luau/Variant.h"

#include <iterator>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <string.h>
#include <stdint.h>

LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuTraits)

namespace Luau
{

template<typename T>
std::optional<T> fromString(std::string_view s);

struct AstName
{
    const char* value;

    AstName()
        : value(nullptr)
    {
    }

    explicit AstName(const char* value)
        : value(value)
    {
    }

    bool operator==(const AstName& rhs) const
    {
        return value == rhs.value;
    }

    bool operator!=(const AstName& rhs) const
    {
        return value != rhs.value;
    }

    bool operator==(const char* rhs) const
    {
        return value && strcmp(value, rhs) == 0;
    }

    bool operator!=(const char* rhs) const
    {
        return !value || strcmp(value, rhs) != 0;
    }

    bool operator<(const AstName& rhs) const
    {
        return (value && rhs.value) ? strcmp(value, rhs.value) < 0 : value < rhs.value;
    }
};

class AstType;
class AstVisitor;
class AstStat;
class AstStatBlock;
class AstExpr;
class AstTypePack;
class AstAttr;
class AstExprTable;

template<typename T>
struct AstArray
{
    T* data = nullptr;
    size_t size = 0;

    const T* begin() const
    {
        return data;
    }

    const T* end() const
    {
        return data + size;
    }

    std::reverse_iterator<const T*> rbegin() const
    {
        return std::make_reverse_iterator(end());
    }

    std::reverse_iterator<const T*> rend() const
    {
        return std::make_reverse_iterator(begin());
    }
};

// Luwu Destructuring (rfcs/destructuring.md): the name of the hidden local an unnamed pattern (`local .{x} = v`)
// binds its value to. Code can't spell it, so nothing can refer to it.
constexpr const char* kDestructuredLocalName = "(destructured)";

struct AstLocal
{
    AstName name;
    Location location;
    AstLocal* shadow;
    size_t functionDepth;
    size_t loopDepth;
    bool isConst;
    // exported is only a property set after construction
    bool isExported = false;

    AstType* annotation;

    // Attributes written above this binding, e.g. `function f(@deprecated a)`. Only a function
    // parameter can carry them today; every other binding leaves this empty.
    AstArray<AstAttr*> attributes{nullptr, 0};

    AstLocal(
        const AstName& name,
        const Location& location,
        AstLocal* shadow,
        size_t functionDepth,
        size_t loopDepth,
        AstType* annotation,
        bool isConst = false
    )
        : name(name)
        , location(location)
        , shadow(shadow)
        , functionDepth(functionDepth)
        , loopDepth(loopDepth)
        , isConst(isConst)
        , annotation(annotation)
    {
    }
};

struct AstTypeList
{
    AstArray<AstType*> types;
    // Null indicates no tail, not an untyped tail.
    AstTypePack* tailType = nullptr;
};

// Don't have Luau::Variant available, it's a bit of an overhead, but a plain struct is nice to use
struct AstTypeOrPack
{
    AstType* type = nullptr;
    AstTypePack* typePack = nullptr;
};

using AstArgumentName = std::pair<AstName, Location>; // TODO: remove and replace when we get a common struct for this pair instead of AstName

extern int gAstRttiIndex;

template<typename T>
struct AstRtti
{
    static const int value;
};

template<typename T>
const int AstRtti<T>::value = ++gAstRttiIndex;

#define LUAU_RTTI(Class) \
    static int ClassIndex() \
    { \
        return AstRtti<Class>::value; \
    }

class AstNode
{
public:
    explicit AstNode(int classIndex, const Location& location)
        : classIndex(classIndex)
        , location(location)
    {
    }

    virtual void visit(AstVisitor* visitor) = 0;

    virtual AstExpr* asExpr()
    {
        return nullptr;
    }
    virtual AstStat* asStat()
    {
        return nullptr;
    }
    virtual AstType* asType()
    {
        return nullptr;
    }
    virtual AstAttr* asAttr()
    {
        return nullptr;
    }

    template<typename T>
    bool is() const
    {
        return classIndex == T::ClassIndex();
    }
    template<typename T>
    T* as()
    {
        return classIndex == T::ClassIndex() ? static_cast<T*>(this) : nullptr;
    }
    template<typename T>
    const T* as() const
    {
        return classIndex == T::ClassIndex() ? static_cast<const T*>(this) : nullptr;
    }

    const int classIndex;
    Location location;

    // Luwu: set on the smallest node whose own syntax only Luwu accepts, so tools and the parser can tell
    // Luwu-only syntax apart from Luau syntax. Upstream Luau has no such field.
    // Children of a Luwu-only node aren't marked unless their own syntax is Luwu-only.
    bool luwuOnly = false;
};

class AstAttr : public AstNode
{
public:
    LUAU_RTTI(AstAttr)

    enum class Type
    {
        Checked,
        Native,
        Deprecated,
        // Luwu @noinline (rfcs/noinline-attribute.md): upstream's `DebugNoinline` (`@debugnoinline`), shipped as
        // `@noinline`.
        Noinline,
        // Luwu user-defined refinements: `@[truthy(param, Type)]`
        Truthy,
        Unknown
    };

    // The syntactic positions an attribute may be written on. These are bit flags so that an
    // attribute's registration can name the whole set it allows as one value, while parsing one
    // particular position passes a single flag. Adding an attribute is then a row in the parser's
    // registry table rather than a check written out at each position that has to be kept in sync.
    enum class Context : unsigned
    {
        None = 0,
        Function = 1 << 0,
        Local = 1 << 1,
        TypeAlias = 1 << 2,
        TableField = 1 << 3,
        TableTypeField = 1 << 4,
        TableIndexer = 1 << 5,
        Class = 1 << 6,
        ClassField = 1 << 7,
        Parameter = 1 << 8,
        Assignment = 1 << 9,
        // A function the compiler can resolve at a call site and therefore inline: a local or const
        // function, a class method, or a function expression bound to one of those. A global
        // `function f()` is not one -- a global is never resolved statically -- and neither is a
        // declared function or a function type, which have no body at all.
        InlinableFunction = 1 << 10,

        AnyFunction = Function | InlinableFunction,

        // A class member's attributes are parsed before its `function` keyword or field name, so
        // they are checked against both and pinned down once the member kind is known.
        ClassMember = InlinableFunction | ClassField,

        // Likewise for a table type's entries: `{ @deprecated x: T }` and `{ @deprecated [K]: V }`
        // are told apart only after the attributes have been consumed.
        TableTypeMember = TableTypeField | TableIndexer,

        // A table constructor's entry: `{ @deprecated x = 1 }` attributes the entry, but in a list
        // entry `{ @native function() end }` they belong to the function.
        TableEntry = TableField | InlinableFunction,

        // Every position a statement-level attribute could still turn out to be. Attributes are
        // parsed before the statement that follows them is known, so they are checked against this
        // set first and against the exact position once the statement has been identified.
        Statement = AnyFunction | Local | TypeAlias | Class | Assignment,

        Any = AnyFunction | Local | TypeAlias | TableField | TableTypeField | TableIndexer | Class | ClassField | Parameter | Assignment,
    };

    // Is `one` (a single position) a member of `set` (an attribute's allowed positions)?
    static constexpr bool contextAllows(Context set, Context one)
    {
        return (static_cast<unsigned>(set) & static_cast<unsigned>(one)) != 0;
    }

    // True for exactly one position, false for None and for a set like Statement. A set means the
    // position isn't settled yet, so it is checked where it gets pinned down rather than at parse
    // time -- otherwise the diagnostic has no single position to name, and would be reported twice.
    static constexpr bool isSingleContext(Context context)
    {
        unsigned bits = static_cast<unsigned>(context);
        return bits != 0 && (bits & (bits - 1)) == 0;
    }

    struct DeprecatedInfo
    {
        bool deprecated = false;
        std::optional<std::string> use;
        std::optional<std::string> reason;
    };

    AstAttr(const Location& location, Type type, AstArray<AstExpr*> args);
    AstAttr(const Location& location, Type type, AstArray<AstExpr*> args, AstName name);

    AstAttr* asAttr() override
    {
        return this;
    }

    void visit(AstVisitor* visitor) override;

    DeprecatedInfo deprecatedInfo() const;

    Type type;
    AstArray<AstExpr*> args;
    AstName name;

    // Luwu user-defined refinements: `@[truthy(param, Type)]` (behind DebugLuwuUserDefinedRefinements) says that when
    // the function returns a truthy value, the argument for its parameter `param` is a `Type`. Only a `Truthy`
    // attribute has them; its `args` stay empty, since `Type` is a type, not an expression.
    AstName refinedParam;
    Location refinedParamLocation;
    AstType* refinedType = nullptr;
};

// The `@deprecated` attribute's payload, or nullopt when the array has no `@deprecated`.
std::optional<AstAttr::DeprecatedInfo> findDeprecatedInfo(const AstArray<AstAttr*>& attributes);

// One attribute as the parser knows it. Defined next to the parser's registry, which is the single
// place an attribute is declared, so anything that needs to enumerate attributes -- autocomplete
// above all -- stays correct when one is added.
struct AttributeInfo
{
    const char* name;
    AstAttr::Type type;
    AstAttr::Context allowedContexts;
    // The record fields the attribute takes as `@[name { field = ... }]`, ending in nullptr; nullptr for an
    // attribute that takes no arguments.
    const char* const* argumentFields;
};

// Every attribute this build accepts, including flag-gated ones whose flag is currently on.
std::vector<AttributeInfo> getKnownAttributes();

class AstExpr : public AstNode
{
public:
    explicit AstExpr(int classIndex, const Location& location)
        : AstNode(classIndex, location)
    {
    }

    AstExpr* asExpr() override
    {
        return this;
    }
};

class AstStat : public AstNode
{
public:
    explicit AstStat(int classIndex, const Location& location)
        : AstNode(classIndex, location)
        , hasSemicolon(false)
    {
    }

    AstStat* asStat() override
    {
        return this;
    }

    bool hasSemicolon;
};

class AstGenericType : public AstNode
{
public:
    LUAU_RTTI(AstGenericType)

    explicit AstGenericType(const Location& location, AstName name, AstType* defaultValue = nullptr);

    void visit(AstVisitor* visitor) override;

    AstName name;
    AstType* defaultValue = nullptr;
};

class AstGenericTypePack : public AstNode
{
public:
    LUAU_RTTI(AstGenericTypePack)

    explicit AstGenericTypePack(const Location& location, AstName name, AstTypePack* defaultValue = nullptr);

    void visit(AstVisitor* visitor) override;

    AstName name;
    AstTypePack* defaultValue = nullptr;
};

class AstExprGroup : public AstExpr
{
public:
    LUAU_RTTI(AstExprGroup)

    explicit AstExprGroup(const Location& location, AstExpr* expr);

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
};

class AstExprConstantNil : public AstExpr
{
public:
    LUAU_RTTI(AstExprConstantNil)

    explicit AstExprConstantNil(const Location& location);

    void visit(AstVisitor* visitor) override;
};

class AstExprConstantBool : public AstExpr
{
public:
    LUAU_RTTI(AstExprConstantBool)

    AstExprConstantBool(const Location& location, bool value);

    void visit(AstVisitor* visitor) override;

    bool value;
};

enum class ConstantNumberParseResult
{
    Ok,
    Imprecise,
    Malformed,
    BinOverflow,
    HexOverflow,
    IntOverflow,
};

class AstExprConstantNumber : public AstExpr
{
public:
    LUAU_RTTI(AstExprConstantNumber)

    AstExprConstantNumber(const Location& location, double value, ConstantNumberParseResult parseResult = ConstantNumberParseResult::Ok);

    void visit(AstVisitor* visitor) override;

    double value;
    ConstantNumberParseResult parseResult;
};

class AstExprConstantInteger : public AstExpr
{
public:
    LUAU_RTTI(AstExprConstantInteger)

    AstExprConstantInteger(const Location& location, int64_t value, ConstantNumberParseResult parseResult = ConstantNumberParseResult::Ok);

    void visit(AstVisitor* visitor) override;

    int64_t value;
    ConstantNumberParseResult parseResult;
};
class AstExprConstantString : public AstExpr
{
public:
    LUAU_RTTI(AstExprConstantString)

    enum class QuoteStyle
    {
        // A string created using double quotes or an interpolated string,
        // as in:
        //
        //  "foo", `My name is {protagonist}! / And I'm {antagonist}!`
        //
        QuotedSimple,
        // A string created using single quotes, as in:
        //
        //  'bar'
        //
        QuotedSingle,
        // A string created using `[[ ... ]]` as in:
        //
        //   [[ Gee, this sure is a long string.
        //   it even has a new line in it! ]]
        //
        QuotedRaw,
        // A "string" in the context of a table literal, as in:
        //
        //  { foo = 42 } -- `foo` here is a "constant string"
        //
        Unquoted,
    };

    AstExprConstantString(const Location& location, const AstArray<char>& value, QuoteStyle quoteStyle);

    void visit(AstVisitor* visitor) override;
    bool isQuoted() const;

    AstArray<char> value;
    QuoteStyle quoteStyle;
};

class AstExprLocal : public AstExpr
{
public:
    LUAU_RTTI(AstExprLocal)

    AstExprLocal(const Location& location, AstLocal* local, bool upvalue);

    void visit(AstVisitor* visitor) override;

    AstLocal* local;
    bool upvalue;
};

class AstExprGlobal : public AstExpr
{
public:
    LUAU_RTTI(AstExprGlobal)

    AstExprGlobal(const Location& location, const AstName& name);

    void visit(AstVisitor* visitor) override;

    AstName name;
};

class AstExprVarargs : public AstExpr
{
public:
    LUAU_RTTI(AstExprVarargs)

    AstExprVarargs(const Location& location);

    void visit(AstVisitor* visitor) override;
};

class AstExprCall : public AstExpr
{
public:
    LUAU_RTTI(AstExprCall)

    AstExprCall(
        const Location& location,
        AstExpr* func,
        const AstArray<AstExpr*>& args,
        bool self,
        const AstArray<AstTypeOrPack>& explicitTypes,
        const Location& argLocation
    );

    void visit(AstVisitor* visitor) override;

    AstExpr* func;
    // These will only be filled in specifically `t:f<<A, B>>()`.
    // In `f<<A, B>>()`, this is parsed as `f<<A, B>>` as an expression,
    // which is then called.
    AstArray<AstTypeOrPack> typeArguments;
    AstArray<AstExpr*> args;
    bool self;
    Location argLocation;
};

class AstExprIndexName : public AstExpr
{
public:
    LUAU_RTTI(AstExprIndexName)

    AstExprIndexName(
        const Location& location,
        AstExpr* expr,
        const AstName& index,
        const Location& indexLocation,
        const Position& opPosition,
        char op
    );

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
    AstName index;
    Location indexLocation;
    Position opPosition;
    char op = '.';
};

class AstExprIndexExpr : public AstExpr
{
public:
    LUAU_RTTI(AstExprIndexExpr)

    AstExprIndexExpr(const Location& location, AstExpr* expr, AstExpr* index);

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
    AstExpr* index;
};

class AstExprFunction : public AstExpr
{
public:
    LUAU_RTTI(AstExprFunction)

    AstExprFunction(
        const Location& location,
        const AstArray<AstAttr*>& attributes,
        const AstArray<AstGenericType*>& generics,
        const AstArray<AstGenericTypePack*>& genericPacks,
        AstLocal* self,
        const AstArray<AstLocal*>& args,
        const AstArray<AstExpr*>& argsDefaults,
        bool vararg,
        const Location& varargLocation,
        AstStatBlock* body,
        size_t functionDepth,
        const AstName& debugname,
        AstTypePack* returnAnnotation,
        AstTypePack* varargAnnotation = nullptr,
        const std::optional<Location>& argLocation = std::nullopt
    );

    void visit(AstVisitor* visitor) override;

    bool hasNativeAttribute() const;
    bool hasAttribute(AstAttr::Type attributeType) const;
    AstAttr* getAttribute(AstAttr::Type attributeType) const;

    AstArray<AstAttr*> attributes;
    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;
    AstLocal* self;
    AstArray<AstLocal*> args;
    AstArray<AstExpr*> argsDefaults;
    AstTypePack* returnAnnotation = nullptr;
    bool vararg = false;
    Location varargLocation;
    AstTypePack* varargAnnotation;

    AstStatBlock* body;

    size_t functionDepth;

    AstName debugname;

    std::optional<Location> argLocation;
};

class AstExprTable : public AstExpr
{
public:
    LUAU_RTTI(AstExprTable)

    struct Item
    {
        enum class Kind
        {
            List,    // foo, in which case key is a nullptr
            Record,  // foo=bar, in which case key is a AstExprConstantString
            General, // [foo]=bar
        };

        Kind kind;

        AstExpr* key; // can be nullptr!
        AstExpr* value;

        // Attributes written above the entry, e.g. `{ @deprecated cat = "meow" }`.
        AstArray<AstAttr*> attributes{nullptr, 0};
    };

    AstExprTable(const Location& location, const AstArray<Item>& items);

    void visit(AstVisitor* visitor) override;

    std::optional<AstExpr*> getRecord(const char* key) const;

    AstArray<Item> items;
};

class AstExprUnary : public AstExpr
{
public:
    LUAU_RTTI(AstExprUnary)

    enum class Op
    {
        Not,
        Minus,
        Len
    };

    AstExprUnary(const Location& location, Op op, AstExpr* expr);

    void visit(AstVisitor* visitor) override;

    Op op;
    AstExpr* expr;
};

std::string toString(AstExprUnary::Op op);

class AstExprBinary : public AstExpr
{
public:
    LUAU_RTTI(AstExprBinary)

    enum Op
    {
        Add,
        Sub,
        Mul,
        Div,
        FloorDiv,
        Mod,
        Pow,
        Concat,
        CompareNe,
        CompareEq,
        CompareLt,
        CompareLe,
        CompareGt,
        CompareGe,
        And,
        Or,

        Op__Count
    };

    AstExprBinary(const Location& location, Op op, AstExpr* left, AstExpr* right);

    void visit(AstVisitor* visitor) override;

    Op op;
    AstExpr* left;
    AstExpr* right;
};

std::string toString(AstExprBinary::Op op);

class AstExprTypeAssertion : public AstExpr
{
public:
    LUAU_RTTI(AstExprTypeAssertion)

    AstExprTypeAssertion(const Location& location, AstExpr* expr, AstType* annotation);

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
    AstType* annotation;
};

class AstExprIfElse : public AstExpr
{
public:
    LUAU_RTTI(AstExprIfElse)

    AstExprIfElse(
        const Location& location,
        AstExpr* condition,
        bool hasThen,
        AstExpr* trueExpr,
        bool hasElse,
        AstExpr* falseExpr,
        const Location& ifLocation,
        const std::optional<Location>& thenLocation,
        const std::optional<Location>& elseLocation
    );

    void visit(AstVisitor* visitor) override;

    AstExpr* condition;
    bool hasThen;
    AstExpr* trueExpr;
    bool hasElse;
    AstExpr* falseExpr;

    // Location of the leading 'if' or 'elseif' keyword token only. An `elseif` clause is parsed as
    // a nested AstExprIfElse in the false branch, so a node whose ifLocation spans six columns is
    // an `elseif` rather than an `if`.
    Location ifLocation;

    std::optional<Location> thenLocation;

    // Only set for a literal `else` token: an `elseif` clause carries its own keyword as the
    // ifLocation of the nested AstExprIfElse it parses into, and leaves this unset.
    std::optional<Location> elseLocation;
};

class AstExprInterpString : public AstExpr
{
public:
    LUAU_RTTI(AstExprInterpString)

    AstExprInterpString(const Location& location, const AstArray<AstArray<char>>& strings, const AstArray<AstExpr*>& expressions);

    void visit(AstVisitor* visitor) override;

    /// An interpolated string such as `foo{bar}baz` is represented as
    /// an array of strings for "foo" and "bar", and an array of expressions for "baz".
    /// `strings` will always have one more element than `expressions`.
    AstArray<AstArray<char>> strings;
    AstArray<AstExpr*> expressions;
};

// f<<T>>
class AstExprInstantiate : public AstExpr
{
public:
    LUAU_RTTI(AstExprInstantiate)

    AstExprInstantiate(const Location& location, AstExpr* expr, AstArray<AstTypeOrPack> types);

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
    AstArray<AstTypeOrPack> typeArguments;
};

class AstStatBlock : public AstStat
{
public:
    LUAU_RTTI(AstStatBlock)

    AstStatBlock(const Location& location, const AstArray<AstStat*>& body, bool hasEnd = true);

    void visit(AstVisitor* visitor) override;

    AstArray<AstStat*> body;

    /* Indicates whether or not this block has been terminated in a
     * syntactically valid way.
     *
     * This is usually but not always done with the 'end' keyword.  AstStatIf
     * and AstStatRepeat are the two main exceptions to this.
     *
     * The 'then' clause of an if statement can properly be closed by the
     * keywords 'else' or 'elseif'.  A 'repeat' loop's body is closed with the
     * 'until' keyword.
     */
    bool hasEnd = false;
};

class AstStatIf : public AstStat
{
public:
    LUAU_RTTI(AstStatIf)

    AstStatIf(
        const Location& location,
        AstExpr* condition,
        AstStatBlock* thenbody,
        AstStat* elsebody,
        const std::optional<Location>& thenLocation,
        const std::optional<Location>& elseLocation,
        const Location& ifLocation
    );

    void visit(AstVisitor* visitor) override;

    AstExpr* condition;
    AstStatBlock* thenbody;
    AstStat* elsebody;

    std::optional<Location> thenLocation;

    // Active for 'elseif' as well
    std::optional<Location> elseLocation;

    // Location of the leading 'if' or 'elseif' keyword token only (not the whole clause).
    Location ifLocation;
};

class AstStatWhile : public AstStat
{
public:
    LUAU_RTTI(AstStatWhile)

    AstStatWhile(
        const Location& location,
        AstExpr* condition,
        AstStatBlock* body,
        bool hasDo,
        const Location& doLocation,
        const Location& whileLocation
    );

    void visit(AstVisitor* visitor) override;

    AstExpr* condition;
    AstStatBlock* body;

    bool hasDo = false;
    Location doLocation;

    // Location of the leading 'while' keyword token only.
    Location whileLocation;
};

class AstStatRepeat : public AstStat
{
public:
    LUAU_RTTI(AstStatRepeat)

    AstStatRepeat(
        const Location& location,
        AstExpr* condition,
        AstStatBlock* body,
        bool DEPRECATED_hasUntil,
        const Location& repeatLocation,
        const Location& untilLocation
    );

    void visit(AstVisitor* visitor) override;

    AstExpr* condition;
    AstStatBlock* body;

    bool DEPRECATED_hasUntil = false;

    // Location of the leading 'repeat' keyword token only.
    Location repeatLocation;
    // Location of the 'until' keyword token only.
    Location untilLocation;
};

class AstStatBreak : public AstStat
{
public:
    LUAU_RTTI(AstStatBreak)

    AstStatBreak(const Location& location);

    void visit(AstVisitor* visitor) override;
};

class AstStatContinue : public AstStat
{
public:
    LUAU_RTTI(AstStatContinue)

    AstStatContinue(const Location& location);

    void visit(AstVisitor* visitor) override;
};

class AstStatReturn : public AstStat
{
public:
    LUAU_RTTI(AstStatReturn)

    AstStatReturn(const Location& location, const AstArray<AstExpr*>& list, const Location& returnLocation);

    void visit(AstVisitor* visitor) override;

    AstArray<AstExpr*> list;

    // Location of the leading 'return' keyword token only.
    Location returnLocation;
};

class AstStatExpr : public AstStat
{
public:
    LUAU_RTTI(AstStatExpr)

    AstStatExpr(const Location& location, AstExpr* expr);

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
};

// Luwu Destructuring (rfcs/destructuring.md): the source form of a destructuring declaration, kept on the first
// statement it desugars to so tools can print it back. The desugared statements are what everything else reads.
struct AstDestructureField;
struct AstDestructurePattern
{
    // The local the value is bound to. Unnamed, it is a hidden local named kDestructuredLocalName.
    AstLocal* local = nullptr;
    // From the `.` to the `}`, when the value is destructured further; unset for a plain binding.
    std::optional<Location> location;
    // False when the `}` is missing, as it is while a pattern is being written: `location` then ends where the
    // pattern stopped, and a cursor there is still inside it.
    bool closed = true;
    AstArray<AstDestructureField> fields{nullptr, 0};
};

struct AstDestructureField
{
    AstName key;
    Location keyLocation;
    // The `as` keyword, when the field is bound under another name or only destructured.
    std::optional<Location> asLocation;
    AstDestructurePattern target;
};

class AstStatLocal : public AstStat
{
public:
    LUAU_RTTI(AstStatLocal)

    AstStatLocal(
        const Location& location,
        const AstArray<AstLocal*>& vars,
        const AstArray<AstExpr*>& values,
        const std::optional<Location>& equalsSignLocation,
        bool isConst = false
    );

    void visit(AstVisitor* visitor) override;

    AstArray<AstLocal*> vars;
    AstArray<AstExpr*> values;

    bool isConst = false;
    bool isExported = false;

    // Location of the leading `const` or `local` keyword token only.
    std::optional<Location> keywordLocation;
    std::optional<Location> equalsSignLocation;

    // Attributes written above this declaration, e.g. `@deprecated`. Empty when there are none.
    AstArray<AstAttr*> attributes{nullptr, 0};

    // Luwu Destructuring (rfcs/destructuring.md): a destructuring declaration desugars to several `local`s (see
    // Parser::parseDestructuring). The first spans the whole declaration and holds its source form in
    // `destructure`; every one after it points to the first through `destructuredFrom`.
    AstDestructurePattern* destructure = nullptr;
    AstStatLocal* destructuredFrom = nullptr;
};

class AstStatFor : public AstStat
{
public:
    LUAU_RTTI(AstStatFor)

    AstStatFor(
        const Location& location,
        AstLocal* var,
        AstExpr* from,
        AstExpr* to,
        AstExpr* step,
        AstStatBlock* body,
        bool hasDo,
        const Location& doLocation,
        const Location& forLocation
    );

    void visit(AstVisitor* visitor) override;

    AstLocal* var;
    AstExpr* from;
    AstExpr* to;
    AstExpr* step;
    AstStatBlock* body;

    bool hasDo = false;
    Location doLocation;

    // Location of the leading 'for' keyword token only.
    Location forLocation;
};

class AstStatForIn : public AstStat
{
public:
    LUAU_RTTI(AstStatForIn)

    AstStatForIn(
        const Location& location,
        const AstArray<AstLocal*>& vars,
        const AstArray<AstExpr*>& values,
        AstStatBlock* body,
        bool hasIn,
        const Location& inLocation,
        bool hasDo,
        const Location& doLocation,
        const Location& forLocation
    );

    void visit(AstVisitor* visitor) override;

    AstArray<AstLocal*> vars;
    AstArray<AstExpr*> values;
    AstStatBlock* body;

    bool hasIn = false;
    Location inLocation;

    bool hasDo = false;
    Location doLocation;

    // Location of the leading 'for' keyword token only.
    Location forLocation;
};

class AstStatAssign : public AstStat
{
public:
    LUAU_RTTI(AstStatAssign)

    AstStatAssign(const Location& location, const AstArray<AstExpr*>& vars, const AstArray<AstExpr*>& values);

    void visit(AstVisitor* visitor) override;

    AstArray<AstExpr*> vars;
    AstArray<AstExpr*> values;

    // Attributes written above this declaration, e.g. `@deprecated`. Empty when there are none.
    AstArray<AstAttr*> attributes{nullptr, 0};
};

class AstStatCompoundAssign : public AstStat
{
public:
    LUAU_RTTI(AstStatCompoundAssign)

    AstStatCompoundAssign(const Location& location, AstExprBinary::Op op, AstExpr* var, AstExpr* value);

    void visit(AstVisitor* visitor) override;

    AstExprBinary::Op op;
    AstExpr* var;
    AstExpr* value;
};

class AstStatFunction : public AstStat
{
public:
    LUAU_RTTI(AstStatFunction)

    AstStatFunction(const Location& location, AstExpr* name, AstExprFunction* func, const Location& functionLocation);

    void visit(AstVisitor* visitor) override;

    AstExpr* name;
    AstExprFunction* func;

    // Location of the leading 'function' keyword token only.
    Location functionLocation;
};

class AstStatLocalFunction : public AstStat
{
public:
    LUAU_RTTI(AstStatLocalFunction)

    AstStatLocalFunction(
        const Location& location,
        AstLocal* name,
        AstExprFunction* func,
        bool isConst,
        Position constKeywordBegin,
        const Location& keywordLocation,
        const Location& functionLocation
    );

    void visit(AstVisitor* visitor) override;

    AstLocal* name;
    AstExprFunction* func;
    bool isConst;
    // Position of the `const` keyword; Position::missing() when isConst is false.
    Position constKeywordBegin;

    // Location of the leading `local` or `const` keyword token only.
    Location keywordLocation;
    // Location of the `function` keyword token only.
    Location functionLocation;
};

class AstStatTypeAlias : public AstStat
{
public:
    LUAU_RTTI(AstStatTypeAlias)

    AstStatTypeAlias(
        const Location& location,
        const AstName& name,
        const Location& nameLocation,
        const AstArray<AstGenericType*>& generics,
        const AstArray<AstGenericTypePack*>& genericPacks,
        AstType* type,
        bool exported,
        const Location& typeLocation
    );

    void visit(AstVisitor* visitor) override;

    AstName name;
    Location nameLocation;
    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;
    AstType* type;
    bool exported;

    // Location of the leading 'type' keyword token only.
    Location typeLocation;

    // Attributes written above this declaration, e.g. `@deprecated`. Empty when there are none.
    AstArray<AstAttr*> attributes{nullptr, 0};
};

class AstStatTypeFunction : public AstStat
{
public:
    LUAU_RTTI(AstStatTypeFunction);

    AstStatTypeFunction(
        const Location& location,
        const AstName& name,
        const Location& nameLocation,
        AstExprFunction* body,
        bool exported,
        bool hasErrors
    );

    void visit(AstVisitor* visitor) override;

    AstName name;
    Location nameLocation;
    AstExprFunction* body = nullptr;
    bool exported = false;
    bool hasErrors = false;
};

class AstStatDeclareGlobal : public AstStat
{
public:
    LUAU_RTTI(AstStatDeclareGlobal)

    AstStatDeclareGlobal(
        const Location& location,
        const AstName& name,
        const Location& nameLocation,
        AstType* type,
        const Location& declareLocation
    );

    void visit(AstVisitor* visitor) override;

    AstName name;
    Location nameLocation;
    // Luwu Declare Statements (rfcs/declare-statements.md): null for `declare name`, which takes its type from the
    // environment. Upstream requires one.
    AstType* type;

    // Location of the leading 'declare' keyword token only.
    Location declareLocation;
};

class AstStatDeclareFunction : public AstStat
{
public:
    LUAU_RTTI(AstStatDeclareFunction)

    AstStatDeclareFunction(
        const Location& location,
        const AstName& name,
        const Location& nameLocation,
        const AstArray<AstGenericType*>& generics,
        const AstArray<AstGenericTypePack*>& genericPacks,
        const AstTypeList& params,
        const AstArray<AstArgumentName>& paramNames,
        bool vararg,
        const Location& varargLocation,
        AstTypePack* retTypes,
        const Location& declareLocation,
        const Location& functionLocation
    );

    AstStatDeclareFunction(
        const Location& location,
        const AstArray<AstAttr*>& attributes,
        const AstName& name,
        const Location& nameLocation,
        const AstArray<AstGenericType*>& generics,
        const AstArray<AstGenericTypePack*>& genericPacks,
        const AstTypeList& params,
        const AstArray<AstArgumentName>& paramNames,
        bool vararg,
        const Location& varargLocation,
        AstTypePack* retTypes,
        const Location& declareLocation,
        const Location& functionLocation
    );

    void visit(AstVisitor* visitor) override;

    bool isCheckedFunction() const;
    bool hasAttribute(AstAttr::Type attributeType) const;
    AstAttr* getAttribute(AstAttr::Type attributeType) const;

    AstArray<AstAttr*> attributes;
    AstName name;
    Location nameLocation;
    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;
    AstTypeList params;
    AstArray<AstArgumentName> paramNames;
    bool vararg = false;
    Location varargLocation;
    AstTypePack* retTypes;

    // Location of the leading 'declare' keyword token only.
    Location declareLocation;
    // Location of the 'function' keyword token only.
    Location functionLocation;
};

enum class AstTableAccess
{
    Read = 0b01,
    Write = 0b10,
    ReadWrite = 0b11,
};

struct AstDeclaredExternTypeProperty
{
    AstName name;
    Location nameLocation;
    AstType* ty = nullptr;
    bool isMethod = false;
    Location location;
    AstTableAccess access = AstTableAccess::ReadWrite;
};

enum class AstClassMemberVisibility
{
    Public,
    Private,
};

std::string toString(AstClassMemberVisibility visibility);
template<>
std::optional<AstClassMemberVisibility> fromString<AstClassMemberVisibility>(std::string_view s);

struct AstClassProperty
{
    std::optional<Location> qualifierLocation = std::nullopt;
    AstClassMemberVisibility visibility = AstClassMemberVisibility::Public;
    AstName name;
    Location nameLocation;
    std::optional<Location> typeColonLocation = std::nullopt;
    AstType* ty = nullptr;
    bool hasSemicolon = false;
    bool isConst = false;
    // Location of the `const` keyword; nullopt when isConst is false.
    std::optional<Location> constLocation = std::nullopt;
    // Location of the `=` token; nullopt when defaultValue is nullptr, except in a declared class, where
    // `name = T` has one and no defaultValue (see AstStatDeclareClass).
    std::optional<Location> equalsLocation = std::nullopt;
    AstExpr* defaultValue = nullptr;
    // Attributes written above the field, e.g. `@deprecated`. A method's attributes live on its
    // AstExprFunction instead, since that is where a function's attributes already are.
    AstArray<AstAttr*> attributes{nullptr, 0};
    // Luwu Traits (rfcs/classes/traits.md): location of `expect` in a trait's `expect name: T`, a field every implementing
    // class must declare itself. nullopt for a provided field, and always in a class.
    std::optional<Location> expectLocation = std::nullopt;
    // Luwu Traits (rfcs/classes/traits.md): location of `final` in a trait's `final name = value`: a const field only the
    // trait sets, from its default; not even an implementing class's `__init` can write it. nullopt in a class.
    std::optional<Location> finalLocation = std::nullopt;
};

struct AstClassMethod
{
    std::optional<Location> qualifierLocation;
    AstClassMemberVisibility visibility = AstClassMemberVisibility::Public;
    Location keywordLocation;
    AstName functionName;
    Location nameLocation;
    AstExprFunction* function;
    bool hasSemicolon = false;
    // Luwu Traits (rfcs/classes/traits.md): location of `expect` in a trait's `expect function name(self)`, a function every
    // implementing class must define. Its `function` is a signature with an empty body. nullopt in a class.
    std::optional<Location> expectLocation = std::nullopt;
    // Luwu Traits (rfcs/classes/traits.md): `expect function name?(self)`, an expected function a class may leave out; reading
    // it on such a class gives nil. Only set together with expectLocation.
    bool isOptional = false;
    // Luwu Traits (rfcs/classes/traits.md): location of `final` in a trait's `final function`, which no implementing class may
    // define itself. nullopt in a class.
    std::optional<Location> finalLocation = std::nullopt;
};

using AstClassMember = Variant<AstClassProperty, AstClassMethod>;

// Luwu Classes (rfcs/classes): the access specifier and modifiers written directly on a primary
// constructor parameter, Kotlin-style: `class SshKey(public const public_key: string)`. A parameter
// that carries neither is described by a default-constructed instance of this.
struct AstClassPrimaryConstructorParamQualifiers
{
    // Location of the `public`/`private` keyword in front of the parameter; nullopt when absent.
    std::optional<Location> qualifierLocation = std::nullopt;
    AstClassMemberVisibility visibility = AstClassMemberVisibility::Public;
    // Location of the `const` modifier; nullopt when the parameter's field is not const.
    std::optional<Location> constLocation = std::nullopt;
    bool isConst = false;
    // Luwu Declare Statements (rfcs/declare-statements.md): the `=` of `name = T` in a declared class's primary
    // constructor, where the parameter's type is its annotation and it has a default.
    std::optional<Location> declaredDefaultLocation = std::nullopt;
};

// Luwu Classes (rfcs/classes): the primary constructor of a class, `class Cat(name: string, age = 0)`.
// A class using the default (POD) table constructor has none of these at all; a class written as
// `class Cat()` has one with zero parameters, which is what deliberately disables the table constructor.
//
// Parameters are ordinary function parameters -- annotations and default values both optional -- and
// each one implicitly declares a field of the same name, public and non-const unless the parameter
// says otherwise (see argsQualifiers) or the class body restates it. They are only in scope within
// the class body's field initializer expressions, never within its methods.
struct AstClassPrimaryConstructor
{
    // Location of the `public`/`private` keyword before the parameter list; nullopt when absent.
    std::optional<Location> qualifierLocation = std::nullopt;
    AstClassMemberVisibility visibility = AstClassMemberVisibility::Public;
    AstArray<AstLocal*> args;
    // Parallel to `args`; an entry is nullptr when that parameter has no default value.
    AstArray<AstExpr*> argsDefaults;
    // Parallel to `args`; the access specifier and modifiers written on each parameter, if any.
    AstArray<AstClassPrimaryConstructorParamQualifiers> argsQualifiers;
    // Location of the parameter list, parentheses included.
    Location argLocation;
};

// Luwu Traits (rfcs/classes/traits.md): one entry of a class's `implements` list or a trait's `needs` list:
// `Element("div")`, `Iterable<number>`, `mod.Trait`.
struct AstClassTraitRef
{
    // The trait value: a name, or a name indexed with `.` (`mod.Trait`).
    AstExpr* trait = nullptr;
    // The whole entry, generic and trait arguments included.
    Location location;
    AstArray<AstTypeOrPack> typeArguments{nullptr, 0};
    // Whether the entry has a trait argument list, even an empty one: `Element()` passes no arguments, but says so.
    bool hasArgs = false;
    // Trait arguments, parsed like a class field's default value: one function scope deeper than the class, with the
    // class's primary constructor parameters in scope.
    AstArray<AstExpr*> args{nullptr, 0};
    Location argsLocation;
};

class AstStatClass : public AstStat
{
public:
    LUAU_RTTI(AstStatClass)

    AstLocal* name;
    AstArray<AstClassMember> members;
    bool exported;
    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;
    // Null when the class has no primary constructor, i.e. it uses the default table constructor.
    AstClassPrimaryConstructor* primaryConstructor = nullptr;
    // Set once the class's closing `end` has actually been matched, as opposed to being
    // synthesized by error recovery. Mirrors AstStatBlock::hasEnd.
    bool hasEnd = false;

    AstStatClass(
        const Location& location,
        AstLocal* name,
        AstArray<AstClassMember> members,
        bool exported,
        const Location& keywordLocation,
        const AstArray<AstGenericType*>& generics = {},
        const AstArray<AstGenericTypePack*>& genericPacks = {},
        AstClassPrimaryConstructor* primaryConstructor = nullptr
    );

    void visit(AstVisitor* visitor) override;

    // Location of the leading 'class' keyword token only (not 'export').
    Location keywordLocation;

    // Attributes written above this declaration, e.g. `@deprecated`. Empty when there are none.
    AstArray<AstAttr*> attributes{nullptr, 0};

    // Luwu Traits (rfcs/classes/traits.md): `trait Name ... end` is parsed as a class with this set. A trait's primary
    // constructor holds its trait parameters (`trait Element(tag: string)`), and `keywordLocation` is its `trait`.
    bool isTrait = false;
    // Luwu Traits (rfcs/classes/traits.md): a class's `implements` list. Always empty on a trait.
    AstArray<AstClassTraitRef> implements{nullptr, 0};
    // Luwu Traits (rfcs/classes/traits.md): the `implements` and `needs` keywords; nullopt when the list is absent
    std::optional<Location> implementsLocation = std::nullopt;
    std::optional<Location> needsLocation = std::nullopt;
    // Luwu Traits (rfcs/classes/traits.md): a trait's `needs` list; entries never have trait arguments. Always empty on a class.
    AstArray<AstClassTraitRef> needs{nullptr, 0};
};

// Luwu Declare Statements (rfcs/declare-statements.md): `declare [export] class`, a class that exists at runtime but
// whose implementation the type checker doesn't see (an embedder's class compiled into its binary, say). Its shape is
// parsed with the class grammar, minus implementations: methods are signatures and `name = T` is a field or primary
// constructor parameter of type T that has a default. The shape is never a statement of its own, and this node's
// visit only walks its type annotations, so nothing treats a declared class as a class it can compile or run.
class AstStatDeclareClass : public AstStat
{
public:
    LUAU_RTTI(AstStatDeclareClass)

    AstStatDeclareClass(const Location& location, AstStatClass* shape, const Location& declareLocation);

    void visit(AstVisitor* visitor) override;

    AstStatClass* shape;

    // Location of the leading 'declare' keyword token only.
    Location declareLocation;
};

struct AstTableIndexer
{
    AstType* indexType;
    AstType* resultType;
    Location location;

    AstTableAccess access = AstTableAccess::ReadWrite;
    std::optional<Location> accessLocation;
    // Attributes written above the indexer, e.g. `{ @deprecated [string]: number }`. An array-like
    // table type `{T}` desugars to an indexer, so its attributes land here too.
    AstArray<AstAttr*> attributes{nullptr, 0};
};

class AstStatDeclareExternType : public AstStat
{
public:
    LUAU_RTTI(AstStatDeclareExternType)

    AstStatDeclareExternType(
        const Location& location,
        const AstName& name,
        std::optional<AstName> superName,
        const AstArray<AstDeclaredExternTypeProperty>& props,
        const Location& declareLocation,
        const Location& classLocation,
        const std::optional<Location>& extendsLocation,
        AstTableIndexer* indexer = nullptr,
        const AstArray<AstGenericType*>& generics = {},
        const AstArray<AstGenericTypePack*>& genericPacks = {}
    );

    void visit(AstVisitor* visitor) override;

    AstName name;
    std::optional<AstName> superName;

    AstArray<AstDeclaredExternTypeProperty> props;
    AstTableIndexer* indexer;

    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;

    // Location of the leading 'declare' keyword token only.
    Location declareLocation;
    // Location of the 'class' or 'type' keyword token only.
    Location classLocation;
    // Location of the 'extends' keyword token only; nullopt when there's no superclass clause.
    std::optional<Location> extendsLocation;

    // Luwu Declare Statements (rfcs/declare-statements.md): `export declare extern type`. Outside definition files
    // an extern type is scoped like a type alias, so only an exported one is visible to a module that requires this
    // one. The `export` and `with` keywords' locations; `with` is optional there.
    std::optional<Location> exportLocation;
    std::optional<Location> withLocation;
    // Luwu: the type's name, which the statement's location no longer starts at, and the supertype's name, which an
    // error about the supertype underlines instead of the whole declaration.
    Location nameLocation;
    std::optional<Location> superNameLocation;
};

class AstType : public AstNode
{
public:
    AstType(int classIndex, const Location& location)
        : AstNode(classIndex, location)
    {
    }

    AstType* asType() override
    {
        return this;
    }
};

class AstTypeReference : public AstType
{
public:
    LUAU_RTTI(AstTypeReference)

    AstTypeReference(
        const Location& location,
        std::optional<AstName> prefix,
        AstName name,
        std::optional<Location> prefixLocation,
        const Location& nameLocation,
        bool hasParameterList = false,
        const AstArray<AstTypeOrPack>& parameters = {},
        AstLocal* prefixLocal = nullptr
    );

    void visit(AstVisitor* visitor) override;

    bool hasParameterList;
    std::optional<AstName> prefix;
    std::optional<Location> prefixLocation;
    AstLocal* prefixLocal = nullptr;
    AstName name;
    Location nameLocation;
    AstArray<AstTypeOrPack> parameters;
};

struct AstTableProp
{
    AstName name;
    Location location;
    AstType* type;
    AstTableAccess access = AstTableAccess::ReadWrite;
    std::optional<Location> accessLocation;
    // Attributes written above the field, e.g. `{ @deprecated x: number }`. A raw allocation of
    // AstTableProp entries (TypeAttach makes one) must construct each entry in place.
    AstArray<AstAttr*> attributes{nullptr, 0};
};

class AstTypeTable : public AstType
{
public:
    LUAU_RTTI(AstTypeTable)

    AstTypeTable(const Location& location, const AstArray<AstTableProp>& props, AstTableIndexer* indexer = nullptr);

    void visit(AstVisitor* visitor) override;

    AstArray<AstTableProp> props;
    AstTableIndexer* indexer;
};

class AstTypeFunction : public AstType
{
public:
    LUAU_RTTI(AstTypeFunction)

    AstTypeFunction(
        const Location& location,
        const AstArray<AstGenericType*>& generics,
        const AstArray<AstGenericTypePack*>& genericPacks,
        const AstTypeList& argTypes,
        const AstArray<std::optional<AstArgumentName>>& argNames,
        AstTypePack* returnTypes
    );

    AstTypeFunction(
        const Location& location,
        const AstArray<AstAttr*>& attributes,
        const AstArray<AstGenericType*>& generics,
        const AstArray<AstGenericTypePack*>& genericPacks,
        const AstTypeList& argTypes,
        const AstArray<std::optional<AstArgumentName>>& argNames,
        AstTypePack* returnTypes
    );

    void visit(AstVisitor* visitor) override;

    bool isCheckedFunction() const;
    bool hasAttribute(AstAttr::Type attributeType) const;
    AstAttr* getAttribute(AstAttr::Type attributeType) const;

    AstArray<AstAttr*> attributes;
    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;
    AstTypeList argTypes;
    AstArray<std::optional<AstArgumentName>> argNames;
    AstTypePack* returnTypes;
};

class AstTypeTypeof : public AstType
{
public:
    LUAU_RTTI(AstTypeTypeof)

    AstTypeTypeof(const Location& location, AstExpr* expr);

    void visit(AstVisitor* visitor) override;

    AstExpr* expr;
};

class AstTypeOptional : public AstType
{
public:
    LUAU_RTTI(AstTypeOptional)

    AstTypeOptional(const Location& location);

    void visit(AstVisitor* visitor) override;
};

class AstTypeUnion : public AstType
{
public:
    LUAU_RTTI(AstTypeUnion)

    AstTypeUnion(const Location& location, const AstArray<AstType*>& types);

    void visit(AstVisitor* visitor) override;

    AstArray<AstType*> types;
};

class AstTypeIntersection : public AstType
{
public:
    LUAU_RTTI(AstTypeIntersection)

    AstTypeIntersection(const Location& location, const AstArray<AstType*>& types);

    void visit(AstVisitor* visitor) override;

    AstArray<AstType*> types;
};

class AstExprError : public AstExpr
{
public:
    LUAU_RTTI(AstExprError)

    AstExprError(const Location& location, const AstArray<AstExpr*>& expressions, unsigned messageIndex);

    void visit(AstVisitor* visitor) override;

    AstArray<AstExpr*> expressions;
    unsigned messageIndex;
};

class AstStatError : public AstStat
{
public:
    LUAU_RTTI(AstStatError)

    AstStatError(const Location& location, const AstArray<AstExpr*>& expressions, const AstArray<AstStat*>& statements, unsigned messageIndex);

    void visit(AstVisitor* visitor) override;

    AstArray<AstExpr*> expressions;
    AstArray<AstStat*> statements;
    unsigned messageIndex;
};

class AstTypeError : public AstType
{
public:
    LUAU_RTTI(AstTypeError)

    AstTypeError(const Location& location, const AstArray<AstType*>& types, bool isMissing, unsigned messageIndex);

    void visit(AstVisitor* visitor) override;

    AstArray<AstType*> types;
    bool isMissing;
    unsigned messageIndex;
};

class AstTypeSingletonBool : public AstType
{
public:
    LUAU_RTTI(AstTypeSingletonBool)

    AstTypeSingletonBool(const Location& location, bool value);

    void visit(AstVisitor* visitor) override;

    bool value;
};

class AstTypeSingletonString : public AstType
{
public:
    LUAU_RTTI(AstTypeSingletonString)

    AstTypeSingletonString(const Location& location, const AstArray<char>& value);

    void visit(AstVisitor* visitor) override;

    const AstArray<char> value;
};

class AstTypeGroup : public AstType
{
public:
    LUAU_RTTI(AstTypeGroup)

    explicit AstTypeGroup(const Location& location, AstType* type);

    void visit(AstVisitor* visitor) override;

    AstType* type;
};

class AstTypePack : public AstNode
{
public:
    AstTypePack(int classIndex, const Location& location)
        : AstNode(classIndex, location)
    {
    }
};

class AstTypePackExplicit : public AstTypePack
{
public:
    LUAU_RTTI(AstTypePackExplicit)

    AstTypePackExplicit(const Location& location, AstTypeList typeList);

    void visit(AstVisitor* visitor) override;

    AstTypeList typeList;
};

class AstTypePackVariadic : public AstTypePack
{
public:
    LUAU_RTTI(AstTypePackVariadic)

    AstTypePackVariadic(const Location& location, AstType* variadicType);

    void visit(AstVisitor* visitor) override;

    AstType* variadicType;
};

class AstTypePackGeneric : public AstTypePack
{
public:
    LUAU_RTTI(AstTypePackGeneric)

    AstTypePackGeneric(const Location& location, AstName name);

    void visit(AstVisitor* visitor) override;

    AstName genericName;
};

class AstVisitor
{
public:
    virtual ~AstVisitor() {}

    virtual bool visit(class AstNode*)
    {
        return true;
    }

    // Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): upstream forwards to
    // visit(AstNode*), but never reaches here because nodes don't visit their attributes. Luwu's nodes do,
    // so this returns false: an attribute's arguments are data, not code, and a visitor that typechecks,
    // compiles or lints expressions must not see them. A visitor that looks for what is at a position
    // overrides this to return true.
    virtual bool visit(class AstAttr*)
    {
        return false;
    }

    virtual bool visit(class AstGenericType* node)
    {
        return visit(static_cast<AstNode*>(node));
    }

    virtual bool visit(class AstGenericTypePack* node)
    {
        return visit(static_cast<AstNode*>(node));
    }

    virtual bool visit(class AstExpr* node)
    {
        return visit(static_cast<AstNode*>(node));
    }

    virtual bool visit(class AstExprGroup* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprConstantNil* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprConstantBool* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprConstantNumber* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprConstantInteger* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprConstantString* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprLocal* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprGlobal* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprVarargs* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprCall* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprIndexName* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprIndexExpr* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprFunction* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprTable* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprUnary* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprBinary* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprTypeAssertion* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprIfElse* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprInterpString* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprInstantiate* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }
    virtual bool visit(class AstExprError* node)
    {
        return visit(static_cast<AstExpr*>(node));
    }

    virtual bool visit(class AstStat* node)
    {
        return visit(static_cast<AstNode*>(node));
    }

    virtual bool visit(class AstStatBlock* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatIf* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatWhile* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatRepeat* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatBreak* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatContinue* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatReturn* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatExpr* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatLocal* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatFor* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatForIn* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatAssign* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatCompoundAssign* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatFunction* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatLocalFunction* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatTypeAlias* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatTypeFunction* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatDeclareFunction* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatDeclareGlobal* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatClass* node)
    {
        LUAU_ASSERT(FFlag::LuwuClasses);
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatDeclareExternType* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatDeclareClass* node)
    {
        return visit(static_cast<AstStat*>(node));
    }
    virtual bool visit(class AstStatError* node)
    {
        return visit(static_cast<AstStat*>(node));
    }

    // By default visiting type annotations is disabled; override this in your visitor if you need to!
    virtual bool visit(class AstType* node)
    {
        return false;
    }

    virtual bool visit(class AstTypeReference* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeTable* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeFunction* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeTypeof* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeOptional* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeUnion* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeIntersection* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeSingletonBool* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeSingletonString* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeGroup* node)
    {
        return visit(static_cast<AstType*>(node));
    }
    virtual bool visit(class AstTypeError* node)
    {
        return visit(static_cast<AstType*>(node));
    }

    virtual bool visit(class AstTypePack* node)
    {
        return false;
    }
    virtual bool visit(class AstTypePackExplicit* node)
    {
        return visit(static_cast<AstTypePack*>(node));
    }
    virtual bool visit(class AstTypePackVariadic* node)
    {
        return visit(static_cast<AstTypePack*>(node));
    }
    virtual bool visit(class AstTypePackGeneric* node)
    {
        return visit(static_cast<AstTypePack*>(node));
    }
};

bool isLValue(const AstExpr*);
bool isConstantLiteral(const AstExpr*);
bool isLiteralTable(const AstExpr*);
AstName getIdentifier(AstExpr*);
Location getLocation(const AstTypeList& typeList);

template<typename T> // AstNode, AstExpr, AstLocal, etc
Location getLocation(AstArray<T*> array)
{
    if (0 == array.size)
        return {};

    return Location{array.data[0]->location.begin, array.data[array.size - 1]->location.end};
}

#undef LUAU_RTTI

} // namespace Luau

namespace std
{

template<>
struct hash<Luau::AstName>
{
    size_t operator()(const Luau::AstName& value) const
    {
        // note: since operator== uses pointer identity, hashing function uses it as well
        // the hasher is the same as DenseHashPointer (DenseHash.h)
        return (uintptr_t(value.value) >> 4) ^ (uintptr_t(value.value) >> 9);
    }
};

} // namespace std

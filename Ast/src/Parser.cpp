// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Parser.h"

#include "Luau/Ast.h"
#include "Luau/Common.h"
#include "Luau/TimeTrace.h"

#include <algorithm>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unordered_set>

LUAU_FASTINTVARIABLE(LuauRecursionLimit, 1000)
LUAU_FASTINTVARIABLE(LuauTypeLengthLimit, 1000)
LUAU_FASTINTVARIABLE(LuauParseErrorLimit, 100)

// Warning: If you are introducing new syntax, ensure that it is behind a separate
// flag so that we don't break production games by reverting syntax changes.
// See docs/SyntaxChanges.md for an explanation.
LUAU_FASTFLAGVARIABLE(LuauSolverV2)
LUAU_DYNAMIC_FASTFLAGVARIABLE(DebugLuauReportReturnTypeVariadicWithTypeSuffix, false)
LUAU_FASTFLAGVARIABLE(LuauIntegerType2)
LUAU_FASTFLAGVARIABLE(LuauExportValueSyntax)
LUAU_FLAGVERSION(LuauExportValueSyntax, 3)

// Luwu @noinline (rfcs/noinline-attribute.md): gates parsing `@noinline`, and the compiler honoring it (no
// inlining, no LPF_INLINABLE). Upstream spells this `@debugnoinline` behind the Debug flag `DebugLuauNoInline`;
// Luwu ships it as `@noinline` and drops the debug spelling.
LUAU_FASTFLAGVARIABLE(LuwuNoinlineAttribute)
LUAU_FASTFLAGVARIABLE(DebugLuwuUserDefinedRefinements)
// Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): attributes on variables, type
// aliases, table fields, classes, class fields, parameters and assignments, where upstream only allows them on
// functions.
LUAU_FASTFLAGVARIABLE(LuwuAttributesEverywhere)
// Luwu Destructuring (rfcs/destructuring.md): `local`/`const` declarations that bind fields of a value,
// `const fs.{readfile, path} = require("@std/fs")`. Still in progress.
LUAU_FASTFLAGVARIABLE(LuwuDestructuring)
// Luwu Declare Statements (rfcs/declare-statements.md): `declare` in ordinary source, where upstream only accepts it
// in definition files. Still in progress.
LUAU_FASTFLAGVARIABLE(LuwuDeclareStatements)
LUAU_FASTFLAGVARIABLE(LuauAllowGlobalDeclarationToBeCalledClass)
LUAU_FASTFLAGVARIABLE(LuauDisallowExternClassInTypeDefinitions)
LUAU_FASTFLAGVARIABLE(LuauTableEntriesDontNeedToMatchIndent)
LUAU_FASTFLAGVARIABLE(LuauCstAttr)
LUAU_FASTFLAGVARIABLE(LuauStoreConstKeywordBegin)
LUAU_FASTFLAGVARIABLE(LuauTrackPrefixLocal)
LUAU_FASTFLAGVARIABLE(LuwuDefaultArguments)
LUAU_FASTFLAGVARIABLE(LuwuExternTypeGenericMethods)
LUAU_FASTFLAGVARIABLE(LuwuGenericNominals)
LUAU_FASTFLAGVARIABLE(LuauNoDuplicateBinaryPrefix)

// Clip with DebugLuauReportReturnTypeVariadicWithTypeSuffix
bool luau_telemetry_parsed_return_type_variadic_with_type_suffix = false;

namespace Luau
{

namespace
{

// Luwu Classes (rfcs/classes): `class` is a contextual keyword, so with the classes flags off a
// class declaration parses as a nonsense expression statement and reports something about assignments
// (upstream: "Incomplete statement: expected assignment or a function call").
// Users have read that as "my Luwu build is broken", so say what is actually wrong.
// Luwu Destructuring (rfcs/destructuring.md): destructuring always declares new locals, never assigns existing ones.
const char* const kDestructuringNeedsKeywordError = "Destructuring declares new locals; start it with 'local' or 'const'";

// Luwu Declare Statements (rfcs/declare-statements.md): outside definition files, `declare` is only allowed where it
// visibly applies to the whole file.
const char* const kDeclarationPlacementError =
    "'declare' applies to the whole file, so it must be at the top level of the file or in a 'do' block there";
// Luwu Declare Statements (rfcs/declare-statements.md): a declaration has no implementation to take a default from, so
// its defaults (class fields, primary constructor and function parameters) are written as types.
const char* const kDeclaredDefaultIsATypeError = "In a declaration, a default is written as its type: 'name = T'";
// Luwu Declare Statements (rfcs/declare-statements.md): a declared value belongs to its file; values shared by
// several files come from a definitions file.
const char* const kDeclareExportValueError =
    "Only types can be exported with 'export declare'; to declare a global for several files, use a definitions file";
// Luwu: upstream's legacy `declare class` declares an extern type; in Luwu it is a class or nothing.
const char* const kLegacyDeclareClassError = "In Luwu, 'declare class' does not declare an extern type; write 'declare extern type' instead";
const char* const kDeclareClassNeedsTypeError =
    "A declared class is written 'declare class type Name'; to declare an extern type, write 'declare extern type Name'";
const char* const kDeclareExportOrderError = "'export' goes before 'declare': write 'export declare'";
const char* const kClassesDisabledError = "Classes are currently disabled; enable the 'LuwuClasses' fast flag to use 'class'";

// Luwu Classes (rfcs/classes): class members may not be named after the keywords that appear in the
// class header or in front of a member. This keeps those positions unambiguous, and leaves room to give
// these words meaning there later without breaking existing code.
const std::unordered_set<std::string> DISALLOWED_CLASS_MEMBER_NAMES{
    "class",
    "public",
    "private",
    "const",
    "extends",
    "implements",
};

// Only ask this from a position that is unambiguously a field or a function, so the caller's message
// can say which one it is.
bool isDisallowedClassMemberName(const AstName& name)
{
    return name.value != nullptr && DISALLOWED_CLASS_MEMBER_NAMES.count(name.value) > 0;
}

} // namespace

using AttributeArgumentsValidator = std::function<std::vector<std::pair<Location, std::string>>(Location, const AstArray<AstExpr*>&)>;

struct AttributeEntry
{
    const char* name;
    AstAttr::Type type;
    // Every syntactic position this attribute may be written on. An attribute that reaches a
    // position outside this set is a parse error, so a new attribute declares where it makes
    // sense once, here, instead of each position growing a check for it.
    AstAttr::Context allowedContexts;
    // What to say when the attribute reaches a position it does not allow, for an attribute whose
    // allowed set is not something a reader would recognize from the position's name alone. Null
    // falls back to naming the position that was written on.
    const char* allowedPositionsHint;
    // The record fields the attribute takes, ending in nullptr, for editors to offer; nullptr for an
    // attribute that takes no arguments. argsValidator is what enforces them.
    const char* const* argumentFields;
    std::optional<AttributeArgumentsValidator> argsValidator;
};

// A table entry, in a value or a type, may itself *be* a table or a string, so `@deprecated {1, 2}`
// there is a legitimately attributed entry rather than someone writing arguments. Everywhere else the
// attributed thing starts with a keyword or a name, so a `{` or a string can only be a mistake.
static bool contextCanStartWithTableOrString(AstAttr::Context context)
{
    return AstAttr::contextAllows(context, AstAttr::Context::TableField) ||
           AstAttr::contextAllows(context, AstAttr::Context::TableTypeField) ||
           AstAttr::contextAllows(context, AstAttr::Context::TableIndexer);
}

// For "Attribute '@x' cannot be applied to %s". Takes a single context, never a set.
static const char* attributeContextName(AstAttr::Context context)
{
    switch (context)
    {
    case AstAttr::Context::Function:
    case AstAttr::Context::InlinableFunction:
        return "a function";
    case AstAttr::Context::Local:
        return "a local variable";
    case AstAttr::Context::TypeAlias:
        return "a type alias";
    case AstAttr::Context::TableField:
        return "a table field";
    case AstAttr::Context::TableTypeField:
        return "a table type field";
    case AstAttr::Context::TableIndexer:
        return "a table indexer";
    case AstAttr::Context::Class:
        return "a class";
    case AstAttr::Context::ClassField:
        return "a class field";
    case AstAttr::Context::Parameter:
        return "a parameter";
    case AstAttr::Context::Assignment:
        return "an assignment";
    default:
        return "this";
    }
}

std::vector<std::pair<Location, std::string>> deprecatedArgsValidator(Location attrLoc, const AstArray<AstExpr*>& args)
{

    if (args.size == 0)
        return {};
    if (args.size > 1)
        return {{attrLoc, "@deprecated can be parametrized only by 1 argument"}};

    if (!args.data[0]->is<AstExprTable>())
        return {{args.data[0]->location, "Unknown argument type for @deprecated"}};

    std::vector<std::pair<Location, std::string>> errors;
    for (const AstExprTable::Item& item : args.data[0]->as<AstExprTable>()->items)
    {
        if (item.kind == AstExprTable::Item::Kind::Record)
        {
            AstArray<char> keyString = item.key->as<AstExprConstantString>()->value;
            std::string key(keyString.data, keyString.size);
            if (key != "use" && key != "reason")
            {
                errors.emplace_back(
                    item.key->location,
                    format("Unknown argument '%s' for @deprecated. Only string constants for 'use' and 'reason' are allowed", key.c_str())
                );
            }
            else if (!item.value->is<AstExprConstantString>())
            {
                errors.emplace_back(item.value->location, format("Only constant string allowed as value for '%s'", key.c_str()));
            }
        }
        else
        {
            errors.emplace_back(item.value->location, "Only constants keys 'use' and 'reason' are allowed for @deprecated attribute");
        }
    }
    return errors;
}

// @checked and @native describe how a function is compiled or typechecked, so they mean nothing
// anywhere else. @deprecated marks an API as one callers should stop using, which every position
// can have.
// The fields deprecatedArgsValidator accepts.
const char* const kDeprecatedArgumentFields[] = {"use", "reason", nullptr};

AttributeEntry kAttributeEntries[] = {
    {"checked", AstAttr::Type::Checked, AstAttr::Context::AnyFunction, "functions", nullptr, {}},
    {"native", AstAttr::Type::Native, AstAttr::Context::AnyFunction, "functions", nullptr, {}},
    {"deprecated", AstAttr::Type::Deprecated, AstAttr::Context::Any, nullptr, kDeprecatedArgumentFields, deprecatedArgsValidator},
    {nullptr, AstAttr::Type::Checked, AstAttr::Context::None, nullptr, nullptr, {}}
};

// Attributes that are still behind a flag. Same shape as above; the flag is checked before the entry
// is offered, so an attribute whose flag is off is simply not a known attribute.
std::pair<AttributeEntry, Luau::FValue<bool>&> kFlaggedAttributeEntries[] = {
    {{"noinline",
      AstAttr::Type::Noinline,
      AstAttr::Context::InlinableFunction,
      "a local function, a const function, a class method or a function expression",
      nullptr,
      {}},
     FFlag::LuwuNoinlineAttribute},
    // Luwu user-defined refinements: parsed by parseTruthyAttribute, since its second argument is a type
    {{"truthy", AstAttr::Type::Truthy, AstAttr::Context::AnyFunction, "functions", nullptr, {}}, FFlag::DebugLuwuUserDefinedRefinements},
};

std::vector<AttributeInfo> getKnownAttributes()
{
    std::vector<AttributeInfo> result;

    for (int i = 0; kAttributeEntries[i].name; ++i)
    {
        const AttributeEntry& entry = kAttributeEntries[i];
        result.push_back(AttributeInfo{entry.name, entry.type, entry.allowedContexts, entry.argumentFields});
    }

    for (const auto& entry : kFlaggedAttributeEntries)
    {
        if (entry.second)
            result.push_back(AttributeInfo{entry.first.name, entry.first.type, entry.first.allowedContexts, entry.first.argumentFields});
    }

    return result;
}

// The one place an attribute name is resolved to its registration; returns nullptr for a name that
// isn't a known attribute, or one whose flag is off.
static const AttributeEntry* findAttributeEntry(const char* attributeName)
{
    for (int i = 0; kAttributeEntries[i].name; ++i)
    {
        if (strcmp(attributeName, kAttributeEntries[i].name) == 0)
            return &kAttributeEntries[i];
    }

    for (const auto& entry : kFlaggedAttributeEntries)
    {
        if (entry.second && strcmp(attributeName, entry.first.name) == 0)
            return &entry.first;
    }

    return nullptr;
}

ParseError::ParseError(const Location& location, std::string message)
    : location(location)
    , message(std::move(message))
{
}

const char* ParseError::what() const throw()
{
    return message.c_str();
}

const Location& ParseError::getLocation() const
{
    return location;
}

const std::string& ParseError::getMessage() const
{
    return message;
}

// LUAU_NOINLINE is used to limit the stack cost of this function due to std::string object / exception plumbing
LUAU_NOINLINE void ParseError::raise(const Location& location, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string message = vformat(format, args);
    va_end(args);

    throw ParseError(location, message);
}

ParseErrors::ParseErrors(std::vector<ParseError> errors)
    : errors(std::move(errors))
{
    LUAU_ASSERT(!this->errors.empty());

    if (this->errors.size() == 1)
        message = this->errors.front().what();
    else
        message = format("%d parse errors", int(this->errors.size()));
}

const char* ParseErrors::what() const throw()
{
    return message.c_str();
}

const std::vector<ParseError>& ParseErrors::getErrors() const
{
    return errors;
}

template<typename T>
TempVector<T>::TempVector(std::vector<T>& storage)
    : storage(storage)
    , offset(storage.size())
    , size_(0)
{
}

template<typename T>
TempVector<T>::~TempVector()
{
    LUAU_ASSERT(storage.size() == offset + size_);
    storage.erase(storage.begin() + offset, storage.end());
}

template<typename T>
const T& TempVector<T>::operator[](size_t index) const
{
    LUAU_ASSERT(index < size_);
    return storage[offset + index];
}

template<typename T>
const T& TempVector<T>::front() const
{
    LUAU_ASSERT(size_ > 0);
    return storage[offset];
}

template<typename T>
const T& TempVector<T>::back() const
{
    LUAU_ASSERT(size_ > 0);
    return storage.back();
}

template<typename T>
bool TempVector<T>::empty() const
{
    return size_ == 0;
}

template<typename T>
size_t TempVector<T>::size() const
{
    return size_;
}

template<typename T>
void TempVector<T>::push_back(const T& item)
{
    LUAU_ASSERT(storage.size() == offset + size_);
    storage.push_back(item);
    size_++;
}

static bool shouldParseTypePack(Lexer& lexer)
{
    if (lexer.current().type == Lexeme::Dot3)
        return true;
    else if (lexer.current().type == Lexeme::Name && lexer.lookahead().type == Lexeme::Dot3)
        return true;

    return false;
}

ParseResult Parser::parse(const char* buffer, size_t bufferSize, AstNameTable& names, Allocator& allocator, ParseOptions options)
{
    LUAU_TIMETRACE_SCOPE("Parser::parse", "Parser");

    Parser p(buffer, bufferSize, names, allocator, options);

    try
    {
        AstStatBlock* root = p.parseChunk();
        size_t lines = p.lexer.current().location.end.line + (bufferSize > 0 && buffer[bufferSize - 1] != '\n');

        return ParseResult{root, lines, std::move(p.hotcomments), std::move(p.parseErrors), std::move(p.commentLocations), std::move(p.cstNodeMap)};
    }
    catch (ParseError& err)
    {
        // when catching a fatal error, append it to the list of non-fatal errors and return
        p.parseErrors.push_back(err);

        return ParseResult{nullptr, 0, {}, p.parseErrors, {}, std::move(p.cstNodeMap)};
    }
}

template<typename Node, typename F>
ParseNodeResult<Node> Parser::runParse(const char* buffer, size_t bufferSize, AstNameTable& names, Allocator& allocator, ParseOptions options, F f)
{
    LUAU_TIMETRACE_SCOPE("Parser::parse", "Parser");

    Parser p(buffer, bufferSize, names, allocator, options);

    try
    {
        Node* expr = f(p);
        size_t lines = p.lexer.current().location.end.line + (bufferSize > 0 && buffer[bufferSize - 1] != '\n');

        Lexeme eof = p.lexer.next();
        if (eof.type != Lexeme::Eof)
        {
            expr = nullptr;
            p.parseErrors.emplace_back(eof.location, "Expected end of file");
        }

        return ParseNodeResult<Node>{
            expr, lines, std::move(p.hotcomments), std::move(p.parseErrors), std::move(p.commentLocations), std::move(p.cstNodeMap)
        };
    }
    catch (ParseError& err)
    {
        // when catching a fatal error, append it to the list of non-fatal errors and return
        p.parseErrors.push_back(err);

        return ParseNodeResult<Node>{nullptr, 0, {}, p.parseErrors, {}, std::move(p.cstNodeMap)};
    }
}

ParseNodeResult<AstExpr> Parser::parseExpr(const char* buffer, size_t bufferSize, AstNameTable& names, Allocator& allocator, ParseOptions options)
{
    return Parser::runParse<AstExpr>(
        buffer,
        bufferSize,
        names,
        allocator,
        std::move(options),
        [](auto&& parser)
        {
            return parser.parseExpr();
        }
    );
}

ParseNodeResult<AstType> Parser::parseType(const char* buffer, size_t bufferSize, AstNameTable& names, Allocator& allocator, ParseOptions options)
{
    return Parser::runParse<AstType>(
        buffer,
        bufferSize,
        names,
        allocator,
        std::move(options),
        [](auto&& parser)
        {
            return parser.parseType();
        }
    );
}

Parser::Parser(const char* buffer, size_t bufferSize, AstNameTable& names, Allocator& allocator, const ParseOptions& options)
    : options(options)
    , lexer(buffer, bufferSize, names, options.parseFragment ? options.parseFragment->resumePosition : Position(0, 0))
    , allocator(allocator)
    , recursionCounter(0)
    , endMismatchSuspect(Lexeme(Location(), Lexeme::Eof))
    , localMap(AstName())
    , declaredExportBindings(AstName())
    , cstNodeMap(nullptr)
{
    Function top;
    top.vararg = true;

    functionStack.reserve(8);
    functionStack.push_back(top);

    nameSelf = names.getOrAdd("self");
    nameNumber = names.getOrAdd("number");
    nameAny = names.getOrAdd("any");
    nameError = names.getOrAdd(kParseNameError);
    nameDestructured = names.getOrAdd(kDestructuredLocalName);
    nameNil = names.getOrAdd("nil"); // nil is a reserved keyword

    matchRecoveryStopOnToken.assign(Lexeme::Type::Reserved_END, 0);
    matchRecoveryStopOnToken[Lexeme::Type::Eof] = 1;

    // required for lookahead() to work across a comment boundary and for nextLexeme() to work when captureComments is false
    lexer.setSkipComments(true);

    // read first lexeme (any hot comments get .header = true)
    LUAU_ASSERT(hotcommentHeader);
    nextLexeme();

    // all hot comments parsed after the first non-comment lexeme are special in that they don't affect type checking / linting mode
    hotcommentHeader = false;

    // preallocate some buffers that are very likely to grow anyway; this works around std::vector's inefficient growth policy for small arrays
    localStack.reserve(16);
    scratchStat.reserve(16);
    scratchExpr.reserve(16);
    scratchLocal.reserve(16);
    scratchBinding.reserve(16);

    if (options.parseFragment)
    {
        localMap = options.parseFragment->localMap;
        localStack = options.parseFragment->localStack;
    }
}

bool Parser::blockFollow(const Lexeme& l)
{
    return l.type == Lexeme::Eof || l.type == Lexeme::ReservedElse || l.type == Lexeme::ReservedElseif || l.type == Lexeme::ReservedEnd ||
           l.type == Lexeme::ReservedUntil;
}

AstStatBlock* Parser::parseChunk()
{
    nextBlockAllowsDeclarations = true;
    AstStatBlock* result = parseBlock();

    if (lexer.current().type != Lexeme::Eof)
        expectAndConsumeFail(Lexeme::Eof, nullptr);

    if (traitsEnabled())
        checkTraitsDeclaredBeforeUse(result);

    return result;
}

// Luwu Traits (rfcs/classes/traits.md): a class reads its traits when its statement runs, so a trait declared further down
// the chunk is nil there. Its name in the `implements` list parsed as a global, which would fail at runtime. Classes
// and traits are only declared at the top level, so the whole chunk says whether that global is really a later trait.
void Parser::checkTraitsDeclaredBeforeUse(AstStatBlock* chunk)
{
    DenseHashMap<AstName, const AstStatClass*> traits{AstName()};

    for (AstStat* stat : chunk->body)
    {
        const AstStatClass* trait = stat->as<AstStatClass>();
        if (trait && trait->isTrait && !traits.contains(trait->name->name))
            traits[trait->name->name] = trait;
    }

    for (AstStat* stat : chunk->body)
    {
        const AstStatClass* cls = stat->as<AstStatClass>();
        if (!cls || cls->isTrait)
            continue;

        for (const AstClassTraitRef& ref : cls->implements)
        {
            const AstExprGlobal* global = ref.trait->as<AstExprGlobal>();
            const AstStatClass* const* trait = global ? traits.find(global->name) : nullptr;

            if (trait && (*trait)->location.begin > cls->location.begin)
                report(ref.trait->location, "Trait '%s' must be declared before class '%s'", global->name.value, cls->name->name.value);
        }
    }
}

// chunk ::= {stat [`;']} [laststat [`;']]
// block ::= chunk
AstStatBlock* Parser::parseBlock()
{
    unsigned int localsBegin = saveLocals();

    AstStatBlock* result = parseBlockNoScope();

    restoreLocals(localsBegin);

    return result;
}

static bool isStatLast(AstStat* stat)
{
    return stat->is<AstStatBreak>() || stat->is<AstStatContinue>() || stat->is<AstStatReturn>();
}

AstStatBlock* Parser::parseBlockNoScope()
{
    TempVector<AstStat*> body(scratchStat);

    bool outerBlockAllowsDeclarations = blockAllowsDeclarations;
    blockAllowsDeclarations = nextBlockAllowsDeclarations;
    nextBlockAllowsDeclarations = false;

    const Position prevPosition = lexer.previousLocation().end;

    while (!blockFollow(lexer.current()))
    {
        unsigned int oldRecursionCount = recursionCounter;

        incrementRecursionCounter("block");

        AstStat* stat = parseStat();

        recursionCounter = oldRecursionCount;

        // Luwu Destructuring (rfcs/destructuring.md): a trailing `;` belongs to the last statement a
        // destructuring declaration desugared to.
        AstStat* last = pendingStatements.empty() ? stat : pendingStatements.back();

        if (lexer.current().type == ';')
        {
            nextLexeme();
            last->hasSemicolon = true;
            last->location.end = lexer.previousLocation().end;
        }

        body.push_back(stat);

        for (AstStat* desugared : pendingStatements)
            body.push_back(desugared);
        pendingStatements.clear();

        if (isStatLast(stat))
            break;
    }

    blockAllowsDeclarations = outerBlockAllowsDeclarations;

    const Location location = Location(prevPosition, lexer.current().location.begin);

    return allocator.alloc<AstStatBlock>(location, copy(body));
}

// Luwu Classes (rfcs/classes): the grammar below adds classStatement (see parseClassStat).
// stat ::=
// varlist `=' explist |
// functioncall |
// do block end |
// while exp do block end |
// repeat block until exp |
// if exp then block {elseif exp then block} [else block] end |
// for binding `=' exp `,' exp [`,' exp] do block end |
// for namelist in explist do block end |
// function funcname funcbody |
// attributes function funcname funcbody |
// local function Name funcbody |
// local attributes function Name funcbody |
// local namelist [`=' explist] |
// classStatement
// laststat ::= return [explist] | break
AstStat* Parser::parseStat()
{
    // guess the type from the token type
    switch (lexer.current().type)
    {
    case Lexeme::ReservedIf:
        return parseIf();
    case Lexeme::ReservedWhile:
        return parseWhile();
    case Lexeme::ReservedDo:
        return parseDo();
    case Lexeme::ReservedFor:
        return parseFor();
    case Lexeme::ReservedRepeat:
        return parseRepeat();
    case Lexeme::ReservedFunction:
        return parseFunctionStat(AstArray<AstAttr*>({nullptr, 0}));
    case Lexeme::ReservedLocal:
    {
        Location start = lexer.current().location;
        return parseLocal(start, start, {nullptr, 0}, false);
    }
    case Lexeme::ReservedReturn:
        return parseReturn();
    case Lexeme::ReservedBreak:
        return parseBreak();
    case Lexeme::Attribute:
    case Lexeme::AttributeOpen:
        return parseAttributeStat();
    default:;
    }

    Location start = lexer.current().location;

    // Luwu Destructuring (rfcs/destructuring.md): `.{x} = t` with no `local`/`const`. Parsed as a `local` so the
    // rest of the file still sees its names.
    if (destructurePatternFollows())
    {
        report(start, "%s", kDestructuringNeedsKeywordError);
        return parseDestructuring(start, start, /* isConst= */ false);
    }

    // we need to disambiguate a few cases, primarily assignment (lvalue = ...) vs statements-that-are calls
    AstExpr* expr = parsePrimaryExpr(/* asStatement= */ true);

    // Luwu Destructuring (rfcs/destructuring.md): `name.{x} = t` with no `local`/`const`, see parsePrimaryExpr.
    bool destructuringWithoutKeyword = destructurePatternFollows() && getIdentifier(expr) != "const";
    if (destructuringWithoutKeyword)
    {
        report(expr->location, "%s", kDestructuringNeedsKeywordError);
        return parseDestructuring(start, start, /* isConst= */ false, Name(getIdentifier(expr), expr->location));
    }

    if (expr->is<AstExprCall>())
        return allocator.alloc<AstStatExpr>(expr->location, expr);

    // if the next token is , or =, it's an assignment (, means it's an assignment with multiple variables)
    if (lexer.current().type == ',' || lexer.current().type == '=')
        return parseAssignment(expr);

    // if the next token is a compound assignment operator, it's a compound assignment (these don't support multiple variables)
    if (std::optional<AstExprBinary::Op> op = parseCompoundOp(lexer.current()))
        return parseCompoundAssignment(expr, *op);

    // we know this isn't a call or an assignment; therefore it must be a context-sensitive keyword such as `type` or `continue`
    AstName ident = getIdentifier(expr);

    if (ident == "type")
        return parseTypeAlias(expr->location, /* exported= */ false, expr->location.begin, expr->location);

    if (ident == "class")
    {
        if (FFlag::LuwuClasses)
            return parseClassStat(start, /*exported*/ false, start);

        // Without the feature, `class` is an ordinary identifier, so only diagnose the shape a class
        // declaration actually has (`class Name`); anything else is someone's variable named `class`.
        if (lexer.current().type == Lexeme::Name)
            return reportStatError(expr->location, copy({expr}), {}, "%s", kClassesDisabledError);
    }

    // Luwu Traits (rfcs/classes/traits.md): `trait Name`. Like `class`, `trait` is only a keyword where a declaration starts,
    // so a variable named `trait` keeps working.
    if (ident == "trait" && traitsEnabled() && lexer.current().type == Lexeme::Name)
        return parseClassStat(start, /* exported= */ false, start, {nullptr, 0}, /* declared= */ false, /* isTrait= */ true);

    if (ident == "export")
    {
        // Luwu Declare Statements (rfcs/declare-statements.md): `export declare extern type`, `export declare class`
        bool exportsDeclaration = FFlag::LuwuDeclareStatements && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "declare";
        if (exportsDeclaration)
        {
            Location declareLocation = lexer.current().location;
            nextLexeme();
            return parseDeclaration(declareLocation, AstArray<AstAttr*>({nullptr, 0}), expr->location);
        }

        if (FFlag::LuauExportValueSyntax)
        {
            Lexeme current = lexer.current();

            // Luwu Traits (rfcs/classes/traits.md): `export trait Name`
            bool exportsTrait = current.type == Lexeme::Name && AstName(current.name) == "trait" && traitsEnabled() &&
                                lexer.lookahead().type == Lexeme::Name;

            if (current.type == Lexeme::ReservedLocal || current.type == Lexeme::ReservedFunction ||
                (current.type == Lexeme::Name && AstName(current.name) == "const") ||
                (current.type == Lexeme::Name && AstName(current.name) == "class") || exportsTrait)
            {
                // `export class` routes here with the classes feature off too, so that it reports the
                // feature being disabled rather than 'export' wanting an identifier.
                return parseExportValue(expr->location, expr->location, AstArray<AstAttr*>({nullptr, 0}));
            }
            else if (current.type == Lexeme::Name && AstName(current.name) == "type")
            {
                Position typeKeywordPosition = current.location.begin;
                Location typeKeywordLocation = current.location;
                nextLexeme();
                return parseTypeAlias(expr->location, /* exported= */ true, typeKeywordPosition, typeKeywordLocation);
            }
        }
        // TODO: remove with LuauExportValueSyntax
        else if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "class")
        {
            if (!FFlag::LuwuClasses)
                return reportStatError(expr->location, copy({expr}), {}, "%s", kClassesDisabledError);

            Location classKeywordLocation = lexer.current().location;
            nextLexeme();
            return parseClassStat(start, /*exported*/ true, classKeywordLocation);
        }
        else
        {
            if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "type")
            {
                Position typeKeywordPosition = lexer.current().location.begin;
                Location typeKeywordLocation = lexer.current().location;
                nextLexeme();
                return parseTypeAlias(expr->location, /* exported= */ true, typeKeywordPosition, typeKeywordLocation);
            }
        }
    }


    if (ident == "continue")
        return parseContinue(expr->location);

    if (ident == "const")
        return parseLocal(expr->location, expr->location, AstArray<AstAttr*>({nullptr, 0}), true);

    if (ident == "declare" && declarationsAllowed())
        return parseDeclaration(expr->location, AstArray<AstAttr*>({nullptr, 0}));

    // skip unexpected symbol if lexer couldn't advance at all (statements are parsed in a loop)
    if (start == lexer.current().location)
        nextLexeme();

    return reportStatError(expr->location, copy({expr}), {}, "Incomplete statement: expected assignment or a function call");
}

// if exp then block {elseif exp then block} [else block] end
AstStat* Parser::parseIf()
{
    Location start = lexer.current().location;

    nextLexeme(); // if / elseif

    AstExpr* cond = parseExpr();

    Lexeme matchThen = lexer.current();
    std::optional<Location> thenLocation;
    if (expectAndConsume(Lexeme::ReservedThen, "if statement"))
        thenLocation = matchThen.location;

    AstStatBlock* thenbody = parseBlock();

    AstStat* elsebody = nullptr;
    Location end = start;
    std::optional<Location> elseLocation;

    if (lexer.current().type == Lexeme::ReservedElseif)
    {
        thenbody->hasEnd = true;
        unsigned int oldRecursionCount = recursionCounter;
        incrementRecursionCounter("elseif");
        elseLocation = lexer.current().location;
        elsebody = parseIf();
        end = elsebody->location;
        recursionCounter = oldRecursionCount;
    }
    else
    {
        Lexeme matchThenElse = matchThen;

        if (lexer.current().type == Lexeme::ReservedElse)
        {
            thenbody->hasEnd = true;
            elseLocation = lexer.current().location;
            matchThenElse = lexer.current();
            nextLexeme();

            elsebody = parseBlock();
            elsebody->location.begin = matchThenElse.location.end;
        }

        end = lexer.current().location;

        bool hasEnd = expectMatchEndAndConsume(Lexeme::ReservedEnd, matchThenElse);

        if (elsebody)
        {
            if (AstStatBlock* elseBlock = elsebody->as<AstStatBlock>())
                elseBlock->hasEnd = hasEnd;
        }
        else
            thenbody->hasEnd = hasEnd;
    }

    return allocator.alloc<AstStatIf>(Location(start, end), cond, thenbody, elsebody, thenLocation, elseLocation, start);
}

// while exp do block end
AstStat* Parser::parseWhile()
{
    Location start = lexer.current().location;

    nextLexeme(); // while

    AstExpr* cond = parseExpr();

    Lexeme matchDo = lexer.current();
    bool hasDo = expectAndConsume(Lexeme::ReservedDo, "while loop");

    functionStack.back().loopDepth++;

    AstStatBlock* body = parseBlock();

    functionStack.back().loopDepth--;

    Location end = lexer.current().location;

    bool hasEnd = expectMatchEndAndConsume(Lexeme::ReservedEnd, matchDo);
    body->hasEnd = hasEnd;

    return allocator.alloc<AstStatWhile>(Location(start, end), cond, body, hasDo, matchDo.location, start);
}

// repeat block until exp
AstStat* Parser::parseRepeat()
{
    Location start = lexer.current().location;

    Lexeme matchRepeat = lexer.current();
    nextLexeme(); // repeat

    unsigned int localsBegin = saveLocals();

    functionStack.back().loopDepth++;

    AstStatBlock* body = parseBlockNoScope();

    functionStack.back().loopDepth--;

    Lexeme matchUntil = lexer.current();
    bool hasUntil = expectMatchEndAndConsume(Lexeme::ReservedUntil, matchRepeat);
    body->hasEnd = hasUntil;
    Position untilPosition = hasUntil ? lexer.previousLocation().begin : Position::missing();

    AstExpr* cond = parseExpr();

    restoreLocals(localsBegin);

    AstStatRepeat* node = allocator.alloc<AstStatRepeat>(Location(start, cond->location), cond, body, hasUntil, start, matchUntil.location);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstStatRepeat>(untilPosition);
    return node;
}

// do block end
AstStat* Parser::parseDo()
{
    Location start = lexer.current().location;

    Lexeme matchDo = lexer.current();
    nextLexeme(); // do

    Position statsStart = lexer.current().location.begin;

    nextBlockAllowsDeclarations = blockAllowsDeclarations;
    AstStatBlock* body = parseBlock();

    body->location.begin = start.begin;

    Location endLocation = lexer.current().location;
    body->hasEnd = expectMatchEndAndConsume(Lexeme::ReservedEnd, matchDo);
    if (body->hasEnd)
        body->location.end = endLocation.end;

    if (options.storeCstData)
        cstNodeMap[body] = allocator.alloc<CstStatDo>(statsStart, body->hasEnd ? endLocation.begin : Position::missing());

    return body;
}

// break
AstStat* Parser::parseBreak()
{
    Location start = lexer.current().location;

    nextLexeme(); // break

    if (functionStack.back().loopDepth == 0)
        return reportStatError(start, {}, copy<AstStat*>({allocator.alloc<AstStatBreak>(start)}), "break statement must be inside a loop");

    return allocator.alloc<AstStatBreak>(start);
}

// continue
AstStat* Parser::parseContinue(const Location& start)
{
    if (functionStack.back().loopDepth == 0)
        return reportStatError(start, {}, copy<AstStat*>({allocator.alloc<AstStatContinue>(start)}), "continue statement must be inside a loop");

    // note: the token is already parsed for us!

    return allocator.alloc<AstStatContinue>(start);
}

// for binding `=' exp `,' exp [`,' exp] do block end |
// for bindinglist in explist do block end |
AstStat* Parser::parseFor()
{
    Location start = lexer.current().location;

    nextLexeme(); // for

    Binding varname = parseBinding();

    if (lexer.current().type == '=')
    {
        Position equalsPosition = lexer.current().location.begin;
        nextLexeme();

        AstExpr* from = parseExpr();

        bool hasEndComma = expectAndConsume(',', "index range");
        Position endCommaPosition = hasEndComma ? lexer.previousLocation().begin : Position::missing();

        AstExpr* to = parseExpr();

        Position stepCommaPosition = Position::missing();
        AstExpr* step = nullptr;

        if (lexer.current().type == ',')
        {
            stepCommaPosition = lexer.current().location.begin;
            nextLexeme();

            step = parseExpr();
        }

        Lexeme matchDo = lexer.current();
        bool hasDo = expectAndConsume(Lexeme::ReservedDo, "for loop");

        unsigned int localsBegin = saveLocals();

        functionStack.back().loopDepth++;

        AstLocal* var = pushLocal(varname);

        AstStatBlock* body = parseBlock();

        functionStack.back().loopDepth--;

        restoreLocals(localsBegin);

        Location end = lexer.current().location;

        bool hasEnd = expectMatchEndAndConsume(Lexeme::ReservedEnd, matchDo);
        body->hasEnd = hasEnd;

        AstStatFor* node = allocator.alloc<AstStatFor>(Location(start, end), var, from, to, step, body, hasDo, matchDo.location, start);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstStatFor>(varname.colonPosition, equalsPosition, endCommaPosition, stepCommaPosition);

        return node;
    }
    else
    {
        TempVector<Binding> names(scratchBinding);
        AstArray<Position> varsCommaPosition;
        names.push_back(varname);

        if (lexer.current().type == ',')
        {
            if (options.storeCstData)
            {
                Position initialCommaPosition = lexer.current().location.begin;
                nextLexeme();
                parseBindingList(names, false, false, &varsCommaPosition, &initialCommaPosition);
            }
            else
            {
                nextLexeme();

                parseBindingList(names);
            }
        }

        Location inLocation = lexer.current().location;
        bool hasIn = expectAndConsume(Lexeme::ReservedIn, "for loop");

        TempVector<AstExpr*> values(scratchExpr);
        TempVector<Position> valuesCommaPositions(scratchPosition);
        parseExprList(values, options.storeCstData ? &valuesCommaPositions : nullptr);

        Lexeme matchDo = lexer.current();
        bool hasDo = expectAndConsume(Lexeme::ReservedDo, "for loop");

        unsigned int localsBegin = saveLocals();

        functionStack.back().loopDepth++;

        TempVector<AstLocal*> vars(scratchLocal);

        for (size_t i = 0; i < names.size(); ++i)
            vars.push_back(pushLocal(names[i]));

        AstStatBlock* body = parseBlock();

        functionStack.back().loopDepth--;

        restoreLocals(localsBegin);

        Location end = lexer.current().location;

        bool hasEnd = expectMatchEndAndConsume(Lexeme::ReservedEnd, matchDo);
        body->hasEnd = hasEnd;

        AstStatForIn* node = allocator.alloc<AstStatForIn>(
            Location(start, end), copy(vars), copy(values), body, hasIn, inLocation, hasDo, matchDo.location, start
        );
        if (options.storeCstData)
        {
            cstNodeMap[node] = allocator.alloc<CstStatForIn>(extractAnnotationColonPositions(names), varsCommaPosition, copy(valuesCommaPositions));
        }
        return node;
    }
}

// funcname ::= Name {`.' Name} [`:' Name]
AstExpr* Parser::parseFunctionName(bool& hasself, AstName& debugname)
{
    if (lexer.current().type == Lexeme::Name)
        debugname = AstName(lexer.current().name);

    // parse funcname into a chain of indexing operators
    AstExpr* expr = parseNameExpr("function name");

    unsigned int oldRecursionCount = recursionCounter;

    while (lexer.current().type == '.')
    {
        Position opPosition = lexer.current().location.begin;
        nextLexeme();

        Name name = parseName("field name");

        // while we could concatenate the name chain, for now let's just write the short name
        debugname = name.name;

        expr = allocator.alloc<AstExprIndexName>(Location(expr->location, name.location), expr, name.name, name.location, opPosition, '.');

        // note: while the parser isn't recursive here, we're generating recursive structures of unbounded depth
        incrementRecursionCounter("function name");
    }

    recursionCounter = oldRecursionCount;

    // finish with :
    if (lexer.current().type == ':')
    {
        Position opPosition = lexer.current().location.begin;
        nextLexeme();

        Name name = parseName("method name");

        // while we could concatenate the name chain, for now let's just write the short name
        debugname = name.name;

        expr = allocator.alloc<AstExprIndexName>(Location(expr->location, name.location), expr, name.name, name.location, opPosition, ':');

        hasself = true;
    }

    return expr;
}

AstStatClass* Parser::getMatchingClass(AstExpr* expr)
{
    LUAU_ASSERT(FFlag::LuwuClasses);
    if (AstExprGlobal* g = expr->as<AstExprGlobal>())
    {
        if (AstStatClass** classDecl = classesWithinModule.find(g->name))
            return *classDecl;
    }
    return nullptr;
}

bool Parser::isExprLValue(AstExpr* expr)
{
    return (expr->is<AstExprLocal>() && !expr->as<AstExprLocal>()->local->isConst) ||
           (expr->is<AstExprGlobal>() && !(FFlag::LuwuClasses && getMatchingClass(expr) != nullptr)) ||
           expr->is<AstExprIndexExpr>() || expr->is<AstExprIndexName>();
}

// function funcname funcbody
AstStatFunction* Parser::parseFunctionStat(const AstArray<AstAttr*>& attributes, TempVector<CstAttrList*>* cstAttrLists)
{
    if (cstAttrLists != nullptr)
        LUAU_ASSERT(FFlag::LuauCstAttr);

    Location start = lexer.current().location;
    if (FFlag::LuauCstAttr)
        start = getAttributeStartLocation(attributes, cstAttrLists, lexer.current().location);
    else if (attributes.size > 0)
        start = attributes.data[0]->location;

    Lexeme matchFunction = lexer.current();
    nextLexeme();

    bool hasself = false;
    AstName debugname;
    AstExpr* expr = parseFunctionName(hasself, debugname);

    if (!isExprLValue(expr))
    {
        expr = reportLValueError(expr);
    }

    matchRecoveryStopOnToken[Lexeme::ReservedEnd]++;

    AstExprFunction* body = parseFunctionBody(hasself, matchFunction, debugname, nullptr, attributes).first;

    matchRecoveryStopOnToken[Lexeme::ReservedEnd]--;

    AstStatFunction* node = allocator.alloc<AstStatFunction>(Location(start, body->location), expr, body, matchFunction.location);
    if (options.storeCstData)
        cstNodeMap[node] = FFlag::LuauCstAttr && cstAttrLists ? allocator.alloc<CstStatFunction>(copy(*cstAttrLists), matchFunction.location.begin)
                                                              : allocator.alloc<CstStatFunction>(matchFunction.location.begin);

    return node;
}

std::optional<AstAttr::Type> Parser::validateAttribute(
    Location loc,
    const char* attributeName,
    const TempVector<AstAttr*>& attributes,
    const AstArray<AstExpr*>& args,
    AstAttr::Context context
)
{
    // check if the attribute name is valid
    std::optional<AstAttr::Type> type;
    std::optional<AttributeArgumentsValidator> argsValidator;
    AstAttr::Context allowedContexts = AstAttr::Context::None;
    const char* allowedPositionsHint = nullptr;

    if (const AttributeEntry* entry = findAttributeEntry(attributeName))
    {
        type = entry->type;
        allowedContexts = entry->allowedContexts;
        allowedPositionsHint = entry->allowedPositionsHint;
        argsValidator = entry->argsValidator;
    }

    if (!type)
    {
        if (strlen(attributeName) == 0)
            report(loc, "Attribute name is missing");
        else
            report(loc, "Invalid attribute '@%s'", attributeName);
    }
    else
    {
        // check that the attribute means something where it was written
        if (AstAttr::isSingleContext(context) && !AstAttr::contextAllows(allowedContexts, context))
            reportAttributeNotAllowed(loc, attributeName, allowedPositionsHint, context);

        // check that attribute is not duplicated
        for (const AstAttr* attr : attributes)
        {
            if (attr->type == *type)
                report(loc, "Cannot duplicate attribute '@%s'", attributeName);
        }
        if (argsValidator)
        {
            auto errorsToReport = (*argsValidator)(loc, args);
            for (const auto& [errorLoc, msg] : errorsToReport)
            {
                report(errorLoc, "%s", msg.c_str());
            }
        }
    }

    return type;
}

// Luwu user-defined refinements:
// truthyattr = 'truthy' '(' Name ',' Type ')'
AstAttr* Parser::parseTruthyAttribute(const TempVector<AstAttr*>& attributes, const Name& name, AstAttr::Context context)
{
    AstArray<AstExpr*> empty;
    validateAttribute(name.location, name.name.value, attributes, empty, context);

    Location end = name.location;
    AstAttr* node = allocator.alloc<AstAttr>(name.location, AstAttr::Type::Truthy, empty, name.name);

    if (lexer.current().type != '(')
    {
        report(name.location, "'truthy' needs a parameter and the type it has when the function returns a truthy value: truthy(param, Type)");
        return node;
    }

    Lexeme open = lexer.current();
    nextLexeme();

    Name param = parseName("parameter name");
    node->refinedParam = param.name;
    node->refinedParamLocation = param.location;

    expectAndConsume(',', "truthy attribute");
    node->refinedType = parseType();

    end = lexer.current().location;
    expectMatchAndConsume(')', open);

    node->location = Location(name.location, end);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstAttr>(/* hasAt */ false);

    return node;
}

// attrlist = '@[' parattr {',' parattr} ']'
void Parser::parseAttrList(TempVector<AstAttr*>& attributes, TempVector<CstAttrList*>* cstAttrLists, AstAttr::Context context)
{
    LUAU_ASSERT(FFlag::LuauCstAttr);

    Lexeme open = lexer.current();

    LUAU_ASSERT(open.type == Lexeme::Type::AttributeOpen);

    nextLexeme();

    AstArray<AstExpr*> empty;
    TempVector<Position> commaPositions(scratchPosition);
    const size_t firstInList = attributes.size();

    if (lexer.current().type != ']')
    {
        while (true)
        {
            Name name = parseName("attribute name");

            Location nameLoc = name.location;
            const char* attrName = name.name.value;

            Lexeme argOpen = lexer.current();
            Lexeme::Type argOpenType = argOpen.type;

            if (FFlag::DebugLuwuUserDefinedRefinements && strcmp(attrName, "truthy") == 0)
            {
                attributes.push_back(parseTruthyAttribute(attributes, name, context));
            }
            else if (argOpenType == Lexeme::RawString || argOpenType == Lexeme::QuotedString || argOpenType == '{' || argOpenType == '(')
            {
                Position openParenPosition = argOpenType == '(' ? argOpen.location.begin : Position::missing();
                TempVector<Position> argCommaPositions(scratchPosition2);
                Position closeParenPosition = Position::missing();

                auto [args, argsLocation, _exprLocation] =
                    options.storeCstData ? parseCallList(&argCommaPositions, &closeParenPosition) : parseCallList(nullptr, nullptr);

                reportNonLiteralAttributeArgs(args, argsLocation);

                std::optional<AstAttr::Type> type = validateAttribute(nameLoc, attrName, attributes, args, context);

                AstAttr* node =
                    allocator.alloc<AstAttr>(Location(nameLoc, argsLocation), type.value_or(AstAttr::Type::Unknown), args, AstName(attrName));

                if (options.storeCstData)
                    cstNodeMap[node] = allocator.alloc<CstParametrizedAttr>(openParenPosition, closeParenPosition, copy(argCommaPositions));

                attributes.push_back(node);
            }
            else
            {
                std::optional<AstAttr::Type> type = validateAttribute(nameLoc, attrName, attributes, empty, context);

                AstAttr* node = allocator.alloc<AstAttr>(nameLoc, type.value_or(AstAttr::Type::Unknown), empty, AstName(attrName));

                if (options.storeCstData)
                    cstNodeMap[node] = allocator.alloc<CstAttr>(/* hasAt */ false);

                attributes.push_back(node);
            }

            const Lexeme& current = lexer.current();
            if (current.type == ',')
            {
                if (options.storeCstData)
                    commaPositions.push_back(current.location.begin);

                nextLexeme();
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        report(Location(open.location, lexer.current().location), "Attribute list cannot be empty");

        // autocomplete expects at least one unknown attribute.
        AstAttr* node = allocator.alloc<AstAttr>(Location(open.location, lexer.current().location), AstAttr::Type::Unknown, empty, nameError);

        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstAttr>(/* hasAt */ false);

        attributes.push_back(node);
    }

    bool closingBracketFound = expectMatchAndConsume(']', open);

    if (options.storeCstData)
    {
        CstAttrList* list = allocator.alloc<CstAttrList>(
            open.location.begin, closingBracketFound ? lexer.previousLocation().begin : Position::missing(), copy(commaPositions)
        );

        // Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): the list is also recorded on
        // its first attribute, so a position that has no CST list of its own can still print it. Upstream
        // asserts that every caller passes `cstAttrLists`; positions that only Luwu allows attributes on don't.
        LUAU_ASSERT(firstInList < attributes.size());
        if (CstNode** first = cstNodeMap.find(attributes[firstInList]))
        {
            if (CstAttr* attr = (*first)->as<CstAttr>())
                attr->openedList = list;
            else if (CstParametrizedAttr* parametrized = (*first)->as<CstParametrizedAttr>())
                parametrized->openedList = list;
        }

        if (cstAttrLists)
            cstAttrLists->push_back(list);
    }
}

bool Parser::parseMisplacedBareAttributeArgs(const char* name, AstAttr::Context context, AstArray<AstExpr*>& args, Location& argsLocation)
{
    // Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): upstream reports whatever the
    // next token then fails to be.
    if (!FFlag::LuwuAttributesEverywhere)
        return false;

    // In a table entry, value or type, the entry may itself *be* a table or a string, so there is
    // nothing misplaced to report.
    if (contextCanStartWithTableOrString(context))
        return false;

    const Lexeme::Type next = lexer.current().type;
    const bool argumentsFollow = next == '{' || next == Lexeme::RawString || next == Lexeme::QuotedString;
    if (!argumentsFollow)
        return false;

    report(lexer.current().location, "Attribute arguments must be written as '@[%s ...]'; a bare '@%s' cannot take arguments", name, name);

    std::tie(args, argsLocation, std::ignore) = parseCallList(nullptr);
    reportNonLiteralAttributeArgs(args, argsLocation);

    return true;
}

// attribute ::= '@' NAME
void Parser::parseAttribute_DEPRECATED(TempVector<AstAttr*>& attributes, AstAttr::Context context)
{
    LUAU_ASSERT(!FFlag::LuauCstAttr);

    AstArray<AstExpr*> empty;

    LUAU_ASSERT(lexer.current().type == Lexeme::Type::Attribute || lexer.current().type == Lexeme::Type::AttributeOpen);

    if (lexer.current().type == Lexeme::Type::Attribute)
    {
        Location loc = lexer.current().location;

        const char* name = lexer.current().name;

        nextLexeme();

        AstArray<AstExpr*> args = empty;
        Location argsLocation;
        const bool misplaced = parseMisplacedBareAttributeArgs(name, context, args, argsLocation);

        std::optional<AstAttr::Type> type = validateAttribute(loc, name, attributes, args, context);

        attributes.push_back(allocator.alloc<AstAttr>(
            misplaced ? Location(loc, argsLocation) : loc, type.value_or(AstAttr::Type::Unknown), args, AstName(name)
        ));
    }
    else
    {
        Lexeme open = lexer.current();
        nextLexeme();

        if (lexer.current().type != ']')
        {
            while (true)
            {
                Name name = parseName("attribute name");

                Location nameLoc = name.location;
                const char* attrName = name.name.value;

                // Luwu user-defined refinements: as in parseAttrList, so the attribute doesn't depend on LuauCstAttr
                if (FFlag::DebugLuwuUserDefinedRefinements && strcmp(attrName, "truthy") == 0)
                {
                    attributes.push_back(parseTruthyAttribute(attributes, name, context));
                }
                else if (lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString || lexer.current().type == '{' ||
                    lexer.current().type == '(')
                {

                    auto [args, argsLocation, _exprLocation] = parseCallList(nullptr);

                    reportNonLiteralAttributeArgs(args, argsLocation);

                    std::optional<AstAttr::Type> type = validateAttribute(nameLoc, attrName, attributes, args, context);

                    attributes.push_back(
                        allocator.alloc<AstAttr>(Location(nameLoc, argsLocation), type.value_or(AstAttr::Type::Unknown), args, AstName(attrName))
                    );
                }
                else
                {
                    std::optional<AstAttr::Type> type = validateAttribute(nameLoc, attrName, attributes, empty, context);
                    attributes.push_back(allocator.alloc<AstAttr>(nameLoc, type.value_or(AstAttr::Type::Unknown), empty, AstName(attrName)));
                }

                if (lexer.current().type == ',')
                {
                    nextLexeme();
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            report(Location(open.location, lexer.current().location), "Attribute list cannot be empty");

            // autocomplete expects at least one unknown attribute.
            attributes.push_back(
                allocator.alloc<AstAttr>(Location(open.location, lexer.current().location), AstAttr::Type::Unknown, empty, nameError)
            );
        }

        expectMatchAndConsume(']', open);
    }
}

// attribute ::= '@' NAME
void Parser::parseAttribute(TempVector<AstAttr*>& attributes, AstAttr::Context context)
{
    LUAU_ASSERT(FFlag::LuauCstAttr);

    AstArray<AstExpr*> empty;

    LUAU_ASSERT(lexer.current().type == Lexeme::Type::Attribute);

    Location loc = lexer.current().location;

    const char* name = lexer.current().name;

    nextLexeme();

    // Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): a bare `@name` never takes
    // arguments; `@[name { ... }]` is the form that does. Arguments written bare anyway are reported as that,
    // and parsed so this is the only error.
    AstArray<AstExpr*> args = empty;
    Location argsLocation;
    const bool argumentsFollowBare = parseMisplacedBareAttributeArgs(name, context, args, argsLocation);

    std::optional<AstAttr::Type> type = validateAttribute(loc, name, attributes, args, context);

    // Luwu user-defined refinements: its arguments are a name and a type, which only the list form parses
    if (type == AstAttr::Type::Truthy)
        report(loc, "'truthy' takes a parameter and a type: write it as @[truthy(param, Type)]");

    AstAttr* node = allocator.alloc<AstAttr>(
        argumentsFollowBare ? Location(loc, argsLocation) : loc, type.value_or(AstAttr::Type::Unknown), args, AstName(name)
    );
    attributes.push_back(node);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstAttr>(/* hasAt */ true);
}

// attributes ::= {attribute}
AstArray<AstAttr*> Parser::parseAttributes(AstAttr::Context context, TempVector<CstAttrList*>* cstAttrLists)
{
    LUAU_ASSERT(cstAttrLists != nullptr ? FFlag::LuauCstAttr : true);

    Lexeme::Type type = lexer.current().type;

    LUAU_ASSERT(type == Lexeme::Attribute || type == Lexeme::AttributeOpen);

    TempVector<AstAttr*> attributes(scratchAttr);

    while (lexer.current().type == Lexeme::Attribute || lexer.current().type == Lexeme::AttributeOpen)
    {
        if (FFlag::LuauCstAttr)
        {
            if (lexer.current().type == Lexeme::Type::Attribute)
                parseAttribute(attributes, context);
            else
                parseAttrList(attributes, cstAttrLists, context);
        }
        else
            parseAttribute_DEPRECATED(attributes, context);
    }

    return copy(attributes);
}

AstArray<AstAttr*> Parser::concatAttributes(const AstArray<AstAttr*>& first, const AstArray<AstAttr*>& second)
{
    if (first.size == 0)
        return second;
    if (second.size == 0)
        return first;

    TempVector<AstAttr*> merged(scratchAttr);
    for (AstAttr* attr : first)
        merged.push_back(attr);
    for (AstAttr* attr : second)
        merged.push_back(attr);

    return copy(merged);
}

void Parser::validateAttributeContexts(const AstArray<AstAttr*>& attributes, AstAttr::Context context)
{
    for (const AstAttr* attr : attributes)
    {
        // An unknown attribute was already reported when it was parsed; don't pile a second error on it.
        if (attr->type == AstAttr::Type::Unknown)
            continue;

        const AttributeEntry* entry = findAttributeEntry(attr->name.value);
        if (entry && !AstAttr::contextAllows(entry->allowedContexts, context))
            reportAttributeNotAllowed(attr->location, attr->name.value, entry->allowedPositionsHint, context);
    }
}

void Parser::reportAttributeNotAllowed(const Location& location, const char* attributeName, const char* allowedPositionsHint, AstAttr::Context context)
{
    if (allowedPositionsHint)
        report(location, "Attribute '@%s' can only be applied to %s", attributeName, allowedPositionsHint);
    else
        report(location, "Attribute '@%s' cannot be applied to %s", attributeName, attributeContextName(context));
}

void Parser::reportNonLiteralAttributeArgs(const AstArray<AstExpr*>& args, const Location& argsLocation)
{
    for (const AstExpr* arg : args)
    {
        if (!isConstantLiteral(arg) && !isLiteralTable(arg))
            report(argsLocation, "Only literals can be passed as arguments for attributes");
    }
}

bool Parser::attributesFollow() const
{
    return lexer.current().type == Lexeme::Attribute || lexer.current().type == Lexeme::AttributeOpen;
}

Location Parser::getAttributeStartLocation(
    const AstArray<AstAttr*>& attributes,
    const TempVector<CstAttrList*>* cstAttrLists,
    const Location& defaultLocation
)
{
    LUAU_ASSERT(FFlag::LuauCstAttr);
    if (attributes.size > 0)
    {
        if (cstAttrLists && cstAttrLists->size() > 0)
        {
            Location firstAttrLocation = attributes.data[0]->location;
            const Position atBracketPosition = (*cstAttrLists)[0]->atBracketPosition;

            if (firstAttrLocation.begin < atBracketPosition)
                return firstAttrLocation;
            else
                return Location(atBracketPosition, atBracketPosition);
        }
        else
            return attributes.data[0]->location;
    }
    else if (cstAttrLists && cstAttrLists->size() > 0)
    {
        const Position atBracketPosition = (*cstAttrLists)[0]->atBracketPosition;
        return Location(atBracketPosition, atBracketPosition);
    }
    else
        return defaultLocation;
}

// attributes local function Name funcbody
// attributes function funcname funcbody
// attributes `declare function' Name`(' [parlist] `)' [`:` Type]
// declare Name '{' Name ':' attributes `(' [parlist] `)' [`:` Type] '}'
AstStat* Parser::parseAttributeStat()
{
    const Location startLocation = lexer.current().location;

    AstArray<AstAttr*> attributes;
    TempVector<CstAttrList*> cstAttrLists(scratchCstAttrList);
    attributes = parseAttributes(AstAttr::Context::Statement, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);

    const Location attributeStart = FFlag::LuauCstAttr ? getAttributeStartLocation(attributes, &cstAttrLists, startLocation)
                                                       : (attributes.size > 0 ? attributes.data[0]->location : startLocation);

    Lexeme::Type type = lexer.current().type;

    switch (type)
    {
    case Lexeme::Type::ReservedFunction:
        validateAttributeContexts(attributes, AstAttr::Context::Function);
        return parseFunctionStat(attributes, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);
    case Lexeme::Type::ReservedLocal:
        return parseLocal(attributeStart, lexer.current().location, attributes, false, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);
    case Lexeme::Type::Name:
    {
        if (FFlag::LuauExportValueSyntax && AstName(lexer.current().name) == "export")
        {
            Location keywordLoc = lexer.current().location;
            nextLexeme();
            return parseExportValue(attributeStart, keywordLoc, attributes, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);
        }

        if (strcmp("const", lexer.current().data) == 0)
        {
            Location keywordLoc = lexer.current().location;
            nextLexeme();
            return parseLocal(attributeStart, keywordLoc, attributes, true, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);
        }
        if (declarationsAllowed() && !strcmp("declare", lexer.current().data))
        {
            // A declared function has no body, so it is a function but never an inlinable one.
            validateAttributeContexts(attributes, AstAttr::Context::Function);

            AstExpr* expr = parsePrimaryExpr(/* asStatement= */ true);
            return parseDeclaration(expr->location, attributes);
        }

        // `type`, `class` and an assignment target all begin with a Name, so they are told apart the
        // way parseStat does it: parse the primary expression and look at what it turned out to be.
        // The order matters and matches parseStat's -- `type = 5` is an assignment to a global named
        // `type`, not a malformed type alias.
        if (FFlag::LuwuAttributesEverywhere)
        {
            AstExpr* expr = parsePrimaryExpr(/* asStatement= */ true);

            if (lexer.current().type == ',' || lexer.current().type == '=')
            {
                validateAttributeContexts(attributes, AstAttr::Context::Assignment);

                AstStat* node = parseAssignment(expr);
                node->location = Location(attributeStart, node->location);
                if (AstStatAssign* assign = node->as<AstStatAssign>())
                    assign->attributes = attributes;
                return node;
            }

            AstName ident = getIdentifier(expr);

            if (ident == "type")
            {
                // A type function is not a position any attribute allows, and it has nowhere to keep
                // them, so they are refused rather than dropped.
                if (lexer.current().type == Lexeme::ReservedFunction)
                    report(Location(attributeStart, expr->location), "Attributes cannot be applied to a type function");
                else
                    validateAttributeContexts(attributes, AstAttr::Context::TypeAlias);

                return parseTypeAlias(attributeStart, /* exported= */ false, expr->location.begin, expr->location, attributes);
            }

            if (ident == "class" && FFlag::LuwuClasses)
            {
                validateAttributeContexts(attributes, AstAttr::Context::Class);
                return parseClassStat(attributeStart, /* exported= */ false, expr->location, attributes);
            }

            if (ident == "trait" && traitsEnabled())
            {
                validateAttributeContexts(attributes, AstAttr::Context::Class);
                return parseClassStat(attributeStart, /* exported= */ false, expr->location, attributes, /* declared= */ false, /* isTrait= */ true);
            }

            // The expression is already consumed, so name what was attributed rather than quoting a
            // token from past the end of it.
            return reportStatError(
                expr->location,
                copy({expr}),
                {},
                "Expected 'function', 'local function', 'const function', 'declare function', 'type', 'class' or an assignment after attribute"
            );
        }

        // Without LuwuAttributesEverywhere, any other name is reported by the default case.
        LUAU_FALLTHROUGH;
    }
    default:
        return reportStatError(
            lexer.current().location,
            {},
            {},
            FFlag::LuwuAttributesEverywhere
                ? "Expected 'function', 'local function', 'const function', 'declare function', 'type', 'class', an assignment or a function "
                  "type declaration after attribute, but got %s instead"
                : "Expected 'function', 'local function', 'const function', 'declare function' or a function type declaration after attribute, "
                  "but got %s instead",
            lexer.current().toString().c_str()
        );
    }
}

bool isEnoughValues(TempVector<AstExpr*>& values, size_t expected)
{
    if (values.size() > 0)
    {
        AstExpr* last = values.back();
        if (last->is<AstExprCall>() || last->is<AstExprVarargs>())
            return true;
    }
    return values.size() == expected;
}

AstStat* Parser::parseLocal(
    const Location start,
    const Location& keywordLocation,
    const AstArray<AstAttr*>& attributes,
    bool isConst,
    TempVector<CstAttrList*>* cstAttrLists
)
{
    LUAU_ASSERT(cstAttrLists != nullptr ? FFlag::LuauCstAttr : true);

    const Position keywordPosition = keywordLocation.begin;

    // `start` is the start of the whole statement, which with attributes is the first `@` rather
    // than the keyword. The caller has already consumed a `const` keyword, but not a `local` one.
    const Location localKeywordLocation = isConst ? keywordLocation : lexer.current().location;

    if (!isConst)
        nextLexeme(); // local

    if (lexer.current().type == Lexeme::ReservedFunction)
    {
        // A local or const function is resolved at its call sites, so it is one the compiler can inline.
        validateAttributeContexts(attributes, AstAttr::Context::InlinableFunction);

        Lexeme matchFunction = lexer.current();
        nextLexeme();

        Position functionKeywordPosition = matchFunction.location.begin;
        Location functionKeywordLocation = matchFunction.location;

        // The closing 'end' indentation-mismatch diagnostic matches against the column where
        // 'local'/'const' starts rather than 'function', so it gets a patched copy of the token.
        // Luwu: upstream patches `matchFunction` itself, which parseFunctionBody also takes the
        // AstExprFunction's start from, so upstream's function expression starts at 'local'/'const'.
        // Luwu's starts at 'function', so keyword hovers can tell the tokens apart by location.
        Lexeme endMatchLexeme = matchFunction;
        if (endMatchLexeme.location.begin.line == start.begin.line)
            endMatchLexeme.location.begin.column = start.begin.column;

        Name name = parseName("variable name");

        matchRecoveryStopOnToken[Lexeme::ReservedEnd]++;

        auto [body, var] = parseFunctionBody(false, matchFunction, name.name, &name, attributes, isConst, nullptr, &endMatchLexeme);

        matchRecoveryStopOnToken[Lexeme::ReservedEnd]--;

        Location location{start.begin, body->location.end};

        AstStatLocalFunction* node = allocator.alloc<AstStatLocalFunction>(
            location,
            var,
            body,
            isConst,
            isConst && FFlag::LuauStoreConstKeywordBegin ? keywordPosition : Position::missing(),
            start,
            functionKeywordLocation
        );
        if (options.storeCstData)
        {
            cstNodeMap[node] = FFlag::LuauCstAttr && cstAttrLists != nullptr
                                   ? allocator.alloc<CstStatLocalFunction>(copy(*cstAttrLists), keywordPosition, functionKeywordPosition)
                                   : allocator.alloc<CstStatLocalFunction>(keywordPosition, functionKeywordPosition);
        }
        return node;
    }
    else
    {
        if (destructuringFollows())
        {
            if (attributes.size != 0)
                report(attributes.data[0]->location, "Attributes can't be applied to a destructuring declaration");

            return parseDestructuring(start, localKeywordLocation, isConst);
        }

        // The attributes were parsed before we knew this was a plain binding rather than a local
        // function, so this is where they get checked against the position they landed on.
        if (attributes.size != 0)
        {
            if (!FFlag::LuwuAttributesEverywhere)
            {
                return reportStatError(
                    lexer.current().location,
                    {},
                    {},
                    "Expected 'function' after local declaration with attribute, but got %s instead",
                    lexer.current().toString().c_str()
                );
            }

            validateAttributeContexts(attributes, AstAttr::Context::Local);
        }

        matchRecoveryStopOnToken['=']++;

        TempVector<Binding> names(scratchBinding);
        AstArray<Position> varsCommaPositions;
        if (options.storeCstData)
            parseBindingList(names, false, false, &varsCommaPositions, nullptr, nullptr, isConst);
        else
            parseBindingList(names, false, false, nullptr, nullptr, nullptr, isConst);

        matchRecoveryStopOnToken['=']--;

        TempVector<AstLocal*> vars(scratchLocal);

        TempVector<AstExpr*> values(scratchExpr);
        TempVector<Position> valuesCommaPositions(scratchPosition);

        std::optional<Location> equalsSignLocation;

        if (lexer.current().type == '=')
        {
            equalsSignLocation = lexer.current().location;

            nextLexeme();

            parseExprList(values, options.storeCstData ? &valuesCommaPositions : nullptr);
        }

        for (size_t i = 0; i < names.size(); ++i)
            vars.push_back(pushLocal(names[i]));

        Location end = values.empty() ? lexer.previousLocation() : values.back()->location;

        AstStatLocal* node = allocator.alloc<AstStatLocal>(Location(start, end), copy(vars), copy(values), equalsSignLocation, isConst);
        // Only when attributes are present do `start` and the keyword differ; every other path
        // passes the keyword's own location as `start`, so this keeps those byte-identical.
        node->keywordLocation = attributes.size > 0 ? localKeywordLocation : start;
        node->attributes = attributes;
        if (options.storeCstData)
        {
            cstNodeMap[node] = allocator.alloc<CstStatLocal>(extractAnnotationColonPositions(names), varsCommaPositions, copy(valuesCommaPositions));
        }

        // It is a syntax error when a const declaration *definitely* does
        // not have enough values, for example:
        //
        //  const foo
        //  const bar, baz = 42
        //
        // Both error as there's probably user error (`foo` and `baz` can
        // only ever be `nil`). We report an error but return the
        // declaration as-is, as it's still reasonable syntactically.
        if (isConst && !isEnoughValues(values, vars.size()))
            report(node->location, "Missing initializer in const declaration");

        return node;
    }
}

// Luwu Destructuring (rfcs/destructuring.md): `.{` or `name.{` right after `local`/`const`. Neither `name.` nor
// `{` can start anything else there, so both are parsed as a (malformed) destructuring for a useful error.
bool Parser::destructuringFollows()
{
    if (lexer.current().type == '{')
        return true;

    if (lexer.current().type == '.')
        return destructurePatternFollows();

    return lexer.current().type == Lexeme::Name && lexer.lookahead().type == '.';
}

// Luwu Destructuring (rfcs/destructuring.md): `.{`, which starts a pattern wherever it appears.
bool Parser::destructurePatternFollows()
{
    return lexer.current().type == '.' && lexer.lookahead().type == '{';
}

// Luwu Destructuring (rfcs/destructuring.md): the declaration desugars to one `local`/`const` per binding, in
// source order. The value goes to the named local, or to a hidden one, and each field is read from there:
//
//   const fs.{readfile as rf, path} = require("@std/fs")
//
//   const fs = require("@std/fs")
//   const rf = fs.readfile
//   const path = fs.path
//
// The first statement is returned; the rest are left in pendingStatements for parseBlockNoScope.
AstStat* Parser::parseDestructuring(const Location& start, const Location& keywordLocation, bool isConst, std::optional<Name> name)
{
    DestructureTarget target;
    target.name = name;
    if (!target.name && lexer.current().type == Lexeme::Name)
        target.name = parseName("variable name");

    // JavaScript's `local {x, y} = t`
    if (lexer.current().type == '{')
        report(lexer.current().location, "Destructuring needs a '.' before '{': 'local .{x, y} = t'");

    parseDestructurePattern(target);

    if (lexer.current().type == ':')
    {
        nextLexeme();
        target.annotation = parseType();
    }

    Location equalsLocation = lexer.current().location;
    AstExpr* value = nullptr;

    if (lexer.current().type == '=')
    {
        nextLexeme();
        value = parseExpr();
    }
    else
    {
        value = reportExprError(lexer.current().location, {}, "Expected '=' and a value after the destructuring pattern");
    }

    if (lexer.current().type == ',')
    {
        Location extra = lexer.current().location;
        TempVector<AstExpr*> rest(scratchExpr);
        nextLexeme();
        parseExprList(rest);
        report(Location(extra, lexer.previousLocation()), "A destructuring declaration takes exactly one value");
    }

    Location location(start, value->location);

    if (!FFlag::LuwuDestructuring)
        report(location, "Destructuring is a Luwu feature; enable the 'LuwuDestructuring' fast flag to use it");

    std::vector<AstStat*> statements;
    AstDestructurePattern pattern = desugarDestructuring(target, value, location, equalsLocation, isConst, statements);

    AstStatLocal* first = statements.front()->as<AstStatLocal>();
    LUAU_ASSERT(first);
    first->keywordLocation = keywordLocation;
    first->destructure = allocator.alloc<AstDestructurePattern>(pattern);

    // Upstream's destructuring RFC (luau-lang/rfcs#260) is still open, so none of this is Luau syntax yet.
    for (AstStat* statement : statements)
    {
        statement->luwuOnly = true;

        if (statement != first)
            statement->as<AstStatLocal>()->destructuredFrom = first;
    }

    pendingStatements.insert(pendingStatements.end(), statements.begin() + 1, statements.end());

    return first;
}

// Parses `.{ fieldlist }` into `target`, starting at the `.` (or at the `{`, when the caller already reported
// the missing `.`). A malformed field gets one error that names the form that works, and parsing resumes at the
// next field so the rest of the pattern still binds.
void Parser::parseDestructurePattern(DestructureTarget& target)
{
    unsigned int oldRecursionCount = recursionCounter;
    incrementRecursionCounter("destructuring");

    Location begin = lexer.current().location;
    if (lexer.current().type == '.')
        nextLexeme();

    Lexeme open = lexer.current();
    expectAndConsume('{', "destructuring");

    if (lexer.current().type == '}')
        report(Location(begin, lexer.current().location), "A destructuring pattern needs at least one field");

    while (lexer.current().type != '}' && lexer.current().type != Lexeme::Eof)
    {
        if (lexer.current().type != Lexeme::Name)
        {
            reportDestructureKeyError();
            skipDestructureField();
        }
        else
        {
            target.fields.push_back(parseDestructureField());
        }

        if (lexer.current().type == ',')
        {
            nextLexeme();
            continue;
        }

        if (lexer.current().type == '}' || lexer.current().type == Lexeme::Eof)
            break;

        // Two fields with no comma between them: report it and carry on with the next field
        if (lexer.current().type == Lexeme::Name)
        {
            report(lexer.current().location, "Expected ',' between destructured fields, got '%s'", lexer.current().name);
            continue;
        }

        report(lexer.current().location, "Expected ',' or '}' after a destructured field, got %s", lexer.current().toString().c_str());
        skipDestructureField();

        if (lexer.current().type == ',')
            nextLexeme();
    }

    target.patternLocation = Location(begin, lexer.current().location);
    target.closed = expectMatchAndConsume('}', open);

    recursionCounter = oldRecursionCount;
}

// field ::= Name [`.' `{' fieldlist `}'] | Name `as' target
Parser::DestructureField Parser::parseDestructureField()
{
    DestructureField field{parseName("field name"), std::nullopt, {}};
    const char* key = field.key.name.value;

    if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "as")
    {
        field.asLocation = lexer.current().location;
        nextLexeme();

        if (lexer.current().type == Lexeme::Name)
        {
            field.target.name = parseName("variable name");
        }
        else if (lexer.current().type != '.')
        {
            report(lexer.current().location, "Expected a name or '.{' after 'as', got %s", lexer.current().toString().c_str());
            // Bind the field under its own name so the rest of the declaration still parses, placed in the gap after
            // `as` where the name will be written, so tools see a cursor there as being on a binding name
            field.target.name = Name(field.key.name, Location(field.asLocation->end, lexer.current().location.begin));
        }
    }
    else
    {
        field.target.name = field.key;
    }

    if (lexer.current().type == '.' && lexer.lookahead().type == Lexeme::Name)
    {
        // `key.inner`: a path, which a pattern doesn't have
        const char* inner = lexer.lookahead().name;
        report(
            Location(field.key.location, lexer.lookahead().location),
            "To take '%s' out of '%s', write '%s.{%s}' to bind both, or '%s as .{%s}' to bind only '%s'",
            inner,
            key,
            key,
            inner,
            key,
            inner,
            inner
        );
        skipDestructureField();

        // `key as .inner` reaches here with nothing to bind the field to
        if (!field.target.name)
            field.target.name = field.key;

        return field;
    }

    if (lexer.current().type == '.')
        parseDestructurePattern(field.target);

    if (lexer.current().type == ':')
    {
        nextLexeme();
        field.target.annotation = parseType();

        // `key: T as name`: the annotation types what `as` binds, so it goes after it
        bool asAfterAnnotation = !field.asLocation && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "as";
        if (asAfterAnnotation)
        {
            Location as = lexer.current().location;
            nextLexeme();
            std::optional<Name> renamed = parseNameOpt("variable name");
            report(
                Location(as, lexer.previousLocation()),
                "An annotation goes after the name it types: '%s as %s: T'",
                key,
                renamed ? renamed->name.value : "name"
            );
            if (renamed)
                field.target.name = renamed;
        }
    }

    if (lexer.current().type == '=')
    {
        // A default value, which destructuring doesn't have: parse it so it doesn't cascade into more errors
        Location equals = lexer.current().location;
        nextLexeme();
        AstExpr* defaultValue = parseExpr();
        report(
            Location(equals, defaultValue->location),
            "Destructured fields can't have default values; a missing field reads as whatever indexing the value gives"
        );
    }

    return field;
}

// Something other than a name where a field's key goes.
void Parser::reportDestructureKeyError()
{
    const Lexeme& current = lexer.current();

    bool nestedWithoutField = current.type == '{' || current.type == '.';
    bool otherKey =
        current.type == '[' || current.type == Lexeme::QuotedString || current.type == Lexeme::RawString || current.type == Lexeme::Number;

    if (current.type == Lexeme::Dot3)
        report(current.location, "Destructuring has no rest pattern; bind the value with a name to keep the rest: 'local t.{x} = ...'");
    else if (nestedWithoutField)
        report(current.location, "A nested pattern needs the field it destructures: 'field.{...}' or 'field as .{...}'");
    else if (otherKey)
        report(current.location, "Destructured fields are names; read any other key by indexing the value: 'local v = t[key]'");
    else
        report(current.location, "Expected a field name in the destructuring pattern, got %s", current.toString().c_str());
}

// Skips to the `,` or `}` that ends the current field, stepping over anything bracketed inside it.
void Parser::skipDestructureField()
{
    int depth = 0;

    while (lexer.current().type != Lexeme::Eof)
    {
        Lexeme::Type type = lexer.current().type;

        if (depth == 0 && (type == ',' || type == '}'))
            return;

        if (type == '{' || type == '(' || type == '[')
            depth++;
        else if (type == '}' || type == ')' || type == ']')
            depth--;

        nextLexeme();
    }
}

AstDestructurePattern Parser::desugarDestructuring(
    const DestructureTarget& target,
    AstExpr* value,
    const Location& location,
    const Location& equalsLocation,
    bool isConst,
    std::vector<AstStat*>& out
)
{
    AstLocal* local = nullptr;

    if (target.name)
    {
        local = pushLocal(Binding(*target.name, target.annotation, Position{0, 0}, isConst));
    }
    else
    {
        // Code can't name the hidden local, so it never enters the scope: nothing can shadow it or be
        // shadowed by it, and the linter has nothing to report about it.
        local = allocator.alloc<AstLocal>(
            nameDestructured,
            target.patternLocation.value_or(location),
            /* shadow= */ nullptr,
            functionStack.size() - 1,
            functionStack.back().loopDepth,
            target.annotation,
            isConst
        );
    }

    out.push_back(allocator.alloc<AstStatLocal>(location, copy({local}), copy({value}), equalsLocation, isConst));

    AstDestructurePattern pattern;
    pattern.local = local;
    pattern.location = target.patternLocation;
    pattern.closed = target.closed;

    TempVector<AstDestructureField> fields(scratchDestructureField);

    for (const DestructureField& field : target.fields)
    {
        Location keyLocation = field.key.location;
        AstExpr* object = allocator.alloc<AstExprLocal>(keyLocation, local, /* upvalue= */ false);
        AstExpr* read = allocator.alloc<AstExprIndexName>(keyLocation, object, field.key.name, keyLocation, keyLocation.begin, '.');

        Position end = keyLocation.end;
        if (field.target.annotation)
            end = field.target.annotation->location.end;
        else if (field.target.patternLocation)
            end = field.target.patternLocation->end;
        else if (field.target.name)
            end = field.target.name->location.end;

        AstDestructurePattern fieldTarget = desugarDestructuring(field.target, read, Location(keyLocation.begin, end), keyLocation, isConst, out);
        fields.push_back(AstDestructureField{field.key.name, keyLocation, field.asLocation, fieldTarget});
    }

    pattern.fields = copy(fields);
    return pattern;
}

// return [explist]
AstStat* Parser::parseReturn()
{
    Location start = lexer.current().location;

    nextLexeme();

    TempVector<AstExpr*> list(scratchExpr);
    TempVector<Position> commaPositions(scratchPosition);

    if (!blockFollow(lexer.current()) && lexer.current().type != ';')
        parseExprList(list, options.storeCstData ? &commaPositions : nullptr);

    Location end = list.empty() ? start : list.back()->location;

    AstStatReturn* node = allocator.alloc<AstStatReturn>(Location(start, end), copy(list), start);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstStatReturn>(copy(commaPositions));

    if (FFlag::LuauExportValueSyntax && functionStack.size() == 1)
    {
        if (!declaredExportBindings.empty())
            report(node->location, "Exporting values is not compatible with top-level return (export/return conflict)");

        hasModuleReturn = true;
    }

    return node;
}

// type Name [`<' varlist `>'] `=' Type
AstStat* Parser::parseTypeAlias(
    const Location& start,
    bool exported,
    Position typeKeywordPosition,
    const Location& typeKeywordLocation,
    const AstArray<AstAttr*>& attributes
)
{
    // parsing a type function
    if (lexer.current().type == Lexeme::ReservedFunction)
        return parseTypeFunction(start, exported, typeKeywordPosition);

    // parsing a type alias

    // note: `type` token is already parsed for us, so we just need to parse the rest

    std::optional<Name> name = parseNameOpt("type name");

    // Use error name if the name is missing
    if (!name)
        name = Name(nameError, lexer.current().location);
    else
        checkTypeName(*name, /* declared= */ false);

    Position genericsOpenPosition = Position::missing();
    AstArray<Position> genericsCommaPositions;
    Position genericsClosePosition = Position::missing();
    auto [generics, genericPacks] = options.storeCstData
                                        ? parseGenericTypeList(
                                              /* withDefaultValues= */ true, &genericsOpenPosition, &genericsCommaPositions, &genericsClosePosition
                                          )
                                        : parseGenericTypeList(/* withDefaultValues= */ true);

    bool equalsFound = expectAndConsume('=', "type alias");
    Position equalsPosition = equalsFound ? lexer.previousLocation().begin : Position::missing();

    AstType* type = parseType();

    AstStatTypeAlias* node = allocator.alloc<AstStatTypeAlias>(
        Location(start, type->location), name->name, name->location, generics, genericPacks, type, exported, typeKeywordLocation
    );
    node->attributes = attributes;
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstStatTypeAlias>(
            typeKeywordPosition, genericsOpenPosition, genericsCommaPositions, genericsClosePosition, equalsPosition
        );
    return node;
}

namespace
{

const std::unordered_set<std::string> ALLOWED_METAMETHODS{
    "__call",
    "__concat",
    "__unm",
    "__add",
    "__sub",
    "__mul",
    "__div",
    "__mod",
    "__pow",
    "__tostring",
    "__eq",
    "__lt",
    "__le",
    "__iter",
    "__len",
    "__idiv",
};

const std::unordered_set<std::string> EXPLICITLY_DISALLOWED_METAMETHODS{
    "__index",
    "__newindex",
    "__mode",
    "__metatable",
    "__type",
};

} // namespace

// Luwu Classes (rfcs/classes): parse the parameter list of a class's primary constructor, e.g. the
// `(name: string, age = 0)` of `class Cat(name: string, age = 0)`. Each parameter also declares a field
// of the same name, public and mutable unless qualified. The list is compiled into a synthesized `__init`.
LUAU_NOINLINE AstClassPrimaryConstructor* Parser::parseClassPrimaryConstructor(
    const std::optional<Location>& qualifierLocation,
    AstClassMemberVisibility visibility,
    bool declared,
    bool isTrait
)
{
    LUAU_ASSERT(FFlag::LuwuClasses);
    // Luwu Traits (rfcs/classes/traits.md): a trait's parameter list is parsed by this function too
    const char* listName = isTrait ? "A trait's parameter list" : "A class's primary constructor";

    Lexeme matchParen = lexer.current();
    Location start = lexer.current().location;
    expectAndConsume('(', "class primary constructor");

    TempVector<Binding> args(scratchBinding);
    TempVector<AstClassPrimaryConstructorParamQualifiers> argQualifiers(scratchClassParamQualifiers);
    DenseHashSet<AstName> argNames{{}};

    // The parameters and their default values are compiled into the class's synthesized `__init`,
    // which is one function scope deeper than the class declaration. Parse them at that depth, so
    // references to outer locals are marked as upvalues. Field default values do the same
    // dummyFunction push.
    static Function dummyFunction;
    functionStack.emplace_back(dummyFunction);

    while (lexer.current().type != ')')
    {
        if (lexer.current().type == Lexeme::Dot3)
        {
            report(lexer.current().location, "%s cannot be variadic", listName);
            nextLexeme();
        }
        else
        {
            // Luwu Classes (rfcs/classes): a parameter may carry the access specifier and `const`
            // modifier of the field it declares, Kotlin-style:
            // `class SshKey private (public const public_key: string, private const private_key: string)`.
            //
            // `public`, `private` and `const` only count as qualifiers when another name follows them.
            // A parameter named just `public` is read as a parameter name, and then rejected below
            // because fields can't be named after those keywords.
            AstClassPrimaryConstructorParamQualifiers qualifiers;

            // A parameter's attributes may be written on either side of its access specifier, the
            // same rule class members follow, so `@deprecated public x` and `public @deprecated x`
            // both read naturally. parseBinding picks up the ones written after it.
            AstArray<AstAttr*> attributesBeforeQualifier{nullptr, 0};
            if (FFlag::LuwuAttributesEverywhere && attributesFollow())
                attributesBeforeQualifier = parseAttributes(AstAttr::Context::Parameter);

            // A qualifier is only a qualifier when a parameter follows it, and an attribute can only
            // introduce one.
            auto qualifierIntroducesParam = [&]()
            {
                Lexeme::Type next = lexer.lookahead().type;
                return next == Lexeme::Name ||
                       (FFlag::LuwuAttributesEverywhere && (next == Lexeme::Attribute || next == Lexeme::AttributeOpen));
            };

            // `const public x` is the wrong order; the modifier follows the access specifier.
            if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "const" && lexer.lookahead().type == Lexeme::Name &&
                (AstName(lexer.lookahead().name) == "public" || AstName(lexer.lookahead().name) == "private"))
            {
                report(
                    lexer.current().location, "The 'const' modifier must come after the access specifier, e.g. '%s const'", lexer.lookahead().name
                );
                nextLexeme(); // skip the misplaced 'const' and let the access specifier parse normally
            }

            const bool isAccessSpecifier = lexer.current().type == Lexeme::Name && qualifierIntroducesParam() &&
                                           (AstName(lexer.current().name) == "public" || AstName(lexer.current().name) == "private");
            if (isAccessSpecifier)
            {
                qualifiers.qualifierLocation = lexer.current().location;

                if (AstName(lexer.current().name) == "private")
                    qualifiers.visibility = AstClassMemberVisibility::Private;

                nextLexeme();

                // `public @deprecated const x` -- between the access specifier and the `const`
                // modifier is still the specifier's side, which is where a class field accepts it too.
                if (FFlag::LuwuAttributesEverywhere && attributesFollow())
                {
                    AstArray<AstAttr*> afterQualifier = parseAttributes(AstAttr::Context::Parameter);
                    attributesBeforeQualifier = concatAttributes(attributesBeforeQualifier, afterQualifier);
                }
            }

            const bool isConstModifier =
                lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "const" && qualifierIntroducesParam();
            if (isConstModifier)
            {
                qualifiers.constLocation = lexer.current().location;
                qualifiers.isConst = true;
                nextLexeme();
            }

            // a primary constructor's parameters are function parameters that happen to belong to a
            // class, so their defaults ride on the same flag function parameter defaults do
            Binding binding = declared ? parseDeclaredClassBinding(qualifiers.declaredDefaultLocation)
                                       : parseBinding(/* isConst= */ false, /* allowDefault= */ FFlag::LuwuDefaultArguments, /* allowAttributes= */ true);

            if (attributesBeforeQualifier.size > 0)
            {
                if (binding.attributes.size > 0)
                {
                    // Same rule, and same recovery, as a class member's: report it but keep both, so
                    // the parameter behaves as written.
                    report(
                        binding.attributes.data[0]->location,
                        "Attributes on a class member must all be written on the same side of the access specifier"
                    );
                }

                binding.attributes = concatAttributes(attributesBeforeQualifier, binding.attributes);
            }

            // Every parameter declares a field, so the keywords banned from field names are banned here
            // too (rfcs/classes).
            if (isDisallowedClassMemberName(binding.name.name))
                report(binding.name.location, "Fields are not allowed to be named '%s'", binding.name.name.value);

            if (argNames.contains(binding.name.name))
                report(binding.name.location, "Duplicate primary constructor parameter '%s'", binding.name.name.value);
            else
                argNames.insert(binding.name.name);

            args.push_back(binding);
            argQualifiers.push_back(qualifiers);
        }

        if (lexer.current().type != ',')
            break;

        Location commaLocation = lexer.current().location;
        nextLexeme();

        // Parameters follow the same rules as function parameters, which do not allow a trailing
        // comma either -- but say so, rather than reporting a missing parameter name.
        if (lexer.current().type == ')')
        {
            report(commaLocation, "%s cannot have a trailing comma", listName);
            break;
        }
    }

    // The locals are created inside the dummy scope, so the synthesized `__init` sees them as its own
    // parameters. Then they go straight back out of scope. Only field initializer expressions can see
    // them, and those push them again (see pushClassPrimaryConstructorParams).
    unsigned int localsBegin = saveLocals();

    TempVector<AstLocal*> vars(scratchLocal);
    TempVector<AstExpr*> varsDefaults(scratchExpr);

    for (const Binding& binding : args)
    {
        vars.push_back(pushLocal(binding));
        varsDefaults.push_back(binding.defaultValue);
    }

    restoreLocals(localsBegin);
    functionStack.pop_back();

    Location end = lexer.current().location;
    expectMatchAndConsume(')', matchParen);

    AstClassPrimaryConstructor* primaryConstructor = allocator.alloc<AstClassPrimaryConstructor>();
    primaryConstructor->qualifierLocation = qualifierLocation;
    primaryConstructor->visibility = visibility;
    primaryConstructor->args = copy(vars);
    primaryConstructor->argsDefaults = copy(varsDefaults);
    primaryConstructor->argsQualifiers = copy(argQualifiers);
    primaryConstructor->argLocation = Location(start, end);

    return primaryConstructor;
}

bool Parser::traitsEnabled() const
{
    return FFlag::LuwuTraits && FFlag::LuwuClasses;
}

// Luwu Classes (rfcs/classes): does the current token in a class body start a statement rather than
// a member? A member is `name`, `name: T`, `name = expr` or a `function`. A statement keyword, a name
// followed by a call, index or comma, or a declaration (`class Name`, `trait Name`, `export ...`,
// `type Name`) means the class was never closed and we are now eating the code that follows it.
bool Parser::classBodyLooksLikeStatement()
{
    if (lexer.current().type == '(')
        return true;

    switch (lexer.current().type)
    {
    case Lexeme::ReservedLocal:
    case Lexeme::ReservedReturn:
    case Lexeme::ReservedIf:
    case Lexeme::ReservedFor:
    case Lexeme::ReservedWhile:
    case Lexeme::ReservedRepeat:
    case Lexeme::ReservedDo:
        return true;
    case Lexeme::Name:
        break;
    default:
        return false;
    }

    // `print(...)`, `t.field = x` and `a, b = 1, 2` can't be class members. `name:` is not in this
    // list on purpose: it starts a member's type annotation.
    Lexeme::Type next = lexer.lookahead().type;
    if (next == '(' || next == '.' || next == ',' || next == '[')
        return true;

    // Two names on one line are never a member: a field named `trait` followed by another member would be on the
    // next line. Nested declarations aren't allowed, so `trait Name` here is the next statement.
    bool nameFollowsOnSameLine = next == Lexeme::Name && lexer.lookahead().location.begin.line == lexer.current().location.begin.line;
    if (!nameFollowsOnSameLine)
        return false;

    AstName ident(lexer.current().name);
    return ident == "class" || ident == "export" || ident == "type" || (ident == "trait" && traitsEnabled());
}

// Luwu Classes (rfcs/classes): the grammar is also in the RFC's "Class definition syntax" section.
// classStatement ::= [`export'] `class' Name [`<' GenericTypeListWithDefaults `>'] [primaryCtor] [`implements' traitRefList]
//                    {classMember} `end'
// primaryCtor ::= [access] `(' [ctorParam {`,' ctorParam}] `)'
// ctorParam ::= [access] [`const'] Name [`:' Type] [`=' exp]
// classMember ::= [access] [`const'] Name [`:' Type] [`=' exp] [`;']
//              | {attribute} [access] {attribute} `function' Name funcbody [`;']
// access ::= `public' | `private'
// Luwu Traits (rfcs/classes/traits.md): a trait is parsed by the same function.
// traitStatement ::= [`export'] `trait' Name [`<' GenericTypeListWithDefaults `>'] [`(' [ctorParam {`,' ctorParam}] `)']
//                    [`needs' traitRefList] {traitMember} `end'
// traitMember ::= classMember | [access] `final' `function' Name funcbody [`;']
//              | `expect' [access] [`const'] Name [`:' Type] [`;']
//              | `expect' [access] `function' Name [`?'] funcsignature [`;']
LUAU_NOINLINE AstStat* Parser::parseClassStat(
    const Location& start,
    bool exported,
    const Location& classKeywordLocation,
    const AstArray<AstAttr*>& classAttributes,
    bool declared,
    bool isTrait
)
{
    LUAU_ASSERT(FFlag::LuwuClasses);
    LUAU_ASSERT(!isTrait || FFlag::LuwuTraits);
    // what the declaration is called in messages
    const char* kind = isTrait ? "trait" : "class";

    std::optional<Name> name = parseNameOpt("type name");

    // Use error name if the name is missing
    if (!name)
        name = Name(nameError, lexer.current().location);
    else
        checkTypeName(*name, declared);

    AstArray<AstGenericType*> generics{};
    AstArray<AstGenericTypePack*> genericPacks{};
    if (FFlag::LuwuGenericNominals)
    {
        // Luwu Classes (rfcs/classes): a class's generic parameter list may carry defaults, like
        // a type alias's -- `class Box<T = string>`.
        std::tie(generics, genericPacks) = parseGenericTypeList(/* withDefaultValues= */ true);
    }

    // Luwu Classes (rfcs/classes): an optional primary constructor, which may carry an access
    // specifier of its own: `class Cat(name: string)`, `class Account private (holder: User)`.
    // The `(` lookahead is what keeps `public`/`private` here from being confused with the access
    // specifier of the class's first member.
    std::optional<Location> ctorQualifierLocation;
    AstClassMemberVisibility ctorVisibility = AstClassMemberVisibility::Public;

    if (lexer.current().type == Lexeme::Name && lexer.lookahead().type == '(' &&
        (AstName(lexer.current().name) == "public" || AstName(lexer.current().name) == "private"))
    {
        ctorQualifierLocation = lexer.current().location;

        if (AstName(lexer.current().name) == "private")
            ctorVisibility = AstClassMemberVisibility::Private;

        nextLexeme();
    }

    // Luwu Classes (rfcs/classes): no classical inheritance. Upstream Luau spells it `class A extends B`.
    // Here that would parse as a class with two bare fields, `extends` and `B`, so report what's actually
    // wrong for anyone porting upstream classes. Checked both before and after a primary constructor.
    auto rejectExtends = [&]()
    {
        if (lexer.current().type != Lexeme::Name || AstName(lexer.current().name) != "extends" ||
            lexer.lookahead().type != Lexeme::Name)
            return;

        Location extendsLocation = lexer.current().location;
        nextLexeme();
        Location baseLocation = lexer.current().location;
        nextLexeme();

        report(
            Location(extendsLocation, baseLocation),
            "Luwu classes don't support classical inheritance ('extends'); consider composing your classes (holding an object of another "
            "class in your class) or waiting for Luwu traits"
        );
    };

    // Luwu Classes (rfcs/classes): `implements` is reserved in the class header for traits. Like
    // `extends`, it would otherwise parse as a list of bare fields. The whole interface list is consumed
    // so it produces only one error.
    auto rejectImplements = [&]()
    {
        if (FFlag::LuwuTraits)
            return;

        if (lexer.current().type != Lexeme::Name || AstName(lexer.current().name) != "implements" ||
            lexer.lookahead().type != Lexeme::Name)
            return;

        Location implementsLocation = lexer.current().location;
        nextLexeme();
        Location lastLocation = lexer.current().location;
        nextLexeme();

        while (lexer.current().type == ',' && lexer.lookahead().type == Lexeme::Name)
        {
            nextLexeme();
            lastLocation = lexer.current().location;
            nextLexeme();
        }

        report(Location(implementsLocation, lastLocation), "The 'implements' keyword has not yet been implemented");
    };

    rejectExtends();
    rejectImplements();

    AstClassPrimaryConstructor* primaryConstructor = nullptr;

    if (lexer.current().type == '(')
        primaryConstructor = parseClassPrimaryConstructor(ctorQualifierLocation, ctorVisibility, declared, isTrait);

    if (primaryConstructor)
    {
        rejectExtends();
        rejectImplements();
    }

    // Luwu Traits (rfcs/classes/traits.md): a trait is never constructed, so its parameter list takes no access specifier.
    if (isTrait && ctorQualifierLocation)
        report(*ctorQualifierLocation, "A trait's parameter list can't be public or private");

    // Luwu Traits (rfcs/classes/traits.md): `class C(...) implements A, B("arg")` and `trait T(...) needs A, B`. Both are
    // contextual keywords, recognized only where a name follows.
    AstArray<AstClassTraitRef> implements{nullptr, 0};
    AstArray<AstClassTraitRef> needs{nullptr, 0};
    std::optional<Location> implementsKeywordLocation;
    std::optional<Location> needsKeywordLocation;

    auto contextualKeywordListFollows = [&](const char* keyword)
    {
        return FFlag::LuwuTraits && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == keyword &&
               lexer.lookahead().type == Lexeme::Name;
    };

    if (contextualKeywordListFollows("implements"))
    {
        Location implementsLocation = lexer.current().location;
        nextLexeme();

        if (isTrait)
        {
            report(implementsLocation, "Traits can't implement traits; did you mean 'needs'?");
            needsKeywordLocation = implementsLocation;
            needs = parseClassTraitRefs(/* allowArgs= */ false, primaryConstructor);
        }
        else
        {
            implementsKeywordLocation = implementsLocation;
            implements = parseClassTraitRefs(/* allowArgs= */ true, primaryConstructor);
        }
    }
    else if (contextualKeywordListFollows("needs"))
    {
        Location needsLocation = lexer.current().location;
        nextLexeme();

        if (!isTrait)
        {
            report(needsLocation, "Classes can't need traits; did you mean 'implements'?");
            implementsKeywordLocation = needsLocation;
            implements = parseClassTraitRefs(/* allowArgs= */ true, primaryConstructor);
        }
        else
        {
            needsKeywordLocation = needsLocation;
            needs = parseClassTraitRefs(/* allowArgs= */ false, primaryConstructor);
        }
    }

    // Every parameter declares a field of the same name, so a *method* with that name collides with it.
    // A *field* with that name doesn't: restating a parameter's field in the class body is how the RFC
    // gives it an access specifier or modifier. So parameters are kept out of classMemberNamespace, and a
    // restated field is added to it like any other member.
    DenseHashSet<AstName> primaryConstructorParams{{}};

    if (primaryConstructor)
    {
        for (AstLocal* arg : primaryConstructor->args)
            primaryConstructorParams.insert(arg->name);
    }

    // Luwu Classes (rfcs/classes): a parameter may carry its field's access specifier and `const`
    // modifier directly (`class SshKey(public const public_key: string)`). Restating such a field in
    // the class body is allowed, but the restatement has to agree with the parameter, and once
    // *anything* in the class carries an access specifier, everything must.
    bool sawQualifiedParam = false;
    bool sawPrivateMember = false;

    if (primaryConstructor)
    {
        for (const AstClassPrimaryConstructorParamQualifiers& qualifiers : primaryConstructor->argsQualifiers)
        {
            if (qualifiers.qualifierLocation)
                sawQualifiedParam = true;

            if (qualifiers.visibility == AstClassMemberVisibility::Private)
                sawPrivateMember = true;
        }
    }

    // Index of the primary constructor parameter a class body member restates, or -1.
    auto findPrimaryConstructorParam = [&](const AstName& memberName) -> int
    {
        if (!primaryConstructor)
            return -1;

        for (size_t i = 0; i < primaryConstructor->args.size; ++i)
            if (primaryConstructor->args.data[i]->name == memberName)
                return int(i);

        return -1;
    };

    // Not pushed as a local: this is what makes hoisted classes work.
    AstLocal* nameLocal =
        allocator.alloc<AstLocal>(name->name, name->location, nullptr, functionStack.size() - 1, functionStack.back().loopDepth, nullptr, true);

    TempVector<AstClassMember> declarations(scratchClassDeclarations);

    // TODO: This does not seem particularly performant, but we need to
    // establish the invariant that properties and methods share a
    // namespace, so writing something like:
    //
    //  class Foo
    //      public x
    //      function x() end
    //  end
    //
    // ... must fail. This gets the job done but maybe we can do something
    // slightly more performant here (e.g.: a "scratch" set).
    DenseHashSet<AstName> classMemberNamespace{{}};

    // Members without an access specifier, collected while parsing the body. Whether they are errors
    // depends on whether any other member has a specifier, which is only known once the whole class is
    // parsed (see the all-or-nothing check after the body, rfcs/classes).
    std::vector<std::pair<Location, bool>> unqualifiedMemberLocations; // (location, isFunction)
    std::vector<Location> explicitPublicQualifierLocations;
    // Primary constructor parameters whose field the class body restates with an explicit access
    // specifier: that is the other place a parameter's field may be qualified.
    DenseHashSet<AstName> paramsQualifiedInBody{{}};

    // Set once we conclude the class was never closed, so the trailing `end` isn't reported missing a
    // second time.
    bool unterminated = false;

    while (lexer.current().type != Lexeme::ReservedEnd && lexer.current().type != Lexeme::Eof)
    {
        // A class body and a statement list overlap: `const x = f()` is valid as either. So a class
        // missing its `end` swallows the statements after it, and the error shows up at whichever token
        // finally isn't a member, often many lines away. When the current token reads as a statement,
        // report the missing `end` here and hand the rest of the file back to the enclosing block.
        if (classBodyLooksLikeStatement())
        {
            report(
                lexer.current().location,
                "Expected 'end' (to close '%s' at line %d), got %s",
                kind,
                classKeywordLocation.begin.line + 1,
                lexer.current().toString().c_str()
            );
            unterminated = true;
            break;
        }

        std::optional<Location> qualifierLocation;
        AstClassMemberVisibility visibility = AstClassMemberVisibility::Public;

        // Luwu Classes (rfcs/classes): a class method may carry attributes, like a free function:
        // `@native function update(self) end`. They can go on either side of the access specifier.
        // `@native private function` reads well when the attribute has its own line, and
        // `private @native function` when it doesn't. They can't go on both sides at once, so a reader
        // only has to look in one place.
        AstArray<AstAttr*> attributes{nullptr, 0};
        TempVector<CstAttrList*> cstAttrLists(scratchCstAttrList);

        auto memberAttributesFollow = [&]()
        {
            return lexer.current().type == Lexeme::Attribute || lexer.current().type == Lexeme::AttributeOpen;
        };

        auto parseMemberAttributes = [&]()
        {
            attributes = parseAttributes(AstAttr::Context::ClassMember, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);
        };

        if (memberAttributesFollow())
            parseMemberAttributes();

        // Luwu Traits (rfcs/classes/traits.md): `expect` leads a trait member that implementing classes must declare
        // themselves, before its access specifier: `expect private model: Model`, `expect public function fire(self)`.
        // It is a keyword only where a member follows it, so `expect: number` is a field named `expect`.
        std::optional<Location> expectLocation;

        bool expectFollows = FFlag::LuwuTraits && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "expect" &&
                             (lexer.lookahead().type == Lexeme::Name || lexer.lookahead().type == Lexeme::ReservedFunction);
        if (expectFollows)
        {
            expectLocation = lexer.current().location;

            if (!isTrait)
                report(*expectLocation, "Only traits can 'expect' members");

            nextLexeme();
        }

        // Luwu Classes (rfcs/classes): functions in a class are always const, so `const function` is rejected
        // rather than being read as a field -- it's valid outside a class, and easy to paste into one. The `const`
        // is dropped so the function itself still parses.
        auto rejectConstFunction = [&]()
        {
            if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "const" &&
                lexer.lookahead().type == Lexeme::ReservedFunction)
            {
                report(lexer.current().location, "Functions in a %s are always const; remove 'const' here", kind);
                nextLexeme();
            }
        };

        rejectConstFunction();

        if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "const")
        {
            const Lexeme& next = lexer.lookahead();
            if (next.type == Lexeme::Name && (AstName(next.name) == "public" || AstName(next.name) == "private"))
            {
                report(lexer.current().location, "The 'const' modifier must come after the access specifier, e.g. '%s const'", next.name);
                nextLexeme(); // skip the misplaced 'const' and let the access specifier parse normally
            }
        }

        // An access specifier only counts as one when a member follows it. `public: number` is a member
        // *named* `public`. The RFC disallows that name, and reporting it is clearer than reporting a
        // missing field name (rfcs/classes).
        auto qualifierIntroducesMember = [&]()
        {
            Lexeme::Type next = lexer.lookahead().type;
            // An attribute can only introduce a method, so it counts as a member following the
            // specifier: `private @native function ...`.
            return next == Lexeme::Name || next == Lexeme::ReservedFunction || next == Lexeme::Attribute || next == Lexeme::AttributeOpen;
        };

        if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "public" && qualifierIntroducesMember())
        {
            qualifierLocation = lexer.current().location;
            explicitPublicQualifierLocations.push_back(*qualifierLocation);
            nextLexeme();
        }
        else if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "private" &&
                 qualifierIntroducesMember())
        {
            qualifierLocation = lexer.current().location;
            visibility = AstClassMemberVisibility::Private;
            sawPrivateMember = true;
            nextLexeme();
        }

        // `public @native function` -- the attributes may equally well follow the access specifier.
        if (qualifierLocation && memberAttributesFollow())
        {
            if (attributes.size > 0)
            {
                report(lexer.current().location, "Attributes on a %s member must all be written on the same side of the access specifier", kind);

                // Report and keep going: both lists are still kept on the function, so the member
                // behaves as written and the CST has an entry for every attribute it prints.
                AstArray<AstAttr*> after = parseAttributes(AstAttr::Context::ClassMember, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);
                attributes = concatAttributes(attributes, after);
            }
            else
                parseMemberAttributes();
        }

        // `public const function`
        if (qualifierLocation)
            rejectConstFunction();

        // Luwu Traits (rfcs/classes/traits.md): `final` follows the access specifier, like `const`. A final function can't
        // be defined by an implementing class.
        std::optional<Location> finalLocation;

        bool finalFollows = FFlag::LuwuTraits && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "final" &&
                            (lexer.lookahead().type == Lexeme::Name || lexer.lookahead().type == Lexeme::ReservedFunction);
        if (finalFollows)
        {
            finalLocation = lexer.current().location;

            if (!isTrait)
                report(*finalLocation, "Only trait members can be 'final'");
            else if (expectLocation)
                report(*finalLocation, "Expected members can't be 'final'");

            nextLexeme();
        }

        // Attributes are parsed before the member kind is known, and both members can carry them, so
        // the only shapes rejected here are the ones that are no member at all -- the class's own
        // `end` above all, which must not be parsed as a field name or it swallows the rest of the
        // file. The attributes were consumed either way, so this makes progress.
        const bool attributedMemberFollows =
            lexer.current().type == Lexeme::ReservedFunction || (FFlag::LuwuAttributesEverywhere && lexer.current().type == Lexeme::Name);
        if (attributes.size > 0 && !attributedMemberFollows)
        {
            if (FFlag::LuwuAttributesEverywhere)
            {
                report(
                    lexer.current().location,
                    "Expected 'function' or a field name after attribute, but got %s instead",
                    lexer.current().toString().c_str()
                );
            }
            else
            {
                report(lexer.current().location, "Expected 'function' after attribute, but got %s instead", lexer.current().toString().c_str());
            }

            attributes = {nullptr, 0};

            // Without the feature, a field name still parses as a field, with whatever access specifier came
            // with it. Anything else, most often the class's own `end`, goes back around the loop: parsing it
            // as a field would consume the `end` and swallow the rest of the file.
            if (FFlag::LuwuAttributesEverywhere || lexer.current().type != Lexeme::Name)
                continue;
        }

        // Luwu Classes (rfcs/classes): anything but `function` starts a property, qualified or not
        // (upstream requires a qualifier here); whether the class needed qualifiers is checked after
        // the class body.
        if (lexer.current().type != Lexeme::ReservedFunction)
        {
            std::optional<Location> constLocation;
            bool isConst = false;
            // As with the access specifiers above, `const` is only the modifier when a field name
            // follows it; `const = 1` declares a field named `const`.
            if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "const" &&
                lexer.lookahead().type == Lexeme::Name)
            {
                constLocation = lexer.current().location;
                isConst = true;
                nextLexeme();
            }

            if (finalLocation && constLocation)
                report(*constLocation, "'final' fields are already const");

            std::optional<Name> propName = parseNameOpt("class field name");
            if (!propName)
            {
                nextLexeme(); // skip the unexpected token to avoid an infinite loop
                continue;
            }

            if (isDisallowedClassMemberName(propName->name))
                report(propName->location, "Fields are not allowed to be named '%s'", propName->name.value);

            if (!qualifierLocation)
                unqualifiedMemberLocations.push_back({propName->location, /* isFunction */ false});

            // Luwu Classes (rfcs/classes): restating a primary constructor parameter's field in the
            // body is how an *unqualified* parameter gets its access specifier. A parameter that already
            // has one can be restated, but not contradicted: with `public text` in the header and
            // `private text` in the body, a reader couldn't trust either.
            if (int paramIndex = findPrimaryConstructorParam(propName->name); paramIndex >= 0)
            {
                const AstClassPrimaryConstructorParamQualifiers& param = primaryConstructor->argsQualifiers.data[paramIndex];

                if (qualifierLocation)
                    paramsQualifiedInBody.insert(propName->name);

                if (param.qualifierLocation && qualifierLocation && param.visibility != visibility)
                {
                    // Do not inline these into the report call: MSVC miscompiles inlined ternaries
                    // feeding %s in Windows Debug CI (see the identical note further down).
                    const char* declared = param.visibility == AstClassMemberVisibility::Private ? "private" : "public";
                    const char* restated = visibility == AstClassMemberVisibility::Private ? "private" : "public";
                    report(
                        propName->location,
                        "Field '%s' was explicitly marked as %s on line %d, cannot reassign it as %s",
                        propName->name.value,
                        declared,
                        param.qualifierLocation->begin.line + 1,
                        restated
                    );
                }

                if (param.isConst && !isConst)
                    report(
                        propName->location,
                        "Field '%s' was explicitly marked as const on line %d, cannot reassign it as mutable",
                        propName->name.value,
                        param.constLocation->begin.line + 1
                    );
            }

            AstType* propType = nullptr;
            std::optional<Location> typeColonLocation;

            if (lexer.current().type == ':')
            {
                typeColonLocation = lexer.current().location;
                nextLexeme();
                propType = parseType();
            }

            std::optional<Location> equalsLocation;
            AstExpr* defaultValue = nullptr;
            if (declared && lexer.current().type == '=')
            {
                // Luwu Declare Statements (rfcs/declare-statements.md): a declared field's default is written as its type
                equalsLocation = lexer.current().location;
                nextLexeme();

                // `x: T = value` copied from a class: the annotation is the type, and the value is skipped
                if (propType)
                {
                    report(*equalsLocation, "%s", kDeclaredDefaultIsATypeError);
                    parseExpr();
                }
                else
                    propType = parseType();
            }
            else if (lexer.current().type == '=')
            {
                equalsLocation = lexer.current().location;
                nextLexeme();

                // Default value expressions are compiled into the class's constructor (either the
                // user's `__init` or a synthesized one), which is one function scope deeper than
                // the class declaration itself. Parse it at that depth so that references to
                // outer locals are correctly marked as upvalues (see the identical dummyFunction
                // push for default argument expressions elsewhere in this file).
                static Function dummyFunction;
                functionStack.emplace_back(dummyFunction);

                // A field initializer is the only place a primary constructor's parameters are
                // visible; they are not in scope for the class's methods.
                unsigned int localsBegin = saveLocals();

                if (primaryConstructor)
                    pushClassPrimaryConstructorParams(primaryConstructor);

                defaultValue = parseExpr();

                restoreLocals(localsBegin);
                functionStack.pop_back();
            }

            if (strncmp(propName->name.value, "__", 2) == 0)
                report(propName->location, "%s fields cannot start with '__'", isTrait ? "Trait" : "Class");

            if (expectLocation && defaultValue)
                report(*equalsLocation, "Expected fields can't have default values");

            // Luwu Traits (rfcs/classes/traits.md): a final field's value is the trait's default, which nothing else can set
            bool isFinalField = isTrait && finalLocation && !expectLocation;
            if (isFinalField && !defaultValue)
                report(propName->location, "Final field '%s' needs a default value", propName->name.value);
            else if (isFinalField && findPrimaryConstructorParam(propName->name) >= 0)
                report(propName->location, "Final field '%s' can't be a trait parameter", propName->name.value);

            // Luwu Traits (rfcs/classes/traits.md): a trait field the trait provides needs a value to provide. Without one it
            // would be nil in every implementing class, whatever its type says.
            bool traitFieldHasNoValue =
                isTrait && !declared && !expectLocation && !finalLocation && !defaultValue && findPrimaryConstructorParam(propName->name) < 0;
            if (traitFieldHasNoValue)
                report(
                    propName->location, "Trait field '%s' needs a default value; did you mean 'expect %s'?", propName->name.value, propName->name.value
                );

            // Luwu Declare Statements (rfcs/declare-statements.md): a declaration writes every type out
            if (declared && !propType)
                report(propName->location, "Declared class field '%s' needs a type: 'name: T', or 'name = T' if it has a default", propName->name.value);

            // The member turned out to be a field rather than a method, so pin the attributes down
            // to that position now -- `@native` is a method-only attribute.
            if (attributes.size > 0)
                validateAttributeContexts(attributes, AstAttr::Context::ClassField);

            bool hasSemicolon = false;
            if (lexer.current().type == ';')
            {
                nextLexeme();
                hasSemicolon = true;
            }

            if (classMemberNamespace.contains(propName->name))
            {
                report(propName->location, "Duplicate %s member '%s'", kind, propName->name.value);
            }
            else
            {
                classMemberNamespace.insert(propName->name);

                // Either both of these are present or neither are, except for a declared class's `name = T`.
                LUAU_ASSERT((bool)propType == (bool)typeColonLocation || (declared && equalsLocation && !typeColonLocation));
                declarations.push_back(
                    AstClassProperty{
                        qualifierLocation,
                        visibility,
                        propName->name,
                        propName->location,
                        typeColonLocation,
                        propType,
                        hasSemicolon,
                        isConst,
                        constLocation,
                        equalsLocation,
                        defaultValue,
                        attributes,
                        expectLocation,
                        finalLocation,
                    }
                );
            }
        }
        else
        {
            validateAttributeContexts(attributes, AstAttr::Context::InlinableFunction);

            auto matchFunction = lexer.current();
            nextLexeme();

            Name name = parseName("method name");

            // Luwu Traits (rfcs/classes/traits.md): `expect function name?(self)` is an expected function a class may leave out
            bool isOptional = false;
            if (lexer.current().type == '?')
            {
                if (!expectLocation)
                    report(lexer.current().location, "Only expected functions can be optional: 'expect function %s?()'", name.name.value);

                isOptional = expectLocation.has_value();
                nextLexeme();
            }

            // This is a little funky as we pass in a debug name but not a
            // local name. The reason is that // in the declaration:
            //
            //  class Student
            //      public name
            //      function print(self)
            //          print(`Hello, I'm a student and my name is {self.name}`)
            //      end
            //  end
            //
            // ... `print` inside `Student.print` is the _global_ print, and not
            // the class' print. That would be `self.print`.
            matchRecoveryStopOnToken[Lexeme::ReservedEnd]++;

            auto [body, _] = parseFunctionBody(
                false,
                matchFunction,
                name.name,
                nullptr,
                attributes,
                /* isConst= */ false,
                FFlag::LuauCstAttr ? &cstAttrLists : nullptr,
                /* endMatchLexeme= */ nullptr,
                /* isClassFunction= */ true,
                /* signatureOnly= */ declared || expectLocation.has_value()
            );

            matchRecoveryStopOnToken[Lexeme::ReservedEnd]--;

            if (declared)
                checkDeclaredClassMethod(body);

            if (isDisallowedClassMemberName(name.name))
                report(name.location, "Functions are not allowed to be named '%s'", name.name.value);

            if (body->args.size > 0 && body->args.data[0]->name == "self" && body->args.data[0]->annotation != nullptr)
                report(body->args.data[0]->annotation->location, "The 'self' parameter cannot have a type annotation");

            if (strncmp(name.name.value, "__", 2) == 0)
            {
                if (name.name == "__create")
                {
                    // Luwu Traits (rfcs/classes/traits.md): `__create` makes a trait callable, as a factory: `Path("./src")` returns whatever
                    // it returns, usually an object of one of the classes implementing the trait. The trait defines it
                    // itself, and it runs before any object exists, so it takes no `self`. Trait parameters would make
                    // `Trait(...)` read two ways (trait arguments, or a factory call), so a trait has one or the other.
                    if (!isTrait)
                        report(name.location, "Only traits can define '__create'; did you mean '__init'?");
                    else if (expectLocation)
                        report(name.location, "'__create' can't be expected");
                    else if (primaryConstructor)
                        report(name.location, "Traits with parameters can't define '__create'");
                    else if (body->args.size > 0 && body->args.data[0]->name == "self")
                        report(name.location, "'__create' can't take 'self'");
                }
                else if (name.name == "__init" && isTrait && !expectLocation)
                {
                    // Luwu Traits (rfcs/classes/traits.md): construction belongs to the class. A trait's state comes from its field defaults and
                    // trait parameters. A trait may only expect `__init`, to require a constructor its `__create` can call.
                    report(name.location, "Traits can't define '__init'; give fields default values or trait parameters instead");
                }
                else if (name.name == "__init")
                {
                    if (isOptional)
                        report(name.location, "An expected '__init' can't be optional");

                    // The primary constructor already defines this class's `__init`.
                    if (primaryConstructor && !isTrait)
                        report(
                            name.location,
                            "Cannot define an '__init' constructor because this class defines a primary constructor on line %d; remove the "
                            "primary constructor to define '__init' explicitly",
                            primaryConstructor->argLocation.begin.line + 1
                        );

                    // `__init` is not a metamethod (it applies to the class itself, not its
                    // instances), but it's a valid special method to define: it overrides the
                    // class's constructor. It must take `self` as its first parameter.
                    if (body->args.size == 0 || body->args.data[0]->name != "self")
                        report(name.location, "'__init' must take 'self' as its first parameter");
                }
                else if (EXPLICITLY_DISALLOWED_METAMETHODS.count(name.name.value) > 0)
                    report(name.location, "%s cannot define '%s' as a metamethod", isTrait ? "Traits" : "Classes", name.name.value);
                else if (ALLOWED_METAMETHODS.count(name.name.value) == 0)
                    report(name.location, "Cannot use '%s' as a method name: names starting with '__' are reserved", name.name.value);
            }

            bool hasSemicolon = false;
            if (lexer.current().type == ';')
            {
                // used by linter to explicitly ignore SameLineStatement with class prop decls
                nextLexeme();
                hasSemicolon = true;
            }

            if (classMemberNamespace.contains(name.name) || primaryConstructorParams.contains(name.name))
            {
                report(name.location, "Duplicate %s member '%s'", kind, name.name.value);
            }
            else
            {
                classMemberNamespace.insert(name.name);

                if (!qualifierLocation)
                    unqualifiedMemberLocations.push_back({name.location, /* isFunction */ true});

                declarations.push_back(
                    AstClassMethod{
                        qualifierLocation,
                        visibility,
                        matchFunction.location,
                        name.name,
                        name.location,
                        body,
                        hasSemicolon,
                        expectLocation,
                        isOptional,
                        finalLocation,
                    }
                );
            }
        }
    }

    // Luwu Classes (rfcs/classes): access specifiers are all-or-nothing within a class.
    //
    // A class with no access specifiers is entirely public. That's the POD case, which the RFC keeps
    // short on purpose. Once any member or primary constructor parameter says `public` or `private`,
    // every other member and parameter must say one too, so a missing one can't be an oversight.
    //
    // The primary constructor's own specifier (`class PositiveNumber private (...)`) makes the
    // constructor private. It isn't a member's specifier, so it doesn't count here.
    if ((sawPrivateMember || sawQualifiedParam || !explicitPublicQualifierLocations.empty()))
    {
        if (sawPrivateMember || sawQualifiedParam)
        {
            for (const auto& [loc, isFunction] : unqualifiedMemberLocations)
            {
                // Do not inline these ternaries because doing so causes MSVC to miscompile in Windows Debug CI.
                // It is some weird issue with format string %s specifically in MSVC RTC1 that will cause a segfault.
                const char* memberKind = isFunction ? "function" : "field";

                if (sawPrivateMember)
                    report(
                        loc,
                        "This %s contains non-public members; put the 'public' or 'private' keyword in front of this %s to prevent ambiguity",
                        kind,
                        memberKind
                    );
                else
                    report(
                        loc,
                        "This %s mixes explicit and implicit 'public'; put the 'public' or 'private' keyword in front of this %s to prevent "
                        "ambiguity",
                        kind,
                        memberKind
                    );
            }
        }
        else if (!unqualifiedMemberLocations.empty())
        {
            // Everything qualified here is qualified `public`, which is the one case where deleting
            // the qualifiers is as good a fix as adding the rest, so point at them instead.
            for (const Location& loc : explicitPublicQualifierLocations)
                report(
                    loc,
                    "This %s mixes explicit and implicit 'public'; remove 'public' or add 'public' or 'private' to all other members to prevent "
                    "ambiguity",
                    kind
                );
        }

        // A parameter's field can be qualified in the parameter list, or by a class body member that
        // restates it. A field qualified in neither place is as ambiguous as an unqualified member. The
        // message points at the place that is the natural fix: the parameter list if other parameters
        // are qualified there, otherwise either one.
        if (primaryConstructor)
        {
            for (size_t i = 0; i < primaryConstructor->args.size; ++i)
            {
                AstLocal* arg = primaryConstructor->args.data[i];

                if (primaryConstructor->argsQualifiers.data[i].qualifierLocation || paramsQualifiedInBody.contains(arg->name))
                    continue;

                if (sawQualifiedParam)
                    report(arg->location, "Qualify this class field parameter as 'public' or 'private' to prevent ambiguity");
                else
                    report(
                        arg->location,
                        "Field '%s' at position %d of class field parameters must be explicitly marked as 'public' or 'private' in the class "
                        "parameter list or the class body",
                        arg->name.value,
                        int(i + 1)
                    );
            }
        }
    }

    // TODO: We should use `expectMatchEndAndConsume`. It is difficult as we
    // are treating "class" as a contextual keyword (and we must as we also)
    // plan to add a `class` library.
    // An unterminated class ends at its last member, not at the statement that follows it.
    Location end = unterminated ? lexer.previousLocation() : lexer.current().location;
    bool hasEnd = unterminated ? false : expectAndConsume(Lexeme::ReservedEnd, kind);
    Location location{start, end};

    // We only allow classes at the top level: we can make use of the
    // recursion counter to check this, though it's a little clowny.
    // Luwu Declare Statements (rfcs/declare-statements.md): a declared class follows the rules for declarations instead.
    if (recursionCounter > 1 && !declared)
        report(nameLocal->location, "Cannot declare %s '%s' inside another statement or expression", kind, nameLocal->name.value);

    AstStatClass* cls = allocator.alloc<AstStatClass>(
        location, nameLocal, copy(declarations), exported, classKeywordLocation, generics, genericPacks, primaryConstructor
    );
    cls->hasEnd = hasEnd;
    cls->attributes = classAttributes;
    cls->isTrait = isTrait;
    cls->implements = implements;
    cls->needs = needs;
    cls->implementsLocation = implementsKeywordLocation;
    cls->needsLocation = needsKeywordLocation;

    // A declared class has no value, so its name is not a class name for assignment and lookup; a clash with a
    // class's type name is reported by the type checker.
    if (declared)
        return cls;

    if (AstStatClass** previous = classesWithinModule.find(nameLocal->name))
    {
        // We do not allow shadowing classes with the same name. Luwu Traits (rfcs/classes/traits.md): traits share the name
        // space, since both are hoisted values.
        const char* kinds = isTrait || (*previous)->isTrait ? "class or trait" : "class";
        return reportStatError(
            nameLocal->location,
            {},
            copy({static_cast<AstStat*>(cls)}),
            "A %s named '%s' has already been declared in this module",
            kinds,
            nameLocal->name.value
        );
    }
    classesWithinModule[nameLocal->name] = cls;
    return cls;
}

// Luwu Traits (rfcs/classes/traits.md):
// traitRefList ::= traitRef {`,' traitRef}
// traitRef ::= Name {`.' Name} [`<' TypeList `>'] [`(' [explist] `)']
AstArray<AstClassTraitRef> Parser::parseClassTraitRefs(bool allowArgs, AstClassPrimaryConstructor* primaryConstructor)
{
    LUAU_ASSERT(FFlag::LuwuTraits);
    std::vector<AstClassTraitRef> refs;

    while (true)
    {
        AstClassTraitRef ref;
        Location start = lexer.current().location;

        // A trait's needed traits are compiled into its `__needs` function, one function scope deeper than the trait
        // (see Compiler's ClassInitDefaultsVisitor::visitTrait); a class's traits are read by the class statement itself.
        static Function needsFunction;
        if (!allowArgs)
            functionStack.emplace_back(needsFunction);

        AstExpr* trait = parseNameExpr("trait name");

        while (lexer.current().type == '.' && lexer.lookahead().type == Lexeme::Name)
        {
            Position opPosition = lexer.current().location.begin;
            nextLexeme();

            Name index = parseName("trait name");
            trait = allocator.alloc<AstExprIndexName>(Location(start, index.location), trait, index.name, index.location, opPosition, '.');
        }

        if (!allowArgs)
            functionStack.pop_back();

        ref.trait = trait;

        if (lexer.current().type == '<')
            ref.typeArguments = parseTypeParams();

        if (lexer.current().type == '(')
        {
            MatchLexeme matchParen = lexer.current();
            Location argsStart = lexer.current().location;
            nextLexeme();

            // Trait arguments are evaluated on every construction, with the class's primary constructor parameters in
            // scope, like a field default; they get the same function depth for the same reason (see parseClassStat).
            static Function dummyFunction;
            functionStack.emplace_back(dummyFunction);

            unsigned int localsBegin = saveLocals();

            if (primaryConstructor)
                pushClassPrimaryConstructorParams(primaryConstructor);

            TempVector<AstExpr*> args(scratchExpr);

            if (lexer.current().type != ')')
            {
                args.push_back(parseExpr());

                while (lexer.current().type == ',')
                {
                    nextLexeme();
                    args.push_back(parseExpr());
                }
            }

            restoreLocals(localsBegin);
            functionStack.pop_back();

            Location argsEnd = lexer.current().location;
            expectMatchAndConsume(')', matchParen);

            ref.hasArgs = true;
            ref.args = copy(args);
            ref.argsLocation = Location(argsStart, argsEnd);

            if (!allowArgs)
                report(ref.argsLocation, "Traits in a 'needs' list can't take arguments; the implementing class passes them");
        }

        ref.location = Location(start, lexer.previousLocation());
        refs.push_back(ref);

        if (lexer.current().type != ',')
            break;

        nextLexeme();
    }

    return copy(refs.data(), refs.size());
}

// type function Name `(' arglist `)' `=' funcbody `end'
AstStat* Parser::parseTypeFunction(const Location& start, bool exported, Position typeKeywordPosition)
{
    Lexeme matchFn = lexer.current();
    nextLexeme();

    size_t errorsAtStart = parseErrors.size();

    // parse the name of the type function
    std::optional<Name> fnName = parseNameOpt("type function name");
    if (!fnName)
        fnName = Name(nameError, lexer.current().location);

    matchRecoveryStopOnToken[Lexeme::ReservedEnd]++;

    size_t oldTypeFunctionDepth = typeFunctionDepth;
    typeFunctionDepth = functionStack.size();

    AstExprFunction* body = parseFunctionBody(/* hasself */ false, matchFn, fnName->name, nullptr, AstArray<AstAttr*>({nullptr, 0})).first;

    typeFunctionDepth = oldTypeFunctionDepth;

    matchRecoveryStopOnToken[Lexeme::ReservedEnd]--;

    bool hasErrors = parseErrors.size() > errorsAtStart;

    // After hasErrors: a name clash doesn't make the function body unusable
    checkTypeName(*fnName, /* declared= */ false);

    AstStatTypeFunction* node =
        allocator.alloc<AstStatTypeFunction>(Location(start, body->location), fnName->name, fnName->location, body, exported, hasErrors);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstStatTypeFunction>(typeKeywordPosition, matchFn.location.begin);
    return node;
}

AstDeclaredExternTypeProperty Parser::parseDeclaredExternTypeMethod(const AstArray<AstAttr*>& attributes)
{
    Location start = lexer.current().location;

    nextLexeme();

    Name fnName = parseName("function name");

    AstArray<AstGenericType*> generics;
    AstArray<AstGenericTypePack*> genericPacks;

    if (FFlag::LuwuExternTypeGenericMethods)
    {
        std::tie(generics, genericPacks) = parseGenericTypeList(/* withDefaultValues= */ false);
    }
    else
    {
        generics.size = 0;
        generics.data = nullptr;
        genericPacks.size = 0;
        genericPacks.data = nullptr;
    }

    MatchLexeme matchParen = lexer.current();
    expectAndConsume('(', "function parameter list start");

    TempVector<Binding> args(scratchBinding);

    bool vararg = false;
    Location varargLocation;
    AstTypePack* varargAnnotation = nullptr;
    if (lexer.current().type != ')')
    {
        DeclaredParameters declaredParameters(*this);
        std::tie(vararg, varargLocation, varargAnnotation) = parseBindingList(args, /* allowDot3 */ true, /* allowDefault= */ false);
    }

    expectMatchAndConsume(')', matchParen);

    AstTypePack* retTypes = parseOptionalReturnType();
    if (!retTypes)
        retTypes = allocator.alloc<AstTypePackExplicit>(lexer.current().location, AstTypeList{copy<AstType*>(nullptr, 0), nullptr});
    Location end = lexer.previousLocation();

    TempVector<AstType*> vars(scratchType);
    TempVector<std::optional<AstArgumentName>> varNames(scratchOptArgName);

    if (args.size() == 0 || args[0].name.name != "self" || args[0].annotation != nullptr)
    {
        return AstDeclaredExternTypeProperty{
            fnName.name, fnName.location, reportTypeError(Location(start, end), {}, "'self' must be present as the unannotated first parameter"), true
        };
    }

    // Skip the first index.
    for (size_t i = 1; i < args.size(); ++i)
    {
        varNames.push_back(AstArgumentName{args[i].name.name, args[i].name.location});

        if (args[i].annotation)
            vars.push_back(args[i].annotation);
        else
            vars.push_back(reportTypeError(Location(start, end), {}, "All declaration parameters aside from 'self' must be annotated"));
    }

    if (vararg && !varargAnnotation)
        report(start, "All declaration parameters aside from 'self' must be annotated");

    AstType* fnType = allocator.alloc<AstTypeFunction>(
        Location(start, end), attributes, generics, genericPacks, AstTypeList{copy(vars), varargAnnotation}, copy(varNames), retTypes
    );

    return AstDeclaredExternTypeProperty{fnName.name, fnName.location, fnType, true, Location(start, end)};
}

bool Parser::declarationsAllowed() const
{
    return options.allowDeclarationSyntax || FFlag::LuwuDeclareStatements;
}

AstStat* Parser::parseDeclaration(const Location& start, const AstArray<AstAttr*>& attributes, std::optional<Location> exportKeywordLocation)
{
    // `declare` token is already parsed at this point

    // Luwu Declare Statements (rfcs/declare-statements.md): outside definition files, a declaration applies to the
    // whole file, so it may only appear where the whole file can see it written: at the top level, or in a `do`
    // block there (which editors can fold).
    if (!options.allowDeclarationSyntax && !blockAllowsDeclarations)
        report(start, "%s", kDeclarationPlacementError);

    if ((attributes.size != 0) && (lexer.current().type != Lexeme::ReservedFunction))
        return reportStatError(
            lexer.current().location,
            {},
            {},
            "Expected a function type declaration after attribute, but got %s instead",
            lexer.current().toString().c_str()
        );

    // Luwu Declare Statements (rfcs/declare-statements.md): `export declare extern type` and `export declare class`.
    // `export` leads, as in `export type` and `export class`; `declare export` is reported and parsed as meant. That
    // keeps `declare export: T`, a global named `export`, unambiguous. Only types can be exported: a declared value is
    // visible to its own file, and one that every file sees belongs in a definitions file.
    std::optional<Location> exportLocation = exportKeywordLocation;
    bool misplacedExport = FFlag::LuwuDeclareStatements && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "export" &&
                           lexer.lookahead().type != ':';
    if (misplacedExport)
    {
        report(lexer.current().location, "%s", kDeclareExportOrderError);
        if (!exportLocation)
            exportLocation = lexer.current().location;
        nextLexeme();
    }

    if (exportLocation)
    {
        bool exportsType = declaresClass() || (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "extern");
        if (!exportsType)
        {
            report(*exportLocation, "%s", kDeclareExportValueError);
            exportLocation.reset();
        }
    }

    if (declaresClass())
    {
        Location classKeywordLocation = lexer.current().location;
        nextLexeme();

        // `declare class type Name`: the `type` says this is a type and brings no value into scope. Without it, report
        // and parse the class it meant.
        bool typeKeyword = lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "type" && lexer.lookahead().type == Lexeme::Name;
        if (typeKeyword)
            nextLexeme();
        else
            report(classKeywordLocation, "%s", kDeclareClassNeedsTypeError);

        AstStat* stat = parseClassStat(start, /* exported= */ exportLocation.has_value(), classKeywordLocation, {nullptr, 0}, /* declared= */ true);
        AstStatClass* shape = stat->as<AstStatClass>();
        if (!shape)
            return stat;

        Location location = shape->location;
        if (exportKeywordLocation)
            location.begin = exportKeywordLocation->begin;

        return allocator.alloc<AstStatDeclareClass>(location, shape, start);
    }

    if (lexer.current().type == Lexeme::ReservedFunction)
    {
        Location functionLocation = lexer.current().location;
        nextLexeme();

        Name globalName = parseName("global function name");
        auto [generics, genericPacks] = parseGenericTypeList(/* withDefaultValues= */ false);

        MatchLexeme matchParen = lexer.current();

        expectAndConsume('(', "global function declaration");

        TempVector<Binding> args(scratchBinding);

        bool vararg = false;
        Location varargLocation;
        AstTypePack* varargAnnotation = nullptr;

        if (lexer.current().type != ')')
        {
            DeclaredParameters declaredParameters(*this);
            std::tie(vararg, varargLocation, varargAnnotation) = parseBindingList(args, /* allowDot3= */ true, /* allowDefault= */ false);
        }

        expectMatchAndConsume(')', matchParen);

        AstTypePack* retTypes;
        retTypes = parseOptionalReturnType();
        if (!retTypes)
            retTypes = allocator.alloc<AstTypePackExplicit>(lexer.current().location, AstTypeList{copy<AstType*>(nullptr, 0), nullptr});
        // Luwu: upstream ends the declaration at the token after it, so whatever follows a `declare function` looks
        // like it's on the same line (the SameLineStatement lint reports it, once declarations can be in source).
        Location end = lexer.previousLocation();

        TempVector<AstType*> vars(scratchType);
        TempVector<AstArgumentName> varNames(scratchArgName);

        // Luwu Declare Statements (rfcs/declare-statements.md): a declared *value* may leave its types out, so an
        // untyped parameter is `any` and an untyped `...` is `...any`. Upstream requires every parameter's type.
        for (size_t i = 0; i < args.size(); ++i)
        {
            AstType* annotation = args[i].annotation;
            if (!annotation)
            {
                if (!FFlag::LuwuDeclareStatements)
                    return reportStatError(Location(start, end), {}, {}, "All declaration parameters must be annotated");

                annotation = untypedDeclarationType(args[i].name.location);
            }

            vars.push_back(annotation);
            varNames.push_back({args[i].name.name, args[i].name.location});
        }

        if (vararg && !varargAnnotation)
        {
            if (!FFlag::LuwuDeclareStatements)
                return reportStatError(Location(start, end), {}, {}, "All declaration parameters must be annotated");

            varargAnnotation = allocator.alloc<AstTypePackVariadic>(varargLocation, untypedDeclarationType(varargLocation));
        }

        checkDuplicateDeclaration(globalName);

        return allocator.alloc<AstStatDeclareFunction>(
            Location(start, end),
            attributes,
            globalName.name,
            globalName.location,
            generics,
            genericPacks,
            AstTypeList{copy(vars), varargAnnotation},
            copy(varNames),
            vararg,
            varargLocation,
            retTypes,
            start,
            functionLocation
        );
    }
    // When FFlag::LuauAllowGlobalDeclarationToBeCalledClass is set, `declare class : T` is parsed as a
    // global variable declaration whose name is `class`, not as a malformed class declaration. This allows
    // us to support a global table like string/math/bit32 called `class`. CLI-203833 tracks the work to actually
    // remove support for `declare class X [extends Y]` syntax.
    //
    // Luwu: `declare class X` never declares an extern type, whatever LuauDisallowExternClassInTypeDefinitions says. With
    // Luwu Declare Statements (rfcs/declare-statements.md) it declares a Luwu class (see declaresClass above). Otherwise
    // it is an error naming `declare extern type`, and it is parsed as the extern type it meant so nothing cascades.
    // `declare class: T` is always a global named `class`.
    else if (AstName(lexer.current().name) == "extern" || legacyDeclareClass())
    {
        bool isExtern = AstName(lexer.current().name) == "extern";
        if (!isExtern)
            report(lexer.current().location, "%s", kLegacyDeclareClassError);
        else
        {
            nextLexeme();
            if (AstName(lexer.current().name) != "type")
                return reportStatError(
                    lexer.current().location, {}, {}, "Expected `type` keyword after `extern`, but got %s instead", lexer.current().name
                );
        }

        Location classLocation = lexer.current().location;
        nextLexeme();

        Name className = parseName("type name");
        checkTypeName(className, /* declared= */ true);

        AstArray<AstGenericType*> classGenerics;
        AstArray<AstGenericTypePack*> classGenericPacks;

        if (FFlag::LuwuGenericNominals)
        {
            // Luwu Generic Nominals (rfcs/generics-on-extern-types.md): an extern type's generic parameter list may carry defaults:
            // `declare extern type Box<T = string> with ... end`.
            std::tie(classGenerics, classGenericPacks) = parseGenericTypeList(/* withDefaultValues= */ true);
        }
        else
        {
            classGenerics.size = 0;
            classGenerics.data = nullptr;
            classGenericPacks.size = 0;
            classGenericPacks.data = nullptr;
        }

        std::optional<AstName> superName = std::nullopt;
        std::optional<Location> superNameLocation = std::nullopt;
        std::optional<Location> extendsLocation = std::nullopt;

        if (AstName(lexer.current().name) == "extends")
        {
            extendsLocation = lexer.current().location;
            nextLexeme();
            Name parsedSuperName = parseName("supertype name");
            superName = parsedSuperName.name;
            superNameLocation = parsedSuperName.location;
        }

        // Luwu Declare Statements (rfcs/declare-statements.md): `with` is optional, as in a class, and kept only for
        // compatibility with upstream, which requires it. A `with` followed by `:` is the first property, named `with`;
        // the keyword is never followed by `:`.
        std::optional<Location> withLocation;
        if (isExtern)
        {
            bool withKeyword = AstName(lexer.current().name) == "with" && (!FFlag::LuwuDeclareStatements || lexer.lookahead().type != ':');
            if (withKeyword)
            {
                withLocation = lexer.current().location;
                nextLexeme();
            }
            else if (!FFlag::LuwuDeclareStatements)
                report(
                    lexer.current().location,
                    "Expected `with` keyword before listing properties of the external type, but got %s instead",
                    lexer.current().name
                );
        }

        TempVector<AstDeclaredExternTypeProperty> props(scratchDeclaredClassProps);
        AstTableIndexer* indexer = nullptr;

        while (lexer.current().type != Lexeme::ReservedEnd)
        {
            AstArray<AstAttr*> attributes{nullptr, 0};

            skipClassOnlyExternTypeKeywords();

            if (lexer.current().type == Lexeme::Attribute || lexer.current().type == Lexeme::AttributeOpen)
            {
                attributes = Parser::parseAttributes(AstAttr::Context::Function);

                if (lexer.current().type != Lexeme::ReservedFunction)
                    return reportStatError(
                        lexer.current().location,
                        {},
                        {},
                        "Expected a method type declaration after attribute, but got %s instead",
                        lexer.current().toString().c_str()
                    );
            }

            // There are two possibilities: Either it's a property or a function.
            if (lexer.current().type == Lexeme::ReservedFunction)
            {
                props.push_back(parseDeclaredExternTypeMethod(attributes));
            }
            else if (lexer.current().type == '[')
            {
                const Lexeme begin = lexer.current();
                nextLexeme(); // [

                if ((lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString) && lexer.lookahead().type == ']')
                {
                    const Location nameBegin = lexer.current().location;
                    std::optional<AstArray<char>> chars = parseCharArray();

                    const Location nameEnd = lexer.previousLocation();

                    expectMatchAndConsume(']', begin);
                    expectAndConsume(':', "property type annotation");
                    AstType* type = parseType();

                    // since AstName contains a char*, it can't contain null
                    bool containsNull = chars && (memchr(chars->data, 0, chars->size) != nullptr);

                    if (chars && !containsNull)
                    {
                        props.push_back(
                            AstDeclaredExternTypeProperty{
                                AstName(chars->data), Location(nameBegin, nameEnd), type, false, Location(begin.location, lexer.previousLocation())
                            }
                        );
                    }
                    else
                    {
                        report(begin.location, "String literal contains malformed escape sequence or \\0");
                    }
                }
                else if (indexer)
                {
                    // maybe we don't need to parse the entire badIndexer...
                    // however, we either have { or [ to lint, not the entire table type or the bad indexer.
                    AstTableIndexer* badIndexer = parseTableIndexer(AstTableAccess::ReadWrite, std::nullopt, begin).node;

                    // we lose all additional indexer expressions from the AST after error recovery here
                    report(badIndexer->location, "Cannot have more than one indexer on an extern type");
                }
                else
                {
                    indexer = parseTableIndexer(AstTableAccess::ReadWrite, std::nullopt, begin).node;
                }
            }
            else
            {
                AstTableAccess access = AstTableAccess::ReadWrite;

                // Luwu: `name = T` is a property with a (rejected) default below, not an access modifier
                if (lexer.current().type == Lexeme::Name && lexer.lookahead().type != ':' && lexer.lookahead().type != '=')
                {
                    if (AstName(lexer.current().name) == "read")
                    {
                        access = AstTableAccess::Read;
                        lexer.next();
                    }
                    else if (AstName(lexer.current().name) == "write")
                    {
                        access = AstTableAccess::Write;
                        lexer.next();
                    }
                    else
                    {
                        report(lexer.current().location, "Expected blank or 'read' or 'write' attribute, got '%s'", lexer.current().name);
                        lexer.next();
                    }
                }

                Location propStart = lexer.current().location;
                std::optional<Name> propName = parseNameOpt("property name");

                if (!propName)
                    break;

                // Luwu: `name = T` is how a declared class writes a default; an extern type has no defaults
                if (lexer.current().type == '=')
                {
                    report(lexer.current().location, "Extern type properties have no defaults; write 'name: T'");
                    nextLexeme();
                }
                else
                    expectAndConsume(':', "property type annotation");

                AstType* propType = parseType();
                props.push_back(
                    AstDeclaredExternTypeProperty{
                        propName->name, propName->location, propType, false, Location(propStart, lexer.previousLocation()), access
                    }
                );
            }
        }

        Location classEnd = lexer.current().location;
        nextLexeme(); // skip past `end`

        // Luwu: upstream starts the statement at the type's name, so `declare extern type` falls outside it and tools
        // can't find the statement from its keywords. It starts at `declare` (or `export`); the name has nameLocation.
        Position statementBegin = exportLocation ? exportLocation->begin : start.begin;
        AstStatDeclareExternType* declaration = allocator.alloc<AstStatDeclareExternType>(
            Location(statementBegin, classEnd.end),
            className.name,
            superName,
            copy(props),
            start,
            classLocation,
            extendsLocation,
            indexer,
            classGenerics,
            classGenericPacks
        );
        declaration->exportLocation = exportLocation;
        declaration->withLocation = withLocation;
        declaration->nameLocation = className.location;
        declaration->superNameLocation = superNameLocation;
        bool withOmitted = isExtern && !withLocation;
        if (exportLocation || withOmitted)
            declaration->luwuOnly = true;
        return declaration;
    }
    else if (std::optional<Name> globalName = parseNameOpt("global variable name"))
    {
        // Luwu Declare Statements (rfcs/declare-statements.md): `declare name` with no type says the global exists and
        // leaves its type to the environment (see ConstraintGenerator). Upstream requires the type.
        AstType* type = nullptr;
        if (lexer.current().type == ':' || !FFlag::LuwuDeclareStatements)
        {
            expectAndConsume(':', "global variable declaration");
            type = parseType(/* in declaration context */ true);
        }

        // Luwu Declare Statements (rfcs/declare-statements.md): a declaration has no value; `declare cat = 2` is reported
        // and the value is skipped, so the declaration still stands
        if (FFlag::LuwuDeclareStatements && lexer.current().type == '=')
        {
            report(
                lexer.current().location,
                "declare may only be used to initialize globals; assign to '%s' on a new line to assign to the global variable",
                globalName->name.value
            );
            nextLexeme();
            parseExpr();
        }

        checkDuplicateDeclaration(*globalName);
        Location end = type ? type->location : globalName->location;
        return allocator.alloc<AstStatDeclareGlobal>(Location(start, end), globalName->name, globalName->location, type, start);
    }
    else
    {
        return reportStatError(start, {}, {}, "declare must be followed by an identifier, 'function', or 'extern type'");
    }
}

// Luwu Declare Statements (rfcs/declare-statements.md): `declare class type Name` (or the `declare class Name` it is
// reported for), as opposed to `declare class: T`, a global named `class`. It takes the place of upstream's legacy
// `declare class`, which declares an extern type.
// Luwu: class and trait member keywords in an extern type body each get one error naming the fix, and are skipped so
// the member itself still parses. They only count when a member follows: `public: number` is a property named `public`.
void Parser::skipClassOnlyExternTypeKeywords()
{
    while (lexer.current().type == Lexeme::Name)
    {
        Lexeme::Type next = lexer.lookahead().type;
        bool memberFollows = next == Lexeme::Name || next == Lexeme::ReservedFunction || next == Lexeme::Attribute || next == Lexeme::AttributeOpen;
        if (!memberFollows)
            return;

        AstName keyword(lexer.current().name);
        if (keyword == "public" || keyword == "private")
            report(
                lexer.current().location,
                "Extern types have no access specifiers; every member is public. To declare a class, use 'declare class type'"
            );
        else if (keyword == "const")
            report(lexer.current().location, "Extern types have no 'const'; write 'read' for a read-only property");
        else if (keyword == "expect" || keyword == "final")
            report(lexer.current().location, "'%s' belongs in a trait, not an extern type", keyword.value);
        else
            return;

        nextLexeme();
    }
}

// Upstream's legacy `declare class Name`, an extern type, where it isn't a Luwu class
bool Parser::legacyDeclareClass()
{
    return !declaresClass() && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "class" &&
           lexer.lookahead().type == Lexeme::Name;
}

bool Parser::declaresClass()
{
    return FFlag::LuwuDeclareStatements && FFlag::LuwuClasses && lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "class" &&
           lexer.lookahead().type == Lexeme::Name;
}

Parser::DeclaredParameters::DeclaredParameters(Parser& parser)
    : parser(parser)
    , outer(parser.parsingDeclaredParameters)
{
    parser.parsingDeclaredParameters = FFlag::LuwuDeclareStatements;
}

Parser::DeclaredParameters::~DeclaredParameters()
{
    parser.parsingDeclaredParameters = outer;
}

// The `any` an untyped part of a declared value stands for, e.g. a `declare function` parameter.
AstType* Parser::untypedDeclarationType(const Location& location)
{
    return allocator.alloc<AstTypeReference>(location, std::nullopt, nameAny, std::nullopt, location);
}

// A declared class's primary constructor parameter: `name: T`, or `name = T` for one with a default.
Parser::Binding Parser::parseDeclaredClassBinding(std::optional<Location>& declaredDefaultLocation)
{
    std::optional<Name> name = parseNameOpt("variable name");
    if (!name)
        name = Name(nameError, lexer.current().location);

    AstType* annotation = nullptr;
    if (lexer.current().type == ':')
    {
        nextLexeme();
        annotation = parseType();
    }

    if (lexer.current().type == '=')
    {
        declaredDefaultLocation = lexer.current().location;
        nextLexeme();

        // `x: T = value` copied from a class: the annotation is the type, and the value is skipped
        if (annotation)
        {
            report(*declaredDefaultLocation, "%s", kDeclaredDefaultIsATypeError);
            parseExpr();
        }
        else
            annotation = parseType();
    }

    if (!annotation)
        report(name->location, "All declaration parameters must be annotated");

    return Binding(*name, annotation);
}

// A declared method is a signature, so everything but `self` needs a type.
void Parser::checkDeclaredClassMethod(AstExprFunction* function)
{
    for (size_t i = 0; i < function->args.size; ++i)
    {
        AstLocal* arg = function->args.data[i];
        bool isSelf = i == 0 && arg->name == "self";
        if (!isSelf && !arg->annotation)
            report(arg->location, "All declaration parameters must be annotated");
    }

    if (function->vararg && !function->varargAnnotation)
        report(function->varargLocation, "All declaration parameters must be annotated");
}

// Luwu Declare Statements (rfcs/declare-statements.md): a declared type (extern type or class) applies to the whole file,
// so no other type in the file may have its name: a second declaration, or an alias, type function or class at any
// depth. Definition files keep upstream's rules.
void Parser::checkTypeName(const Name& name, bool declared)
{
    if (options.allowDeclarationSyntax || !FFlag::LuwuDeclareStatements || name.name == nameError)
        return;

    if (const Location* previous = fileTypeDeclarations.find(name.name))
    {
        report(name.location, "Type '%s' is declared on line %d, and a declared type can't be shadowed", name.name.value, previous->begin.line + 1);
        return;
    }

    if (declared)
    {
        if (const Location* previous = fileTypeNames.find(name.name))
        {
            report(name.location, "Type '%s' is already defined on line %d, and a declared type can't shadow it", name.name.value, previous->begin.line + 1);
            return;
        }

        fileTypeDeclarations[name.name] = name.location;
    }
    else if (!fileTypeNames.contains(name.name))
        fileTypeNames[name.name] = name.location;
}

// Luwu Declare Statements (rfcs/declare-statements.md): a file may declare each global once. Definition files keep
// upstream's rules, which allow redeclaring.
void Parser::checkDuplicateDeclaration(const Name& name)
{
    if (options.allowDeclarationSyntax)
        return;

    if (const Location* previous = fileDeclarations.find(name.name))
    {
        report(name.location, "'%s' is already declared on line %d", name.name.value, previous->begin.line + 1);
        return;
    }

    fileDeclarations[name.name] = name.location;
}

// Luwu: upstream (still at 0.738) names the const variable only under LuauExportValueSyntax, and reports
// "Assigned expression must be a variable or a field" otherwise. Luwu always calls this, so the error
// names the variable (a class method's const `self` exists whatever that flag is).
AstExprError* Parser::reportLValueError(AstExpr* expr)
{
    if (expr->is<AstExprLocal>() && expr->as<AstExprLocal>()->local->isConst)
    {
        AstExprLocal* local = expr->as<AstExprLocal>();
        return reportExprError(expr->location, copy({expr}), "Variable '%s' is constant and may not be reassigned", local->local->name.value);
    }
    if (FFlag::LuwuClasses)
    {
        if (AstStatClass* classStat = getMatchingClass(expr))
        {
            return reportExprError(
                expr->location,
                copy({expr}),
                "'%s' refers to a class and cannot be used as a variable name (defined on line %d)",
                classStat->name->name.value,
                classStat->location.begin.line + 1
            );
        }
    }

    return reportExprError(expr->location, copy({expr}), "Assigned expression must be a variable or a field");
}

// varlist `=' explist
AstStat* Parser::parseAssignment(AstExpr* initial)
{
    if (!isExprLValue(initial))
        initial = reportLValueError(initial);

    TempVector<AstExpr*> vars(scratchExpr);
    TempVector<Position> varsCommaPositions(scratchPosition);
    vars.push_back(initial);

    while (lexer.current().type == ',')
    {
        if (options.storeCstData)
            varsCommaPositions.push_back(lexer.current().location.begin);
        nextLexeme();

        AstExpr* expr = parsePrimaryExpr(/* asStatement= */ true);

        if (!isExprLValue(expr))
            expr = reportLValueError(expr);

        vars.push_back(expr);
    }

    bool equalsFound = expectAndConsume('=', "assignment");
    Position equalsPosition = equalsFound ? lexer.previousLocation().begin : Position::missing();

    TempVector<AstExpr*> values(scratchExprAux);
    TempVector<Position> valuesCommaPositions(scratchPosition);
    parseExprList(values, options.storeCstData ? &valuesCommaPositions : nullptr);

    AstStatAssign* node = allocator.alloc<AstStatAssign>(Location(initial->location, values.back()->location), copy(vars), copy(values));
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstStatAssign>(copy(varsCommaPositions), equalsPosition, copy(valuesCommaPositions));
    return node;
}

AstStat* Parser::parseExportValue(
    const Location& start,
    const Location& keywordLocation,
    const AstArray<AstAttr*>& attributes,
    TempVector<CstAttrList*>* cstAttrLists
)
{
    if (functionStack.size() != 1 || recursionCounter != 1)
        report(start, "'export' may only be applied to top-level statements");

    if (hasModuleReturn)
        report(start, "Exporting values is not compatible with top-level return (export/return conflict)");

    auto checkDuplicateExport = [&](AstName name, const Location& location) -> bool
    {
        if (declaredExportBindings.find(name))
            return false;

        declaredExportBindings[name] = location;
        return true;
    };

    auto exportLocalStat = [&](AstStat* stat, const Location& keywordLocation) -> AstStat*
    {
        // Luwu Destructuring (rfcs/destructuring.md): not supported with `export` yet.
        if (!pendingStatements.empty())
        {
            report(stat->location, "A destructuring declaration can't be exported");
            return stat;
        }

        if (AstStatLocal* localStat = stat->as<AstStatLocal>())
        {
            localStat->isExported = true;

            for (AstLocal* local : localStat->vars)
            {
                if (!checkDuplicateExport(local->name, local->location))
                {
                    report(local->location, "Duplicate exported identifier '%s'", local->name.value);
                    continue;
                }

                local->isExported = true;
            }

            localStat->keywordLocation = keywordLocation;
        }
        else
            LUAU_ASSERT(!"Expected export local/const to parse as AstStatLocal");

        return stat;
    };

    if (attributes.size != 0 && lexer.current().type != Lexeme::ReservedFunction)
    {
        report(
            lexer.current().location,
            "Expected 'function' after export declaration with attribute, but got %s instead",
            lexer.current().toString().c_str()
        );
    }

    if (lexer.current().type == Lexeme::ReservedLocal)
    {
        Location localKeywordLocation = lexer.current().location;

        if (lexer.lookahead().type == Lexeme::ReservedFunction)
        {
            report(start, "'export' must be followed by an identifier or 'function'; try removing 'local'");
            // still parse the function for error recovery
            return parseLocal(start, localKeywordLocation, {nullptr, 0}, true);
        }

        return exportLocalStat(parseLocal(start, keywordLocation, {nullptr, 0}, false), localKeywordLocation);
    }
    else if (lexer.current().type == Lexeme::ReservedFunction)
    {
        auto funcStat = parseLocal(start, keywordLocation, attributes, true, cstAttrLists);
        if (!funcStat->is<AstStatLocalFunction>())
            // parseLocal returned a parse error
            return funcStat;

        auto func = funcStat->as<AstStatLocalFunction>();

        if (!checkDuplicateExport(func->name->name, func->name->location))
            report(func->name->location, "Duplicate exported identifier '%s'", func->name->name.value);

        func->name->isExported = true;
        func->name->isConst = true;
        return func;
    }
    else if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "const")
    {
        Location constKeywordLocation = lexer.current().location;
        nextLexeme();

        if (lexer.current().type == Lexeme::ReservedFunction)
        {
            report(start, "'export' must be followed by an identifier or 'function'");
            // still parse the function for error recovery
            return parseLocal(start, constKeywordLocation, {nullptr, 0}, true);
        }

        return exportLocalStat(parseLocal(start, constKeywordLocation, {nullptr, 0}, true), constKeywordLocation);
    }
    else if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "class")
    {
        if (!FFlag::LuwuClasses)
            return reportStatError(start, {}, {}, "%s", kClassesDisabledError);

        Location classKeywordLocation = lexer.current().location;
        nextLexeme();
        auto stat = parseClassStat(start, /*exported*/ true, classKeywordLocation);
        if (auto classStat = stat->as<AstStatClass>())
        {
            if (!checkDuplicateExport(classStat->name->name, classStat->name->location))
                report(classStat->name->location, "Duplicate exported class '%s'", classStat->name->name.value);

            classStat->name->isExported = true;
        }
        return stat;
    }
    else if (lexer.current().type == Lexeme::Name && AstName(lexer.current().name) == "trait" && traitsEnabled())
    {
        Location traitKeywordLocation = lexer.current().location;
        nextLexeme();
        auto stat = parseClassStat(start, /*exported*/ true, traitKeywordLocation, {nullptr, 0}, /* declared= */ false, /* isTrait= */ true);
        if (auto traitStat = stat->as<AstStatClass>())
        {
            if (!checkDuplicateExport(traitStat->name->name, traitStat->name->location))
                report(traitStat->name->location, "Duplicate exported trait '%s'", traitStat->name->name.value);

            traitStat->name->isExported = true;
        }
        return stat;
    }

    return reportStatError(start, {}, {}, "'export' must be followed by an identifier or 'function'");
}

// var [`+=' | `-=' | `*=' | `/=' | `%=' | `^=' | `..='] exp
AstStat* Parser::parseCompoundAssignment(AstExpr* initial, AstExprBinary::Op op)
{
    if (!isExprLValue(initial))
    {
        initial = reportLValueError(initial);
    }

    Position opPosition = lexer.current().location.begin;
    nextLexeme();

    AstExpr* value = parseExpr();

    AstStatCompoundAssign* node = allocator.alloc<AstStatCompoundAssign>(Location(initial->location, value->location), op, initial, value);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstStatCompoundAssign>(opPosition);
    return node;
}

std::tuple<AstLocal*, AstArray<AstLocal*>, AstArray<AstExpr*>> Parser::prepareFunctionArguments(const Location& start, bool hasself, const TempVector<Binding>& args)
{
    AstLocal* self = nullptr;

    if (hasself)
        self = pushLocal(Binding(Name(nameSelf, start), nullptr));

    TempVector<AstLocal*> vars(scratchLocal);
    TempVector<AstExpr*> varsDefaults(scratchExpr);

    for (size_t i = 0; i < args.size(); ++i)
    {
        vars.push_back(pushLocal(args[i]));
        varsDefaults.push_back(args[i].defaultValue);
    }

    return {self, copy(vars), copy(varsDefaults)};
}

// funcbody ::= `(' [parlist] `)' [`:' ReturnType] block end
// parlist ::= bindinglist [`,' `...'] | `...'
std::pair<AstExprFunction*, AstLocal*> Parser::parseFunctionBody(
    bool hasself,
    const Lexeme& matchFunction,
    const AstName& debugname,
    const Name* localName,
    const AstArray<AstAttr*>& attributes,
    const bool isConst,
    TempVector<CstAttrList*>* cstAttrLists,
    const Lexeme* endMatchLexeme,
    bool isClassFunction,
    bool signatureOnly
)
{
    LUAU_ASSERT(cstAttrLists != nullptr ? FFlag::LuauCstAttr : true);

    Location start = matchFunction.location;

    if (attributes.size > 0)
        start = attributes.data[0]->location;

    auto* cstNode = options.storeCstData ? allocator.alloc<CstExprFunction>() : nullptr;

    if (FFlag::LuauCstAttr && cstNode && cstAttrLists)
        cstNode->attrLists = copy(*cstAttrLists);

    auto [generics, genericPacks] =
        cstNode
            ? parseGenericTypeList(
                  /* withDefaultValues= */ false, &cstNode->openGenericsPosition, &cstNode->genericsCommaPositions, &cstNode->closeGenericsPosition
              )
            : parseGenericTypeList(/* withDefaultValues= */ false);

    MatchLexeme matchParen = lexer.current();
    expectAndConsume('(', "function");

    // NOTE: This was added in conjunction with passing `searchForMissing` to
    // `expectMatchAndConsume` inside `parseTableType` so that the behavior of
    // parsing code like below (note the missing `}`):
    //
    //  function (t: { a: number  ) end
    //
    // ... will still parse as (roughly):
    //
    //  function (t: { a: number }) end
    //
    matchRecoveryStopOnToken[')']++;

    TempVector<Binding> args(scratchBinding);

    bool vararg = false;
    Location varargLocation;
    AstTypePack* varargAnnotation = nullptr;

    if (lexer.current().type != ')')
    {
        std::optional<DeclaredParameters> declaredParameters;
        if (signatureOnly)
            declaredParameters.emplace(*this);

        if (cstNode)
            std::tie(vararg, varargLocation, varargAnnotation) = parseBindingList(
                args,
                /* allowDot3= */ true,
                /* allowDefault= */ FFlag::LuwuDefaultArguments,
                &cstNode->argsCommaPositions,
                nullptr,
                &cstNode->varargAnnotationColonPosition,
                /* isConst= */ false,
                /* allowAttributes= */ true
            );
        else
            std::tie(vararg, varargLocation, varargAnnotation) = parseBindingList(
                args,
                /* allowDot3= */ true,
                /* allowDefault= */ FFlag::LuwuDefaultArguments,
                nullptr,
                nullptr,
                nullptr,
                /* isConst= */ false,
                /* allowAttributes= */ true
            );
    }

    std::optional<Location> argLocation;

    if (matchParen.type == Lexeme::Type('(') && lexer.current().type == Lexeme::Type(')'))
        argLocation = Location(matchParen.position, lexer.current().location.end);

    expectMatchAndConsume(')', matchParen, true);

    matchRecoveryStopOnToken[')']--;

    AstTypePack* typelist = parseOptionalReturnType(cstNode ? &cstNode->returnSpecifierPosition : nullptr);

    AstLocal* funLocal = nullptr;

    if (localName)
    {
        funLocal = pushLocal(Binding(*localName, nullptr, {0, 0}, isConst));
    }

    unsigned int localsBegin = saveLocals();

    Function fun;
    fun.vararg = vararg;

    functionStack.emplace_back(fun);

    auto [self, vars, varsDefaults] = prepareFunctionArguments(start, hasself, args);

    // Luwu Classes (rfcs/classes): a method's `self` is const, so every write to it (assignment,
    // `function self()`, a write from a nested closure) is rejected like a write to any const local.
    // Field writes (`self.x = v`) and a new `local self` are unaffected.
    if (isClassFunction && vars.size > 0 && vars.data[0]->name == "self")
        vars.data[0]->isConst = true;

    AstStatBlock* body = nullptr;
    Location end;
    if (signatureOnly)
    {
        // Like `declare function`, a signature with no return type returns nothing.
        if (!typelist)
            typelist = allocator.alloc<AstTypePackExplicit>(lexer.previousLocation(), AstTypeList{copy<AstType*>(nullptr, 0), nullptr});

        end = lexer.previousLocation();
        body = allocator.alloc<AstStatBlock>(Location(end.end, end.end), AstArray<AstStat*>{nullptr, 0});
        body->hasEnd = true;

        functionStack.pop_back();
        restoreLocals(localsBegin);
    }
    else
    {
        body = parseBlock();

        functionStack.pop_back();

        restoreLocals(localsBegin);

        end = lexer.current().location;

        bool hasEnd = expectMatchEndAndConsume(Lexeme::ReservedEnd, endMatchLexeme ? *endMatchLexeme : matchFunction);
        body->hasEnd = hasEnd;
    }

    AstExprFunction* node = allocator.alloc<AstExprFunction>(
        Location(start, end),
        attributes,
        generics,
        genericPacks,
        self,
        vars,
        varsDefaults,
        vararg,
        varargLocation,
        body,
        functionStack.size(),
        debugname,
        typelist,
        varargAnnotation,
        argLocation
    );
    if (options.storeCstData)
    {
        cstNode->functionKeywordPosition = matchFunction.location.begin;
        cstNode->argsAnnotationColonPositions = extractAnnotationColonPositions(args);
        cstNodeMap[node] = cstNode;
    }

    return {node, funLocal};
}

// explist ::= {exp `,'} exp
void Parser::parseExprList(TempVector<AstExpr*>& result, TempVector<Position>* commaPositions)
{
    result.push_back(parseExpr());

    while (lexer.current().type == ',')
    {
        if (commaPositions)
            commaPositions->push_back(lexer.current().location.begin);
        nextLexeme();

        if (lexer.current().type == ')')
        {
            report(lexer.current().location, "Expected expression after ',' but got ')' instead");
            break;
        }

        result.push_back(parseExpr());
    }
}

Parser::Binding Parser::parseBinding(bool isConst, bool allowDefault, bool allowAttributes)
{
    // A parameter's attributes come before its name: `function f(@deprecated a: number)`.
    AstArray<AstAttr*> attributes{nullptr, 0};
    if (FFlag::LuwuAttributesEverywhere && allowAttributes && attributesFollow())
        attributes = parseAttributes(AstAttr::Context::Parameter);

    std::optional<Name> name = parseNameOpt("variable name");

    // Use placeholder if the name is missing
    if (!name)
        name = Name(nameError, lexer.current().location);

    Position colonPosition = lexer.current().type == ':' ? lexer.current().location.begin : Position::missing();
    AstType* annotation = parseOptionalType();

    AstExpr* defaultValue = nullptr;
    if (parsingDeclaredParameters && lexer.current().type == '=')
    {
        // Luwu Declare Statements (rfcs/declare-statements.md): a declared parameter's default is written as its type,
        // and only tells the caller it may leave the parameter out. `x: T = value` copied from a function keeps its
        // annotation, and the value is skipped.
        Location equalsLocation = lexer.current().location;
        nextLexeme();

        if (annotation)
        {
            report(equalsLocation, "%s", kDeclaredDefaultIsATypeError);
            parseExpr();
        }
        else
        {
            AstType* type = parseType();
            AstType* parts[] = {type, allocator.alloc<AstTypeOptional>(Location(equalsLocation))};
            annotation = allocator.alloc<AstTypeUnion>(type->location, copy(parts, 2));
        }
    }
    else if (allowDefault && lexer.current().type == '=')
    {
        nextLexeme();
        // The depth of functionStack is used to determine the scoping for a local
        // The expressions for default arguments need scoped to the function body, not the parent
        // scope
        // We can't access the Function for the body, because that gets constructed during a later
        // parse step. A dummy function is safe to use here, as code only inspects .vararg and ...
        // is not legal in a default argument expression.
        static Function dummyFunction;
        functionStack.emplace_back(dummyFunction);

        defaultValue = parseExpr();

        functionStack.pop_back();
    }

    if (options.storeCstData)
        return Binding(*name, annotation, colonPosition, isConst, defaultValue, attributes);
    else
        return Binding(*name, annotation, Position::missing(), isConst, defaultValue, attributes);
}

AstArray<Position> Parser::extractAnnotationColonPositions(const TempVector<Binding>& bindings)
{
    TempVector<Position> annotationColonPositions(scratchPosition);
    for (size_t i = 0; i < bindings.size(); ++i)
        annotationColonPositions.push_back(bindings[i].colonPosition);
    return copy(annotationColonPositions);
}

// bindinglist ::= (binding | `...') [`,' bindinglist]
LUAU_NOINLINE std::tuple<bool, Location, AstTypePack*> Parser::parseBindingList(
    TempVector<Binding>& result,
    bool allowDot3,
    bool allowDefault,
    AstArray<Position>* commaPositions,
    Position* initialCommaPosition,
    Position* varargAnnotationColonPosition,
    bool isConst,
    bool allowAttributes
)
{
    TempVector<Position> localCommaPositions(scratchPosition);

    if (commaPositions && initialCommaPosition)
        localCommaPositions.push_back(*initialCommaPosition);

    while (true)
    {
        if (lexer.current().type == Lexeme::Dot3 && allowDot3)
        {
            Location varargLocation = lexer.current().location;
            nextLexeme();

            AstTypePack* tailAnnotation = nullptr;
            if (lexer.current().type == ':')
            {
                if (varargAnnotationColonPosition)
                    *varargAnnotationColonPosition = lexer.current().location.begin;

                nextLexeme();
                tailAnnotation = parseVariadicArgumentTypePack();
            }

            if (commaPositions)
                *commaPositions = copy(localCommaPositions);

            return {true, varargLocation, tailAnnotation};
        }

        // Luwu Destructuring (rfcs/destructuring.md): a pattern where only names are allowed. Parsed and dropped,
        // so the error is the only one.
        if (destructurePatternFollows())
        {
            Location patternStart = lexer.current().location;
            DestructureTarget ignored;
            parseDestructurePattern(ignored);

            // Only parameter lists allow `...`
            const char* message = "A destructuring pattern has to be the only binding in its declaration";
            if (allowDot3)
                message = "Destructuring isn't supported in function parameters; destructure the parameter in the body";

            report(Location(patternStart, lexer.previousLocation()), "%s", message);
            result.push_back(Binding(Name(nameError, patternStart)));
        }
        else
        {
            result.push_back(parseBinding(isConst, allowDefault, allowAttributes));
        }

        if (lexer.current().type != ',')
            break;
        if (commaPositions)
            localCommaPositions.push_back(lexer.current().location.begin);
        nextLexeme();
    }

    if (commaPositions)
        *commaPositions = copy(localCommaPositions);

    return {false, Location(), nullptr};
}

AstType* Parser::parseOptionalType()
{
    if (lexer.current().type == ':')
    {
        nextLexeme();
        return parseType();
    }
    else
        return nullptr;
}

// TypeList ::= Type [`,' TypeList] | ...Type
AstTypePack* Parser::parseTypeList(
    TempVector<AstType*>& result,
    TempVector<std::optional<AstArgumentName>>& resultNames,
    TempVector<Position>* commaPositions,
    TempVector<Position>* nameColonPositions
)
{
    while (true)
    {
        if (shouldParseTypePack(lexer))
            return parseTypePack();

        if (lexer.current().type == Lexeme::Name && lexer.lookahead().type == ':')
        {
            // Fill in previous argument names with empty slots
            while (resultNames.size() < result.size())
                resultNames.push_back({});
            if (nameColonPositions)
            {
                while (nameColonPositions->size() < result.size())
                    nameColonPositions->push_back(Position::missing());
            }

            resultNames.push_back(AstArgumentName{AstName(lexer.current().name), lexer.current().location});
            nextLexeme();

            if (nameColonPositions)
                nameColonPositions->push_back(lexer.current().location.begin);
            expectAndConsume(':');
        }
        else if (!resultNames.empty())
        {
            // If we have a type with named arguments, provide elements for all types
            resultNames.push_back({});
            if (nameColonPositions)
                nameColonPositions->push_back(Position::missing());
        }

        result.push_back(parseType());
        if (lexer.current().type != ',')
            break;

        if (commaPositions)
            commaPositions->push_back(lexer.current().location.begin);
        nextLexeme();

        if (lexer.current().type == ')')
        {
            report(lexer.current().location, "Expected type after ',' but got ')' instead");
            break;
        }
    }

    return nullptr;
}

AstTypePack* Parser::parseOptionalReturnType(Position* returnSpecifierPosition)
{
    if (lexer.current().type == ':' || lexer.current().type == Lexeme::SkinnyArrow)
    {
        if (lexer.current().type == Lexeme::SkinnyArrow)
            report(lexer.current().location, "Function return type annotations are written after ':' instead of '->'");

        if (returnSpecifierPosition)
            *returnSpecifierPosition = lexer.current().location.begin;
        nextLexeme();

        unsigned int oldRecursionCount = recursionCounter;

        auto result = parseReturnType();
        LUAU_ASSERT(result);

        // At this point, if we find a , character, it indicates that there are multiple return types
        // in this type annotation, but the list wasn't wrapped in parentheses.
        if (lexer.current().type == ',')
        {
            report(lexer.current().location, "Expected a statement, got ','; did you forget to wrap the list of return types in parentheses?");

            nextLexeme();
        }

        recursionCounter = oldRecursionCount;

        return result;
    }

    return nullptr;
}

// ReturnType ::= Type | `(' TypeList `)'
AstTypePack* Parser::parseReturnType()
{
    incrementRecursionCounter("type annotation");

    Lexeme begin = lexer.current();

    if (lexer.current().type != '(')
    {
        if (shouldParseTypePack(lexer))
        {
            return parseTypePack();
        }
        else
        {
            AstType* type = parseType();
            AstTypePackExplicit* node = allocator.alloc<AstTypePackExplicit>(type->location, AstTypeList{copy(&type, 1), nullptr});
            if (options.storeCstData)
                cstNodeMap[node] = allocator.alloc<CstTypePackExplicit>();
            return node;
        }
    }

    nextLexeme();

    matchRecoveryStopOnToken[Lexeme::SkinnyArrow]++;

    TempVector<AstType*> result(scratchType);
    TempVector<std::optional<AstArgumentName>> resultNames(scratchOptArgName);
    TempVector<Position> commaPositions(scratchPosition);
    TempVector<Position> nameColonPositions(scratchPosition2);
    AstTypePack* varargAnnotation = nullptr;

    // possibly () -> ReturnType
    if (lexer.current().type != ')')
    {
        if (options.storeCstData)
            varargAnnotation = parseTypeList(result, resultNames, &commaPositions, &nameColonPositions);
        else
            varargAnnotation = parseTypeList(result, resultNames);
    }

    const Location location{begin.location, lexer.current().location};
    bool closeParenFound = expectMatchAndConsume(')', begin, true);
    Position closeParenthesesPosition = closeParenFound ? lexer.previousLocation().begin : Position::missing();

    matchRecoveryStopOnToken[Lexeme::SkinnyArrow]--;

    if (lexer.current().type != Lexeme::SkinnyArrow && resultNames.empty())
    {
        // If it turns out that it's just '(A)', it's possible that there are unions/intersections to follow, so fold over it.
        if (result.size() == 1)
        {
            // TODO(CLI-140667): stop parsing type suffix when varargAnnotation != nullptr - this should be a parse error
            AstType* inner = nullptr;

            if (varargAnnotation == nullptr)
            {
                inner = allocator.alloc<AstTypeGroup>(location, result[0]);

                if (options.storeCstData)
                    cstNodeMap[inner] = allocator.alloc<CstTypeGroup>(closeParenFound ? closeParenthesesPosition : Position::missing());
            }
            else
                inner = result[0];

            AstType* returnType = parseTypeSuffix(inner, begin.location);

            if (DFFlag::DebugLuauReportReturnTypeVariadicWithTypeSuffix && varargAnnotation != nullptr &&
                (returnType->is<AstTypeUnion>() || returnType->is<AstTypeIntersection>()))
                luau_telemetry_parsed_return_type_variadic_with_type_suffix = true;

            // If parseType parses nothing, then returnType->location.end only points at the last non-type-pack
            // type to successfully parse.  We need the span of the whole annotation.
            Position endPos = result.size() == 1 ? location.end : returnType->location.end;

            AstTypePackExplicit* node =
                allocator.alloc<AstTypePackExplicit>(Location{location.begin, endPos}, AstTypeList{copy(&returnType, 1), varargAnnotation});
            if (options.storeCstData)
                cstNodeMap[node] = allocator.alloc<CstTypePackExplicit>();
            return node;
        }

        AstTypePackExplicit* node = allocator.alloc<AstTypePackExplicit>(location, AstTypeList{copy(result), varargAnnotation});
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypePackExplicit>(location.begin, closeParenthesesPosition, copy(commaPositions));
        return node;
    }

    Position returnArrowPosition = lexer.current().location.begin;
    AstType* tail = parseFunctionTypeTail(begin, {nullptr, 0}, {}, {}, copy(result), copy(resultNames), varargAnnotation);

    if (options.storeCstData && tail->is<AstTypeFunction>())
    {
        cstNodeMap[tail] = allocator.alloc<CstTypeFunction>(
            Position::missing(),
            AstArray<Position>{},
            Position::missing(),
            location.begin,
            copy(nameColonPositions),
            copy(commaPositions),
            closeParenthesesPosition,
            returnArrowPosition
        );
    }

    AstTypePackExplicit* node = allocator.alloc<AstTypePackExplicit>(Location{location, tail->location}, AstTypeList{copy(&tail, 1), nullptr});
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstTypePackExplicit>();
    return node;
}

std::pair<CstExprConstantString::QuoteStyle, unsigned int> Parser::extractStringDetails()
{
    CstExprConstantString::QuoteStyle style;
    unsigned int blockDepth = 0;

    switch (lexer.current().type)
    {
    case Lexeme::QuotedString:
        style = lexer.current().getQuoteStyle() == Lexeme::QuoteStyle::Double ? CstExprConstantString::QuoteStyle::QuotedDouble
                                                                              : CstExprConstantString::QuoteStyle::QuotedSingle;
        break;
    case Lexeme::InterpStringSimple:
        style = CstExprConstantString::QuoteStyle::QuotedInterp;
        break;
    case Lexeme::RawString:
    {
        style = CstExprConstantString::QuoteStyle::QuotedRaw;
        blockDepth = lexer.current().getBlockDepth();
        break;
    }
    default:
        LUAU_ASSERT(false && "Invalid string type");
    }

    return {style, blockDepth};
}

// TableIndexer ::= `[' Type `]' `:' Type
Parser::TableIndexerResult Parser::parseTableIndexer(AstTableAccess access, std::optional<Location> accessLocation, Lexeme begin)
{
    AstType* index = parseType();

    bool indexerCloseFound = expectMatchAndConsume(']', begin);
    Position indexerClosePosition = indexerCloseFound ? lexer.previousLocation().begin : Position::missing();

    bool colonFound = expectAndConsume(':', "table field");
    Position colonPosition = colonFound ? lexer.previousLocation().begin : Position::missing();

    AstType* result = parseType();

    return {
        allocator.alloc<AstTableIndexer>(AstTableIndexer{index, result, Location(begin.location, result->location), access, accessLocation}),
        begin.location.begin,
        indexerClosePosition,
        colonPosition,
    };
}

// TableProp ::= Name `:' Type
// TablePropOrIndexer ::= TableProp | TableIndexer
// PropList ::= TablePropOrIndexer {fieldsep TablePropOrIndexer} [fieldsep]
// TableType ::= `{' PropList `}'
AstType* Parser::parseTableType(bool inDeclarationContext)
{
    incrementRecursionCounter("type annotation");

    TempVector<AstTableProp> props(scratchTableTypeProps);
    TempVector<CstTypeTable::Item> cstItems(scratchCstTableTypeProps);
    AstTableIndexer* indexer = nullptr;

    Location start = lexer.current().location;

    MatchLexeme matchBrace = lexer.current();
    expectAndConsume('{', "table type");

    bool isArray = false;

    while (lexer.current().type != '}')
    {
        AstTableAccess access = AstTableAccess::ReadWrite;
        std::optional<Location> accessLocation;

        // Attributes come before the `read`/`write` modifier, so `@deprecated read x: T` reads in
        // the order it is written. Which entry kind they belong to isn't known yet, so they are
        // checked against both and pinned down in each branch below.
        AstArray<AstAttr*> attributes{nullptr, 0};
        if (FFlag::LuwuAttributesEverywhere && attributesFollow())
            attributes = parseAttributes(AstAttr::Context::TableTypeMember);

        if (lexer.current().type == Lexeme::Name && lexer.lookahead().type != ':')
        {
            if (AstName(lexer.current().name) == "read")
            {
                accessLocation = lexer.current().location;
                access = AstTableAccess::Read;
                lexer.next();
            }
            else if (AstName(lexer.current().name) == "write")
            {
                accessLocation = lexer.current().location;
                access = AstTableAccess::Write;
                lexer.next();
            }
        }

        if (lexer.current().type == '[')
        {
            const Lexeme begin = lexer.current();
            nextLexeme(); // [

            if ((lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString) && lexer.lookahead().type == ']')
            {
                CstExprConstantString::QuoteStyle style;
                unsigned int blockDepth = 0;
                if (options.storeCstData)
                    std::tie(style, blockDepth) = extractStringDetails();

                Position stringPosition = lexer.current().location.begin;
                AstArray<char> sourceString;
                std::optional<AstArray<char>> chars = parseCharArray(options.storeCstData ? &sourceString : nullptr);

                bool closingBracketFound = expectMatchAndConsume(']', begin);
                Position indexerClosePosition = closingBracketFound ? lexer.previousLocation().begin : Position::missing();
                bool colonFound = expectAndConsume(':', "table field");
                Position colonPosition = colonFound ? lexer.previousLocation().begin : Position::missing();

                AstType* type = parseType();

                // since AstName contains a char*, it can't contain null
                bool containsNull = chars && (memchr(chars->data, 0, chars->size) != nullptr);

                if (chars && !containsNull)
                {
                    validateAttributeContexts(attributes, AstAttr::Context::TableTypeField);
                    props.push_back(AstTableProp{AstName(chars->data), begin.location, type, access, accessLocation, attributes});
                    if (options.storeCstData)
                    {
                        CstExprTable::Separator separator = tableSeparator();
                        cstItems.push_back(
                            CstTypeTable::Item{
                                CstTypeTable::Item::Kind::StringProperty,
                                begin.location.begin,
                                indexerClosePosition,
                                colonPosition,
                                separator,
                                separator != CstExprTable::Separator::Missing ? lexer.current().location.begin : Position::missing(),
                                allocator.alloc<CstExprConstantString>(sourceString, style, blockDepth),
                                stringPosition
                            }
                        );
                    }
                }
                else
                    report(begin.location, "String literal contains malformed escape sequence or \\0");
            }
            else
            {
                if (indexer)
                {
                    // maybe we don't need to parse the entire badIndexer...
                    // however, we either have { or [ to lint, not the entire table type or the bad indexer.
                    AstTableIndexer* badIndexer = parseTableIndexer(access, accessLocation, begin).node;

                    // we lose all additional indexer expressions from the AST after error recovery here
                    report(badIndexer->location, "Cannot have more than one table indexer");
                }
                else
                {
                    auto tableIndexerResult = parseTableIndexer(access, accessLocation, begin);
                    indexer = tableIndexerResult.node;
                    validateAttributeContexts(attributes, AstAttr::Context::TableIndexer);
                    indexer->attributes = attributes;
                    if (options.storeCstData)
                    {
                        CstExprTable::Separator separator = tableSeparator();
                        cstItems.push_back(
                            CstTypeTable::Item{
                                CstTypeTable::Item::Kind::Indexer,
                                tableIndexerResult.indexerOpenPosition,
                                tableIndexerResult.indexerClosePosition,
                                tableIndexerResult.colonPosition,
                                separator,
                                separator != CstExprTable::Separator::Missing ? lexer.current().location.begin : Position::missing(),
                            }
                        );
                    }
                }
            }
        }
        else if (props.empty() && !indexer && !(lexer.current().type == Lexeme::Name && lexer.lookahead().type == ':'))
        {
            AstType* type = parseType();

            // array-like table type: {T} desugars into {[number]: T}
            isArray = true;
            Location nullTypeLocation = Location(start.begin, 0);
            AstType* index = allocator.alloc<AstTypeReference>(nullTypeLocation, std::nullopt, nameNumber, std::nullopt, nullTypeLocation);
            validateAttributeContexts(attributes, AstAttr::Context::TableIndexer);
            indexer = allocator.alloc<AstTableIndexer>(AstTableIndexer{index, type, type->location, access, accessLocation, attributes});
            break;
        }
        else
        {
            std::optional<Name> name = parseNameOpt("table field");

            if (!name)
                break;

            bool colonFound = expectAndConsume(':', "table field");
            Position colonPosition = colonFound ? lexer.previousLocation().begin : Position::missing();

            AstType* type = parseType(inDeclarationContext);

            validateAttributeContexts(attributes, AstAttr::Context::TableTypeField);
            props.push_back(AstTableProp{name->name, name->location, type, access, accessLocation, attributes});
            if (options.storeCstData)
            {
                CstExprTable::Separator separator = tableSeparator();
                cstItems.push_back(
                    CstTypeTable::Item{
                        CstTypeTable::Item::Kind::Property,
                        Position::missing(),
                        Position::missing(),
                        colonPosition,
                        separator,
                        separator != CstExprTable::Separator::Missing ? lexer.current().location.begin : Position::missing(),
                    }
                );
            }
        }

        if (lexer.current().type == ',' || lexer.current().type == ';')
        {
            nextLexeme();
        }
        else
        {
            if (lexer.current().type != '}')
                break;
        }
    }

    Location end = lexer.current().location;

    if (!expectMatchAndConsume('}', matchBrace, /* searchForMissing = */ true))
        end = lexer.previousLocation();

    AstTypeTable* node = allocator.alloc<AstTypeTable>(Location(start, end), copy(props), indexer);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstTypeTable>(copy(cstItems), isArray);
    return node;
}

// ReturnType ::= Type | `(' TypeList `)'
// FunctionType ::= [`<' varlist `>'] `(' [TypeList] `)' `->` ReturnType
AstTypeOrPack Parser::parseFunctionType(bool allowPack, const AstArray<AstAttr*>& attributes)
{
    incrementRecursionCounter("type annotation");

    bool forceFunctionType = lexer.current().type == '<';

    Lexeme begin = lexer.current();

    Position genericsOpenPosition = Position::missing();
    AstArray<Position> genericsCommaPositions;
    Position genericsClosePosition = Position::missing();
    auto [generics, genericPacks] = options.storeCstData
                                        ? parseGenericTypeList(
                                              /* withDefaultValues= */ false, &genericsOpenPosition, &genericsCommaPositions, &genericsClosePosition
                                          )
                                        : parseGenericTypeList(/* withDefaultValues= */ false);

    Lexeme parameterStart = lexer.current();

    bool openArgsFound = expectAndConsume('(', "function parameters");

    matchRecoveryStopOnToken[Lexeme::SkinnyArrow]++;

    TempVector<AstType*> params(scratchType);
    TempVector<std::optional<AstArgumentName>> names(scratchOptArgName);
    TempVector<Position> nameColonPositions(scratchPosition);
    TempVector<Position> argCommaPositions(scratchPosition2);
    AstTypePack* varargAnnotation = nullptr;

    if (lexer.current().type != ')')
    {
        if (options.storeCstData)
            varargAnnotation = parseTypeList(params, names, &argCommaPositions, &nameColonPositions);
        else
            varargAnnotation = parseTypeList(params, names);
    }

    Location closeArgsLocation = lexer.current().location;
    bool closeArgsFound = expectMatchAndConsume(')', parameterStart, true);

    matchRecoveryStopOnToken[Lexeme::SkinnyArrow]--;

    AstArray<AstType*> paramTypes = copy(params);

    if (!names.empty())
        forceFunctionType = true;

    bool returnTypeIntroducer = lexer.current().type == Lexeme::SkinnyArrow || lexer.current().type == ':';

    // Not a function at all. Just a parenthesized type. Or maybe a type pack with a single element
    if (params.size() == 1 && !varargAnnotation && !forceFunctionType && !returnTypeIntroducer)
    {
        if (allowPack)
        {
            AstTypePackExplicit* node = allocator.alloc<AstTypePackExplicit>(begin.location, AstTypeList{paramTypes, nullptr});
            if (options.storeCstData)
                cstNodeMap[node] = allocator.alloc<CstTypePackExplicit>(
                    openArgsFound ? parameterStart.location.begin : Position::missing(),
                    closeArgsFound ? closeArgsLocation.begin : Position::missing(),
                    copy(argCommaPositions)
                );
            return {{}, node};
        }
        else
        {
            AstTypeGroup* node = allocator.alloc<AstTypeGroup>(Location(parameterStart.location, closeArgsLocation), params[0]);

            if (options.storeCstData)
                cstNodeMap[node] = allocator.alloc<CstTypeGroup>(closeArgsFound ? closeArgsLocation.begin : Position::missing());

            return {node, {}};
        }
    }

    if (!forceFunctionType && !returnTypeIntroducer && allowPack)
    {
        AstTypePackExplicit* node = allocator.alloc<AstTypePackExplicit>(begin.location, AstTypeList{paramTypes, varargAnnotation});
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypePackExplicit>(
                openArgsFound ? parameterStart.location.begin : Position::missing(),
                closeArgsFound ? closeArgsLocation.begin : Position::missing(),
                copy(argCommaPositions)
            );
        return {{}, node};
    }

    AstArray<std::optional<AstArgumentName>> paramNames = copy(names);

    Position returnArrowPosition = lexer.current().location.begin;
    AstType* node = parseFunctionTypeTail(begin, attributes, generics, genericPacks, paramTypes, paramNames, varargAnnotation);
    if (options.storeCstData && node->is<AstTypeFunction>())
    {
        cstNodeMap[node] = allocator.alloc<CstTypeFunction>(
            genericsOpenPosition,
            genericsCommaPositions,
            genericsClosePosition,
            openArgsFound ? parameterStart.location.begin : Position::missing(),
            copy(nameColonPositions),
            copy(argCommaPositions),
            closeArgsFound ? closeArgsLocation.begin : Position::missing(),
            returnArrowPosition
        );
    }
    return {node, {}};
}

AstType* Parser::parseFunctionTypeTail(
    const Lexeme& begin,
    const AstArray<AstAttr*>& attributes,
    AstArray<AstGenericType*> generics,
    AstArray<AstGenericTypePack*> genericPacks,
    AstArray<AstType*> params,
    AstArray<std::optional<AstArgumentName>> paramNames,
    AstTypePack* varargAnnotation
)
{
    incrementRecursionCounter("type annotation");

    if (lexer.current().type == ':')
    {
        report(lexer.current().location, "Return types in function type annotations are written after '->' instead of ':'");
        lexer.next();
    }
    // Users occasionally write '()' as the 'unit' type when they actually want to use 'nil', here we'll try to give a more specific error
    else if (lexer.current().type != Lexeme::SkinnyArrow && generics.size == 0 && genericPacks.size == 0 && params.size == 0)
    {
        report(Location(begin.location, lexer.previousLocation()), "Expected '->' after '()' when parsing function type; did you mean 'nil'?");

        return allocator.alloc<AstTypeReference>(begin.location, std::nullopt, nameNil, std::nullopt, begin.location);
    }
    else
    {
        expectAndConsume(Lexeme::SkinnyArrow, "function type");
    }

    auto returnType = parseReturnType();
    LUAU_ASSERT(returnType);

    AstTypeList paramTypes = AstTypeList{params, varargAnnotation};
    return allocator.alloc<AstTypeFunction>(
        Location(begin.location, returnType->location), attributes, generics, genericPacks, paramTypes, paramNames, returnType
    );
}

static bool isTypeFollow(Lexeme::Type c)
{
    return c == '|' || c == '?' || c == '&';
}

// Type ::=
//      nil |
//      Name[`.' Name] [`<' namelist `>'] |
//      `{' [PropList] `}' |
//      `(' [TypeList] `)' `->` ReturnType
//      `typeof` Type
AstType* Parser::parseTypeSuffix(AstType* type, const Location& begin)
{
    TempVector<AstType*> parts(scratchType);
    TempVector<Position> separatorPositions(scratchPosition);
    Position leadingPosition = Position::missing();

    if (type != nullptr)
        parts.push_back(type);

    incrementRecursionCounter("type annotation");

    bool isUnion = false;
    bool isIntersection = false;
    unsigned int optionalCount = 0;

    Location location = begin;

    while (true)
    {
        Lexeme::Type c = lexer.current().type;
        Position separatorPosition = lexer.current().location.begin;
        if (c == '|')
        {
            nextLexeme();

            unsigned int oldRecursionCount = recursionCounter;
            parts.push_back(parseSimpleType(/* allowPack= */ false).type);
            recursionCounter = oldRecursionCount;

            isUnion = true;

            if (options.storeCstData)
            {
                if (type == nullptr && !leadingPosition.hasValue())
                    leadingPosition = separatorPosition;
                else
                    separatorPositions.push_back(separatorPosition);
            }
        }
        else if (c == '?')
        {
            LUAU_ASSERT(parts.size() >= 1);

            Location loc = lexer.current().location;
            nextLexeme();

            parts.push_back(allocator.alloc<AstTypeOptional>(Location(loc)));
            optionalCount++;

            isUnion = true;
        }
        else if (c == '&')
        {
            nextLexeme();

            unsigned int oldRecursionCount = recursionCounter;
            parts.push_back(parseSimpleType(/* allowPack= */ false).type);
            recursionCounter = oldRecursionCount;

            isIntersection = true;

            if (options.storeCstData)
            {
                if (type == nullptr && !leadingPosition.hasValue())
                    leadingPosition = separatorPosition;
                else
                    separatorPositions.push_back(separatorPosition);
            }
        }
        else if (c == Lexeme::Dot3)
        {
            report(lexer.current().location, "Unexpected '...' after type annotation");
            nextLexeme();
        }
        else
            break;

        if (parts.size() > unsigned(FInt::LuauTypeLengthLimit) + optionalCount)
            ParseError::raise(parts.back()->location, "Exceeded allowed type length; simplify your type annotation to make the code compile");
    }

    if (parts.size() == 1 && !isUnion && !isIntersection)
        return parts[0];
    if (isUnion && isIntersection)
    {
        return reportTypeError(
            Location(begin, parts.back()->location),
            copy(parts),
            "Mixing union and intersection types is not allowed; consider wrapping in parentheses."
        );
    }

    location.end = parts.back()->location.end;

    if (isUnion)
    {
        AstTypeUnion* node = allocator.alloc<AstTypeUnion>(location, copy(parts));
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypeUnion>(leadingPosition, copy(separatorPositions));
        return node;
    }

    if (isIntersection)
    {
        AstTypeIntersection* node = allocator.alloc<AstTypeIntersection>(location, copy(parts));
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypeIntersection>(leadingPosition, copy(separatorPositions));
        return node;
    }

    LUAU_ASSERT(false);
    ParseError::raise(begin, "Composite type was not an intersection or union.");
}

AstTypeOrPack Parser::parseSimpleTypeOrPack()
{
    unsigned int oldRecursionCount = recursionCounter;
    // recursion counter is incremented in parseSimpleType

    Location begin = lexer.current().location;

    auto [type, typePack] = parseSimpleType(/* allowPack= */ true);

    if (typePack)
    {
        LUAU_ASSERT(!type);
        return {{}, typePack};
    }

    recursionCounter = oldRecursionCount;

    return {parseTypeSuffix(type, begin), {}};
}

AstType* Parser::parseType(bool inDeclarationContext)
{
    unsigned int oldRecursionCount = recursionCounter;
    // recursion counter is incremented in parseSimpleType and/or parseTypeSuffix

    Location begin = lexer.current().location;

    AstType* type = nullptr;

    Lexeme::Type c = lexer.current().type;
    if (c != '|' && c != '&')
    {
        type = parseSimpleType(/* allowPack= */ false, /* in declaration context */ inDeclarationContext).type;
        recursionCounter = oldRecursionCount;
    }

    AstType* typeWithSuffix = parseTypeSuffix(type, begin);
    recursionCounter = oldRecursionCount;

    return typeWithSuffix;
}

// Type ::= nil | Name[`.' Name] [ `<' Type [`,' ...] `>' ] | `typeof' `(' expr `)' | `{' [PropList] `}'
//   | [`<' varlist `>'] `(' [TypeList] `)' `->` ReturnType
AstTypeOrPack Parser::parseSimpleType(bool allowPack, bool inDeclarationContext)
{
    incrementRecursionCounter("type annotation");

    Location start = lexer.current().location;

    AstArray<AstAttr*> attributes{nullptr, 0};

    if (lexer.current().type == Lexeme::Attribute || lexer.current().type == Lexeme::AttributeOpen)
    {
        if (!inDeclarationContext)
        {
            return {reportTypeError(start, {}, "attributes are not allowed in declaration context")};
        }
        else
        {
            attributes = Parser::parseAttributes(AstAttr::Context::Function);
            return parseFunctionType(allowPack, attributes);
        }
    }
    else if (lexer.current().type == Lexeme::ReservedNil)
    {
        nextLexeme();
        return {allocator.alloc<AstTypeReference>(start, std::nullopt, nameNil, std::nullopt, start), {}};
    }
    else if (lexer.current().type == Lexeme::ReservedTrue)
    {
        nextLexeme();
        return {allocator.alloc<AstTypeSingletonBool>(start, true)};
    }
    else if (lexer.current().type == Lexeme::ReservedFalse)
    {
        nextLexeme();
        return {allocator.alloc<AstTypeSingletonBool>(start, false)};
    }
    else if (lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString)
    {
        CstExprConstantString::QuoteStyle style;
        unsigned int blockDepth = 0;
        if (options.storeCstData)
            std::tie(style, blockDepth) = extractStringDetails();

        AstArray<char> originalString;
        if (std::optional<AstArray<char>> value = parseCharArray(options.storeCstData ? &originalString : nullptr))
        {
            AstArray<char> svalue = *value;
            auto node = allocator.alloc<AstTypeSingletonString>(start, svalue);
            if (options.storeCstData)
                cstNodeMap[node] = allocator.alloc<CstTypeSingletonString>(originalString, style, blockDepth);
            return {node};
        }
        else
            return {reportTypeError(start, {}, "String literal contains malformed escape sequence")};
    }
    else if (lexer.current().type == Lexeme::InterpStringBegin || lexer.current().type == Lexeme::InterpStringSimple)
    {
        parseInterpString();

        return {reportTypeError(start, {}, "Interpolated string literals cannot be used as types")};
    }
    else if (lexer.current().type == Lexeme::BrokenString)
    {
        nextLexeme();
        return {reportTypeError(start, {}, "Malformed string; did you forget to finish it?")};
    }
    else if (lexer.current().type == Lexeme::Name)
    {
        std::optional<AstName> prefix;
        Position prefixPointPosition = Position::missing();
        std::optional<Location> prefixLocation;
        AstLocal* prefixLocal = nullptr;
        Name name = parseName("type name");

        if (lexer.current().type == '.')
        {
            prefixPointPosition = lexer.current().location.begin;
            nextLexeme();

            prefix = name.name;
            prefixLocation = name.location;

            if (FFlag::LuauTrackPrefixLocal)
            {
                AstLocal* const* prefixLocalValue = localMap.find(name.name);
                prefixLocal = (prefixLocalValue && *prefixLocalValue) ? *prefixLocalValue : nullptr;
            }

            name = parseIndexName("field name", prefixPointPosition);
        }
        else if (lexer.current().type == Lexeme::Dot3)
        {
            report(lexer.current().location, "Unexpected '...' after type name; type pack is not allowed in this context");
            nextLexeme();
        }
        else if (name.name == "typeof")
        {
            Lexeme typeofBegin = lexer.current();
            bool openParenFound = expectAndConsume('(', "typeof type");

            AstExpr* expr = parseExpr();

            Location end = lexer.current().location;

            bool closeParenFound = expectMatchAndConsume(')', typeofBegin);

            AstTypeTypeof* node = allocator.alloc<AstTypeTypeof>(Location(start, end), expr);
            if (options.storeCstData)
            {
                cstNodeMap[node] = allocator.alloc<CstTypeTypeof>(
                    openParenFound ? typeofBegin.location.begin : Position::missing(), closeParenFound ? end.begin : Position::missing()
                );
            }
            return {node, {}};
        }

        bool hasParameters = false;
        AstArray<AstTypeOrPack> parameters{};
        Position parametersOpeningPosition = Position::missing();
        TempVector<Position> parametersCommaPositions(scratchPosition);
        Position parametersClosingPosition = Position::missing();

        if (lexer.current().type == '<')
        {
            hasParameters = true;
            if (options.storeCstData)
                parameters = parseTypeParams(&parametersOpeningPosition, &parametersCommaPositions, &parametersClosingPosition);
            else
                parameters = parseTypeParams();
        }

        Location end = lexer.previousLocation();

        AstTypeReference* node =
            allocator.alloc<AstTypeReference>(Location(start, end), prefix, name.name, prefixLocation, name.location, hasParameters, parameters, prefixLocal);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypeReference>(
                prefixPointPosition, parametersOpeningPosition, copy(parametersCommaPositions), parametersClosingPosition
            );
        return {node, {}};
    }
    else if (lexer.current().type == '{')
    {
        return {parseTableType(/* inDeclarationContext */ inDeclarationContext), {}};
    }
    else if (lexer.current().type == '(' || lexer.current().type == '<')
    {
        return parseFunctionType(allowPack, AstArray<AstAttr*>({nullptr, 0}));
    }
    else if (lexer.current().type == Lexeme::ReservedFunction)
    {
        nextLexeme();

        return {
            reportTypeError(
                start,
                {},
                "Using 'function' as a type annotation is not supported, consider replacing with a function type annotation e.g. '(...any) -> "
                "...any'"
            ),
            {}
        };
    }
    else
    {
        // For a missing type annotation, capture 'space' between last token and the next one
        Location astErrorlocation(lexer.previousLocation().end, start.begin);
        // The parse error includes the next lexeme to make it easier to display where the error is (e.g. in an IDE or a CLI error message).
        // Including the current lexeme also makes the parse error consistent with other parse errors returned by Luau.
        Location parseErrorLocation(lexer.previousLocation().end, start.end);
        return {reportMissingTypeError(parseErrorLocation, astErrorlocation, "Expected type, got %s", lexer.current().toString().c_str()), {}};
    }
}

AstTypePack* Parser::parseVariadicArgumentTypePack()
{
    // Generic: a...
    if (lexer.current().type == Lexeme::Name && lexer.lookahead().type == Lexeme::Dot3)
    {
        Name name = parseName("generic name");
        Location end = lexer.current().location;

        // This will not fail because of the lookahead guard.
        expectAndConsume(Lexeme::Dot3, "generic type pack annotation");
        AstTypePackGeneric* node = allocator.alloc<AstTypePackGeneric>(Location(name.location, end), name.name);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypePackGeneric>(end.begin);
        return node;
    }
    // Variadic: T
    else
    {
        AstType* variadicAnnotation = parseType();
        return allocator.alloc<AstTypePackVariadic>(variadicAnnotation->location, variadicAnnotation);
    }
}

AstTypePack* Parser::parseTypePack()
{
    // Variadic: ...T
    if (lexer.current().type == Lexeme::Dot3)
    {
        Location start = lexer.current().location;
        nextLexeme();
        AstType* varargTy = parseType();
        return allocator.alloc<AstTypePackVariadic>(Location(start, varargTy->location), varargTy);
    }
    // Generic: a...
    else if (lexer.current().type == Lexeme::Name && lexer.lookahead().type == Lexeme::Dot3)
    {
        Name name = parseName("generic name");
        Location end = lexer.current().location;

        // This will not fail because of the lookahead guard.
        expectAndConsume(Lexeme::Dot3, "generic type pack annotation");
        AstTypePackGeneric* node = allocator.alloc<AstTypePackGeneric>(Location(name.location, end), name.name);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstTypePackGeneric>(end.begin);
        return node;
    }

    // TODO: shouldParseTypePack can be removed and parseTypePack can be called unconditionally instead
    LUAU_ASSERT(!"parseTypePack can't be called if shouldParseTypePack() returned false");
    return nullptr;
}

std::optional<AstExprUnary::Op> Parser::parseUnaryOp(const Lexeme& l)
{
    if (l.type == Lexeme::ReservedNot)
        return AstExprUnary::Op::Not;
    else if (l.type == '-')
        return AstExprUnary::Op::Minus;
    else if (l.type == '#')
        return AstExprUnary::Op::Len;
    else
        return std::nullopt;
}

std::optional<AstExprBinary::Op> Parser::parseBinaryOp(const Lexeme& l)
{
    if (l.type == '+')
        return AstExprBinary::Add;
    else if (l.type == '-')
        return AstExprBinary::Sub;
    else if (l.type == '*')
        return AstExprBinary::Mul;
    else if (l.type == '/')
        return AstExprBinary::Div;
    else if (l.type == Lexeme::FloorDiv)
        return AstExprBinary::FloorDiv;
    else if (l.type == '%')
        return AstExprBinary::Mod;
    else if (l.type == '^')
        return AstExprBinary::Pow;
    else if (l.type == Lexeme::Dot2)
        return AstExprBinary::Concat;
    else if (l.type == Lexeme::NotEqual)
        return AstExprBinary::CompareNe;
    else if (l.type == Lexeme::Equal)
        return AstExprBinary::CompareEq;
    else if (l.type == '<')
        return AstExprBinary::CompareLt;
    else if (l.type == Lexeme::LessEqual)
        return AstExprBinary::CompareLe;
    else if (l.type == '>')
        return AstExprBinary::CompareGt;
    else if (l.type == Lexeme::GreaterEqual)
        return AstExprBinary::CompareGe;
    else if (l.type == Lexeme::ReservedAnd)
        return AstExprBinary::And;
    else if (l.type == Lexeme::ReservedOr)
        return AstExprBinary::Or;
    else
        return std::nullopt;
}

std::optional<AstExprBinary::Op> Parser::parseCompoundOp(const Lexeme& l)
{
    if (l.type == Lexeme::AddAssign)
        return AstExprBinary::Add;
    else if (l.type == Lexeme::SubAssign)
        return AstExprBinary::Sub;
    else if (l.type == Lexeme::MulAssign)
        return AstExprBinary::Mul;
    else if (l.type == Lexeme::DivAssign)
        return AstExprBinary::Div;
    else if (l.type == Lexeme::FloorDivAssign)
        return AstExprBinary::FloorDiv;
    else if (l.type == Lexeme::ModAssign)
        return AstExprBinary::Mod;
    else if (l.type == Lexeme::PowAssign)
        return AstExprBinary::Pow;
    else if (l.type == Lexeme::ConcatAssign)
        return AstExprBinary::Concat;
    else
        return std::nullopt;
}

std::optional<AstExprUnary::Op> Parser::checkUnaryConfusables()
{
    const Lexeme& curr = lexer.current();

    // early-out: need to check if this is a possible confusable quickly
    if (curr.type != '!')
        return {};

    // slow path: possible confusable
    Location start = curr.location;

    if (curr.type == '!')
    {
        report(start, "Unexpected '!'; did you mean 'not'?");
        return AstExprUnary::Op::Not;
    }

    return {};
}

std::optional<AstExprBinary::Op> Parser::checkBinaryConfusables(const BinaryOpPriority binaryPriority[], unsigned int limit)
{
    const Lexeme& curr = lexer.current();

    // early-out: need to check if this is a possible confusable quickly
    if (curr.type != '&' && curr.type != '|' && curr.type != '!')
        return {};

    // slow path: possible confusable
    Location start = curr.location;
    Lexeme next = lexer.lookahead();

    if (curr.type == '&' && next.type == '&' && curr.location.end == next.location.begin && binaryPriority[AstExprBinary::And].left > limit)
    {
        nextLexeme();
        report(Location(start, next.location), "Unexpected '&&'; did you mean 'and'?");
        return AstExprBinary::And;
    }
    else if (curr.type == '|' && next.type == '|' && curr.location.end == next.location.begin && binaryPriority[AstExprBinary::Or].left > limit)
    {
        nextLexeme();
        report(Location(start, next.location), "Unexpected '||'; did you mean 'or'?");
        return AstExprBinary::Or;
    }
    else if (curr.type == '!' && next.type == '=' && curr.location.end == next.location.begin &&
             binaryPriority[AstExprBinary::CompareNe].left > limit)
    {
        nextLexeme();
        report(Location(start, next.location), "Unexpected '!='; did you mean '~='?");
        return AstExprBinary::CompareNe;
    }

    return std::nullopt;
}

// subexpr -> (asexp | unop subexpr) { binop subexpr }
// where `binop' is any binary operator with a priority higher than `limit'
AstExpr* Parser::parseExpr(unsigned int limit)
{
    static const BinaryOpPriority binaryPriority[] = {
        {6, 6},  // '+'
        {6, 6},  // '-'
        {7, 7},  // '*'
        {7, 7},  // '/'
        {7, 7},  // '//'
        {7, 7},  // `%'
        {10, 9}, // power (right associative)
        {5, 4},  // concat (right associative)
        {3, 3},  // inequality
        {3, 3},  // equality
        {3, 3},  // '<'
        {3, 3},  // '<='
        {3, 3},  // '>'
        {3, 3},  // '>='
        {2, 2},  // logical and
        {1, 1}   // logical or
    };

    static_assert(sizeof(binaryPriority) / sizeof(binaryPriority[0]) == size_t(AstExprBinary::Op__Count), "binaryPriority needs an entry per op");

    unsigned int oldRecursionCount = recursionCounter;

    // this handles recursive calls to parseSubExpr/parseExpr
    incrementRecursionCounter("expression");

    const unsigned int unaryPriority = 8;

    Location start = lexer.current().location;

    AstExpr* expr;

    std::optional<AstExprUnary::Op> uop = parseUnaryOp(lexer.current());

    if (!uop)
        uop = checkUnaryConfusables();

    if (uop)
    {
        Position opPosition = lexer.current().location.begin;
        nextLexeme();

        AstExpr* subexpr = parseExpr(unaryPriority);

        expr = allocator.alloc<AstExprUnary>(Location(start, subexpr->location), *uop, subexpr);
        if (options.storeCstData)
            cstNodeMap[expr] = allocator.alloc<CstExprOp>(opPosition);
    }
    else
    {
        expr = parseAssertionExpr();
    }

    // expand while operators have priorities higher than `limit'
    std::optional<AstExprBinary::Op> op = parseBinaryOp(lexer.current());

    if (!op)
        op = checkBinaryConfusables(binaryPriority, limit);

    while (op && binaryPriority[*op].left > limit)
    {
        Position opPosition = lexer.current().location.begin;
        nextLexeme();

        // read sub-expression with higher priority
        AstExpr* next = parseExpr(binaryPriority[*op].right);

        expr = allocator.alloc<AstExprBinary>(Location(start, next->location), *op, expr, next);
        if (options.storeCstData)
            cstNodeMap[expr] = allocator.alloc<CstExprOp>(opPosition);
        op = parseBinaryOp(lexer.current());

        if (!op)
            op = checkBinaryConfusables(binaryPriority, limit);

        // note: while the parser isn't recursive here, we're generating recursive structures of unbounded depth
        incrementRecursionCounter("expression");
    }

    recursionCounter = oldRecursionCount;

    return expr;
}

// NAME
AstExpr* Parser::parseNameExpr(const char* context)
{
    std::optional<Name> name = parseNameOpt(context);

    if (!name)
        return allocator.alloc<AstExprError>(lexer.current().location, copy<AstExpr*>({}), unsigned(parseErrors.size() - 1));

    AstLocal* const* value = localMap.find(name->name);

    if (value && *value)
    {
        AstLocal* local = *value;

        if (local->functionDepth < typeFunctionDepth)
            return reportExprError(lexer.current().location, {}, "Type function cannot reference outer local '%s'", local->name.value);

        return allocator.alloc<AstExprLocal>(name->location, local, local->functionDepth != functionStack.size() - 1);
    }

    return allocator.alloc<AstExprGlobal>(name->location, name->name);
}

// prefixexp -> NAME | '(' expr ')'
AstExpr* Parser::parsePrefixExpr()
{
    if (lexer.current().type == '(')
    {
        Position start = lexer.current().location.begin;

        MatchLexeme matchParen = lexer.current();
        nextLexeme();

        AstExpr* expr = parseExpr();

        Position end = lexer.current().location.end;

        bool closeParenFound = false;

        if (lexer.current().type != ')')
        {
            const char* suggestion = (lexer.current().type == '=') ? "; did you mean to use '{' when defining a table?" : nullptr;

            expectMatchAndConsumeFail(static_cast<Lexeme::Type>(')'), matchParen, suggestion);

            end = lexer.previousLocation().end;
        }
        else
        {
            closeParenFound = true;

            nextLexeme();
        }

        AstExpr* exprGroup = allocator.alloc<AstExprGroup>(Location(start, end), expr);

        if (options.storeCstData)
            cstNodeMap[exprGroup] = allocator.alloc<CstExprGroup>(closeParenFound ? lexer.previousLocation().begin : Position::missing());

        return exprGroup;
    }
    else
    {
        return parseNameExpr("expression");
    }
}

// primaryexp -> prefixexp { `.' NAME | `[' exp `]' | `:' NAME funcargs | funcargs }
AstExpr* Parser::parsePrimaryExpr(bool asStatement)
{
    Position start = lexer.current().location.begin;

    AstExpr* expr = parsePrefixExpr();

    unsigned int oldRecursionCount = recursionCounter;

    while (true)
    {
        if (lexer.current().type == '.')
        {
            // Luwu Destructuring (rfcs/destructuring.md): `const .{` starts a destructuring declaration, and
            // `name.{` at the start of a statement is one missing its keyword. `.{` can't index anything.
            bool bareName = expr->is<AstExprGlobal>() || expr->is<AstExprLocal>();
            bool startsDestructuring = asStatement && bareName && destructurePatternFollows();
            if (startsDestructuring)
                break;

            Position opPosition = lexer.current().location.begin;
            nextLexeme();

            Name index = parseIndexName(nullptr, opPosition);

            expr = allocator.alloc<AstExprIndexName>(Location(start, index.location.end), expr, index.name, index.location, opPosition, '.');
        }
        else if (lexer.current().type == '[')
        {
            expr = parseIndexExpr(start, expr);
        }
        else if (lexer.current().type == ':')
        {
            expr = parseMethodCall(start, expr);
        }
        else if (lexer.current().type == '(')
        {
            // This error is handled inside 'parseFunctionArgs' as well, but for better error recovery we need to break out the current loop here
            if (!asStatement && expr->location.end.line != lexer.current().location.begin.line)
            {
                reportAmbiguousCallError();
                break;
            }

            expr = parseFunctionArgs(expr, false);
        }
        else if (lexer.current().type == '{' || lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString)
        {
            expr = parseFunctionArgs(expr, false);
        }
        else if (lexer.current().type == '<' && lexer.lookahead().type == '<')
        {
            expr = parseExplicitTypeInstantiationExpr(start, *expr);
        }
        else
        {
            break;
        }

        // note: while the parser isn't recursive here, we're generating recursive structures of unbounded depth
        incrementRecursionCounter("expression");
    }

    recursionCounter = oldRecursionCount;

    return expr;
}

LUAU_NOINLINE AstExpr* Parser::parseIndexExpr(Position start, AstExpr* expr)
{
    MatchLexeme matchBracket = lexer.current();
    nextLexeme();

    AstExpr* index = parseExpr();

    Position closeBracketPosition = lexer.current().location.begin;
    Position end = lexer.current().location.end;

    bool closingBracketFound = expectMatchAndConsume(']', matchBracket);

    expr = allocator.alloc<AstExprIndexExpr>(Location(start, end), expr, index);
    if (options.storeCstData)
        cstNodeMap[expr] = allocator.alloc<CstExprIndexExpr>(matchBracket.position, closingBracketFound ? closeBracketPosition : Position::missing());

    return expr;
}

AstExpr* Parser::parseMethodCall(Position start, AstExpr* expr)
{
    Position opPosition = lexer.current().location.begin;
    nextLexeme();

    Name index = parseIndexName("method name", opPosition);
    AstExpr* func = allocator.alloc<AstExprIndexName>(Location(start, index.location.end), expr, index.name, index.location, opPosition, ':');

    AstArray<AstTypeOrPack> typeArguments;
    CstTypeInstantiation* cstTypeArguments = options.storeCstData ? allocator.alloc<CstTypeInstantiation>() : nullptr;

    if (lexer.current().type == '<' && lexer.lookahead().type == '<')
    {
        typeArguments = parseTypeInstantiationExpr(cstTypeArguments);
    }

    expr = parseFunctionArgs(func, true);

    if (options.storeCstData)
    {
        CstNode** cstNode = cstNodeMap.find(expr);
        if (cstNode)
        {
            CstExprCall* exprCall = (*cstNode)->as<CstExprCall>();
            LUAU_ASSERT(exprCall);
            exprCall->explicitTypes = cstTypeArguments;
        }
    }

    // If we have an AstExprCall, fill in the type arguments
    if (auto call = expr->as<AstExprCall>(); call && typeArguments.size > 0)
        call->typeArguments = typeArguments;

    return expr;
}

// asexp -> simpleexp [`::' Type]
AstExpr* Parser::parseAssertionExpr()
{
    Location start = lexer.current().location;
    AstExpr* expr = parseSimpleExpr();

    if (lexer.current().type == Lexeme::DoubleColon)
    {
        Position opPosition = lexer.current().location.begin;
        nextLexeme();
        AstType* annotation = parseType();
        AstExprTypeAssertion* node = allocator.alloc<AstExprTypeAssertion>(Location(start, annotation->location), expr, annotation);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprTypeAssertion>(opPosition);
        return node;
    }
    else
        return expr;
}

static ConstantNumberParseResult parseInteger(double& result, const char* data, int base)
{
    LUAU_ASSERT(base == 2 || base == 16);

    if (FFlag::LuauNoDuplicateBinaryPrefix)
    {
        // Some libc implementations accept an optional 0b prefix for base-2 parsing.
        // Binary literals have already had their leading 0b stripped by us.
        if (base == 2 && data[0] == '0' && (data[1] == 'b' || data[1] == 'B'))
            return ConstantNumberParseResult::Malformed;
    }

    char* end = nullptr;
    unsigned long long value = strtoull(data, &end, base);

    if (*end != 0)
        return ConstantNumberParseResult::Malformed;

    result = double(value);

    if (value == ULLONG_MAX && errno == ERANGE)
    {
        // 'errno' might have been set before we called 'strtoull', but we don't want the overhead of resetting a TLS variable on each call
        // so we only reset it when we get a result that might be an out-of-range error and parse again to make sure
        errno = 0;
        value = strtoull(data, &end, base);

        if (errno == ERANGE)
            return base == 2 ? ConstantNumberParseResult::BinOverflow : ConstantNumberParseResult::HexOverflow;
    }

    if (value >= (1ull << 53) && static_cast<unsigned long long>(result) != value)
        return ConstantNumberParseResult::Imprecise;

    return ConstantNumberParseResult::Ok;
}

static ConstantNumberParseResult parseInteger64(int64_t& result, const char* data, int base)
{
    LUAU_ASSERT(base == 2 || base == 10 || base == 16);

    char* end = nullptr;

    if (base == 10)
    {
        result = strtoll(data, &end, 10);

        if (end == data || *end != 'i' || end[1] != '\0')
            return ConstantNumberParseResult::Malformed;

        if (((result == LLONG_MIN) || (result == LLONG_MAX)) && (errno == ERANGE))
        {
            // 'errno' might have been set before we called 'strtoll', but we don't want the overhead of resetting a TLS variable on each call
            // so we only reset it when we get a result that might be an out-of-range error and parse again to make sure
            errno = 0;
            result = strtoll(data, &end, 10);

            if (errno == ERANGE)
                return ConstantNumberParseResult::IntOverflow;
        }
    }
    else
    {
        if (FFlag::LuauNoDuplicateBinaryPrefix)
        {
            if (base == 2 && data[0] == '0' && (data[1] == 'b' || data[1] == 'B'))
                return ConstantNumberParseResult::Malformed;
        }

        // hex and binary literals represent bit patterns covering the full uint64 range
        unsigned long long u = strtoull(data, &end, base);

        if (end == data || *end != 'i' || end[1] != '\0')
            return ConstantNumberParseResult::Malformed;

        if ((u == ULLONG_MAX) && (errno == ERANGE))
        {
            // 'errno' might have been set before we called 'strtoull', but we don't want the overhead of resetting a TLS variable on each call
            // so we only reset it when we get a result that might be an out-of-range error and parse again to make sure
            errno = 0;
            u = strtoull(data, &end, base);

            if (errno == ERANGE)
                return base == 2 ? ConstantNumberParseResult::BinOverflow : ConstantNumberParseResult::HexOverflow;
        }

        result = (int64_t)u;
    }

    return ConstantNumberParseResult::Ok;
}

static ConstantNumberParseResult parseDouble(double& result, const char* data)
{
    // binary literal
    if (data[0] == '0' && (data[1] == 'b' || data[1] == 'B') && data[2])
        return parseInteger(result, data + 2, 2);

    // hexadecimal literal
    if (data[0] == '0' && (data[1] == 'x' || data[1] == 'X') && data[2])
        return parseInteger(result, data, 16); // pass in '0x' prefix, it's handled by 'strtoull'

    char* end = nullptr;
    double value = strtod(data, &end);

    // trailing non-numeric characters
    if (*end != 0)
        return ConstantNumberParseResult::Malformed;

    result = value;

    // for linting, we detect integer constants that are parsed imprecisely
    // since the check is expensive we only perform it when the number is larger than the precise integer range
    if (value >= double(1ull << 53) && strspn(data, "0123456789") == strlen(data))
    {
        char repr[512];
        snprintf(repr, sizeof(repr), "%.0f", value);

        if (strcmp(repr, data) != 0)
            return ConstantNumberParseResult::Imprecise;
    }

    return ConstantNumberParseResult::Ok;
}

// LUAU_NOINLINE is used to limit the stack cost of parseSimpleExpr which is on the recursive expression-parsing path
LUAU_NOINLINE AstExpr* Parser::parseAttributedFunction(const Location& start)
{
    AstArray<AstAttr*> attributes{nullptr, 0};
    TempVector<CstAttrList*> cstAttrLists(scratchCstAttrList);

    attributes = parseAttributes(AstAttr::Context::InlinableFunction, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);

    if (lexer.current().type != Lexeme::ReservedFunction)
    {
        return reportExprError(start, {}, "Expected 'function' declaration after attribute, but got %s instead", lexer.current().toString().c_str());
    }

    Lexeme matchFunction = lexer.current();
    nextLexeme();

    return parseFunctionBody(false, matchFunction, AstName(), nullptr, attributes, false, FFlag::LuauCstAttr ? &cstAttrLists : nullptr).first;
}

// simpleexp -> NUMBER | STRING | NIL | true | false | ... | constructor | [attributes] FUNCTION body | primaryexp
AstExpr* Parser::parseSimpleExpr()
{
    Location start = lexer.current().location;

    if (lexer.current().type == Lexeme::Attribute || lexer.current().type == Lexeme::AttributeOpen)
    {
        return parseAttributedFunction(start);
    }

    if (lexer.current().type == Lexeme::ReservedNil)
    {
        nextLexeme();

        return allocator.alloc<AstExprConstantNil>(start);
    }
    else if (lexer.current().type == Lexeme::ReservedTrue)
    {
        nextLexeme();

        return allocator.alloc<AstExprConstantBool>(start, true);
    }
    else if (lexer.current().type == Lexeme::ReservedFalse)
    {
        nextLexeme();

        return allocator.alloc<AstExprConstantBool>(start, false);
    }
    else if (lexer.current().type == Lexeme::ReservedFunction)
    {
        Lexeme matchFunction = lexer.current();
        nextLexeme();

        // Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): a table entry that parsed
        // attributes in front of this function hands them over.
        AstArray<AstAttr*> attributes{nullptr, 0};
        TempVector<CstAttrList*>* cstAttrLists = nullptr;
        if (pendingFunctionAttributes)
        {
            attributes = pendingFunctionAttributes->attributes;
            cstAttrLists = pendingFunctionAttributes->cstAttrLists;
            pendingFunctionAttributes.reset();
        }

        return parseFunctionBody(false, matchFunction, AstName(), nullptr, attributes, false, cstAttrLists).first;
    }
    else if (lexer.current().type == Lexeme::Number)
    {
        return parseNumber();
    }
    else if (lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString ||
             lexer.current().type == Lexeme::InterpStringSimple)
    {
        return parseString();
    }
    else if (lexer.current().type == Lexeme::InterpStringBegin)
    {
        return parseInterpString();
    }
    else if (lexer.current().type == Lexeme::BrokenString)
    {
        nextLexeme();
        return reportExprError(start, {}, "Malformed string; did you forget to finish it?");
    }
    else if (lexer.current().type == Lexeme::BrokenInterpDoubleBrace)
    {
        nextLexeme();
        return reportExprError(start, {}, "Double braces are not permitted within interpolated strings; did you mean '\\{'?");
    }
    else if (lexer.current().type == Lexeme::Dot3)
    {
        if (functionStack.back().vararg)
        {
            nextLexeme();

            return allocator.alloc<AstExprVarargs>(start);
        }
        else
        {
            nextLexeme();

            return reportExprError(start, {}, "Cannot use '...' outside of a vararg function");
        }
    }
    else if (lexer.current().type == '{')
    {
        return parseTableConstructor();
    }
    else if (lexer.current().type == Lexeme::ReservedIf)
    {
        return parseIfElseExpr();
    }
    else
    {
        return parsePrimaryExpr(/* asStatement= */ false);
    }
}

std::tuple<AstArray<AstExpr*>, Location, Location> Parser::parseCallList(TempVector<Position>* commaPositions, Position* closeParenPosition)
{
    LUAU_ASSERT(closeParenPosition != nullptr ? FFlag::LuauCstAttr : true);
    LUAU_ASSERT(
        lexer.current().type == '(' || lexer.current().type == '{' || lexer.current().type == Lexeme::RawString ||
        lexer.current().type == Lexeme::QuotedString
    );
    if (lexer.current().type == '(')
    {
        Position argStart = lexer.current().location.end;

        MatchLexeme matchParen = lexer.current();
        nextLexeme();

        TempVector<AstExpr*> args(scratchExpr);

        if (lexer.current().type != ')')
            parseExprList(args, commaPositions);

        Location end = lexer.current().location;
        Position argEnd = end.end;

        bool closeParenFound = expectMatchAndConsume(')', matchParen);
        if (FFlag::LuauCstAttr && closeParenPosition && closeParenFound)
            *closeParenPosition = end.begin;

        return {copy(args), Location(argStart, argEnd), Location(matchParen.position, lexer.previousLocation().begin)};
    }
    else if (lexer.current().type == '{')
    {
        Position argStart = lexer.current().location.end;
        AstExpr* expr = parseTableConstructor();
        Position argEnd = lexer.previousLocation().end;

        return {copy(&expr, 1), Location(argStart, argEnd), expr->location};
    }
    else
    {
        Location argLocation = lexer.current().location;
        AstExpr* expr = parseString();
        return {copy(&expr, 1), argLocation, expr->location};
    }
}

// args ::=  `(' [explist] `)' | tableconstructor | String
AstExpr* Parser::parseFunctionArgs(AstExpr* func, bool self)
{
    if (lexer.current().type == '(')
    {
        Position argStart = lexer.current().location.end;
        if (func->location.end.line != lexer.current().location.begin.line)
            reportAmbiguousCallError();

        MatchLexeme matchParen = lexer.current();
        nextLexeme();

        TempVector<AstExpr*> args(scratchExpr);
        TempVector<Position> commaPositions(scratchPosition);

        if (lexer.current().type != ')')
            parseExprList(args, options.storeCstData ? &commaPositions : nullptr);

        Location end = lexer.current().location;
        Position argEnd = end.end;

        bool closingParenFound = expectMatchAndConsume(')', matchParen);

        AstExprCall* node = allocator.alloc<AstExprCall>(
            Location(func->location, end), func, copy(args), self, AstArray<AstTypeOrPack>{}, Location(argStart, argEnd)
        );
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprCall>(
                matchParen.position, closingParenFound ? lexer.previousLocation().begin : Position::missing(), copy(commaPositions)
            );
        return node;
    }
    else if (lexer.current().type == '{')
    {
        Position argStart = lexer.current().location.end;
        AstExpr* expr = parseTableConstructor();
        Position argEnd = lexer.previousLocation().end;

        AstExprCall* node = allocator.alloc<AstExprCall>(
            Location(func->location, expr->location), func, copy(&expr, 1), self, AstArray<AstTypeOrPack>{}, Location(argStart, argEnd)
        );
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprCall>(Position::missing(), Position::missing(), AstArray<Position>{nullptr, 0});
        return node;
    }
    else if (lexer.current().type == Lexeme::RawString || lexer.current().type == Lexeme::QuotedString)
    {
        Location argLocation = lexer.current().location;
        AstExpr* expr = parseString();

        AstExprCall* node = allocator.alloc<AstExprCall>(
            Location(func->location, expr->location), func, copy(&expr, 1), self, AstArray<AstTypeOrPack>{}, argLocation
        );
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprCall>(Position::missing(), Position::missing(), AstArray<Position>{nullptr, 0});
        return node;
    }
    else
    {
        return reportFunctionArgsError(func, self);
    }
}

LUAU_NOINLINE AstExpr* Parser::reportFunctionArgsError(AstExpr* func, bool self)
{
    if (self && lexer.current().location.begin.line != func->location.end.line)
    {
        return reportExprError(func->location, copy({func}), "Expected function call arguments after '('");
    }
    else
    {
        return reportExprError(
            Location(func->location.begin, lexer.current().location.begin),
            copy({func}),
            "Expected '(', '{' or <string> when parsing function call, got %s",
            lexer.current().toString().c_str()
        );
    }
}

LUAU_NOINLINE void Parser::reportAmbiguousCallError()
{
    report(
        lexer.current().location,
        "Ambiguous syntax: this looks like an argument list for a function call, but could also be a start of "
        "new statement; use ';' to separate statements"
    );
}

CstExprTable::Separator Parser::tableSeparator()
{
    if (lexer.current().type == ',')
        return CstExprTable::Separator::Comma;
    else if (lexer.current().type == ';')
        return CstExprTable::Separator::Semicolon;
    else
        return CstExprTable::Separator::Missing;
}

// tableconstructor ::= `{' [fieldlist] `}'
// fieldlist ::= field {fieldsep field} [fieldsep]
// field ::= `[' exp `]' `=' exp | Name `=' exp | exp
// fieldsep ::= `,' | `;'
AstExpr* Parser::parseTableConstructor()
{
    TempVector<AstExprTable::Item> items(scratchItem);
    TempVector<CstExprTable::Item> cstItems(scratchCstItem);

    Location start = lexer.current().location;

    MatchLexeme matchBrace = lexer.current();
    expectAndConsume('{', "table literal");
    // Clip with LuauTableEntriesDontNeedToMatchIndent
    unsigned lastElementIndent_DEPRECATED = 0;

    while (lexer.current().type != '}')
    {
        if (!FFlag::LuauTableEntriesDontNeedToMatchIndent)
            lastElementIndent_DEPRECATED = lexer.current().location.begin.column;

        AstArray<AstAttr*> attributes{nullptr, 0};
        TempVector<CstAttrList*> cstAttrLists(scratchCstAttrList);
        if (FFlag::LuwuAttributesEverywhere && attributesFollow())
        {
            attributes = parseAttributes(AstAttr::Context::TableEntry, FFlag::LuauCstAttr ? &cstAttrLists : nullptr);

            // In a list entry, attributes directly in front of `function` belong to the function, as
            // they do wherever else a function expression is written.
            if (lexer.current().type == Lexeme::ReservedFunction)
            {
                validateAttributeContexts(attributes, AstAttr::Context::InlinableFunction);
                pendingFunctionAttributes = PendingFunctionAttributes{attributes, FFlag::LuauCstAttr ? &cstAttrLists : nullptr};
                attributes = {nullptr, 0};
            }
            else
                validateAttributeContexts(attributes, AstAttr::Context::TableField);
        }

        if (lexer.current().type == '[')
        {
            Position indexerOpenPosition = lexer.current().location.begin;
            MatchLexeme matchLocationBracket = lexer.current();
            nextLexeme();

            AstExpr* key = parseExpr();

            bool closingBracketFound = expectMatchAndConsume(']', matchLocationBracket);
            Position indexerClosePosition = closingBracketFound ? lexer.previousLocation().begin : Position::missing();

            bool equalsFound = expectAndConsume('=', "table field");
            Position equalsPosition = equalsFound ? lexer.previousLocation().begin : Position::missing();

            AstExpr* value = parseExpr();

            items.push_back({AstExprTable::Item::Kind::General, key, value, attributes});
            if (options.storeCstData)
            {
                CstExprTable::Separator separator = tableSeparator();
                cstItems.push_back(
                    {indexerOpenPosition,
                     indexerClosePosition,
                     equalsPosition,
                     separator,
                     separator == CstExprTable::Separator::Missing ? Position::missing() : lexer.current().location.begin}
                );
            }
        }
        else if (lexer.current().type == Lexeme::Name && lexer.lookahead().type == '=')
        {
            Name name = parseName("table field");

            Position equalsPosition = lexer.current().location.begin;
            expectAndConsume('=', "table field");

            AstArray<char> nameString;
            nameString.data = const_cast<char*>(name.name.value);
            nameString.size = strlen(name.name.value);

            AstExpr* key = allocator.alloc<AstExprConstantString>(name.location, nameString, AstExprConstantString::QuoteStyle::Unquoted);
            AstExpr* value = parseExpr();

            if (AstExprFunction* func = value->as<AstExprFunction>())
                func->debugname = name.name;

            items.push_back({AstExprTable::Item::Kind::Record, key, value, attributes});
            if (options.storeCstData)
            {
                CstExprTable::Separator separator = tableSeparator();
                cstItems.push_back(
                    {Position::missing(),
                     Position::missing(),
                     equalsPosition,
                     separator,
                     separator == CstExprTable::Separator::Missing ? Position::missing() : lexer.current().location.begin}
                );
            }
        }
        else
        {
            AstExpr* expr = parseExpr();
            LUAU_ASSERT(!pendingFunctionAttributes);

            items.push_back({AstExprTable::Item::Kind::List, nullptr, expr, attributes});
            if (options.storeCstData)
            {
                CstExprTable::Separator separator = tableSeparator();
                cstItems.push_back(
                    {Position::missing(),
                     Position::missing(),
                     Position::missing(),
                     separator,
                     separator == CstExprTable::Separator::Missing ? Position::missing() : lexer.current().location.begin}
                );
            }
        }

        if (lexer.current().type == ',' || lexer.current().type == ';')
        {
            nextLexeme();
        }
        else if ((lexer.current().type == '[' || lexer.current().type == Lexeme::Name) &&
                 (FFlag::LuauTableEntriesDontNeedToMatchIndent ? true : lexer.current().location.begin.column == lastElementIndent_DEPRECATED))
        {
            report(lexer.current().location, "Expected ',' after table constructor element");
        }
        else if (lexer.current().type != '}')
        {
            break;
        }
    }

    Location end = lexer.current().location;

    if (!expectMatchAndConsume('}', matchBrace))
        end = lexer.previousLocation();

    AstExprTable* node = allocator.alloc<AstExprTable>(Location(start, end), copy(items));
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstExprTable>(copy(cstItems));
    return node;
}

AstExpr* Parser::parseIfElseExpr()
{
    bool hasElse = false;
    Location start = lexer.current().location;

    nextLexeme(); // skip if / elseif

    AstExpr* condition = parseExpr();

    bool hasThen = expectAndConsume(Lexeme::ReservedThen, "if then else expression");
    std::optional<Location> thenLocation = hasThen ? std::optional<Location>(lexer.previousLocation()) : std::nullopt;
    Position thenPosition = hasThen ? lexer.previousLocation().begin : Position::missing();

    AstExpr* trueExpr = parseExpr();
    AstExpr* falseExpr = nullptr;

    Position elsePosition = lexer.current().location.begin;
    // An `elseif` clause parses into a nested AstExprIfElse whose own ifLocation is the `elseif`
    // token, so this node has no `else` keyword of its own to record.
    std::optional<Location> elseLocation = std::nullopt;
    bool isElseIf = false;
    if (lexer.current().type == Lexeme::ReservedElseif)
    {
        unsigned int oldRecursionCount = recursionCounter;
        incrementRecursionCounter("expression");
        hasElse = true;
        falseExpr = parseIfElseExpr();
        recursionCounter = oldRecursionCount;
        isElseIf = true;
    }
    else
    {
        hasElse = expectAndConsume(Lexeme::ReservedElse, "if then else expression");
        if (hasElse)
            elseLocation = lexer.previousLocation();
        falseExpr = parseExpr();
    }

    Location end = falseExpr->location;

    AstExprIfElse* node =
        allocator.alloc<AstExprIfElse>(Location(start, end), condition, hasThen, trueExpr, hasElse, falseExpr, start, thenLocation, elseLocation);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstExprIfElse>(thenPosition, elsePosition, isElseIf);
    return node;
}

// Name
std::optional<Parser::Name> Parser::parseNameOpt(const char* context)
{
    if (lexer.current().type != Lexeme::Name)
    {
        reportNameError(context);

        return {};
    }

    Name result(AstName(lexer.current().name), lexer.current().location);

    nextLexeme();

    return result;
}

Parser::Name Parser::parseName(const char* context)
{
    if (std::optional<Name> name = parseNameOpt(context))
        return *name;

    Location location = lexer.current().location;
    location.end = location.begin;

    return Name(nameError, location);
}

Parser::Name Parser::parseIndexName(const char* context, const Position& previous)
{
    if (std::optional<Name> name = parseNameOpt(context))
        return *name;

    // If we have a reserved keyword next at the same line, assume it's an incomplete name
    if (lexer.current().type >= Lexeme::Reserved_BEGIN && lexer.current().type < Lexeme::Reserved_END &&
        lexer.current().location.begin.line == previous.line)
    {
        Name result(AstName(lexer.current().name), lexer.current().location);

        nextLexeme();

        return result;
    }

    Location location = lexer.current().location;
    location.end = location.begin;

    return Name(nameError, location);
}

std::pair<AstArray<AstGenericType*>, AstArray<AstGenericTypePack*>> Parser::parseGenericTypeList(
    bool withDefaultValues,
    Position* openPosition,
    AstArray<Position>* commaPositions,
    Position* closePosition
)
{
    TempVector<AstGenericType*> names{scratchGenericTypes};
    TempVector<AstGenericTypePack*> namePacks{scratchGenericTypePacks};
    TempVector<Position> localCommaPositions{scratchPosition};

    if (lexer.current().type == '<')
    {
        Lexeme begin = lexer.current();
        if (openPosition)
            *openPosition = begin.location.begin;
        nextLexeme();

        bool seenPack = false;
        bool seenDefault = false;

        while (true)
        {
            Location nameLocation = lexer.current().location;
            AstName name = parseName().name;
            if (lexer.current().type == Lexeme::Dot3 || seenPack)
            {
                seenPack = true;

                Position ellipsisPosition = Position::missing();
                if (lexer.current().type != Lexeme::Dot3)
                    report(lexer.current().location, "Generic types come before generic type packs");
                else
                {
                    ellipsisPosition = lexer.current().location.begin;
                    nextLexeme();
                }

                if (withDefaultValues && lexer.current().type == '=')
                {
                    seenDefault = true;
                    Position equalsPosition = lexer.current().location.begin;
                    nextLexeme();

                    if (shouldParseTypePack(lexer))
                    {
                        AstTypePack* typePack = parseTypePack();

                        AstGenericTypePack* node = allocator.alloc<AstGenericTypePack>(nameLocation, name, typePack);
                        if (options.storeCstData)
                            cstNodeMap[node] = allocator.alloc<CstGenericTypePack>(ellipsisPosition, equalsPosition);
                        namePacks.push_back(node);
                    }
                    else
                    {
                        auto [type, typePack] = parseSimpleTypeOrPack();

                        if (type)
                            report(type->location, "Expected type pack after '=', got type");

                        AstGenericTypePack* node = allocator.alloc<AstGenericTypePack>(nameLocation, name, typePack);
                        if (options.storeCstData)
                            cstNodeMap[node] = allocator.alloc<CstGenericTypePack>(ellipsisPosition, equalsPosition);
                        namePacks.push_back(node);
                    }
                }
                else
                {
                    if (seenDefault)
                        report(lexer.current().location, "Expected default type pack after type pack name");

                    AstGenericTypePack* node = allocator.alloc<AstGenericTypePack>(nameLocation, name, nullptr);
                    if (options.storeCstData)
                        cstNodeMap[node] = allocator.alloc<CstGenericTypePack>(ellipsisPosition, Position::missing());
                    namePacks.push_back(node);
                }
            }
            else
            {
                if (withDefaultValues && lexer.current().type == '=')
                {
                    seenDefault = true;
                    Position equalsPosition = lexer.current().location.begin;
                    nextLexeme();

                    AstType* defaultType = parseType();

                    AstGenericType* node = allocator.alloc<AstGenericType>(nameLocation, name, defaultType);
                    if (options.storeCstData)
                        cstNodeMap[node] = allocator.alloc<CstGenericType>(equalsPosition);
                    names.push_back(node);
                }
                else
                {
                    if (seenDefault)
                        report(lexer.current().location, "Expected default type after type name");

                    AstGenericType* node = allocator.alloc<AstGenericType>(nameLocation, name, nullptr);
                    if (options.storeCstData)
                        cstNodeMap[node] = allocator.alloc<CstGenericType>(Position::missing());
                    names.push_back(node);
                }
            }

            if (lexer.current().type == ',')
            {
                if (commaPositions)
                    localCommaPositions.push_back(lexer.current().location.begin);
                nextLexeme();

                if (lexer.current().type == '>')
                {
                    report(lexer.current().location, "Expected type after ',' but got '>' instead");
                    break;
                }
            }
            else
                break;
        }

        bool closingBracketFound = expectMatchAndConsume('>', begin);
        if (closePosition && closingBracketFound)
            *closePosition = lexer.previousLocation().begin;
    }

    if (commaPositions)
        *commaPositions = copy(localCommaPositions);

    AstArray<AstGenericType*> generics = copy(names);
    AstArray<AstGenericTypePack*> genericPacks = copy(namePacks);
    return {generics, genericPacks};
}

AstArray<AstTypeOrPack> Parser::parseTypeParams(Position* openingPosition, TempVector<Position>* commaPositions, Position* closingPosition)
{
    TempVector<AstTypeOrPack> parameters{scratchTypeOrPack};

    if (lexer.current().type == '<')
    {
        Lexeme begin = lexer.current();
        if (openingPosition)
            *openingPosition = begin.location.begin;
        nextLexeme();

        while (true)
        {
            if (shouldParseTypePack(lexer))
            {
                AstTypePack* typePack = parseTypePack();
                parameters.push_back({{}, typePack});
            }
            else if (lexer.current().type == '(')
            {
                Location begin = lexer.current().location;
                AstType* type = nullptr;
                AstTypePack* typePack = nullptr;
                Lexeme::Type c = lexer.current().type;

                if (c != '|' && c != '&')
                {
                    auto typeOrTypePack = parseSimpleType(/* allowPack */ true, /* inDeclarationContext */ false);
                    type = typeOrTypePack.type;
                    typePack = typeOrTypePack.typePack;
                }

                // Consider the following type:
                //
                //  X<(T)>
                //
                // Is this a type pack or a parenthesized type? The
                // assumption will be a type pack, as that's what allows one
                // to express either a singular type pack or a potential
                // complex type.

                if (typePack)
                {
                    auto explicitTypePack = typePack->as<AstTypePackExplicit>();
                    if (explicitTypePack && explicitTypePack->typeList.tailType == nullptr && explicitTypePack->typeList.types.size == 1 &&
                        isTypeFollow(lexer.current().type))
                    {
                        // If we parsed an explicit type pack with a single
                        // type in it (something of the form `(T)`), and
                        // the next lexeme is one that follows a type
                        // (&, |, ?), then assume that this was actually a
                        // parenthesized type.
                        auto parenthesizedType = explicitTypePack->typeList.types.data[0];

                        AstTypeGroup* typeGroup = allocator.alloc<AstTypeGroup>(parenthesizedType->location, parenthesizedType);

                        if (options.storeCstData)
                        {
                            CstNode** cstNode = cstNodeMap.find(explicitTypePack);

                            LUAU_ASSERT(cstNode && *cstNode);
                            if (cstNode && *cstNode)
                            {
                                CstTypePackExplicit* cstExplicitTypePack = (*cstNode)->as<CstTypePackExplicit>();
                                LUAU_ASSERT(cstExplicitTypePack);

                                if (cstExplicitTypePack)
                                    cstNodeMap[typeGroup] = allocator.alloc<CstTypeGroup>(cstExplicitTypePack->closeParenthesesPosition);
                            }
                        }

                        parameters.push_back({parseTypeSuffix(typeGroup, begin), {}});
                    }
                    else
                    {
                        // Otherwise, it's a type pack.
                        parameters.push_back({{}, typePack});
                    }
                }
                else
                {
                    // There's two cases in which `typePack` will be null:
                    // - We try to parse a simple type or a type pack, and
                    //   we get a simple type: there's no ambiguity and
                    //   we attempt to parse a complex type.
                    // - The next lexeme was a `|` or `&` indicating a
                    //   union or intersection type with a leading
                    //   separator. We just fall right into
                    //   `parseTypeSuffix`, which allows its first
                    //   argument to be `nullptr`
                    parameters.push_back({parseTypeSuffix(type, begin), {}});
                }
            }
            else if (lexer.current().type == '>' && parameters.empty())
            {
                break;
            }
            else
            {
                parameters.push_back({parseType(), {}});
            }

            if (lexer.current().type == ',')
            {
                if (commaPositions)
                    commaPositions->push_back(lexer.current().location.begin);
                nextLexeme();
            }
            else
                break;
        }

        bool closingBracketFound = expectMatchAndConsume('>', begin);
        if (closingPosition && closingBracketFound)
            *closingPosition = lexer.previousLocation().begin;
    }

    return copy(parameters);
}

std::optional<AstArray<char>> Parser::parseCharArray(AstArray<char>* originalString)
{
    LUAU_ASSERT(
        lexer.current().type == Lexeme::QuotedString || lexer.current().type == Lexeme::RawString ||
        lexer.current().type == Lexeme::InterpStringSimple
    );

    scratchData.assign(lexer.current().data, lexer.current().getLength());
    if (originalString)
        *originalString = copy(scratchData);

    if (lexer.current().type == Lexeme::QuotedString || lexer.current().type == Lexeme::InterpStringSimple)
    {
        if (!Lexer::fixupQuotedString(scratchData))
        {
            nextLexeme();
            return std::nullopt;
        }
    }
    else
    {
        Lexer::fixupMultilineString(scratchData);
    }

    AstArray<char> value = copy(scratchData);
    nextLexeme();
    return value;
}

AstExpr* Parser::parseString()
{
    Location location = lexer.current().location;

    AstExprConstantString::QuoteStyle style;
    switch (lexer.current().type)
    {
    case Lexeme::QuotedString:
    case Lexeme::InterpStringSimple:
        style = AstExprConstantString::QuoteStyle::QuotedSimple;
        break;
    case Lexeme::RawString:
        style = AstExprConstantString::QuoteStyle::QuotedRaw;
        break;
    default:
        LUAU_ASSERT(false && "Invalid string type");
    }

    CstExprConstantString::QuoteStyle fullStyle;
    unsigned int blockDepth;
    if (options.storeCstData)
        std::tie(fullStyle, blockDepth) = extractStringDetails();

    AstArray<char> originalString;
    if (std::optional<AstArray<char>> value = parseCharArray(options.storeCstData ? &originalString : nullptr))
    {
        AstExprConstantString* node = allocator.alloc<AstExprConstantString>(location, *value, style);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprConstantString>(originalString, fullStyle, blockDepth);
        return node;
    }
    else
        return reportExprError(location, {}, "String literal contains malformed escape sequence");
}

AstExpr* Parser::parseInterpString()
{
    TempVector<AstArray<char>> strings(scratchString);
    TempVector<AstArray<char>> sourceStrings(scratchString2);
    TempVector<Position> stringPositions(scratchPosition);
    TempVector<AstExpr*> expressions(scratchExpr);

    Location startLocation = lexer.current().location;
    Location endLocation;

    do
    {
        Lexeme currentLexeme = lexer.current();
        LUAU_ASSERT(
            currentLexeme.type == Lexeme::InterpStringBegin || currentLexeme.type == Lexeme::InterpStringMid ||
            currentLexeme.type == Lexeme::InterpStringEnd || currentLexeme.type == Lexeme::InterpStringSimple
        );

        endLocation = currentLexeme.location;

        scratchData.assign(currentLexeme.data, currentLexeme.getLength());

        if (options.storeCstData)
        {
            sourceStrings.push_back(copy(scratchData));
            stringPositions.push_back(currentLexeme.location.begin);
        }

        if (!Lexer::fixupQuotedString(scratchData))
        {
            nextLexeme();
            return reportExprError(Location{startLocation, endLocation}, {}, "Interpolated string literal contains malformed escape sequence");
        }

        AstArray<char> chars = copy(scratchData);

        nextLexeme();

        strings.push_back(chars);

        if (currentLexeme.type == Lexeme::InterpStringEnd || currentLexeme.type == Lexeme::InterpStringSimple)
        {
            break;
        }

        bool errorWhileChecking = false;

        switch (lexer.current().type)
        {
        case Lexeme::InterpStringMid:
        case Lexeme::InterpStringEnd:
        {
            errorWhileChecking = true;
            nextLexeme();
            expressions.push_back(reportExprError(endLocation, {}, "Malformed interpolated string, expected expression inside '{}'"));
            break;
        }
        case Lexeme::BrokenString:
        {
            errorWhileChecking = true;
            nextLexeme();
            expressions.push_back(reportExprError(endLocation, {}, "Malformed interpolated string; did you forget to add a '`'?"));
            break;
        }
        default:
            expressions.push_back(parseExpr());
        }

        if (errorWhileChecking)
        {
            break;
        }

        switch (lexer.current().type)
        {
        case Lexeme::InterpStringBegin:
        case Lexeme::InterpStringMid:
        case Lexeme::InterpStringEnd:
            break;
        case Lexeme::BrokenInterpDoubleBrace:
            nextLexeme();
            return reportExprError(endLocation, {}, "Double braces are not permitted within interpolated strings; did you mean '\\{'?");
        case Lexeme::BrokenString:
            nextLexeme();
            LUAU_FALLTHROUGH;
        case Lexeme::Eof:
        {
            AstArray<AstArray<char>> stringsArray = copy(strings);
            AstArray<AstExpr*> exprs = copy(expressions);
            AstExprInterpString* node = allocator.alloc<AstExprInterpString>(Location{startLocation, lexer.previousLocation()}, stringsArray, exprs);
            if (options.storeCstData)
                cstNodeMap[node] = allocator.alloc<CstExprInterpString>(copy(sourceStrings), copy(stringPositions));
            if (auto top = lexer.peekBraceStackTop())
            {
                // We are in a broken interpolated string, the top of the stack is non empty, we are missing '}'
                if (*top == Lexer::BraceType::InterpolatedString)
                    report(lexer.previousLocation(), "Malformed interpolated string; did you forget to add a '}'?");
            }
            else
            {
                // We are in a broken interpolated string, the top of the stack is empty, we are missing '`'.
                report(lexer.previousLocation(), "Malformed interpolated string; did you forget to add a '`'?");
            }
            return node;
        }
        default:
            return reportExprError(endLocation, {}, "Malformed interpolated string, got %s", lexer.current().toString().c_str());
        }
    } while (true);

    AstArray<AstArray<char>> stringsArray = copy(strings);
    AstArray<AstExpr*> expressionsArray = copy(expressions);
    AstExprInterpString* node = allocator.alloc<AstExprInterpString>(Location{startLocation, endLocation}, stringsArray, expressionsArray);
    if (options.storeCstData)
        cstNodeMap[node] = allocator.alloc<CstExprInterpString>(copy(sourceStrings), copy(stringPositions));
    return node;
}

LUAU_NOINLINE AstExpr* Parser::parseExplicitTypeInstantiationExpr(Position start, AstExpr& basedOnExpr)
{
    CstExprExplicitTypeInstantiation* cstNode = nullptr;
    if (options.storeCstData)
    {
        cstNode = allocator.alloc<CstExprExplicitTypeInstantiation>(CstTypeInstantiation{});
    }

    Location endLocation;
    AstArray<AstTypeOrPack> typesOrPacks = parseTypeInstantiationExpr(cstNode ? &cstNode->instantiation : nullptr, &endLocation);

    AstExpr* expr = allocator.alloc<AstExprInstantiate>(Location(start, endLocation.end), &basedOnExpr, typesOrPacks);

    if (options.storeCstData)
    {
        cstNodeMap[expr] = cstNode;
    }

    return expr;
}

AstArray<AstTypeOrPack> Parser::parseTypeInstantiationExpr(CstTypeInstantiation* cstNodeOut, Location* endLocationOut)
{
    LUAU_ASSERT(lexer.current().type == '<' && lexer.lookahead().type == '<');

    if (cstNodeOut)
    {
        cstNodeOut->leftArrow1Position = lexer.current().location.begin;
    }

    Lexeme begin = lexer.current();
    lexer.next();

    TempVector<Position> commaPositions = TempVector{scratchPosition};

    AstArray<AstTypeOrPack> typeOrPacks = parseTypeParams(
        cstNodeOut ? &cstNodeOut->leftArrow2Position : nullptr,
        cstNodeOut ? &commaPositions : nullptr,
        cstNodeOut ? &cstNodeOut->rightArrow1Position : nullptr
    );

    if (cstNodeOut)
    {
        cstNodeOut->commaPositions = copy(commaPositions);

        if (lexer.current().type == '>')
        {
            cstNodeOut->rightArrow2Position = lexer.current().location.begin;
        }
    }

    if (endLocationOut)
    {
        *endLocationOut = lexer.current().location;
    }

    expectMatchAndConsume('>', begin);
    return typeOrPacks;
}


AstExpr* Parser::parseNumber()
{
    Location start = lexer.current().location;

    scratchData.assign(lexer.current().data, lexer.current().getLength());
    AstArray<char> sourceData;
    if (options.storeCstData)
        sourceData = copy(scratchData);

    // Remove all internal _ - they don't hold any meaning and this allows parsing code to just pass the string pointer to strtod et al
    if (scratchData.find('_') != std::string::npos)
    {
        scratchData.erase(std::remove(scratchData.begin(), scratchData.end(), '_'), scratchData.end());
    }

    if (FFlag::LuauIntegerType2 && (scratchData.back() == 'i'))
    {
        int64_t value = 0;
        ConstantNumberParseResult result;
        if ((strncmp(scratchData.c_str(), "0x", 2) == 0) || (strncmp(scratchData.c_str(), "0X", 2) == 0))
            result = parseInteger64(value, scratchData.c_str(), 16); // pass in '0x' prefix, it's handled by strtoll
        else if ((strncmp(scratchData.c_str(), "0b", 2) == 0) || (strncmp(scratchData.c_str(), "0B", 2) == 0))
            result = parseInteger64(value, scratchData.c_str() + 2, 2);
        else
            result = parseInteger64(value, scratchData.c_str(), 10);

        nextLexeme();

        if (result == ConstantNumberParseResult::Malformed)
            return reportExprError(start, {}, "Malformed integer");

        if (result != ConstantNumberParseResult::Ok)
            return reportExprError(start, {}, "Integer overflow");

        AstExprConstantInteger* node = allocator.alloc<AstExprConstantInteger>(start, value, result);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprConstantInteger>(sourceData);
        return node;
    }
    else
    {
        double value = 0;
        ConstantNumberParseResult result = parseDouble(value, scratchData.c_str());
        nextLexeme();

        if (result == ConstantNumberParseResult::Malformed)
            return reportExprError(start, {}, "Malformed number");

        AstExprConstantNumber* node = allocator.alloc<AstExprConstantNumber>(start, value, result);
        if (options.storeCstData)
            cstNodeMap[node] = allocator.alloc<CstExprConstantNumber>(sourceData);
        return node;
    }
}

AstLocal* Parser::pushLocal(const Binding& binding)
{
    const Name& name = binding.name;
    AstLocal*& local = localMap[name.name];

    local = allocator.alloc<AstLocal>(
        name.name, name.location, /* shadow= */ local, functionStack.size() - 1, functionStack.back().loopDepth, binding.annotation, binding.isConst
    );
    local->attributes = binding.attributes;

    localStack.push_back(local);

    return local;
}

// Luwu Classes (rfcs/classes): bring a class's primary constructor parameters into scope for a
// field initializer expression, the only place they are visible. Their AstLocals were created once,
// at the depth of the synthesized `__init`, by parseClassPrimaryConstructor; this re-enters them into
// the scope chain, and the caller's restoreLocals takes them back out.
void Parser::pushClassPrimaryConstructorParams(AstClassPrimaryConstructor* primaryConstructor)
{
    for (AstLocal* arg : primaryConstructor->args)
    {
        AstLocal*& local = localMap[arg->name];
        arg->shadow = local;
        local = arg;

        localStack.push_back(arg);
    }
}

unsigned int Parser::saveLocals()
{
    return unsigned(localStack.size());
}

void Parser::restoreLocals(unsigned int offset)
{
    for (size_t i = localStack.size(); i > offset; --i)
    {
        AstLocal* l = localStack[i - 1];

        localMap[l->name] = l->shadow;
    }

    localStack.resize(offset);
}

bool Parser::expectAndConsume(char value, const char* context)
{
    return expectAndConsume(static_cast<Lexeme::Type>(static_cast<unsigned char>(value)), context);
}

bool Parser::expectAndConsume(Lexeme::Type type, const char* context)
{
    if (lexer.current().type != type)
        return expectAndConsumeFailWithLookahead(type, context);

    nextLexeme();
    return true;
}

// LUAU_NOINLINE is used to limit the stack cost due to std::string objects, and to increase caller performance since this code is cold
LUAU_NOINLINE bool Parser::expectAndConsumeFailWithLookahead(Lexeme::Type type, const char* context)
{
    expectAndConsumeFail(type, context);

    // check if this is an extra token and the expected token is next
    if (lexer.lookahead().type == type)
    {
        // skip invalid and consume expected
        nextLexeme();
        nextLexeme();
    }

    return false;
}

// LUAU_NOINLINE is used to limit the stack cost due to std::string objects, and to increase caller performance since this code is cold
LUAU_NOINLINE void Parser::expectAndConsumeFail(Lexeme::Type type, const char* context)
{
    std::string typeString = Lexeme(Location(Position(0, 0), 0), type).toString();
    std::string currLexemeString = lexer.current().toString();

    if (context)
        report(lexer.current().location, "Expected %s when parsing %s, got %s", typeString.c_str(), context, currLexemeString.c_str());
    else
        report(lexer.current().location, "Expected %s, got %s", typeString.c_str(), currLexemeString.c_str());
}

bool Parser::expectMatchAndConsume(char value, const MatchLexeme& begin, bool searchForMissing)
{
    Lexeme::Type type = static_cast<Lexeme::Type>(static_cast<unsigned char>(value));

    if (lexer.current().type != type)
    {
        expectMatchAndConsumeFail(type, begin);

        return expectMatchAndConsumeRecover(value, begin, searchForMissing);
    }
    else
    {
        nextLexeme();

        return true;
    }
}

// LUAU_NOINLINE is used to limit the stack cost due to std::string objects, and to increase caller performance since this code is cold
LUAU_NOINLINE bool Parser::expectMatchAndConsumeRecover(char value, const MatchLexeme& begin, bool searchForMissing)
{
    Lexeme::Type type = static_cast<Lexeme::Type>(static_cast<unsigned char>(value));

    if (searchForMissing)
    {
        // previous location is taken because 'current' lexeme is already the next token
        unsigned currentLine = lexer.previousLocation().end.line;

        // search to the end of the line for expected token
        // we will also stop if we hit a token that can be handled by parsing function above the current one
        Lexeme::Type lexemeType = lexer.current().type;

        while (currentLine == lexer.current().location.begin.line && lexemeType != type && matchRecoveryStopOnToken[lexemeType] == 0)
        {
            nextLexeme();
            lexemeType = lexer.current().type;
        }

        if (lexemeType == type)
        {
            nextLexeme();

            return true;
        }
    }
    else
    {
        // check if this is an extra token and the expected token is next
        if (lexer.lookahead().type == type)
        {
            // skip invalid and consume expected
            nextLexeme();
            nextLexeme();

            return true;
        }
    }

    return false;
}

// LUAU_NOINLINE is used to limit the stack cost due to std::string objects, and to increase caller performance since this code is cold
LUAU_NOINLINE void Parser::expectMatchAndConsumeFail(Lexeme::Type type, const MatchLexeme& begin, const char* extra)
{
    std::string typeString = Lexeme(Location(Position(0, 0), 0), type).toString();
    std::string matchString = Lexeme(Location(Position(0, 0), 0), begin.type).toString();

    if (lexer.current().location.begin.line == begin.position.line)
        report(
            lexer.current().location,
            "Expected %s (to close %s at column %d), got %s%s",
            typeString.c_str(),
            matchString.c_str(),
            begin.position.column + 1,
            lexer.current().toString().c_str(),
            extra ? extra : ""
        );
    else
        report(
            lexer.current().location,
            "Expected %s (to close %s at line %d), got %s%s",
            typeString.c_str(),
            matchString.c_str(),
            begin.position.line + 1,
            lexer.current().toString().c_str(),
            extra ? extra : ""
        );
}

// LUAU_NOINLINE is used to limit the stack cost of callers inlining it
LUAU_NOINLINE bool Parser::expectMatchEndAndConsume(Lexeme::Type type, const MatchLexeme& begin)
{
    if (lexer.current().type != type)
        return expectMatchEndAndConsumeFailWithLookahead(type, begin);

    // If the token matches on a different line and a different column, it suggests misleading indentation
    // This can be used to pinpoint the problem location for a possible future *actual* mismatch
    if (lexer.current().location.begin.line != begin.position.line && lexer.current().location.begin.column != begin.position.column &&
        endMismatchSuspect.position.line < begin.position.line) // Only replace the previous suspect with more recent suspects
    {
        endMismatchSuspect = begin;
    }

    nextLexeme();

    return true;
}

// LUAU_NOINLINE is used to limit the stack cost due to std::string objects, and to increase caller performance since this code is cold
LUAU_NOINLINE bool Parser::expectMatchEndAndConsumeFailWithLookahead(Lexeme::Type type, const MatchLexeme& begin)
{
    if (endMismatchSuspect.type != Lexeme::Eof && endMismatchSuspect.position.line > begin.position.line)
    {
        std::string matchString = Lexeme(Location(Position(0, 0), 0), endMismatchSuspect.type).toString();
        std::string suggestion = format("; did you forget to close %s at line %d?", matchString.c_str(), endMismatchSuspect.position.line + 1);

        expectMatchAndConsumeFail(type, begin, suggestion.c_str());
    }
    else
    {
        expectMatchAndConsumeFail(type, begin);
    }

    // check if this is an extra token and the expected token is next
    if (lexer.lookahead().type == type)
    {
        // skip invalid and consume expected
        nextLexeme();
        nextLexeme();

        return true;
    }

    return false;
}

template<typename T>
AstArray<T> Parser::copy(const T* data, size_t size)
{
    AstArray<T> result;

    result.data = size ? static_cast<T*>(allocator.allocate(sizeof(T) * size)) : nullptr;
    result.size = size;

    // This is equivalent to std::uninitialized_copy, but without the exception guarantee
    // since our types don't have destructors
    for (size_t i = 0; i < size; ++i)
        new (result.data + i) T(data[i]);

    return result;
}

template<typename T>
AstArray<T> Parser::copy(const TempVector<T>& data)
{
    return copy(data.empty() ? nullptr : &data[0], data.size());
}

template<typename T>
AstArray<T> Parser::copy(std::initializer_list<T> data)
{
    return copy(data.size() == 0 ? nullptr : data.begin(), data.size());
}

AstArray<char> Parser::copy(const std::string& data)
{
    AstArray<char> result = copy(data.c_str(), data.size() + 1);

    result.size = data.size();

    return result;
}

void Parser::incrementRecursionCounter(const char* context)
{
    recursionCounter++;

    if (recursionCounter > unsigned(FInt::LuauRecursionLimit))
    {
        ParseError::raise(lexer.current().location, "Exceeded allowed recursion depth; simplify your %s to make the code compile", context);
    }
}

void Parser::report(const Location& location, const char* format, va_list args)
{
    // To reduce number of errors reported to user for incomplete statements, we skip multiple errors at the same location
    // For example, consider 'local a = (((b + ' where multiple tokens haven't been written yet
    if (!parseErrors.empty() && location == parseErrors.back().getLocation())
        return;

    std::string message = vformat(format, args);

    // when limited to a single error, behave as if the error recovery is disabled
    if (FInt::LuauParseErrorLimit == 1)
        throw ParseError(location, message);

    parseErrors.emplace_back(location, message);

    if (parseErrors.size() >= unsigned(FInt::LuauParseErrorLimit) && !options.noErrorLimit)
        ParseError::raise(location, "Reached error limit (%d)", int(FInt::LuauParseErrorLimit));
}

void Parser::report(const Location& location, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    report(location, format, args);
    va_end(args);
}

LUAU_NOINLINE void Parser::reportNameError(const char* context)
{
    if (context)
        report(lexer.current().location, "Expected identifier when parsing %s, got %s", context, lexer.current().toString().c_str());
    else
        report(lexer.current().location, "Expected identifier, got %s", lexer.current().toString().c_str());
}

AstStatError* Parser::reportStatError(
    const Location& location,
    const AstArray<AstExpr*>& expressions,
    const AstArray<AstStat*>& statements,
    const char* format,
    ...
)
{
    va_list args;
    va_start(args, format);
    report(location, format, args);
    va_end(args);

    return allocator.alloc<AstStatError>(location, expressions, statements, unsigned(parseErrors.size() - 1));
}

AstExprError* Parser::reportExprError(const Location& location, const AstArray<AstExpr*>& expressions, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    report(location, format, args);
    va_end(args);

    return allocator.alloc<AstExprError>(location, expressions, unsigned(parseErrors.size() - 1));
}

AstTypeError* Parser::reportTypeError(const Location& location, const AstArray<AstType*>& types, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    report(location, format, args);
    va_end(args);

    return allocator.alloc<AstTypeError>(location, types, false, unsigned(parseErrors.size() - 1));
}

AstTypeError* Parser::reportMissingTypeError(const Location& parseErrorLocation, const Location& astErrorLocation, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    report(parseErrorLocation, format, args);
    va_end(args);

    return allocator.alloc<AstTypeError>(astErrorLocation, AstArray<AstType*>{}, true, unsigned(parseErrors.size() - 1));
}

void Parser::nextLexeme()
{
    Lexeme::Type type = lexer.next(/* skipComments= */ false, true).type;

    while (type == Lexeme::BrokenComment || type == Lexeme::Comment || type == Lexeme::BlockComment)
    {
        const Lexeme& lexeme = lexer.current();

        if (options.captureComments)
            commentLocations.push_back(Comment{lexeme.type, lexeme.location});

        // Subtlety: Broken comments are weird because we record them as comments AND pass them to the parser as a lexeme.
        // The parser will turn this into a proper syntax error.
        if (lexeme.type == Lexeme::BrokenComment)
            return;

        // Comments starting with ! are called "hot comments" and contain directives for type checking / linting / compiling
        if (lexeme.type == Lexeme::Comment && lexeme.getLength() && lexeme.data[0] == '!')
        {
            const char* text = lexeme.data;

            unsigned int end = lexeme.getLength();
            while (end > 0 && isSpace(text[end - 1]))
                --end;

            hotcomments.push_back({hotcommentHeader, lexeme.location, std::string(text + 1, text + end)});
        }

        type = lexer.next(/* skipComments= */ false, /* updatePrevLocation= */ false).type;
    }
}

} // namespace Luau

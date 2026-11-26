// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/IntegerModes.h"

#include "Luau/Scope.h"
#include "Luau/StringUtils.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"
#include "Luau/TypeArena.h"

LUAU_FASTFLAG(DebugLuwuIntegers)

namespace Luau
{

static const char* const kIntegerModeNames[kIntegerModeCount] = {"int", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64"};

// How a mode's value is written in a suggested fix: `integer.u8`, or `integer.dynamic` for `int`
static std::string modeValueName(IntegerMode mode)
{
    return mode == IntegerMode_Dynamic ? "integer.dynamic" : std::string("integer.") + integerModeName(mode);
}

bool isIntegerWithMode(NotNull<BuiltinTypes> builtinTypes, const Scope& scope, const AstTypeReference* ref)
{
    if (!FFlag::DebugLuwuIntegers || ref->prefix || ref->name != "integer" || !ref->hasParameterList)
        return false;

    // `integer` can be shadowed by a type of the same name
    std::optional<TypeFun> integerAlias = scope.lookupType("integer");
    return integerAlias && integerAlias->type == builtinTypes->integerType;
}

const char* integerModeName(IntegerMode mode)
{
    LUAU_ASSERT(int(mode) < kIntegerModeCount);
    return kIntegerModeNames[mode];
}

std::optional<IntegerMode> integerModeNamed(std::string_view name)
{
    for (int mode = 0; mode < kIntegerModeCount; ++mode)
    {
        if (name == kIntegerModeNames[mode])
            return IntegerMode(mode);
    }
    return std::nullopt;
}

// How deep a mode's unions and intersections are read. A mode is written by hand, so this is far deeper than any real one.
static constexpr int kMaxModeDepth = 16;

static std::optional<IntegerModeSet> modesNamedBy(TypeId modeTy, int depth)
{
    modeTy = follow(modeTy);
    if (depth > kMaxModeDepth)
        return std::nullopt;

    if (const StringSingleton* name = get<StringSingleton>(get<SingletonType>(modeTy)))
    {
        std::optional<IntegerMode> mode = integerModeNamed(name->value);
        if (!mode)
            return std::nullopt;
        return IntegerModeSet(1 << *mode);
    }

    // A generic mode nothing solved, like `integer.cast(text)`'s, is any mode
    if (get<UnknownType>(modeTy))
        return kEveryIntegerMode;
    if (get<NeverType>(modeTy))
        return IntegerModeSet(0);

    if (const UnionType* names = get<UnionType>(modeTy))
    {
        IntegerModeSet modes = 0;
        for (TypeId option : names)
        {
            std::optional<IntegerModeSet> optionModes = modesNamedBy(option, depth + 1);
            if (!optionModes)
                return std::nullopt;
            modes |= *optionModes;
        }
        return modes;
    }

    if (const IntersectionType* parts = get<IntersectionType>(modeTy))
    {
        IntegerModeSet modes = kEveryIntegerMode;
        for (TypeId part : parts)
        {
            std::optional<IntegerModeSet> partModes = modesNamedBy(part, depth + 1);
            if (!partModes)
                return std::nullopt;
            modes &= *partModes;
        }
        return modes;
    }

    return std::nullopt;
}

std::optional<IntegerModeSet> integerModesNamedBy(TypeId modeTy)
{
    return modesNamedBy(modeTy, 0);
}

bool isIntegerModeClass(const ExternType* type)
{
    return type->name == "Mode" && type->definitionModuleName == "@luau";
}

std::optional<IntegerModeSet> integerModesOf(TypeId ty)
{
    const PrimitiveType* integer = get<PrimitiveType>(follow(ty));
    if (!integer || integer->type != PrimitiveType::Integer)
        return std::nullopt;

    if (!integer->mode)
        return kEveryIntegerMode;

    return integerModesNamedBy(*integer->mode);
}

// The mode of an integer type, the union of every mode's name for plain `integer`
static TypeId modeOf(NotNull<BuiltinTypes> builtinTypes, TypeId integerTy)
{
    const PrimitiveType* integer = get<PrimitiveType>(follow(integerTy));
    LUAU_ASSERT(integer && integer->type == PrimitiveType::Integer);
    return integer->mode ? *integer->mode : builtinTypes->everyIntegerModeName;
}

// An integer type of `modeTy`, with the methods every integer has
static TypeId makeIntegerType(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId modeTy)
{
    PrimitiveType integer{PrimitiveType::Integer};
    integer.metatable = get<PrimitiveType>(builtinTypes->integerType)->metatable;
    integer.mode = modeTy;
    return arena.addType(std::move(integer));
}

TypeId integerTypeOf(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, IntegerModeSet modes)
{
    if (modes == 0)
        return builtinTypes->neverType;
    if (modes == kEveryIntegerMode)
        return builtinTypes->integerType;

    std::vector<TypeId> names;
    int lastMode = 0;
    for (int mode = 0; mode < kIntegerModeCount; ++mode)
    {
        if (modes & (1 << mode))
        {
            names.push_back(builtinTypes->integerModeNames[mode]);
            lastMode = mode;
        }
    }

    if (names.size() == 1)
        return builtinTypes->integerModeTypes[lastMode];

    return makeIntegerType(builtinTypes, arena, arena.addType(UnionType{std::move(names)}));
}

// The mode `integer<modeTy>` has, before its modes are counted: a mode's name, a union, or a generic
static std::optional<TypeId> modeArgument(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId modeTy, int depth)
{
    modeTy = follow(modeTy);
    if (depth > kMaxModeDepth)
        return std::nullopt;

    if (get<GenericType>(modeTy) || get<FreeType>(modeTy))
        return modeTy;

    if (const StringSingleton* name = get<StringSingleton>(get<SingletonType>(modeTy)))
    {
        std::optional<IntegerMode> mode = integerModeNamed(name->value);
        if (!mode)
            return std::nullopt;
        return builtinTypes->integerModeNames[*mode];
    }

    // `integer<integer.u8>`, where the type `integer.u8` is the mode's type itself
    if (const PrimitiveType* integer = get<PrimitiveType>(modeTy); integer && integer->type == PrimitiveType::Integer)
        return modeOf(builtinTypes, modeTy);

    // The type of an `integer.Mode` object, `Mode<"u8">`
    const ExternType* modeObject = get<ExternType>(modeTy);
    if (modeObject && isIntegerModeClass(modeObject) && modeObject->instantiatedTypeParams.size() == 1)
        return modeArgument(builtinTypes, arena, modeObject->instantiatedTypeParams[0], depth + 1);

    if (const UnionType* options = get<UnionType>(modeTy))
    {
        std::vector<TypeId> modes;
        for (TypeId option : options)
        {
            std::optional<TypeId> mode = modeArgument(builtinTypes, arena, option, depth + 1);
            if (!mode)
                return std::nullopt;
            modes.push_back(*mode);
        }
        return arena.addType(UnionType{std::move(modes)});
    }

    return std::nullopt;
}

std::optional<TypeId> integerTypeWithMode(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId modeTy)
{
    std::optional<TypeId> mode = modeArgument(builtinTypes, arena, modeTy, 0);
    if (!mode)
        return std::nullopt;

    if (std::optional<IntegerModeSet> modes = integerModesNamedBy(*mode))
        return integerTypeOf(builtinTypes, arena, *modes);

    return makeIntegerType(builtinTypes, arena, *mode);
}

TypeId unionOfIntegers(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId here, TypeId there)
{
    if (get<NeverType>(follow(here)))
        return there;
    if (get<NeverType>(follow(there)) || follow(here) == follow(there))
        return here;

    std::optional<IntegerModeSet> hereModes = integerModesOf(here);
    std::optional<IntegerModeSet> thereModes = integerModesOf(there);
    if (hereModes && thereModes)
        return integerTypeOf(builtinTypes, arena, *hereModes | *thereModes);

    if (hereModes == kEveryIntegerMode || thereModes == kEveryIntegerMode)
        return builtinTypes->integerType;

    return makeIntegerType(builtinTypes, arena, arena.addType(UnionType{{modeOf(builtinTypes, here), modeOf(builtinTypes, there)}}));
}

TypeId intersectionOfIntegers(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId here, TypeId there)
{
    if (get<NeverType>(follow(here)) || follow(here) == follow(there))
        return here;
    if (get<NeverType>(follow(there)))
        return there;

    std::optional<IntegerModeSet> hereModes = integerModesOf(here);
    std::optional<IntegerModeSet> thereModes = integerModesOf(there);
    if (hereModes && thereModes)
        return integerTypeOf(builtinTypes, arena, *hereModes & *thereModes);

    if (hereModes == kEveryIntegerMode)
        return there;
    if (thereModes == kEveryIntegerMode)
        return here;

    return makeIntegerType(builtinTypes, arena, arena.addType(IntersectionType{{modeOf(builtinTypes, here), modeOf(builtinTypes, there)}}));
}

TypeId complementOfIntegers(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId here)
{
    if (get<NeverType>(follow(here)))
        return builtinTypes->integerType;

    // A generic mode's complement can't be written as an integer type, so it's widened to every integer
    std::optional<IntegerModeSet> modes = integerModesOf(here);
    if (!modes)
        return builtinTypes->integerType;

    return integerTypeOf(builtinTypes, arena, IntegerModeSet(kEveryIntegerMode & ~*modes));
}

static bool sameMode(TypeId lhs, TypeId rhs)
{
    const PrimitiveType* left = get<PrimitiveType>(follow(lhs));
    const PrimitiveType* right = get<PrimitiveType>(follow(rhs));
    return left && right && left->mode && right->mode && follow(*left->mode) == follow(*right->mode);
}

TypeId collapseIntegerUnion(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId ty)
{
    const UnionType* options = get<UnionType>(follow(ty));
    if (!options)
        return ty;

    TypeId result = builtinTypes->neverType;
    for (TypeId option : options)
    {
        if (!isInteger(option))
            return ty;
        result = unionOfIntegers(builtinTypes, arena, result, option);
    }
    return result;
}

bool isIntegerArithmetic(TypeId lhs, TypeId rhs)
{
    bool lhsInteger = isInteger(lhs);
    bool rhsInteger = isInteger(rhs);
    return (lhsInteger && (rhsInteger || isNumber(rhs))) || (rhsInteger && isNumber(lhs));
}

TypeId integerArithmeticResult(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId lhs, TypeId rhs)
{
    std::optional<IntegerModeSet> lhsModes = integerModesOf(lhs);
    std::optional<IntegerModeSet> rhsModes = integerModesOf(rhs);

    if (!isInteger(rhs) || sameMode(lhs, rhs))
        return lhs;
    if (!isInteger(lhs))
        return rhs;

    // The operation only succeeds when both operands have the same mode, so the result's modes are those both have
    if (lhsModes && rhsModes)
    {
        IntegerModeSet shared = *lhsModes & *rhsModes;
        return shared ? integerTypeOf(builtinTypes, arena, shared) : lhs;
    }

    return lhsModes == kEveryIntegerMode ? rhs : lhs;
}

// The single mode of `ty`, if it has exactly one
static std::optional<IntegerMode> singleModeOf(TypeId ty)
{
    std::optional<IntegerModeSet> modes = integerModesOf(ty);
    if (!modes)
        return std::nullopt;

    for (int mode = 0; mode < kIntegerModeCount; ++mode)
    {
        if (*modes == IntegerModeSet(1 << mode))
            return IntegerMode(mode);
    }
    return std::nullopt;
}

std::optional<std::string> integerOperandsError(AstExprBinary::Op op, TypeId lhs, TypeId rhs)
{
    bool lhsInteger = isInteger(lhs);
    bool rhsInteger = isInteger(rhs);
    if (!lhsInteger && !rhsInteger)
        return std::nullopt;

    if (op == AstExprBinary::Op::Pow)
        return "Use ':pow()' to raise an integer to a power: '^' raises an error on integers";

    std::string opName = toString(op);
    TypeId integerTy = lhsInteger ? lhs : rhs;
    TypeId otherTy = lhsInteger ? rhs : lhs;

    if (!lhsInteger || !rhsInteger)
    {
        std::string message = format("Operator '%s' can't mix %s and %s", opName.c_str(), toString(lhs).c_str(), toString(rhs).c_str());
        if (std::optional<IntegerMode> mode = singleModeOf(integerTy); mode && isNumber(otherTy))
            message += format("; convert the number with integer.cast(n, %s)", modeValueName(*mode).c_str());
        return message;
    }

    std::optional<IntegerModeSet> lhsModes = integerModesOf(lhs);
    std::optional<IntegerModeSet> rhsModes = integerModesOf(rhs);
    bool disjoint = lhsModes && rhsModes && (*lhsModes & *rhsModes) == 0;
    if (!disjoint)
        return std::nullopt;

    std::string message =
        format("Operator '%s' can't mix integers of modes %s and %s", opName.c_str(), toString(lhs).c_str(), toString(rhs).c_str());
    if (std::optional<IntegerMode> mode = singleModeOf(lhs))
        message += format("; convert one side with ':cast(%s)'", modeValueName(*mode).c_str());
    return message;
}

std::optional<std::string> integerNegationError(TypeId operand, const std::optional<std::string>& name)
{
    std::optional<IntegerModeSet> modes = integerModesOf(operand);
    if (!modes || *modes == 0)
        return std::nullopt;

    for (int mode = 0; mode < kIntegerModeCount; ++mode)
    {
        bool signedModePossible = (*modes & (1 << mode)) && !isUnsignedIntegerMode(IntegerMode(mode));
        if (signedModePossible)
            return std::nullopt;
    }

    std::optional<IntegerMode> mode = singleModeOf(operand);
    std::string zero = mode ? format("0%s", integerModeName(*mode)) : "0";
    std::string fix = format("'%s - %s'", zero.c_str(), name ? name->c_str() : "value");
    return format("Subtract from zero to negate an unsigned %s: %s. Unary '-' raises an error on unsigned integers",
        toString(operand).c_str(), fix.c_str());
}

} // namespace Luau

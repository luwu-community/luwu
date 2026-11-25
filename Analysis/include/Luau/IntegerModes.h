// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/Common.h"
#include "Luau/NotNull.h"
#include "Luau/TypeFwd.h"

#include <optional>
#include <string>
#include <string_view>

// Luwu Integers (rfcs/integers.md): integer types by mode. `5u8` is a `u8`, the same type as `integer<"u8">` and
// `integer<integer.u8>`, and plain `integer` accepts every mode.
namespace Luau
{

struct Scope;
struct TypeArena;

// A set of integer modes, one bit per IntegerMode
using IntegerModeSet = uint16_t;
constexpr IntegerModeSet kEveryIntegerMode = IntegerModeSet((1 << kIntegerModeCount) - 1);

// The name of `mode` as `typeof` gives it and as its type is written: "u8", or "int" for the dynamically-sized mode
const char* integerModeName(IntegerMode mode);
std::optional<IntegerMode> integerModeNamed(std::string_view name);

// Whether `ref` is `integer<T>`: the builtin `integer` with a type argument, which ConstraintGenerator resolves
bool isIntegerWithMode(NotNull<BuiltinTypes> builtinTypes, const Scope& scope, const AstTypeReference* ref);

// The modes `modeTy` names when it's a mode's name, or a union or intersection of them. nullopt when it's a generic.
std::optional<IntegerModeSet> integerModesNamedBy(TypeId modeTy);

// Whether `type` is the `integer.Mode` class or one of its instantiations, `Mode<"u8">`
bool isIntegerModeClass(const ExternType* type);

// The modes an integer type holds, every mode for plain `integer`. nullopt when `ty` isn't an integer type or its mode
// is a generic.
std::optional<IntegerModeSet> integerModesOf(TypeId ty);

// The type of the integers in `modes`: one mode's type, `integer` for every mode, `never` for none, or a union
TypeId integerTypeOf(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, IntegerModeSet modes);

// `integer<modeTy>`. The mode can be a mode's name (`"u8"`), a mode's type (`integer.u8`), the type of an
// `integer.Mode` object, a union of these, or a generic. nullopt when `modeTy` is none of them.
std::optional<TypeId> integerTypeWithMode(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId modeTy);

// The integer part of a normalized type, which is `never` or an integer type, combined with another
TypeId unionOfIntegers(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId here, TypeId there);
TypeId intersectionOfIntegers(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId here, TypeId there);
TypeId complementOfIntegers(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId here);

// A union of integer types (`u8 | i16`) as one integer type, which operators read; any other type unchanged
TypeId collapseIntegerUnion(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId ty);

// Whether an operator works on `lhs` and `rhs` as integer arithmetic: one is an integer and the other an integer or a
// number. Mixing in a number is an error integerOperandsError reports.
bool isIntegerArithmetic(TypeId lhs, TypeId rhs);

// The type an arithmetic operator gives when one of its operands is an integer: the mode both operands share. An
// operation integers don't allow gives the integer operand's type, and integerOperandsError reports it.
TypeId integerArithmeticResult(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId lhs, TypeId rhs);

// The error for arithmetic on `lhs` and `rhs` when one is an integer and the operation can't work: two modes, an integer
// and something else, or `^`
std::optional<std::string> integerOperandsError(AstExprBinary::Op op, TypeId lhs, TypeId rhs);

// The error for `-operand` when the operand can only be unsigned. `name` is the operand's name, when it has one.
std::optional<std::string> integerNegationError(TypeId operand, const std::optional<std::string>& name);

} // namespace Luau

// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Luau
{

// Luwu Classes (rfcs/classes): what a member name the language gives a meaning is for. Every such name starts with `__`.
enum class ClassMemberNameKind : uint8_t
{
    // `__init`: a class's constructor. A trait can only expect one, which its `__create` can call.
    Constructor,
    // `__create` and `__implemented`, which a trait defines for itself
    TraitHook,
    // an operator the VM finds on the object's class by name, like `__tostring` or `__add`
    Metamethod,
    // a metamethod of tables that a class or trait can't define, like `__index`
    ForbiddenMetamethod,
    // `__ok`, the field that tells the `?` operators whether a value is ok
    Field,
    // a function the compiler writes for the VM to call, like `__defaults` or `__traitinit`. No one can declare one.
    Internal,
};

// Luwu Classes (rfcs/classes): where a member name can be declared
enum ClassMemberNameScope : uint8_t
{
    ClassMemberName_InClasses = 1 << 0,
    ClassMemberName_InTraits = 1 << 1,
};

// Luwu Traits (rfcs/classes/traits.md): a member's placement, as `shared`, `static` and `traitonly` write it
enum class ClassMemberNamePlacement : uint8_t
{
    Shared,
    Static,
    TraitOnly,
};

// Luwu Classes (rfcs/classes): what a function of a given name must return, which Analysis checks
enum class ClassMemberNameReturn : uint8_t
{
    // no rule
    Anything,
    // nothing: `__init` assigns fields to `self`
    Nothing,
    // at least one value
    Value,
    String,
    Boolean,
    Number,
};

// Luwu Classes (rfcs/classes): a member name the language gives a meaning, and the rules that come with it
struct ClassMemberName
{
    const char* name;
    ClassMemberNameKind kind;
    // the ClassMemberNameScope bits for where it can be declared
    uint8_t scopes;
    // the placement it has without a modifier
    ClassMemberNamePlacement placement;
    // the placement modifiers it may be written with, as bits `1 << ClassMemberNamePlacement`
    uint8_t allowedPlacements;
    // whether a trait's member of this name is the trait's alone, so implementing classes don't get it
    bool traitAlone;
    // the parameters and returns it's usually written with, which completion offers; nullptr when it offers none
    const char* signature;
    // what a function of this name must return
    ClassMemberNameReturn returns;
    // Luwu Traits (rfcs/classes/traits.md): whether a metamethod can also be the class or trait value's own:
    // `function __tostring(Self)` in a class, `traitonly function __tostring(trait)` in a trait
    bool onValues;
};

// Luwu Classes (rfcs/classes): the entry for `name`, or nullptr when the language gives it no meaning
const ClassMemberName* findClassMemberName(const char* name);

// Luwu Classes (rfcs/classes): every entry, `*count` of them
const ClassMemberName* classMemberNames(size_t* count);

// Luwu Traits (rfcs/classes/traits.md): whether `entry` may be written with the placement modifier `placement`
inline bool allowsPlacement(const ClassMemberName& entry, ClassMemberNamePlacement placement)
{
    return (entry.allowedPlacements & (1 << int(placement))) != 0;
}

} // namespace Luau

// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ClassMemberNames.h"

#include <string.h>

namespace Luau
{

namespace
{

const uint8_t kBoth = ClassMemberName_InClasses | ClassMemberName_InTraits;
const uint8_t kNoPlacement = 0;
const uint8_t kAnyPlacement = (1 << int(ClassMemberNamePlacement::Shared)) | (1 << int(ClassMemberNamePlacement::Static)) |
                              (1 << int(ClassMemberNamePlacement::TraitOnly));
const uint8_t kTraitOnly = 1 << int(ClassMemberNamePlacement::TraitOnly);
const uint8_t kStaticOrTraitOnly = (1 << int(ClassMemberNamePlacement::Static)) | kTraitOnly;

const ClassMemberName kNames[] = {
    // A trait's `__init` is the constructor it expects, which implementing classes write themselves
    {"__init", ClassMemberNameKind::Constructor, kBoth, ClassMemberNamePlacement::Shared, kTraitOnly, true, "(self)", ClassMemberNameReturn::Nothing, false},

    // A trait's factory, and its hook for each implementing class. Both run on the trait, never on an object.
    {"__create", ClassMemberNameKind::TraitHook, ClassMemberName_InTraits, ClassMemberNamePlacement::TraitOnly, kTraitOnly, true, "(...)", ClassMemberNameReturn::Anything, false},
    {"__implemented", ClassMemberNameKind::TraitHook, ClassMemberName_InTraits, ClassMemberNamePlacement::TraitOnly, kTraitOnly, true, "(cls)", ClassMemberNameReturn::Anything, false},

    // The VM finds a metamethod on the object's class by name, so it's always shared. `__tostring` can also be the class
    // value's (`static`, taking `Self`) or the trait value's (`traitonly`).
    {"__tostring", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kStaticOrTraitOnly, false, "(self): string", ClassMemberNameReturn::String, true},
    {"__eq", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean", ClassMemberNameReturn::Boolean, false},
    {"__lt", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean", ClassMemberNameReturn::Boolean, false},
    {"__le", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean", ClassMemberNameReturn::Boolean, false},
    {"__len", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self): number", ClassMemberNameReturn::Number, false},
    {"__add", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__sub", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__mul", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__div", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__idiv", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__mod", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__pow", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__unm", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self)", ClassMemberNameReturn::Value, false},
    {"__concat", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value, false},
    {"__call", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, ...)", ClassMemberNameReturn::Anything, false},
    {"__iter", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self)", ClassMemberNameReturn::Value, false},
    // Luwu Integers (rfcs/integers.md): `integer.cast` and calling a mode convert a value through it, so it isn't a tag method
    {"__tointeger", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self): integer", ClassMemberNameReturn::Value, false},

    // Classes have their own member lookup and layout, which these would replace
    {"__index", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},
    {"__newindex", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},
    {"__mode", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},
    {"__metatable", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},
    {"__type", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},

    // The one field name that starts with `__`, in any placement
    {"__ok", ClassMemberNameKind::Field, kBoth, ClassMemberNamePlacement::Shared, kAnyPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},

    // What the compiler writes: a class's computed field defaults and its trait initialization without an `__init`, and a
    // trait's initializer and `needs` list. A trait's are the VM's, which implementing classes don't get.
    {"__defaults", ClassMemberNameKind::Internal, ClassMemberName_InClasses, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},
    {"__implements", ClassMemberNameKind::Internal, ClassMemberName_InClasses, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything, false},
    {"__traitinit", ClassMemberNameKind::Internal, ClassMemberName_InTraits, ClassMemberNamePlacement::Shared, kNoPlacement, true, nullptr, ClassMemberNameReturn::Anything, false},
    {"__needs", ClassMemberNameKind::Internal, ClassMemberName_InTraits, ClassMemberNamePlacement::Shared, kNoPlacement, true, nullptr, ClassMemberNameReturn::Anything, false},
};

} // namespace

const ClassMemberName* findClassMemberName(const char* name)
{
    if (!name || name[0] != '_' || name[1] != '_')
        return nullptr;

    for (const ClassMemberName& entry : kNames)
        if (strcmp(entry.name, name) == 0)
            return &entry;

    return nullptr;
}

const ClassMemberName* classMemberNames(size_t* count)
{
    *count = sizeof(kNames) / sizeof(kNames[0]);
    return kNames;
}

} // namespace Luau

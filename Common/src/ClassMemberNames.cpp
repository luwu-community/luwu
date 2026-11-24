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

const ClassMemberName kNames[] = {
    // A trait's `__init` is the constructor it expects, which implementing classes write themselves
    {"__init", ClassMemberNameKind::Constructor, kBoth, ClassMemberNamePlacement::Shared, kTraitOnly, true, "(self)", ClassMemberNameReturn::Nothing},

    // A trait's factory, and its hook for each implementing class. Both run on the trait, never on an object.
    {"__create", ClassMemberNameKind::TraitHook, ClassMemberName_InTraits, ClassMemberNamePlacement::TraitOnly, kTraitOnly, true, "(...)", ClassMemberNameReturn::Anything},
    {"__implemented", ClassMemberNameKind::TraitHook, ClassMemberName_InTraits, ClassMemberNamePlacement::TraitOnly, kTraitOnly, true, "(cls)", ClassMemberNameReturn::Anything},

    // The VM finds a metamethod on the object's class by name, so it's always shared
    {"__tostring", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self): string", ClassMemberNameReturn::String},
    {"__eq", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean", ClassMemberNameReturn::Boolean},
    {"__lt", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean", ClassMemberNameReturn::Boolean},
    {"__le", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean", ClassMemberNameReturn::Boolean},
    {"__len", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self): number", ClassMemberNameReturn::Number},
    {"__add", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__sub", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__mul", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__div", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__idiv", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__mod", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__pow", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__unm", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self)", ClassMemberNameReturn::Value},
    {"__concat", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)", ClassMemberNameReturn::Value},
    {"__call", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, ...)", ClassMemberNameReturn::Anything},
    {"__iter", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self)", ClassMemberNameReturn::Value},

    // Classes have their own member lookup and layout, which these would replace
    {"__index", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},
    {"__newindex", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},
    {"__mode", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},
    {"__metatable", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},
    {"__type", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},

    // The one field name that starts with `__`, in any placement
    {"__ok", ClassMemberNameKind::Field, kBoth, ClassMemberNamePlacement::Shared, kAnyPlacement, false, nullptr, ClassMemberNameReturn::Anything},

    // What the compiler writes: a class's computed field defaults and its trait initialization without an `__init`, and a
    // trait's initializer and `needs` list. A trait's are the VM's, which implementing classes don't get.
    {"__defaults", ClassMemberNameKind::Internal, ClassMemberName_InClasses, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},
    {"__implements", ClassMemberNameKind::Internal, ClassMemberName_InClasses, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr, ClassMemberNameReturn::Anything},
    {"__traitinit", ClassMemberNameKind::Internal, ClassMemberName_InTraits, ClassMemberNamePlacement::Shared, kNoPlacement, true, nullptr, ClassMemberNameReturn::Anything},
    {"__needs", ClassMemberNameKind::Internal, ClassMemberName_InTraits, ClassMemberNamePlacement::Shared, kNoPlacement, true, nullptr, ClassMemberNameReturn::Anything},
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

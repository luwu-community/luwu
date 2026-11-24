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
    {"__init", ClassMemberNameKind::Constructor, kBoth, ClassMemberNamePlacement::Shared, kTraitOnly, true, nullptr},

    // A trait's factory, and its hook for each implementing class. Both run on the trait, never on an object.
    {"__create", ClassMemberNameKind::TraitHook, ClassMemberName_InTraits, ClassMemberNamePlacement::TraitOnly, kTraitOnly, true, "(...)"},
    {"__implemented", ClassMemberNameKind::TraitHook, ClassMemberName_InTraits, ClassMemberNamePlacement::TraitOnly, kTraitOnly, true, "(cls)"},

    // The VM finds a metamethod on the object's class by name, so it's always shared
    {"__tostring", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self): string"},
    {"__eq", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean"},
    {"__lt", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean"},
    {"__le", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other): boolean"},
    {"__len", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self): number"},
    {"__add", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__sub", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__mul", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__div", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__idiv", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__mod", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__pow", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__unm", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self)"},
    {"__concat", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, other)"},
    {"__call", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self, ...)"},
    {"__iter", ClassMemberNameKind::Metamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, "(self)"},

    // Classes have their own member lookup and layout, which these would replace
    {"__index", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},
    {"__newindex", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},
    {"__mode", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},
    {"__metatable", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},
    {"__type", ClassMemberNameKind::ForbiddenMetamethod, kBoth, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},

    // The one field name that starts with `__`, in any placement
    {"__ok", ClassMemberNameKind::Field, kBoth, ClassMemberNamePlacement::Shared, kAnyPlacement, false, nullptr},

    // What the compiler writes: a class's computed field defaults and its trait initialization without an `__init`, and a
    // trait's initializer and `needs` list. A trait's are the VM's, which implementing classes don't get.
    {"__defaults", ClassMemberNameKind::Internal, ClassMemberName_InClasses, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},
    {"__implements", ClassMemberNameKind::Internal, ClassMemberName_InClasses, ClassMemberNamePlacement::Shared, kNoPlacement, false, nullptr},
    {"__traitinit", ClassMemberNameKind::Internal, ClassMemberName_InTraits, ClassMemberNamePlacement::Shared, kNoPlacement, true, nullptr},
    {"__needs", ClassMemberNameKind::Internal, ClassMemberName_InTraits, ClassMemberNamePlacement::Shared, kNoPlacement, true, nullptr},
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

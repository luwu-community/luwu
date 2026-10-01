// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Fixture.h"

#include "Luau/BuiltinDefinitions.h"
#include "ScopedFlags.h"

LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuTraits)
LUAU_FASTFLAG(LuauAllowGlobalDeclarationToBeCalledClass)

namespace Luau
{

// Luwu Classes (rfcs/classes): a fixture with classes on and the `class` library declared, with the magic functions
// its type checking needs. The library comes from the embedder, not the builtin definitions, so a test that uses it
// needs this fixture.
struct ClassesFixture : Fixture
{
    const std::string definitions = R"LUAU_SRC(
@checked declare function require(target: any): any
declare function sqrt(n: number): number
declare function tostring<T>(value: T): string
declare function typeof<T>(value: T): string
declare function print(...: any): ()
declare none: none

declare extern type Duration with
    read seconds: number
end

declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    implements: @checked (o: unknown, t: class) -> boolean,
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object) -> string,
    fields: @checked (o: class | object) -> ({ [string]: unknown }, boolean)
}
)LUAU_SRC";
    // the `class` library as the embedded definitions declare it with traits on, where `trait` is a type
    const std::string traitDefinitions = R"LUAU_SRC(
declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    implements: @checked (o: unknown, t: trait) -> boolean,
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object | trait) -> string,
    fields: @checked (o: class | object | trait) -> ({ [string]: unknown }, boolean)
}
)LUAU_SRC";
    Frontend& getFrontend() override
    {
        if (frontend)
            return *frontend;

        Frontend& f = Fixture::getFrontend();
        Luau::unfreeze(f.globals.globalTypes);

        f.loadDefinitionFile(f.globals, f.globals.globalScope, definitions, "@test", false);
        if (FFlag::LuwuTraits)
            f.loadDefinitionFile(f.globals, f.globals.globalScope, traitDefinitions, "@test", false);
        AstName reqName = f.globals.globalNames.names->getOrAdd("require");
        auto it = f.globals.globalScope->bindings.find(reqName);
        LUAU_ASSERT(it != f.globals.globalScope->bindings.end());
        attachTag(it->second.typeId, kRequireTagName);
        attachMagicFunction(it->second.typeId, std::make_shared<MagicRequire>());

        AstName classLibName = f.globals.globalNames.names->getOrAdd("class");
        auto classLibIt = f.globals.globalScope->bindings.find(classLibName);
        LUAU_ASSERT(classLibIt != f.globals.globalScope->bindings.end());
        if (TableType* ctv = getMutable<TableType>(classLibIt->second.typeId))
        {
            auto fieldsIt = ctv->props.find("fields");
            LUAU_ASSERT(fieldsIt != ctv->props.end() && fieldsIt->second.readTy);
            attachMagicFunction(*fieldsIt->second.readTy, std::make_shared<MagicClassFields>());

            auto nameIt = ctv->props.find("name");
            LUAU_ASSERT(nameIt != ctv->props.end() && nameIt->second.readTy);
            attachMagicFunction(*nameIt->second.readTy, std::make_shared<MagicClassName>());

            auto ofIt = ctv->props.find("of");
            LUAU_ASSERT(ofIt != ctv->props.end() && ofIt->second.readTy);
            attachMagicFunction(*ofIt->second.readTy, std::make_shared<MagicClassOf>());

            // the checks refine their first argument (see MagicClassInstanceCheck)
            for (const char* check : {"isinstance", "implements"})
            {
                auto checkIt = ctv->props.find(check);
                LUAU_ASSERT(checkIt != ctv->props.end() && checkIt->second.readTy);
                attachMagicFunction(*checkIt->second.readTy, std::make_shared<MagicClassInstanceCheck>());
            }
        }

        registerTestTypes();
        Luau::freeze(f.globals.globalTypes);


        return *frontend;
    }
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag sff_LuauAllowGlobalDeclarationToBeCalledClass{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
};

} // namespace Luau

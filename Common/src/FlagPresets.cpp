// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/FlagPresets.h"

#include "Luau/Common.h"

#include <string.h>

namespace Luau
{

namespace
{

// Each flag is listed under the first preset that has it. Upstream flags upstream still has behind a flag only appear
// once they're fixes. Debug flags, integer limits and flags upstream reverted never appear. A branch lists the flags
// it defines here, since a test requires every name to be a flag of the build.
const FlagPresetEntry kEntries[] = {
    // Upstream fixes upstream has made permanent, and Luwu's fixes
    {"LuauBidirectionalInferenceVariadics", FlagPreset::Fixes},
    {"LuauIndexingIntoErrorGivesError", FlagPreset::Fixes},
    {"LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier", FlagPreset::Fixes},
    {"LuauTrackPrefixLocal", FlagPreset::Fixes},
    {"LuauTypeFunctionSerializeArgNames", FlagPreset::Fixes},
    {"LuauTypeFunctionSupportsFrozen", FlagPreset::Fixes},
    {"LuauUdataMetatablePinned", FlagPreset::Fixes},
    {"LuwuExportedClassIsNilWorkaround", FlagPreset::Fixes},
    {"LuwuExternTypeUseDefinitionScope", FlagPreset::Fixes},

    // Upstream fixes still behind a flag upstream, upstream speedups and finished upstream changes
    {"FixMathNoisePrecision", FlagPreset::Recommended},
    {"LuauAddRecursionCounterToNonStrictTypeChecker", FlagPreset::Recommended},
    {"LuauAllowGlobalDeclarationToBeCalledClass", FlagPreset::Recommended},
    {"LuauAutocompleteExport", FlagPreset::Recommended},
    {"LuauCallFeedback", FlagPreset::Recommended},
    {"LuauCheckFunctionStatementTypes", FlagPreset::Recommended},
    {"LuauCIProto", FlagPreset::Recommended},
    {"LuauCompileStringInterpTargetTop", FlagPreset::Recommended},
    {"LuauCstAttr", FlagPreset::Recommended},
    {"LuauDirectFieldGet", FlagPreset::Recommended},
    {"LuauDisallowRedefiningBuiltinTypes", FlagPreset::Recommended},
    {"LuauDropUnionSubtypeReasoning", FlagPreset::Recommended},
    {"LuauEmitCallFeedback", FlagPreset::Recommended},
    {"LuauHigherOrderGenericInference", FlagPreset::Recommended},
    {"LuauImproveUniqueTableWidthSubtyping", FlagPreset::Recommended},
    {"LuauSubtypingMissingPropertiesAsNil", FlagPreset::Recommended},
    {"LuauTypeFunctionStructuredErrors", FlagPreset::Recommended},
    {"LuauUdtfTypeUseTaggedMetatable", FlagPreset::Recommended},

    // Luwu's error messages and lints, small library additions, `none`, and embedder APIs that only add functions
    {"LuauBetterMissingPropertiesTypeError", FlagPreset::Recommended},
    {"LuauBetterPackAndVariadicMismatchErrors", FlagPreset::Recommended},
    {"LuauIndexerModifierMismatchErrors", FlagPreset::Recommended},
    {"LuauTruthyFalsy", FlagPreset::Recommended},
    {"LuwuBufferIsFrozen", FlagPreset::Recommended},
    {"LuwuExternallyManagedBuffers", FlagPreset::Recommended},
    {"LuwuExternalString", FlagPreset::Recommended},
    {"LuwuFatCClosure", FlagPreset::Recommended},
    {"LuwuHelpfulSubtypingErrors", FlagPreset::Recommended},
    {"LuwuManagedReferences2", FlagPreset::Recommended},
    {"LuwuNonePrimitive", FlagPreset::Recommended},
    {"LuwuPcallMulti", FlagPreset::Recommended},
    {"LuwuTableDiscard", FlagPreset::Recommended},
    {"LuwuTableRemoveFootgunLint", FlagPreset::Recommended},

    // Luwu's newer features. A branch adds its own when it lands (traits, destructuring and the rest), and DebugLuwu
    // flags go under WorkInProgress.
    {"LuwuClasses", FlagPreset::New},
    {"LuwuDefaultArguments", FlagPreset::New},
    {"LuwuExternTypeGenericMethods", FlagPreset::New},
    {"LuwuGenericNominals", FlagPreset::New},
};

// Turns on the boolean flag `name`; false when the build has no such flag
bool turnOn(const char* name)
{
    for (FValue<bool>* flag = FValue<bool>::list; flag; flag = flag->next)
    {
        if (strcmp(flag->name, name) == 0)
        {
            flag->value = true;
            return true;
        }
    }

    return false;
}

} // namespace

bool flagPresetFromName(const char* name, FlagPreset& preset)
{
    if (strcmp(name, "fixes") == 0)
        preset = FlagPreset::Fixes;
    else if (strcmp(name, "recommended") == 0)
        preset = FlagPreset::Recommended;
    else if (strcmp(name, "new") == 0)
        preset = FlagPreset::New;
    else if (strcmp(name, "workinprogress") == 0)
        preset = FlagPreset::WorkInProgress;
    else
        return false;

    return true;
}

int setFlagPreset(FlagPreset preset)
{
    int count = 0;
    for (const FlagPresetEntry& entry : kEntries)
    {
        bool inPreset = int(entry.preset) <= int(preset);
        if (inPreset && turnOn(entry.name))
            count++;
    }

    return count;
}

const FlagPresetEntry* flagPresetEntries(size_t* count)
{
    *count = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

} // namespace Luau

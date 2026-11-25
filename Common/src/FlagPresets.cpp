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
    {"LuauAllowIntersectionOfOneTableWithExtern", FlagPreset::Fixes},
    {"LuauAlsoInstantiateInferredArguments", FlagPreset::Fixes},
    {"LuauAlwaysIntersectTablesWithTables", FlagPreset::Fixes},
    {"LuauAutocompleteMetatableInheritance", FlagPreset::Fixes},
    {"LuauAutocompleteSkipErrorTypeInUnion", FlagPreset::Fixes},
    {"LuauAutoStack", FlagPreset::Fixes},
    {"LuauAvoidCascadingRecursiveConstraintViolationError", FlagPreset::Fixes},
    {"LuauAvoidTrivialPhis", FlagPreset::Fixes},
    {"LuauBidirectionalInferenceBetterLambdaHandling", FlagPreset::Fixes},
    {"LuauBidirectionalInferenceVariadics", FlagPreset::Fixes},
    {"LuauCheckTypeForDeprecated", FlagPreset::Fixes},
    {"LuauCloneTableFix", FlagPreset::Fixes},
    {"LuauCodegenA64ExitUseCheck", FlagPreset::Fixes},
    {"LuauCodegenBuilinDeadRange", FlagPreset::Fixes},
    {"LuauCodegenFixBufferLenCheck", FlagPreset::Fixes},
    {"LuauCodegenFixTwoResA64Builtin", FlagPreset::Fixes},
    {"LuauCodegenProtectData", FlagPreset::Fixes},
    {"LuauCodegenRexWidth", FlagPreset::Fixes},
    {"LuauCollapseDirectBoundCycles", FlagPreset::Fixes},
    {"LuauConcatDoesntAlwaysReturnString", FlagPreset::Fixes},
    {"LuauDeprecatedAttributeOnAnonymousFunctions", FlagPreset::Fixes},
    {"LuauDoNotEmplaceAnnotatedType", FlagPreset::Fixes},
    {"LuauDoNotOverwriteAstDefs", FlagPreset::Fixes},
    {"LuauDontBindOptionalGenericToNil", FlagPreset::Fixes},
    {"LuauFixInfiniteTypeRedundantBind", FlagPreset::Fixes},
    {"LuauFixPropReadsOnMetatableTypes", FlagPreset::Fixes},
    {"LuauFrontendSourceNodeErase", FlagPreset::Fixes},
    {"LuauGcMarkUdataAccess", FlagPreset::Fixes},
    {"LuauGcTableStepFix", FlagPreset::Fixes},
    {"LuauIndexingIntoErrorGivesError", FlagPreset::Fixes},
    {"LuauInstantiateFunctionTypeBeforePush", FlagPreset::Fixes},
    {"LuauInstantiationUsesPolarity", FlagPreset::Fixes},
    {"LuauLimitUnificationRecursion", FlagPreset::Fixes},
    {"LuauMathRoundNegZero", FlagPreset::Fixes},
    {"LuauNoDuplicateBinaryPrefix", FlagPreset::Fixes},
    {"LuauRemoveConstraintSolverEmplace", FlagPreset::Fixes},
    {"LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier", FlagPreset::Fixes},
    {"LuauSelfIsSelfAndAlwaysSelf", FlagPreset::Fixes},
    {"LuauStoreConstKeywordBegin", FlagPreset::Fixes},
    {"LuauSubtypeUnionsTogether", FlagPreset::Fixes},
    {"LuauTableEntriesDontNeedToMatchIndent", FlagPreset::Fixes},
    {"LuauTrackPrefixLocal", FlagPreset::Fixes},
    {"LuauTypeFunctionSerializeArgNames", FlagPreset::Fixes},
    {"LuauTypeFunctionSupportsFrozen", FlagPreset::Fixes},
    {"LuauTypeFunctionTableIndexerIsReadOnly", FlagPreset::Fixes},
    {"LuauUdataMetatablePinned", FlagPreset::Fixes},
    {"LuauUdtfCreateSingletonFixErrorMessage", FlagPreset::Fixes},
    {"LuauXpcallFixMessageYieldPath", FlagPreset::Fixes},
    {"LuauYieldIter2", FlagPreset::Fixes},
    {"LuwuExportedClassIsNilWorkaround", FlagPreset::Fixes},
    {"LuwuExternTypeUseDefinitionScope", FlagPreset::Fixes},

    // Upstream fixes still behind a flag upstream, upstream speedups and finished upstream changes
    {"FixMathNoisePrecision", FlagPreset::Recommended},
    {"LuauAddRecursionCounterToNonStrictTypeChecker", FlagPreset::Recommended},
    {"LuauAllowGlobalDeclarationToBeCalledClass", FlagPreset::Recommended},
    {"LuauAutocompleteConst", FlagPreset::Recommended},
    {"LuauAutocompleteExport", FlagPreset::Recommended},
    {"LuauAutocompleteFunctionArglistSuggestion", FlagPreset::Recommended},
    {"LuauBidirectionalInferenceSimplifyTables", FlagPreset::Recommended},
    {"LuauCallFeedback", FlagPreset::Recommended},
    {"LuauCheckFunctionStatementTypes", FlagPreset::Recommended},
    {"LuauCIProto", FlagPreset::Recommended},
    {"LuauCodegenConstVectorBufferRead", FlagPreset::Recommended},
    {"LuauCodegenNoEcbData", FlagPreset::Recommended},
    {"LuauCodegenSharedLog", FlagPreset::Recommended},
    {"LuauCodegenSubstituteReplacements", FlagPreset::Recommended},
    {"LuauCodegenVmExitSyncMultiUse", FlagPreset::Recommended},
    {"LuauCompileIifeInline", FlagPreset::Recommended},
    {"LuauCompileStringInterpTargetTop", FlagPreset::Recommended},
    {"LuauCstAttr", FlagPreset::Recommended},
    {"LuauCustomYieldablePcalls", FlagPreset::Recommended},
    {"LuauDirectFieldGet", FlagPreset::Recommended},
    {"LuauDisallowRedefiningBuiltinTypes", FlagPreset::Recommended},
    {"LuauDropUnionSubtypeReasoning", FlagPreset::Recommended},
    {"LuauEmitCallFeedback", FlagPreset::Recommended},
    {"LuauHigherOrderGenericInference", FlagPreset::Recommended},
    {"LuauImproveUniqueTableWidthSubtyping", FlagPreset::Recommended},
    {"LuauPropertyModifierMismatchErrors", FlagPreset::Recommended},
    {"LuauRemoveExtraSubtypingInstances", FlagPreset::Recommended},
    {"LuauSubtypingMissingPropertiesAsNil", FlagPreset::Recommended},
    {"LuauTypeFunctionStructuredErrors", FlagPreset::Recommended},
    {"LuauUdataDirectAccess6", FlagPreset::Recommended},
    {"LuauUdtfTypeIsSubtypeOf", FlagPreset::Recommended},
    {"LuauUdtfTypeToStringMetamethod", FlagPreset::Recommended},
    {"LuauUdtfTypeUseTaggedMetatable", FlagPreset::Recommended},
    {"LuauVirtualBcBuilder", FlagPreset::Recommended},

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

// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"
#include "Luau/FlagPresets.h"

#include "doctest.h"

#include <string.h>

#include <string>
#include <utility>
#include <vector>

LUAU_DYNAMIC_FASTFLAG(LuauSelfIsSelfAndAlwaysSelf)

// Luwu: the boolean flag `name`, or nullptr when the build has none
static Luau::FValue<bool>* findBoolFlag(const char* name)
{
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
    {
        if (strcmp(flag->name, name) == 0)
            return flag;
    }
    return nullptr;
}

// Turns every boolean flag off for the test, and back to its old value afterwards
struct AllFlagsOff
{
    std::vector<std::pair<Luau::FValue<bool>*, bool>> saved;

    AllFlagsOff()
    {
        for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        {
            saved.push_back({flag, flag->value});
            flag->value = false;
        }
    }

    ~AllFlagsOff()
    {
        for (const auto& [flag, value] : saved)
            flag->value = value;
    }
};

TEST_SUITE_BEGIN("FlagPresets");

// Every flag a preset names exists, so a renamed or removed flag can't silently drop out of a preset. This binary links
// every library that defines one.
TEST_CASE("PresetsNameRealFlags")
{
    // Nothing else in this binary uses Require's only preset flag, and an object file nothing uses isn't linked
    CHECK(findBoolFlag("LuauSelfIsSelfAndAlwaysSelf") == &DFFlag::LuauSelfIsSelfAndAlwaysSelf);

    size_t count = 0;
    const Luau::FlagPresetEntry* entries = Luau::flagPresetEntries(&count);
    REQUIRE(count > 0);

    for (size_t i = 0; i < count; i++)
    {
        INFO(std::string(entries[i].name));
        CHECK(findBoolFlag(entries[i].name) != nullptr);

        for (size_t j = 0; j < i; j++)
            CHECK(strcmp(entries[i].name, entries[j].name) != 0);
    }
}

// A preset turns on its flags and every earlier preset's, and nothing else
TEST_CASE("PresetsTurnOnTheirFlagsAndTheEarlierPresets")
{
    AllFlagsOff off;

    size_t count = 0;
    const Luau::FlagPresetEntry* entries = Luau::flagPresetEntries(&count);

    int fixes = 0;
    int recommended = 0;
    for (size_t i = 0; i < count; i++)
    {
        fixes += entries[i].preset == Luau::FlagPreset::Fixes;
        recommended += entries[i].preset <= Luau::FlagPreset::Recommended;
    }

    CHECK_EQ(Luau::setFlagPreset(Luau::FlagPreset::Fixes), fixes);
    for (size_t i = 0; i < count; i++)
    {
        INFO(std::string(entries[i].name));
        Luau::FValue<bool>* flag = findBoolFlag(entries[i].name);
        REQUIRE(flag);
        CHECK_EQ(flag->value, entries[i].preset == Luau::FlagPreset::Fixes);
    }

    CHECK_EQ(Luau::setFlagPreset(Luau::FlagPreset::Recommended), recommended);
    CHECK_EQ(Luau::setFlagPreset(Luau::FlagPreset::WorkInProgress), int(count));

    int on = 0;
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        on += flag->value;
    CHECK_EQ(on, int(count));
}

TEST_CASE("PresetNames")
{
    Luau::FlagPreset preset = Luau::FlagPreset::Fixes;
    CHECK(Luau::flagPresetFromName("workinprogress", preset));
    CHECK(preset == Luau::FlagPreset::WorkInProgress);
    CHECK(Luau::flagPresetFromName("recommended", preset));
    CHECK(preset == Luau::FlagPreset::Recommended);
    CHECK(Luau::flagPresetFromName("new", preset));
    CHECK(preset == Luau::FlagPreset::New);
    CHECK_FALSE(Luau::flagPresetFromName("everything", preset));
}

TEST_SUITE_END();

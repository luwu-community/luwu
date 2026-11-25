// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <stddef.h>

namespace Luau
{

// Luwu: a named set of fast flags an embedder turns on together, instead of finding and naming every fix and speedup
// itself. Each preset holds every flag of the presets before it. FLAGS.md lists every flag and which preset has it.
enum class FlagPreset
{
    // Correctness and safety fixes: upstream's that upstream has made permanent, and Luwu's own
    Fixes,
    // Fixes, speedups and finished upstream changes, plus Luwu's small, stable improvements. No large Luwu features.
    Recommended,
    // Recommended, plus the Luwu flags of Luwu's newer features, such as classes, traits and destructuring. Luwu considers
    // them on by default, but they may still change until Luwu 0.1.0.
    New,
    // New, plus the DebugLuwu flags the full language needs, such as match and try. Features still under heavy work
    // (integers, builtin classes, the `ast` library, buffer comparison, batched buffer reads and writes, luwon) are in no
    // preset.
    WorkInProgress,
};

struct FlagPresetEntry
{
    const char* name;
    FlagPreset preset;
};

// The preset named `name` ("fixes", "recommended", "new" or "workinprogress"); false when no preset has that name
bool flagPresetFromName(const char* name, FlagPreset& preset);

// Turns on every flag of `preset` that this build has, and returns how many it turned on. A flag that belongs to a
// library the build doesn't link, like Analysis in a VM-only build, is skipped.
int setFlagPreset(FlagPreset preset);

// Every flag a preset names, `*count` of them, each with the first preset that has it
const FlagPresetEntry* flagPresetEntries(size_t* count);

} // namespace Luau

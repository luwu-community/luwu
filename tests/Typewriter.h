// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu typewriter tests: catch syntax and type error cascades, where half-typed code breaks more than the line being
// typed. A test types new code into a file one character at a time, the way a user would in an editor running
// luwu-lsp, and parses and checks the whole file after every keystroke.
#pragma once

#include <string>
#include <vector>

namespace Luau
{

struct Fixture;

// Types `text` into a file between `before` and `after`, one character at a time. A keystroke fails when it leaves more
// than one parse error, or any type error or lint warning in `after`: that code was clean before typing began, so
// anything reported there is a cascade. A message containing one of `intentional` never counts: some errors and lints
// come in pairs on purpose, a second span pointing the user at the real problem.
//
// Like the editor, typing an opener (`(`, `[`, `{`, a quote) inserts its closer, and with `autocompleteEnd` (luwu-lsp's
// setting of that name), Enter after a block opener puts its `end` on the line after the cursor, indented like the
// opener; typing the closer steps over it. So write blocks over several lines, the way they're typed. Until a block's
// `end` is in, the block holds the code after it, so it may add one "Expected 'end'" error and the code after the
// cursor isn't checked or linted.
//
// Returns a report of every failing keystroke, with what had been typed and the errors, or "" when none failed.
std::string typewrite(
    Fixture& fixture,
    const std::string& before,
    const std::string& text,
    const std::string& after,
    bool autocompleteEnd,
    const std::vector<std::string>& intentional = {}
);

// Requires `before + after` and `before + text + after` to have no type errors, and no lint warnings in `after`, then
// that typing `text` causes no cascade, with luwu-lsp's autocompleteEnd both on and off.
// `after` mustn't use anything `text` declares, or it reports the names as unknown until they're typed.
void checkTypewriting(
    Fixture& fixture,
    const std::string& before,
    const std::string& text,
    const std::string& after,
    const std::vector<std::string>& intentional = {}
);

} // namespace Luau

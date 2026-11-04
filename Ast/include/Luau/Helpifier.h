// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Luau
{

// Luwu: writes the help in an error message, so every help reads the same:
//
//   Help (<topic>):
//     • <bullet>
//     • <bullet>
//
// Every help in a message goes through one, so turning help off (`FrontendOptions::showHelp`) leaves no help anywhere.
// Off, or with no bullets, it gives "". The caller places it in the message, usually as its own paragraph.
struct Helpifier
{
    bool enabled = true;

    std::string operator()(std::string_view topic, std::initializer_list<std::string_view> bullets) const;
    // The same, for bullets chosen at runtime: `count` of them from `bullets`.
    std::string operator()(std::string_view topic, const std::string_view* bullets, size_t count) const;

    // The help as the last paragraph of `message`; nothing when help is off.
    void appendTo(std::string& message, std::string_view topic, std::initializer_list<std::string_view> bullets) const;
};

} // namespace Luau

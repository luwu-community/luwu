// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace Luwu::Dsl
{

class Evaluator;

// The NCG module: the C++ module, then the Luna definitions of define-ircmd and the headers generated from IR commands
// (defs/ncg/ircmds.luna). Returns the files it loaded, for depfiles.
std::vector<std::string> installNcg(Evaluator& evaluator);

// Runs the compiler with command-line arguments (without the program name), writing normal output and diagnostics
// to the given streams. Returns the process exit code.
int runDriver(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace Luwu::Dsl

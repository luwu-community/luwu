// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <optional>

// Luwu object notation: `--luwon-to-json[=lossless] <file>` writes a data file as JSON, and `--json-to-luwon <file>` writes
// JSON as a data file, both to stdout. `-` reads stdin.

// Runs the conversion `argv` asks for and returns the exit code, or nullopt when it asks for none
std::optional<int> runLuwonJsonCommand(int argc, char** argv);

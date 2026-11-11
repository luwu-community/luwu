// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Driver.h"

#include <iostream>

int main(int argc, char** argv)
{
    return Luwu::Dsl::runDriver(std::vector<std::string>(argv + 1, argv + argc), std::cout, std::cerr);
}

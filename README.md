Luwu ![CI](https://github.com/mluau/luwu/actions/workflows/build.yml/badge.svg) [![codecov](https://codecov.io/gh/mluau/luwu/branch/master/graph/badge.svg)](https://codecov.io/gh/mluau/luwu)
====

Luwu is a fast, small, safe, gradually typed embeddable scripting language based on [Luau](https://luau.org).

Luwu is a community-led fork intended to provide a more featureful and helpful experience for general-purpose, open-source language development. It will evolve with features, syntax, and semantics that may not align with upstream Luau.

Luwu is backwards compatible with Luau up to and including version 0.730, with possible exceptions for `export local` and Luau's unreleased integer feature as those areas are improved in Luwu.

For more information about Luwu and how to contribute, please join our Discord server [hina & ferris](https://discord.gg/3MJ37CFNWh). Credit for the name goes to @Crazyblox!

For RFCs and changes to the language, please see the [RFCs folder](/rfcx/). To propose new features, discuss them in our `#features` channel on Discord.

# Usage

Luwu is an embeddable programming language, but it also comes with two command-line tools by default, `luau` and `luau-analyze`.

`luau` is a command-line REPL and can also run input files. Note that REPL runs in a sandboxed environment and as such doesn't have access to the underlying file system except for ability to `require` modules.

`luau-analyze` is a command-line type checker and linter; given a set of input files, it produces errors/warnings according to the file configuration, which can be customized by using `--!` comments in the files or [`.luaurc`](https://rfcs.luau.org/config-luaurc) files. For details, please refer to our [type checking](https://luau.org/typecheck) and [linting](https://luau.org/lint) documentation. Our community maintains a language server frontend for `luau-analyze` called [luau-lsp](https://github.com/JohnnyMorganz/luau-lsp) for use with text editors.

# Installation

You can install and run Luwu by downloading the compiled binaries from [a recent release](https://github.com/mluau/luwu/releases); note that `luau` and `luau-analyze` binaries from the archives will need to be added to PATH or copied to a directory like `/usr/local/bin` on Linux/macOS.

Alternatively, upstream Luau is available from the packaged distributions below. These packages are not maintained by the Luwu development team and may not match Luwu's Luau 0.730 compatibility baseline:

- macOS: [Install Homebrew](https://docs.brew.sh/Installation) and run `brew install luau`
- Arch Linux: Luau has been added to the official Arch Linux packages repository under the extras repository (see [``luau``](https://archlinux.org/packages/extra/x86_64/luau/)), simply install using ``pacman``: ``pacman -Syu luau``
- Alpine Linux: [Enable community repositories](https://wiki.alpinelinux.org/w/index.php?title=Enable_Community_Repository) and run `apk add luau`
- Gentoo Linux: Luau is [officially packaged by Gentoo](https://packages.gentoo.org/packages/dev-lang/luau) and can be installed using `emerge dev-lang/luau`. You may have to unmask the package first before installing it (which can be done by including the `--autounmask=y` option in the `emerge` command).

After installing, you will want to validate the installation was successful by running the test case [here](https://luau.org/getting-started).

## Building

On all platforms, you can use CMake to run the following commands to build Luwu binaries from source:

```sh
mkdir cmake && cd cmake
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build . --target Luau.Repl.CLI --config RelWithDebInfo
cmake --build . --target Luau.Analyze.CLI --config RelWithDebInfo
```

Alternatively, on Linux and macOS, you can also use `make`:

```sh
make config=release luau luau-analyze
```

To integrate Luwu into your CMake application projects as a library, at the minimum, you'll need to depend on the `Luau.Compiler` and `Luau.VM` projects. From there you need to create a new state (using a Lua 5.x API such as `lua_newstate`), compile source to bytecode, and load it into the VM like this:

```cpp
// needs lua.h and luacode.h
size_t bytecodeSize = 0;
char* bytecode = luau_compile(source, strlen(source), NULL, &bytecodeSize);
int result = luau_load(L, chunkname, bytecode, bytecodeSize, 0);
free(bytecode);

if (result == 0)
    return 1; /* return chunk main function */
```

For more details about the use of the host API, you currently need to consult the [Lua 5.x API](https://www.lua.org/manual/5.1/manual.html#3). Luwu inherits Luau's close alignment with that API, including a few deviations such as the need to compile source separately (which is important for deploying the VM without a compiler) and the lack of `__gc` support (use `lua_newuserdatadtor` instead).

To gain advantage of many performance improvements, it's highly recommended to use the `safeenv` feature, which sandboxes individual scripts' global tables from each other, and protects builtin libraries from monkey-patching. For this to work, you must call `luaL_sandbox` on the global state and `luaL_sandboxthread` for each new script's execution thread.

# Testing

Luwu has an internal test suite; in CMake builds, it is split into two targets, `Luau.UnitTest` (for the bytecode compiler and type checker/linter tests) and `Luau.Conformance` (for the VM tests). The unit tests are written in C++, whereas the conformance tests are largely written in Luwu (see `tests/conformance`).

Makefile builds combine both into a single target that can be run via `make test`.

# Dependencies

Luwu uses C++ as its implementation language. The runtime requires C++11, while the compiler and analysis components require C++17. It should build without issues using Microsoft Visual Studio 2017 or later, or gcc-7 or clang-7 or later.

Other than the STL/CRT, Luwu library components don't have external dependencies. The test suite depends on the [doctest](https://github.com/onqtam/doctest) testing framework, and the REPL command-line depends on [isocline](https://github.com/daanx/isocline).

# License

The Luwu implementation is distributed under the terms of the [MIT License](LICENSE.txt). It is based on [Luau](https://github.com/luau-lang/luau), which is based on the Lua 5.x implementation; both are also distributed under the MIT License.

When Luwu is integrated into external projects, please honor the included license notices and preserve attribution to Luwu, Luau, and Lua.

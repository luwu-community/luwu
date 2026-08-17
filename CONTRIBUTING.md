Thanks for deciding to contribute to Luwu! These guidelines will help make the process painless and efficient.

## Questions

If you have a question about using or implementing the language, please join the [Luwu Discord server](https://discord.gg/3MJ37CFNWh).
Some questions just need answers, but it's nice to keep them for future reference in case other people want to know the same thing.
Some questions help improve the language, implementation or documentation by inspiring future changes.

## Documentation

Luwu is based on Luau, so the [upstream Luau documentation](https://luau.org) remains a useful reference for compatible behavior. Luwu-specific proposals and changes are documented in the [extra RFCs folder](/rfcx/).
Changes that improve clarity, fix grammatical issues, or explain Luwu-specific behavior are warmly welcomed.

Please feel free to [create a pull request](https://help.github.com/articles/about-pull-requests/) to improve our documentation. Note that at this point the documentation is English-only.

## Bugs

If the language implementation doesn't compile on your system, compiles with warnings, doesn't seem to run correctly for your code or if anything else is amiss, please [open a GitHub issue](https://github.com/mluau/luwu/issues/new).
It helps if you note the Git revision issue happens in, the version of your compiler for compilation issues, and a reproduction case for runtime bugs.

Of course, feel free to [create a pull request](https://help.github.com/articles/about-pull-requests/) to fix the bug yourself.

## Features

If you're thinking of adding a new feature to the language, library, analysis tools, etc., please *don't* start by submitting a pull request.
Discuss the idea in the `#features` channel on the [Luwu Discord server](https://discord.gg/3MJ37CFNWh) before starting implementation so the community can refine the proposal and identify potential conflicts.

For features that result in an observable change to the language's syntax or semantics, create an RFC in the [extra RFCs folder](/rfcx/) using the provided [template](/rfcx/TEMPLATE.md). Follow the process in the [extra RFC guidelines](/rfcx/README.md), including the implementation, feature flag, and maintainer requirements.

Luwu is willing to evolve independently from upstream Luau, but every feature must still be evaluated for language simplicity, maintainability, performance, and cross-feature interactions.
Feature requests may not be accepted even if a comprehensive RFC is written; the benefits need to justify the costs to the language and its community.
We generally apply a standard similar to the C\# team's famous [Minus 100 Points](https://learn.microsoft.com/en-us/archive/blogs/ericgu/minus-100-points).

## Code style

Contributions to this project are expected to follow the existing code style.
`.clang-format` file mostly defines syntactic styling rules (you can run `make format` to format the code accordingly).

As for naming conventions, most Luwu components use `lowerCamelCase` for variables and functions, `UpperCamelCase` for types and enums, `kCamelCase` for global constants and `SCARY_CASE` for macros.

Within the VM component, the code style is different - we expect `lua_` or `luaX_` prefix for functions that are public or used across different VM files, camel case isn't used and macros are often using lowercase.

## Testing

All pull requests will run through a continuous integration pipeline using GitHub Actions that will run the built-in unit tests and integration tests on Windows, macOS and Linux.
You can run the tests yourself using `make test` or using `cmake` to build `Luau.UnitTest` and `Luau.Conformance` and run them.

When making code changes please try to make sure they are covered by an existing test or add a new test accordingly.

## Performance

One of the central features of Luwu is performance; our runtime in particular is heavily optimized for high performance and low memory consumption, and code is generally carefully tuned to result in close-to-optimal assembly for x64 and AArch64 architectures. The analysis code is not optimized to the same level of detail, but performance is still very important to make sure that we can support interactive IDE features.

As such, it's important to make sure that the changes, including bug fixes, improve (or at least do not regress) performance. For the VM, this can be validated by running `bench/bench.py` on two binaries built in Release mode, before and after the changes. Note that our benchmark coverage is not complete, and in some cases, additional performance testing will be necessary to determine if the change can be merged.

## Feature flags

For large bug fixes or features that apply to the Luwu components and not just the CLI tools, we may ask that you introduce a feature flag to gate your changes. The feature flags use the `LUAU_FASTFLAG` macro family defined in `Luau/Common.h` and allow changes to be enabled and rolled back safely. The tests run the code with flags in their default and enabled states to ensure correctness.

## Licensing

By contributing changes to this repository, you license your contribution under the MIT License, and you agree that you have the right to license your contribution under those terms.

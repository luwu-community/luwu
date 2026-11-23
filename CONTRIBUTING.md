# Contributions

## Questions

If you have a question about using or implementing the language, please join the [Luwu Discord server](https://discord.gg/3MJ37CFNWh).
Some questions just need answers, but it's nice to keep them for future reference in case other people want to know the same thing.
Some questions help improve the language, implementation or documentation by inspiring future changes.

## Documentation

Luwu is based on Luau, so the [upstream Luau documentation](https://luau.org) remains a useful reference for compatible behavior. Luwu-specific proposals and changes are documented in the [RFCs folder](/rfcs/).

Changes that improve clarity, fix grammatical issues, or explain Luwu-specific behavior are warmly welcomed.

Please feel free to [create a pull request](https://help.github.com/articles/about-pull-requests/) to improve our documentation. Note that at this point the documentation is English-only.

## Bugs

If the language implementation doesn't compile on your system, compiles with warnings, doesn't seem to run correctly for your code or if anything else is amiss, please [open a GitHub issue](https://github.com/luwu-community/luwu/issues/new).
It helps if you note the Git revision issue happens in, the version of your compiler for compilation issues, and a reproduction case for runtime bugs.

Of course, feel free to [create a pull request](https://help.github.com/articles/about-pull-requests/) to fix the bug yourself.

## Features

If you're thinking of adding a new feature to the language, library, analysis tools, etc., please *don't* start by submitting a pull request.
Discuss the idea in the `#features` channel on the [Luwu Discord server](https://discord.gg/3MJ37CFNWh) before starting implementation so the community can refine the proposal and identify potential conflicts.

For features that result in an observable change to the language's syntax or semantics, create an RFC in the [RFCs folder](/rfcs/) using the provided [template](/rfcs/TEMPLATE.md). Follow the process in the [RFC guidelines](/rfcs/README.md), including the implementation, feature flag, and maintainer requirements.

Luwu is willing to evolve independently from upstream Luau, but every feature must still be evaluated for language simplicity, maintainability, performance, and cross-feature interactions.

Feature requests may not be accepted even if a comprehensive RFC is written; the benefits need to justify the costs to the language and its community.
We generally apply a standard similar to the C\# team's famous [Minus 100 Points](https://learn.microsoft.com/en-us/archive/blogs/ericgu/minus-100-points).

## Code style

Contributions to this project are expected to follow the existing code style:

### C++

- Allman braces: an opening brace goes on its own line.
- Four spaces per indent, never tabs.
- Lines are soft-limited to 150 columns. Going a little over is fine when breaking the line would read worse.
- A function whose parameters don't comfortably fit on one line gets one parameter per line:

  ```cpp
  LuauClass* luaR_newclass(
      lua_State* L,
      TString* name,
      uint32_t numberofinstancemembers,
      uint32_t numberofstaticmembers,
      bool hasmemberdefaults
  )
  ```

- Don't write complex ternary expressions. If the condition needs more than a glance, or the branches span
  several lines, use `if`/`else`, and give a complicated condition a named `bool`. Short ternaries that pick
  between two simple values are fine.

We'd like to put `Luwu` tags above new code and comments that change functionality or document a Luwu-specific
design decision. This means if you're implementing a feature, use these headers:

- `// Luwu <Feature> (<doc path>):` when the feature has an RFC: `// Luwu Classes (rfcs/classes):` or
  `// Luwu Function Default Arguments (rfcs/function-default-arguments.md):`.
- `// Luwu <Description>:` when a concept needs no design doc: `// Luwu bytecode versioning:`.
- `// Luwu:` for a one-off difference from upstream Luau that belongs to no feature.

For feature headers, always use Title Case and a title that matches the RFC's title. A feature named after an API
keeps the API's own spelling: `// Luwu table.drop (rfcs/table-drop.md):`, never `Table Drop` or `Table.Drop`.

If a feature has an existing tag convention, reuse the existing tag, and never use a fast/feature flag's name as the tag.

Not all comments and not all code needs such a header, but these headers make it easier for people to find where
the most important parts of a feature's implementation exists.

If you'd like your comments to be preserved (as long as possible) and not clobbered by other editors or automated tooling, use
`// name <(optional@email)>: my comment` syntax. (example: `// deviaze: this diverges from luau b/c luwu-lsp needs a way to..`)
These contributor-tagged comments should be separated by 1 newline (or any code) from any other comments not subject to this.

Upstream Luau code in this repository keeps its own formatting; don't reformat it, since that only makes
future merges from upstream harder.

As for naming conventions, most Luwu components use `lowerCamelCase` for variables and functions, `PascalCase` for types and enums, `kCamelCase` for global constants and `SCREAMING_SNAKE_CASE` for macros.

Within the VM component, the code style is different - we expect `lua_` or `luaX_` prefix for functions that are public or used across different VM files, camel case isn't used and macros are often using lowercase. Luwu is not a fan of this convention inherited from Lua, and we may modernize it in the future.

### Luwu/Luau

New Luwu code (non-testing) should use 4-space indents, no single line statements, etc.

Luau code may follow stylua conventions, but this isn't a hard requirement.

### Rust

Rust code should follow default rustfmt conventions with soft enforcement.

### Python

Any new Python code in this repo should use the Black formatter, but we should try to start replacing Python code with Luwu code.

## Testing

All pull requests will run through a continuous integration pipeline using GitHub Actions that will run the built-in unit tests and integration tests on Windows, macOS and Linux.

You can run the tests yourself using `cargo test`, `make test` or using `cmake` to build `Luwu.UnitTest` and `Luwu.Conformance` and run them.

When making code changes please try to make sure they are covered by an existing test or add a new test accordingly.

## Performance

One of the central features of Luwu is performance; our runtime in particular is heavily optimized for high performance and low memory consumption, and code is generally carefully tuned to result in close-to-optimal assembly for x64 and AArch64 architectures. The analysis code is not optimized to the same level of detail, but performance is still very important to make sure that we can support interactive IDE features.

As such, it's important to make sure that the changes, including bug fixes, improve (or at least do not regress) performance.

<!-- For the VM, this can be validated by running `bench/bench.py` on two binaries built in Release mode, before and after the changes. Note that our benchmark coverage is not complete, and in some cases, additional performance testing will be necessary to determine if the change can be merged. -->

## Feature flags

Feature flags use the `LUAU_FASTFLAG` macro family defined in `Luau/Common.h`, and let a change be switched off without a new release.

Unlike upstream Luau, which puts every change behind a flag, Luwu only adds a flag when someone may actually need to turn the change off: a new
language feature that is still in progress or in the RFC implementation process, a change to the runtime behavior of existing code, or a risky optimization.
Every flag is churn: it doubles the code paths to test, and it has to be removed again later.

So bug fixes, better error messages, refactors, and performance improvements that don't change runtime behavior go in without a flag.

The tests run the code with flags in their default and enabled states to ensure correctness.

We treat flags prefixed `Luwu` or `Luau` as safe, reasonable to enable by default. Embedders have the ability to disable these if they have problems;
please raise an issue if you need to do this due to a bug. Flags prefixed `DebugLuwu` are experimental, may have gotchas, may not be as well-tested,
are useful but not stable enough to keep enabled by default, etc. These should not be enabled by default unless a user or embedder wants to opt into
new features.

## Licensing

By contributing changes to this repository, you license your contribution under the MIT License, and you agree that you have the right to license your contribution under those terms.

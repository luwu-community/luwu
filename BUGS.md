# Bug Severity

Every Luwu bug gets a severity so the most serious ones are fixed first. Features enabled by default count for more
than experimental features, which sit behind FFlags starting with `Debug`.

Bugs in the language, compiler, VM, native code generation (NCG) and Analysis go to
[luwu's issues](https://github.com/luwu-community/luwu/issues). Bugs in editor support go to
[luwu-lsp's issues](https://github.com/luwu-community/luwu-lsp/issues).

> **Memory safety vulnerabilities must be reported privately**, through this repository's
> [security advisories](https://github.com/luwu-community/luwu/security/advisories/new), never in a public issue.
> That covers anything a script can abuse, like an exploitable buffer overflow. Crashes themselves are public issues.
> See [SECURITY.md](SECURITY.md).

## Critical

- Runtime and compiler crashes (`SIGSEGV`, `SIGILL`, ...). Report these as public issues.
- Other memory safety vulnerabilities. Report these privately, as above.
- Runtime correctness bugs in features enabled by default, including NCG behaving differently from the interpreter.
  For example, `none` testing truthy in native code while the interpreter treats it as falsy.

## High

- Runtime correctness bugs in experimental (`Debug`) features.
- Analysis crashes (`SIGSEGV`, `SIGILL`, ...) in features enabled by default.

## Medium

- Analysis crashes in experimental (`Debug`) features.
- Runtime behavior that differs from its RFC, or a feature whose RFC is missing or underspecified.
- Development tooling bugs that stop the community from working on Luwu, or that affect `master`.

## Low

- Misleading or unhelpful Analysis error messages, including ones inherited from upstream Luau.
- Development tooling bugs that only get in the way, or only affect `integration`.

## Development tooling

Development tooling is everything needed to work on Luwu itself, including but not limited to:

- `cargo build` in luwu;
- `rebuild.luau` in luwu-lsp;
- [Luna](https://github.com/luwu-community/luwu/pull/56), the NCG DSL (still a pull request);
- seal, and the Python scripts that generate or update code and docs.

## What to include

- The Luwu commit you're on (or the luwu-lsp version).
- The FFlags you have on, if you changed any.
- The smallest piece of code that shows the bug.
- For runtime bugs, whether it happens in the interpreter, in native code, or both.

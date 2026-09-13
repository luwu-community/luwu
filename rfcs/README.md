# Luwu RFCs

Luwu includes features described in the RFCs (requests for comment/specification documents) in this folder, as well as all the RFCs proposed and accepted by Roblox's Luau until 0.730 (with the exceptions recorded in UPSTREAM.md).

If you want to propose a feature, idea, or breaking change, and/or new C API, you should submit an RFC according to the process described below.

For implementation statuses, replacements, and compatibility with upstream RFCs, see [UPSTREAM.md](/rfcs/UPSTREAM.md).

## Writing an RFC

Luwu RFCs require a proposed specification as well as a proposed *implementation*. If the feature has been proposed but doesn't have an implementation, we cannot add it in until it has an implementation (with exceptions possibly granted by the core team). You can mark your PR as a draft until you have an implementation ready. This is because we're a community project that isn't getting paid to implement RFCs.

The Luwu community and the core team will absolutely help you implement one if you need help with an implementation and we think the idea is great--just mark it as a draft and communicate with us on Discord.

All RFCs should come with their relevant FFlags, implementation statuses, and entries in the [MAINTAINERS.md](/MAINTAINERS.md) describing who owns/is responsible for maintaining the feature. We are looking to improve the optional feature process so that accepted and standardized features don't need to be FFlagged but can be toggled in a different manner.

Luwu FFlags should be prefixed "Luwu" or "DebugLuwu". Flags prefixed "Luwu" *should* be enabled-by-default in standard builds of `luwu` and `luwu-lsp`, whereas experimental `DebugLuwu` prefixed flags should be opted in on an embedder or user basis. Most new features should be "DebugLuwu" unless they're simple API additions or small improvements, but the decision to mark a feature as "Debug" or not eventually falls to the core team.

An RFC is marked as "Proposed" if it's currently in development or a specification is written and an implementation isn't ready yet. The core team can choose to merge an RFC in without an implementation, keep it as "Proposed", and work on it later. Once an RFC has been implemented, it is given the status "Implemented (Flagged)". After a merge and an evaluation period, if there are no reports of needing to disable a default enabled "Luwu"-prefixed FFlag (or no issues with people enabling a "DebugLuwu" FFlag as if it were a default), we can remove the flag and mark the feature as "Stable".

### RFC process

See [TEMPLATE.md](/rfcs/TEMPLATE.md) for the RFC specification template.

1. If this is a user-facing feature (new syntax, new semantics, new library function), make a new `#features` post describing your idea, why it's needed, and discuss its feature design. If you don't have a Discord, you may open a discussion issue instead. If it's just an embedder-facing C API, you can directly open a PR with the RFC and implementation.
2. Once you're satisfied with community feedback and your design, you can open a draft PR to this repo with the RFC specification draft (which lives here in the rfcs folder).
3. You may start working on an implementation if you're comfortable doing so.
4. If the community has any feedback on the RFC or implementation of it they should raise it at this stage.
5. If you need help with an implementation, ask for assistance on Discord. If this feature has a user-facing component that requires lsp changes, you should also have a branch of `luwu-lsp` to test it out.
6. Once done (or mostly done, needing QA and testing) please let us know in your `#features` thread and `#language-work` on the Discord so we can clone, build, and try to break it.
7. Once all tests pass in CI, you may mark the RFC as ready for review (no longer a draft). Please ping a core team member to run CI if your PRs/pushes don't automatically trigger it. If you have Rust installed, you can run `cargo test` locally to run the CI tests on your machine to check that things *should* work before pushing and running the Actions.
8. A core team member (a member with merge rights in the luwu-community org) should then review and merge your PR if it is well-motivated and implemented.
9. Once the feature is in, you may be expected to make a subsequent PR to update it if a bug is found in your implementation or the RFC specification requires amendments.
10. After a month or two, the core team will re-evaluate the state of the RFC/feature. "DebugLuwu" FFlag(s) may be moved to regular "Luwu" FFlags, which keeps the RFC at "Implemented (Flagged)". Once all of a feature's FFlags have been removed, the RFC status moves to "Stable".

Unmerged or backported PRs from luau-lang/luau should also come with a brief RFC (so we remember that we implemented them) and a section in MAINTAINERS.md referencing the original author(s) of the implementation and who is responsible for maintaining the feature/fix/change in Luwu. Please don't mark authors of upstream changes in luau-lang/luau as Luwu maintainers without their explicit consent.

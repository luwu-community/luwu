# Feature name

Status: one of "Proposed", "Implemented (Flagged)", or "Stable"

FFlag: your fflag(s), like "LuwuBetterUserDefinedClasses" or "DebugLuwuMyFeature, DebugLuwuMyFeatureAlternate"

## Summary

One or two paragraph explanation of the feature.

## Motivation

Why are we doing this? What use cases does it support? What is the expected outcome?

## Design

This is the bulk of the proposal. Explain the design in enough detail for somebody familiar with the language to understand, and include examples of how the feature is used.

Although design should be specific, it shouldn't be so technical or theoretic that a moderately experienced Luwu user could not readily understand it.

If this is a user-facing feature that needs type system (`Analysis`) and/or editor (`luwu-lsp`) support, also describe the relevant type system and editor design in subsections named `### Type system` and/or `### Editor support` or similar.

## C API (optional)

If your feature includes new embedder-exposed APIs, document them here with function signatures and a short explanation of each.

## Compatibility

Does this feature reserve new contextual or breaking keywords? Does it require us to make a semver version bump? Does it break existing code that works in Luwu or Luau 0.730; if it does, is there a good reason for it? Does this feature cause any unexpected behavior regressions? How compatible is this with current upstream Luau? Is this a feature that could be proposed upstream if successful in Luwu?

## Drawbacks

Why should we *not* do this? These should note the drawbacks of the current design/implementation, and not overlap with Alternatives.

## Alternatives

What other designs have been considered? What is the impact of not doing this?

## Future work (optional)

How can we build upon this feature in the future? Will this feature be expanded in a future RFC with more semantics? How will this interact with other wanted but not yet implemented features?

## Prior art (required only for syntax and semantics changing RFCs)

How has this feature been influenced by other programming languages, theory, and practical use?

## Implementation details (optional)

Any relevant implementation details the team should know about the proposed implementation, including optimizations, edge cases, and drawbacks. If the implementation details are all very important and need to be mentioned, consider putting the RFC specification in its own directory in `rfcs/` with an `implementation.md`.

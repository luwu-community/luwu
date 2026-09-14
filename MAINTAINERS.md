# Maintainer List

RFCs listed are from the [rfcs](./rfcs/) directory.

## By FFlag

| RFC                                                                             | FFlag                                                                                                                                              | Maintainer   | Note                                        |
| ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ------------ | ------------------------------------------- |
| [External Buffers](./rfcs/external-buffers.md)                                  | LuwuExternallyManagedBuffers, LuwuBufferIsFrozen                                                                                                   | @cheesycod   |                                             |
| [Function default arguments](./rfcs/function-default-arguments.md)              | LuwuDefaultArguments                                                                                                                               | @Bottersnike |                                             |
| [Generic parameters on extern types](./rfcs/generics-on-extern-types.md)        | LuwuGenericNominals, LuwuExternTypeGenericMethods, LuauExternTypeUseDefinitionScope                                                                | @deviaze     |                                             |
| [None Primitive](./rfcs/none.md)                                                | LuwuNonePrimitive                                                                                                                                  | @cheesycod   |                                             |
| [External Strings](./rfcs/external-strings.md)                                  | LuwuExternalString, DebugLuwuAllowNonNullTerminatedStrings                                                                                         | @cheesycod   | The Debug FFlag is only for the test suite. |
| ['Fat' C Closures (C Closures with Data)](./rfcs/fat-c-closures.md)             | LuwuFatCClosure                                                                                                                                    | @cheesycod   |                                             |
| [Faster Luwu-managed references](./rfcs/api-luwu-managed-refs.md)               | LuwuManagedReferences2                                                                                                                             | @cheesycod   |                                             |
| [Pcall error handler with multiple return values](./rfcs/api-luaupcallmulti.md) | LuwuPcallMulti                                                                                                                                     | @cheesycod   |                                             |
| N/A                                                                             | LuauFunctionUnusedRecursiveLinting, LuauBetterPackAndVariadicMismatchErrors, LuauIndexerModifierMismatchErrors, LuauPropertyModifierMismatchErrors | N/A          | Merged from upstream.                       |
| N/A                                                                             | LuauBetterMissingPropertiesTypeError, LuauFunctionUnusedRecursiveLinting                                                                           | @deviaze     | Merged from upstream.                       |

Grouped related FFlags together.

## By C API

| RFC                                                                                           | C API                                                     | Maintainer | Note |
| --------------------------------------------------------------------------------------------- | --------------------------------------------------------- | ---------- | ---- |
| [`lua_findunuseduserdatatag`, `lua_findunusedlightuserdatatag`](./rfcs/api-findunusedtags.md) | lua_findunuseduserdatatag, lua_findunusedlightuserdatatag | N/A        |      |

# Maintainer List

RFCs listed are from the [rfcx](./rfcx/) directory.

## By FFlag

| RFC                                                                             | FFlag                                                                                                                                              | Maintainer   | Note                                        |
| ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ------------ | ------------------------------------------- |
| [External Buffers](./rfcx/external-buffers.md)                                  | LuauExternallyManagedBuffers, LuauBufferIsFrozen                                                                                                   | @cheesycod   |                                             |
| [Function default arguments](./rfcx/function-default-arguments.md)              | LuauDefaultArguments                                                                                                                               | @Bottersnike |                                             |
| [Generic parameters on extern types](./rfcx/generics-on-extern-types.md)        | LuauGenericNominals, LuauExternTypeGenericMethods, LuauExternTypeUseDefinitionScope                                                                | @deviaze     |                                             |
| [None Primitive](./rfcx/none.md)                                                | LuauNonePrimitive                                                                                                                                  | @cheesycod   |                                             |
| [External Strings](./rfcx/external-strings.md)                                  | LuauExternalString, DebugLuauAllowNonNullTerminatedStrings                                                                                         | @cheesycod   | The Debug FFlag is only for the test suite. |
| ['Fat' C Closures (C Closures with Data)](./rfcx/fat-c-closures.md)             | LuauFatCClosure                                                                                                                                    | @cheesycod   |                                             |
| [Faster Luwu-managed references](./rfcx/api-luwu-managed-refs.md)               | LuauManagedReferences2                                                                                                                             | @cheesycod   |                                             |
| [Pcall error handler with multiple return values](./rfcx/api-luaupcallmulti.md) | LuauPcallMulti                                                                                                                                     | @cheesycod   |                                             |
| N/A                                                                             | LuauFunctionUnusedRecursiveLinting, LuauBetterPackAndVariadicMismatchErrors, LuauIndexerModifierMismatchErrors, LuauPropertyModifierMismatchErrors | N/A          | Merged from upstream.                       |
| N/A                                                                             | LuauBetterMissingPropertiesTypeError, LuauFunctionUnusedRecursiveLinting                                                                           | @deviaze     | Merged from upstream.                       |

Grouped related FFlags together.

## By C API

| RFC                                                                                           | C API                                                     | Maintainer | Note |
| --------------------------------------------------------------------------------------------- | --------------------------------------------------------- | ---------- | ---- |
| [`lua_findunuseduserdatatag`, `lua_findunusedlightuserdatatag`](./rfcx/api-findunusedtags.md) | lua_findunuseduserdatatag, lua_findunusedlightuserdatatag | N/A        |      |

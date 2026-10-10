// Luwu doc references: references (`[fs.file.write]`) and bindings (`[json = require("@std/json")]`)
// written as Luwu code in doc comments.
#pragma once

#include "Luau/Ast.h"
#include "Luau/Location.h"
#include "Luau/NotNull.h"
#include "Luau/ParseOptions.h"
#include "Luau/ParseResult.h"
#include "Luau/TypeFwd.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Luau
{

struct SourceModule;
struct Module;
struct BuiltinTypes;
struct FileResolver;
struct ModuleResolver;
struct TypeCheckLimits;
struct TypeError;
struct TypeMetadataTable;

// `[expr]`, `[expr:method]` or `[type T]` in a doc comment
struct DocRef
{
    // where the reference's code is (inside the brackets)
    Location location;
    // a name, `a.b.c`, or `require("...")` followed by `.b.c`; null for a type reference
    AstExpr* expr = nullptr;
    // `[type T]`
    AstType* type = nullptr;
    // `[Parser:advance]`: `expr` is `Parser`, the method is `advance`
    std::optional<AstName> method;
    Location methodLocation;
};

// `[name = expr]` on a line of its own
struct DocBinding
{
    AstName name;
    Location nameLocation;
    AstExpr* value = nullptr;
};

// Where a resolved reference (or one segment of it: `fs`, `file` and `read` in `[fs.file.read]`) is declared. Recorded on
// the checked Module for editors: links, go to definition.
struct DocRefTarget
{
    // the name in the doc comment
    Location location;
    std::string module;
    Location definition;
};

struct DocComment
{
    Location location;
    // The module-level doc: the first doc comment in the file, before any statement and separated from the first
    // statement by a blank line. Its bindings are visible to every doc comment in the module.
    bool moduleDoc = false;
    // The body of the function the comment documents, if it documents one: its parameters and generics are in scope.
    std::optional<Position> functionBody;
    std::vector<DocRef> refs;
    std::vector<DocBinding> bindings;
};

// Finds the doc comments (`---` lines, `--[=[ ]=]` blocks) in a parsed module and parses the references and bindings in
// them into `sourceModule.docComments`. Errors in them go to `sourceModule.docParseErrors`: a doc comment's code never
// stops the module from compiling or running.
void parseDocComments(SourceModule& sourceModule, std::string_view source);

// Reports the doc comments' syntax errors (any mode but nocheck) and the references that don't resolve (strict), as
// type errors on `module`. Names resolve in the comment's bindings, the module-level doc's, the documented function's
// scope, and the module's scope as of the end of the file (docs refer forward), which reaches the globals.
void checkDocComments(
    const SourceModule& sourceModule,
    Module& module,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<FileResolver> fileResolver,
    NotNull<ModuleResolver> moduleResolver,
    const TypeCheckLimits& limits,
    Mode mode
);

// The doc comment lines in a type's metadata (`comments`: the ones attached where the type was declared, or what a type
// function set), in order. Only doc comments count, by their `prefix`: `---` lines and block comments, the latter
// dedented like a block comment in source.
std::vector<std::string> metadataDocLines(const TypeMetadataTable& metadata);

// The metadata that documents `ty`: the first along the chain of bound types it follows to, so a type function call that
// returned a shared class (`Cat` in `type Cat = Explain<CatBase>`) has its own docs, and the class's own docs otherwise
const TypeMetadataTable* docMetadataOf(TypeId ty);

// A reference in free-standing doc text (what a type function wrote), and where it points
struct DocTextLink
{
    // byte offsets of the `[` and `]` around the reference's target in the text
    size_t open = 0;
    size_t close = 0;
    // `[text][target]`: `open` is the second bracket's
    bool displayText = false;
    // where it's declared; none when that isn't known (a builtin, a name that doesn't resolve)
    std::optional<DocRefTarget> target;
};

// Resolves the references in doc text that isn't in a source file, as if it were a doc comment at `at` in `module`
// (`sourceModule` is the same module's: its module-level doc's bindings apply). Errors go to `errors` when it isn't
// null, all at `at`, since the text has no place in the source.
std::vector<DocTextLink> resolveDocText(
    std::string_view text,
    const SourceModule& sourceModule,
    const Module& module,
    const Location& at,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<FileResolver> fileResolver,
    NotNull<ModuleResolver> moduleResolver,
    const TypeCheckLimits& limits,
    std::vector<TypeError>* errors
);

} // namespace Luau

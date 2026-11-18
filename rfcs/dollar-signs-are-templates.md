# Dollar Signs Are Templates

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for
> experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans
> before it's accepted.

Status: Proposed

FFlag: none yet

## Summary

`$( any text )` and `$( any text )...` are template placeholders. Code that runs never contains one: the parser reports each placeholder as a
single syntax error and parses the rest of the file as if the placeholder were the node its position expects. The `ast` library's templates and
rewrite patterns accept placeholders and fill them in. The same `$` forms also put Luwu inside other text, like HTML: there only the code
inside them is checked, and the text around it is written out where it sits.

This builds on upstream Luau's [Reserve dollar sign (`$`) to be forever unused][reserve] RFC, which keeps `$` free so that tools can use it for
placeholders. Luwu keeps that promise for running code and gives the placeholders tools write a shape the parser understands.

[reserve]: https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/reserve-dollar-sign.md

## Motivation

Code generators write their boilerplate as source with gaps to fill:

```luwu
export class type $(INSERT TYPE HERE)
    $(CONSTRUCTOR)
    name: string
end
```

Today `$` is an invalid character, so the parser loses its place at the first one. This file gets eight syntax errors for two placeholders, the
class closes early, `INSERT TYPE HERE` turns into a call, and the type checker adds five unknown variables on top. Nothing about the file is
checked or highlighted correctly while it's being written.

The `ast` library ([rfcs/reflect.md](reflect.md)) needs placeholders for the same reason. Its rewrites are written as Luwu source with gaps,
and the gaps have to parse as nodes:

```luwu
doc:rewrite("pcall($(f), $(args)...)", "try $(f)($(args)...)")
```

## Design

### Placeholders

There are two forms, and a `$` means nothing on its own: only `$(` starts a placeholder.

- `$( any text )` stands for one node and names it with the text between the parentheses, trimmed. The text doesn't have to be Luwu code,
  so it can be a name like `$(item)`, a description like `$(INSERT TYPE HERE)`, or anything a template engine wants to put there. Luwu reads
  it where that helps, as when it links placeholders with the same text. Parentheses inside it have to balance.
- `$( any text )...` stands for a list of nodes where the position takes a list: arguments, parameters, table items, statements in a block,
  class members, the values of a `return`.

Text that can't balance its parentheses (code with `")"` in a string, say) uses a level, the way long strings do: `$(=( text )=)`,
`$(==( text )==)` and so on. A leveled placeholder ends at the first `)` followed by the same number of `=` and another `)`, so anything fits
between them:

```luwu
const js = luwutojs.translate($(==(
    console.log("bye :)")
)==))
```

A placeholder can sit anywhere one of these can: a name (a binding, a class or field name, a type's name), an expression, a statement, a type, a
type pack, a class member, a table item, a parameter. It stands for one node of the kind its position expects. `$` inside a string is plain text;
inside the `{}` of an interpolated string it's a placeholder like anywhere else in an expression.

### In ordinary source

Each placeholder is one syntax error, and the parser continues with the placeholder in place. Running the file (with `require`, a CLI or
an embedder's `load`) fails with these errors, which name each placeholder that was left in:

```
(1,19): Template '$(INSERT TYPE HERE)' was not replaced with real code
(2,5): Template '$(CONSTRUCTOR)' was not replaced with real code
```

The rest of the file parses as usual, so `name: string` is still a field of the class. The parse errors keep the file from compiling, the same as
any other syntax error.
Placeholders never compile to code that raises when it runs (the way Rust's `todo!()` does): code that has to run is rejected by
the parser before any of it runs.

The AST holds the placeholder as a node of its own: `AstStatTemplate`, `AstExprTemplate`, `AstTypeTemplate`, `AstTypePackTemplate` and a
placeholder flag on names. Each records its text and whether it's a list.

### In the `ast` library

The `ast` library parses templates and patterns with placeholders accepted, and its node classes have `TemplateStat`, `TemplateExpr`,
`TemplateType` and `TemplateTypePack`:

```luwu
const template = ast.template(fs.readfile("boilerplate.luwu"))
const filled = template:fill({
    ["INSERT TYPE HERE"] = "Cat",
    CONSTRUCTOR = "function __init(self, name: string) self.name = name end",
})
```

A value passed to `fill` is a node, or source text the library parses in the placeholder's position. A placeholder with no value is an error
naming it. `ast.parse` on ordinary source returns placeholders as `TemplateStat` and the other nodes inside an `Err` document, so a tool can
list the gaps in a file without treating it as a template.

A placeholder's text can be code in its own right. A build step that compiles Luwu to JavaScript can take the code to translate inside a
placeholder and fill the placeholder with the result:

```luwu
const js = luwutojs.translate($(do
    local cats = 2
    const resp = webkit.sendMessage(.{ cats })
    if resp is Ok(allowed is true) then
        console.log(`allowed to adopt {cats} cats`)
    end
    give nil
))
```

The engine finds each such placeholder, reads its text as Luwu with `placeholder:parse(kind)`, and fills that placeholder with a string:

```luwu
for
    _, call in template:find(ast.Call)
    when call.callee:source() == "luwutojs.translate"
    when const gap = call.args:at(1)
    when gap is ast.TemplateExpr
do
    match gap:parse(ast.Expr)
    case is Ok(expr) then
        template:fill(gap, ast.String { value = luwutojs.compile(expr) })
    else
        report(gap)
    end
end
```

- `placeholder:parse(kind)` returns `Result<T, Fragment<T>>` like `ast.expr`, with locations in the original file.
- `template:fill(node, value)` fills one placeholder node. Filling by text isn't enough when two placeholders have the same text.

### Type system

Code around placeholders is type checked as if they weren't there, so a template gets real diagnostics while it's being written:

- A placeholder in a name position declares that name. `local $(item) = load()` declares a local named `$(item)` with `load()`'s type, and
  `export class type $(INSERT TYPE HERE)` declares a class type with that name.
- A placeholder in an expression or type position first looks for a declaration with the same text in scope. If it finds one, it's a use of that
  declaration and has its type: `print($(item).name)` checks `.name` against `load()`'s result, and a field typed `$(INSERT TYPE HERE)` is the
  class above.
- A placeholder with no declaration in scope has the error type, so nothing that uses it reports a cascade. A statement or class member
  placeholder declares nothing.
- A placeholder's type comes from ordinary Luwu syntax around it, so a template needs nothing new to be typed:
  `local $(item): User = load()` declares `$(item)` as a `User`, and `($(value) :: User)` gives a gap with no declaration a type. The `ast`
  library checks each fill against the type its position has.
- The only diagnostic for a placeholder is its syntax error. The checker adds no "Unknown variable" or type errors for it.

### Editor support

luwu-lsp treats placeholders as code, not as broken text:

- It highlights placeholders and reports them as the parser does. A file that's meant to be a template can say so with a `--!template` hot
  comment, which turns the errors into hints.
- Hover, autocomplete, go to definition and find references work through placeholders. Completing after `$(item).` offers the fields of `load()`'s
  result, and members of a class named by a placeholder complete like any other class's.
- A placeholder declared in a name position links every use of the same text in scope, so renaming `$(item)` renames its uses. After `$(`,
  completion offers the placeholders already in scope.
- A template engine can tell luwu-lsp that a placeholder's text is Luwu code (an expression, a block) and what scope it runs in, through
  the builder hook in "Luwu inside other text". The code inside then gets every editor feature. Its scope is the engine's choice: the
  JavaScript translator above gives it a definitions file for `webkit` and `console` instead of the enclosing file's locals.

### Luwu inside other text

The reverse of a template is a file that's mostly some other text, like HTML, with Luwu inside `$[ ]` blocks:

```html
<ul>
$[ for _, user in users do ]
    <li>$[ user.name ]</li>
$[ end ]
</ul>
```

Luwu defines only where the code is, never what a block means. Whether a block's value is written into the text, whether the blocks form one
chunk or are separate fragments, and what happens to the text between them are the template engine's decisions, because engines want
different things: one joins every block into a function that builds a page, another evaluates each block on its own as a value.

- `$[ ]` marks a block of Luwu code. The code inside is read as Luwu tokens to find the closing `]`, so a `]` inside a string or an index
  doesn't end it. Inside a block, `$( text )` is a template placeholder like anywhere else in Luwu code.
- Outside the blocks, `$` is plain text unless `[` follows it, so `costs $5` and `$name` stay as written.
- `ast.blocks(source)` returns the blocks and the text around them, with their locations.

The engine turns the blocks into Luwu code to check and run: its own glue around them (a call that writes each run of text, a rule for which
blocks write their value) and the blocks' code, copied in. It builds that code through the `ast` library with a map from each copied piece
back to the original file, the way Volar maps Vue templates onto virtual TypeScript files:

```luwu
const virtual = ast.virtual()
for _, part in ast.blocks(source) do
    if part is ast.Text then
        virtual:generated(`write({string.format("%q", part.text)})\n`)
    else
        virtual:code(part)
    end
end
```

Type checking and luwu-lsp work on that virtual code and map every diagnostic, hover, completion and rename back through the map, so they
point into the original file. Generated glue has no place in the original, so an error inside it is reported at the nearest block. An engine
registers its builder with luwu-lsp for the file globs it owns, which is how the editor checks the blocks the same way the engine runs them.

## Compatibility

Valid Luau and Luwu code is unaffected: `$` has always been a syntax error. Luwu keeps upstream's contract for running code. `$` never gives a
program a meaning, it's never part of an identifier, and code with a placeholder never compiles. What changes is how the error reads and what
the parser recovers.

## Drawbacks

- Upstream's RFC says `$` won't be a component of any syntax. Luwu's parser reads placeholders, even though only to report them, so a tool that
  uses `$(` differently (as shell does for command substitution) would see Luwu's errors instead of a plain invalid character.
- A stray `$(` in broken code reads up to the matching parenthesis, which can be far away.

## Alternatives

- Leave `$` an invalid character. Generators and the `ast` library then substitute text before parsing and lose positions and errors in the
  original template.
- A different placeholder syntax, like mustache's `{{ name }}`. It reads as a table in Luwu, and upstream already set `$` aside for this.

## Future work

### Template hooks in luwu-lsp

A template engine can extend the builder hook from "Luwu inside other text" to placeholder templates, picked by a glob or the
`--!template` hot comment. The hook answers two questions for each placeholder: what may fill it (completion items, and docs for hovers), and
what a typical fill looks like. The checker uses that preview fill in place of the error type, so code around a placeholder is checked against
what will really be there.

### Expected types on hover

The checker knows what each position expects, so hovering a placeholder can say what it needs: "`$(INSERT TYPE HERE)` must be a type", or
"`$(item)` must be a `{ name: string }` here". Typed holes in Haskell and Agda show this the same way.

## Prior art

- [ast-grep](https://ast-grep.github.io/guide/pattern-syntax.html): `$A` matches one node and `$$$A` a list, which upstream's RFC cites.
- Rust's `quote!` macro: `#name` and `#(#items),*` placeholders in Rust source that build syntax trees.
- SQL prepared statements: `$1`, `$2` bound parameters.
- comby and semgrep: structural search with `:[hole]` and `$X` placeholders.
- Template Haskell: `[| ... |]` quotes code and `$(...)` splices into it. Typed quotes (`[|| ... ||]` with `$$(...)` splices) are type checked
  around their splices.
- Scala 3 macros: `'{ ... }` quotes with `${ ... }` splices, checked by the compiler.
- Elixir's `quote`/`unquote`, Lisp's quasiquote and MetaOCaml's `.< >.`/`.~`: the same idea, mostly untyped.
- Typed holes in Haskell (`_name`), Agda and Idris: a program type checks with holes left in, and the compiler reports the type each hole needs.
- [Hazel](https://hazel.org/): a language built around incomplete programs that always parse and type check, with editor services through the
  holes.
- Razor (C#) and templ (Go): templates whose code is typed, with full autocomplete. Razor projects the C# into a virtual file, and templ
  generates Go that gopls checks.
- Volar (Vue) and Svelte's language tools: template regions mapped onto virtual TypeScript files, with plugin APIs for a tool's own constructs.
- ERB, EEx, PHP and JSP: code inside text, the model "Luwu inside other text" follows with `$` forms instead of their delimiters. Jinja and
  Go's `text/template` show what this RFC avoids for templates of code: placeholders as plain text substitution that nothing checks.
- VS Code snippets (`${1:name}`) and JetBrains live templates (`$VAR$`): the same placeholder shape, filled in by hand.

## Unresolved questions

- Whether `$( text )` may span lines.
- How host text writes a literal `$[` outside a block.
- The exact wording of the errors.

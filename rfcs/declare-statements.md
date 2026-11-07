# Declare statements

Status: Implemented (Flagged)

FFlag: LuwuDeclareStatements

## Summary

Allow declare statements in regular user code.

## Motivation

Currently the only way to define globals (in the type system) is via a definitions file. There are many usecases where a user wants to say a global actually exists, will be present at runtime here, or want to introduce a global variable explicitly without being yelled at in static analysis.

It's common to bundle Luwu code. In fact, we might recommend doing so for perf optimizations. Perhaps multiple files are combined into a single module by the user's build system? There's no way to represent that rn without using a global defs file to say those globals will always be present/nil across the whole codebase.

Some files may be ran in a special env compared to the rest of files in a codebase, and need certain globals specific to one file.

Now with classes and traits, the problem is inverted. An embedder may want to provide a class to users but no class definition appears in user-facing code. Maybe the class's implementation is proprietary. Maybe a user wants to dynamically load a class from require or from a bytecode blob at runtime. The type system can't handle those usecases at the moment.

Another issue this can resolve is namespaced extern types. Embedders currently ship runtimes with standard libraries namespaced to specific require aliases provided by regular stub files. There's no way for embedders to scope an extern type to those paths, they're forced to expose them globally even if semantically every operation relating to that extern type is namespaced to that stub file.

## Design

User code is now allowed to `declare` that certain variables exist in the global environment and name types (including extern types!) that usually cannot be named except with specific syntax that forces a value as well. All declarations are trusted by the type system, it's a user mistake if a declaration drifts from the actual implementation.

Value declarations may leave their types out; type declarations may not. `declare name` says the global exists and takes the type the environment's definitions give it, or `any` if they don't declare it. An untyped `declare function` parameter is `any` (and an untyped `...` is `...any`), and a `declare function` without a return type returns nothing, as in definition files. Forgetting a type anywhere in a type declaration (`declare extern type`, `declare class type`) is a syntax error. Declaring the same binding or type using `declare` syntax more than once in a module is a syntax error. Declarations may be placed in `do/end` blocks so they can easily be rolled/unrolled in editors; if they do they still apply to the entire module.

Both global and type declarations only apply to a single module; users may `export` these type declarations so they can import them in other modules, like existing type aliases. Exporting a value declaration (`declare x`, `declare x: Type`, `declare function foo...`) is a syntax error; use a definition file or copy/paste those declarations in a `do/end` block instead.

To say that a global actually exists in the environment (or that you explicitly want to define a new global), use the `declare` contextual keyword with an identifier (and optionally, a type).

```luwu
declare script: {
    path: Path,
    is_entry_path: (any) -> boolean
} -- you can now use `script` without unknown global/unknown binding

declare bundled_config -- exists; typed by the environment's definitions, or `any` without them
declare function log(message, ...) -- (any, ...any) -> ()

declare function require<Stringleton>(target: Stringleton): MagicRequire<Stringleton>
```

Since default arguments subtly affect the parameter meaning (but not type), we allow them to be specified in `declare function` syntax, as well as all functions later referred to in this RFC using this same function syntax:

```luwu
declare function getfile(path = string): Path?
```

User code may now define extern types, a capability that was previously reserved only to embedders. In Luwu, the `with` keyword is optional (both in definition files as well as user code), and allowed only for backwards compatibility with Luau. `with:` is parsed as a property of the extern type rather than as the keyword.

```luwu
-- 'with' is now made optional to match classes and traits, preserved only for backwards compat with luau
export declare extern type Websocket
    function listen(self, cb: CallbackFunction)
    function send(self, message: Message)
end
```

Classes can be declared via a similar syntax, but with `type` so it's obvious that the class isn't put into value scope.

To provide better tooling experience, both `private` and `public` members may be specified, even though only `public` members would be usable. Since nothing is checking this, embedders may omit `private` fields and functions from declarations if they don't feel comfortable exposing their names to users.

If the class uses a primary constructor, the constructor's `private` or `public` access specifier may be placed in the usual location:

```luwu
export declare class type List<T> private (
    private inner: { T }
)
    public function with_capacity<T>(cap: number): List<T>
    public function foo(self): Bar<T>
end
```

Since default values subtly affect field types (optional to pass when called, but always there when used), we allow specifying a default value type on the RHS in class parameters and fields. The RHS must always be a type.

```luwu
declare class type Cat(name: string, age = number)
    some_field = SomeType
    other_field: OtherType
end
```

If a class constructs via `__init`, then the `__init`  should be defined in the declaration so that tooling knows the class is to be initialized via parameter syntax rather than via table constructor. Like with regular classes, declared classes are not allowed to contain both `__init` and a primary constructor:

```luwu
declare class type Foo -- we know class doesn't have fields a and b
    c: any
    d: any
    function __init(self, a: any, b: any)
end
```

Importantly, declaring an extern type or a class *does not* magically bring a value into scope. You must annotate a value as the type for it to be useful.

```luwu
export declare class type List<T>
    -- ...
end


const List: class<List> = luwu.bytecode.eval(fs.readbytes(bycpath))
```

A declared value/type that shadows an existing value/type from the environment's definition files, but has differing meaning (different parameters, different type annotations, etc.), raises a new lint: `DeclareMismatch`.

We allow exact shadows so users may work in environments that have definition files (without incorrect lints/type errors) but then ship standalone scripts that shouldn't need those definition files to typecheck correctly.

An untyped value declaration (`declare name`) takes the environment's type, so it never triggers the lint.

## Future work

Extend this to traits.

```luwu
-- traits may have public or private access specifiers
export declare trait type Container<T>
    expect public function __len(self): number
    expect public function front?(self): T?
    expect public function back?(self): T?
end

export declare trait type Handler
    private cb: SomeFn?
    public final function on_callback(cb: SomeFn)
end
```

## Drawbacks

- The same keyword is used to declare a global binding (`declare x: T`, `declare function`) (treated as existing in value scope) as with a type (brings the type into type scope). This is mitigated by requiring `type` syntax for both `class` and `trait`, just like `extern` already had unintentionally.
- Word soup: `export declare class type type private() end` becomes legal syntax.

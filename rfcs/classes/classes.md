# Classes

FFlags:

- LuwuBetterUserDefinedClasses
- DebugLuauUserDefinedClasses
- DebugLuauUserDefinedClassesRuntime
- LuwuGenericNominals (classes with generic parameters share the same type system mechanisms as extern types)

## Summary

Add user-defined classes with new primitives `class` (the class definition) and `object` (instances of a class).

```luwu
class Cat
    name: string
    age: number = 0
end
const taz = Cat { name = "Taz", age = 14 }

class Dog(name: string, age: number)
    function bark(self)
        print(`{self.name} barked!`)
    end
end

const dog = Dog("Oreo", 14)
dog:bark()

export class Rounding(
    top = vector.create(15, 15),
    bottom = vector.create(15, 15)
)
    function zero(): Rounding
        return Rounding(vector.zero, vector.zero)
    end

    function is_positive(self): boolean
        return self.top.x > 0 and self.top.y > 0 and self.bottom.x > 0 and self.bottom.y > 0
    end
end

export class Frame
    private id: Id
    public name: string
    public position: vector
    public size: vector
    public rounding: Rounding

    public function __init(self, name: string, position: vector, size: vector?, rounding: Rounding?)
        assert(#name > 0, "name should not be empty string")
        self.name = name
        assert(position.z == 0, "position should not have z")
        self.position = position
        if size then
            assert(size.x > 0 and size.y > 0 and size.z >= 0, "size should have only positive components")
            self.size = size
        else
            self.size = vector.create(100, 100)
        end
        if rounding and rounding:is_positive() then
            self.rounding = rounding
        else
            self.rounding = Rounding()
        end
        const id = next_internal_id()
        self.id = id
    end
end

const frame = Frame("MainFrame", vector.create(1, 2), vector.create(100, 40))
```

## Motivation

Classes are an incredibly common way to encapsulate data structures and behavior in many programming languages, but our language doesn't support it officially and requires users to spend more time (and space) writing boilerplate (`__index`, `setmetatable`, `export type Classy = typeof(Classy.constructor(...)))`) to emulate the concept. We should make it easier and more performant.

As of today, the upstream Luau team has partly implemented classes, but its implementation is not finished and has both runtime and static analysis bugs we need to fix. Completeness aside, the Luau team's recent decisions around class-related RFCs feel strange and seem locked to a particular design without considering other possibilities for the feature. We would prefer a different design with semantics that would be more beneficial towards the community.

Nonetheless, we should mention some of their original ["Classes!!" RFC's motivations](https://github.com/luau-lang/rfcs/blob/c41aae5f0a9a8ad2aa9d37e7c35cca94a7c7c1d9/docs/syntax-classes.md), since they hold equally true today:

> - People write object-oriented code. We should afford it in a polished way.
> - Accurate type inference of `setmetatable` has proven to be very difficult to get right. Because of this, the quality of our autocomplete isn't what it could be.
> - A construct with a fixed shape and a completely locked-down metatable will open up optimization opportunities that could improve performance:
>   - If a value is known to be an instance of a particular class, the bytecode compiler should be able optimize method calls to skip the whole `__index` metamethod process and instead generate code to directly call the correct method.
>   - By the same token, method calls can be inlined more aggressively.  Particularly self-method calls eg `self:SomeOtherMethod()`
>   - Field accesses can compile to a simple integral table offset so that the VM doesn't need to do a hashtable lookup as the program runs.
>   - Since every instance of a class has the same set of properties, we can split the hash table: The set of fields can be associated with the class and instances only need to carry the values of those fields.  We think this can improve performance by improving cache locality.
> - Encapsulation at its current state cannot be truly achieved, tables cannot truly be locked-down, and most workarounds for it are too complex for what it's trying to achieve.

In this RFC, we'll be focusing on a base design for classes that allows us to implement reusable logic shared between classes in the future.

## Design

Firstly, we introduce the new `class` and `object` primitives that represent the class definition (and object factory) as well as actual objects (instances) of the class. We chose a new primitive that has an exact field structure with locked down field shape and metatable for future optimization potential.

Our design of classes aims to equally support 2 primary usecases, "POD" and "heavy".

The POD (plain old data) usecase supports users who want to use classes as "named tables" (or structs) to describe mostly data with maybe a few functions. We don't want to increase verbosity and force access specifiers, etc. when using classes as lightweight data structures.

The other main usecase is "heavy", when the user opts into encapsulation (`private`), access specifiers, const members, and in the future, composition with traits.

### Class definition syntax

Classes are created with a new class definition syntax:

```luwu
class ClassName end

class Cat
    name: string
    age: number
    -- all fields are public, 'public' keyword not required on fields/functions
    function meow(self)
        print(`{self.name} says meow!`)
    end
end

class Dog
    public name: string
    public age: number
    -- class contains members marked with access specifiers; all members must define access specifiers
    public function bark(self)
        print(`{self.name} barked!`)
    end
end

class List<T>
    private inner: { T }
    private function __init(self, initial: { T }?, capacity: number)
        -- ...
    end
    -- class has private members, 'public' keyword required
    public function with_capacity(cap: number): List<T>
        return List(nil, cap)
    end
end

-- classes can define a 'primary constructor' to skip __init:
class Symbol(name: string) end

-- classes may have a private primary constructor
class SecretSymbol private (name: string)
    private name
    public function new() return SecretSymbol(get_random_name()) end
    public function is_same_symbol(self, other: Symbol | SecretSymbol)
        if class.isinstance(other, Symbol) then
            return false
        end
        return self.name == other.name
    end
end
```

Specifically:

- A class definition contains the class header and class body.
- The class header starts with the contextual keyword `class`,
- is followed by the class's name, which must be a valid identifier,
- may be followed by an optional generic parameter list like `<A, B, C = type>`, which may contain default generic parameters,
- may be followed by the primary constructor:
  - if present, the primary constructor may start with an access specifier `private` or `public`
  - if present, the primary constructor must contain a class field parameter list in one of three forms, none of which are allowed to contain variadic (`...`) parameters:
    - A function-like parameter list like `(a: T, b: B, c = default, d)`, which may be empty,
    - A field parameter list without access specifiers, like `(a: string, const b = 2, const c)`
    - A field parameter list with access specifiers, like `(public x: T, private const y = default)`.
- After the class header starts the class body.
- The class body contains zero or more class member (fields and functions) declarations (see below),
- The contextual keywords `extends` and `implements` *may not* be fields or function names.
- The class body ends with the `end` keyword.

Class definitions are a block construct like `for` loops, and do not evaluate to a value.

Defining two classes with the same name in the same module is forbidden and raises a syntax error.

Classes must be defined in the top level of a module; attempting to define a class anywhere else, including in a `do/end` block, a function, another class, or any control flow statement or expression, raises a syntax error.

### The `class` primitive

The action of evaluating a class definition statement introduces a *class* value in the module scope.

A `class` is a value that serves as a factory for instances of the class and as a namespace for any functions that are defined on the class.

Class bindings are always `const` and `class` values are always frozen.

Accessing a nonexistent member of a class results in a runtime error.
Similarly, attempting to access a field present on objects of this class (but not on the class itself), also raises a runtime error.

Taking references to class methods via `ClassName.method` syntax is allowed so that classes can easily compose with existing APIs:

```luwu
local n = pcall(SomeClass.getName, someClassObject)
```

The top type of all classes is named `class`. `type()` and `typeof()` return `"class"` when passed a class.

### The `object` primitive

Objects, often referred to as "class instances", are a new type of value in the VM. Objects are lightweight, do not have an array portion, and may only have members with specific names.

`pairs`, `ipairs` , `getmetatable`, and `setmetatable` all raise a runtime error when invoked on an object. Similarly, an object may not be iterated over unless its class implements `__iter`.

Reading or writing a nonexistent class field raises a runtime error. This makes it easy to disambiguate between a nonexistent field and a field whose value is nil.

We introduce a new top type for instances of a class: `object`. The builtin `type()` and `typeof()` functions return `"object"` for any class instance.

We chose this over having them return the class name because class names do not have to be globally unique (they must only unique within a single module) and because we do not want to make it possible for classes to impersonate embedder-provided types.

```luwu
class Cls end
local inst = Cls()

type(Cls) == "class"
typeof(Cls) == "class"

type(inst) == "object"
typeof(inst) == "object"
```

Comparisons between object instances are the same as with tables: If `__eq` is not defined, object comparisons use physical (pointer) equality.  `__eq` is only invoked if both operands are the same type.

### Class member syntax

The term 'field' refers to properties on objects. The term 'function' includes both static functions on a class as well as methods that exist on a class but are called via methodcall syntax on objects of the class. Class members include both fields and functions.

We introduce two specific flavors of keywords to help introduce class members: access specifiers and modifiers.

- Access specifiers: `public`, `private`
- Modifiers: `const`

- Fields are introduced with the new access specifier keywords (or a bare identifier if all fields are public).
- Fields on a class are mutable by default but functions/methods on a class are `const` by default.
- For now, we plan to only implement `public` and `private`. We may look into implementing other access specifiers such as `protected` in the future.
- Fields may include the `const` modifier.

We reuse the `const` keyword to mean "this field is set at class definition evaluation time and may not be modified".

A previous version of this RFC introduced a `static` modifier. We choose to not introduce a static modifier because both static fields and methods can be handled with existing syntax. Static functions are just functions on classes without a `self` parameter. Static fields can be emulated via module-level upvalue (`const`, `local`, and/or exported). We believe the advantage of having a `private` `static` variable (the only functionality not addressed by not adding `static`) is not substantial enough motivation to add a whole keyword to the language; users can just make a module to hold their class with a `local` or `const` static value if they absolutely don't want code unrelated to that class to touch it. Additionally, `static` is a weird (nonobvious) word and would lead to even further keyword soup than we are already introducing with this RFC.

Class members may not be named `class`, `private`, `public`, `const`, `extends`, or `implements`; doing so is a syntax error.
This is to reduce confusion, ambiguity, and to allow future keywords to be used in the class header/body position without breaking existing code.

- When encountered in an unambiguously field/function shaped position, these error messages should read `Fields/functions are not allowed to be named <keyword>`.
- When the `class` keyword is encountered and classes-related FFlags are not enabled, the syntax error should inform users that the classes feature is currently disabled.
- When `extends` is encountered in the class header, the syntax error should inform users that inheritance is not supported in Luwu.
- When `implements` is encountered in the class header, the syntax error should state that the `implements` keyword has not yet been implemented.

```luwu
-- user.luau
--- I am basically a static field!!
local last_id = 0

export class User
    public id: string

    private function __init(self)
        last_id += 1
        self.id = "ID " .. tostring(last_id)
    end
end
```

Methods are introduced with the familiar `function` keyword and follow existing `function` definition syntax.

- All functions on a class via familiar `function` syntax are `const` and may not be mutated.
- A function that doesn't take `self` as its first parameter is a static function.
- A function that takes `self` as its first parameter is a method. The `self` parameter name is hardcoded, like in Rust.

Specifically:

- If a class only has public members, the `public` keyword may be omitted,
- If a class has members with any access specifier, then access specifiers are required on all members,
- A modifier `const` may optionally follow the access specifier. If `const` is specified, it must follow the access specifier.
- If the member is a field, a valid identifier with an optional type annotation should follow,
- If the member is a field, an optional default value expression may be provided after the identifier or type annotation,
- If the member is a function, use the familiar `function` definition syntax.

Since all functions on classes are inherently const, explicitly defining a `const function` inside a class is forbidden. We raise a syntax error for this because `const function` syntax would otherwise be valid both inside and outside a class, and such a function could easily be unintentionally moved or copy/pasted inside a class block instead of the module's top level scope.

### Access Specifiers

Access specifiers allows the user to control access to a specific field within a class. For the scope of this RFC, we will only be introducing the `public` and `private` access specifiers.

#### `public` access specifier

We introduce the `public` keyword to define fields as public, and accessible from everywhere.
This is a contextual keyword that only applies within class member declarations.

Due to existing prior art in Luau of everything being public (tables), and to facilitate POD (plain old data) forms of classes, we felt it makes most sense to treat all fields on a class as `public` by default.

This means, if all fields on a class are public, the user can omit the `public` keyword in front of the field definitions:

```luwu
class Vector3
   x: number -- public keyword can be omitted here, all fields are public.
   y: number
   z: number
end
```

We acknowledge that omitting type annotations here can look pretty bad, but we feel that the ability to use classes without worrying about access specifiers outweighs the minority of people who would use this brand new feature with zero type annotations.

An example of a badly formatted (and unannotated) but valid class definition:

```luwu
class Employee id
    name
    age location
end
```

To reduce ambiguity, if a class defines a field with any access specifier, then the class must specify access specifiers for **all** members:

```luwu
class Vector4
    x: number -- SyntaxError: This class contains non-public members; add the `public` keyword here to prevent ambiguity
    y: number
    z: number
    private w: number
end
class Coord
    public x: number -- SyntaxError: This class mixes explicit and implicit `public`. Remove `public` or add `public` or `private` to all other members to prevent ambiguity.
    y: number
end
```

#### `private` access specifier

To allow full encapsulation, we introduce the `private` access specifier.
This is a contextual keyword that only applies within class member declarations.

If a member is marked as private, it is only accessible from within its enclosing class definition block,
and is therefore locked to the outside world.

Functions within classes may only access `private` fields on their own class, and never any private field on any other class.

Attempting to access a `private` member from outside its class definition block results in a runtime error.
This includes functions outside the class that were called from a function within its class definition block.

```luwu
class User
    public first_name: string
    public last_name: string
    private ssn: string?

    public function name(self): string
        return self.first_name .. " " .. self.last_name
    end

    private function get_ssn(self): string
        if self.ssn then
            return self.ssn
        end
        return get_ssn_from_files(self)
    end
end

const user = User { -- The default constructor can initialize private fields.
    first_name = "Taz",
    last_name = "Parekh",
    ssn = "126-222-1123",
}
```

If a class only has `private` fields and no functions, we raise a type error because such a class will not be usable.

```luwu
class UseMe -- TypeError: this class cannot be used because it only has private fields
    private please: string
    private uses: number
end
```

### `const` modifier

The `const` modifier may only be applied to fields (all functions/methods are always `const`), and should be placed after an access specifier.

A `const` field must be initialized with a value, by the class's constructor. We raise a runtime error upon attempts to modify a `const` field at runtime. As noted below, `const` fields are not enforced as being const during class construction, to allow the class constructor to modify the fields explicitly, pass them to functions that do, etc.

### Default field values

A class may define fields with default value expressions. The RHS of the default value expression is evaluated with access to upvalues in the class's enclosing scope as well as parameters from the class's primary constructor, but may not refer to previous fields or any functions defined within the class.

`const` fields assigned to by a default value expression *may* be mutated within the class' `__init` constructor because allowing such reduces implementation complexity.

Like default function arguments, default class field expressions are re-evaluated and assigned every time before a constructor is invoked to make a new object of the class. This means that classes with non-constant default field value expressions are more expensive to instantiate than those without default values or with constant default values.

We chose this behavior to prevent stale default arguments and to limit footguns such as Python's default function argument problem surrounding pass by reference data structures.

This means:

```luwu
const function somecounter()
    return math.random(1, 1000)
end

class Counter() -- primary constructor with 0 params to prevent assigning current
    const current = somecounter()
end

const counter1 = Counter()
const counter2 = Counter()
-- both counters have likely have different `counter.current` values.
```

### Runtime checking of `self` for methods

To ensure more correct code, we prevent passing a different class of `self` to a method via `object.method(object)` syntax.
This frees users from needing to assert `class.isinstance(self, TheClass)` if they want to ensure correct calling conventions.

This also allows for further in-module optimizations, such as method inlining of methods of `self` within other methods of `self`.
Unfortunately, this increases the cost of cross-module method calls of methods with small bodies compared to equivalent metatable-OOP implementations of the same class.

### Metamethods

Classes may define only one `__init` constructor, that may be `public` or `private`.

- `__init`

The following metamethods apply to instances of the class (`object`s), not the class itself.

They all work just like they do on a metatable:

- `__call`
- `__concat`
- `__unm`
- `__add`
- `__sub`
- `__mul`
- `__div`
- `__mod`
- `__pow`
- `__tostring`
- `__eq`
- `__lt`
- `__le`
- `__iter`
- `__len`
- `__idiv`

\* `__init` is not a metamethod per se but we call it out here as a valid method to define on a class.

For now, `__index` and `__newindex` are forbidden in classes. We will most likely re-visit this later.
For forward-compatibility, it is a syntax error to define any other method whose name starts with two underscores.

Keep in mind that only `__init` applies to the class and the object; defining any of the other metamethods
defines them for **`objects`** (instances) of the class instead of the class itself.
It is impossible to define custom metamethods for a `class`, only `object`s of a class.

Since class and object metatables are supposed to be fully locked-down, `getmetatable` should always return `nil` when called on a `class` or `object` and `setmetatable` raises an error when called on a `class` or `object`. We choose to return `nil` for `getmetatable` since that's the current behavior of calling it on primitives that don't have a metatable (such as `number`) and to prevent existing serializers from erroring on a function that used to not error.

### The `class` library

We introduce a new global library `class`. Its contents are:

```luwu
local class: {
    isinstance: (o: unknown, C: class) -> boolean,
    of: (o: unknown) -> class?,
    name: (o: class | object) -> string,
    fields: (o: class | object) -> ({ [string]: unknown }, boolean)
}
```

This library also serves as an obvious extension point for future features like reflection. In the future, we may allow classes to opt-out of reflection using this library.

The function `class.isinstance(o, Class)` returns `true` if the object `o` is an instance of `Class`. It returns `false` if `o` is the `Class` value itself. At runtime, it raises an error if the second argument is not a class. If the first argument is not an `object`, `class.isinstance` returns false. Even though the name `class.isinstance` is not ideal (we don't have "instances", we have "objects"; `isinstance` implies inheritance once Luau supports inheritance), we choose to keep the name `class.isinstance` from upstream just for a slightly better source-to-source compatibility for classes that don't inherit and have all fully qualified `public` fields. We will have specific syntax errors against the `extends` keyword in class declarations to help users migrating Luau code to Luwu. In the future, we may deprecate this function once we implement an `is` keyword that consolidates refinements between classes/objects, primitives, and extern types.

The function `class.fields` returns a map of all public fields (not methods) of the class or object, with their values, as well as a boolean `complete`, representing whether the returned map is a complete representation of the fields on that object (the class does not have any private fields). When `class.fields` is called on a `class` instead of an `object`, returns a map of the field names to `none` instead of `nil` or the default values of those fields. We similarly set table values to `none` instead of `nil` for any field values on an `object` that are `nil`. This is so we don't return a useless table (`nil` can't be stored in tables), we don't omit any field names from `object`s, and so calling `class.fields` doesn't accidentally invoke a default value expression that executes side-effects (such as an IIFE that modifies top-level scope before returning the default value). This function relies on the `none` primitive for proper functionality, and it is intended that Luwu classes and Luwu's none primitive are enabled together while both features are implemented (flagged) but not yet stable.

The `class.name` function returns the identifier name defined in the class declaration. All classes have names, and classes are not anonymous, therefore this function should always return a value when it succeeds. Raises an error if `o` is not an `object` or a `class`.

The `class.of` function returns the class corresponding to the first argument. If the first argument is not an `object`, the result is `nil`. We chose to rename `class.classof` from upstream to just `class.of` because it's better API design, matches existing APIs and is a lower-traffic function than `class.isinstance` for code that needs compatibility between Luwu and Luau.

### Constructors

There are 3 different forms of constructors:

- Primary constructors: defined in parentheses alongside the class declaration.
- The POD table constructor: defined when a class does not define a primary constructor.
- An `__init` constructor function: the user explicitly defines a `function __init` that allows for custom initialization behavior.

#### The primary constructor

The primary (or parameterized) constructor may be defined in the class declaration header. The primary constructor exists to allow users to easily define classes that take in positional parameters without needing to define an `__init` and associated boilerplate, significantly reducing verbosity for a common construction paradigm.

Additionally, having the positional primary constructor allows us to bypass `__init` and opens up a significant optimization opportunity in the extremely common case that users want to construct a class by passing multiple parameters instead of a table.

Primary constructor parameters mostly follow the same rules as function parameters: they are allowed default values, may not have trailing commas, etc.
Primary constructor parameters are allowed to define access specifiers and modifiers like in Kotlin.
As with default function parameter values, default primary constructor values are re-evaluated every call if necessary (when the relevant parameter is not passed).

Primary constructor parameters are only visible to field initializations within the class body (same place as default field values) and are not accessible to functions within the class. If the class body defines fields of the same name as parameter names, we assume the field references or otherwise modifies the parameter and do not count such fields as duplicates.

```luwu
class UDim(scale: vector, offset: vector) end
const dimmy = UDim(vector.create(1, 2), vector.create(0, 0))
print(dimmy.scale) -- vector<1, 2, 0>
print(dimmy.offset) -- vector<0, 0, 0>
```

If the parameter list does not contain fields with access specifiers, all fields introduced by the class field parameters are `public` unless specified otherwise in the class body.

```luwu
class Vector4(x, y, z, w) end -- x, y, z, w are public
class Employee(name: string, pay: number)
    private id = nextid()
    public name
    private pay
end
```

Alternatively, fields may be qualified with access specifiers and/or modifiers directly in the class field parameter list.

If *any* class field parameter uses an access specifier, *all* other parameters and all class members *must* also specify an access specifier to prevent ambiguity:

```luwu
class SshKey private (
    public const public_key: string,
    private const private_key: string
)
    public function keygen(): SshKey
        const keys = crypt.ssh.keygen()
        return SshKey(keys.public, keys.private)
    end
end

class Box(
    public size: vector,
    cat -- SyntaxError: Qualify this class field parameter as `public` or `private` to prevent ambiguity
) end
```

To prevent confusion, uses of qualified field parameters in the class body must match their declarations in class field parameters:

```luwu
class TextBox(
    public name: string,
    public text: string,
    private frame = Frame.default(),
    public placeholder_text: string?
)
    private text -- SyntaxError: Field 'text' was explicitly marked as public on line 3, cannot reassign it as private
    public frame -- SyntaxError: Field 'frame' was explicitly marked as private on line 4, cannot reassign it as public
    public placeholder_text: string = escape_strings(placeholder_text or "") -- this is fine, access specifiers match
end

class NonNegative(const inner: number)
    inner = math.max(0, inner) -- SyntaxError: Field 'inner' was explicitly marked as const on line 1, cannot reassign it as mutable
end
```

It is possible a class may have multiple class field parameters used in the class body. To prevent users from needing to restate access specifiers and modifiers between the parameter list and class body, users may use the class field parameter list without access specifiers as long as all fields are given access specifiers in the class body:

```luwu
class Frame(name, position, size, rounding = Rounding.default())
    public name: string
    public const id = nextid()
    private position: vector
    private size: vector = vector.abs(size)
    public rounding: Rounding = rounding:clamp()
end

-- SyntaxError: Field `position` at position 1 of class field parameters must be explicitly marked as `public` or `private` in the class parameter list or the class body
class Rectangle(position: vector, size: vector, id: number?) 
    private id
    public size
end
```

To declare a `private` primary constructor, put the `private` keyword between the class name and the parameters. If the only part of a class that's private is its primary constructor, the user is *not* required to mark all other fields/functions on the class with an access specifier!

```luwu
class PositiveNumber private (
    const inner: number
)
    function new(n: number): PositiveNumber?
        if n >= 0 then
            return PositiveNumber(n)
        else
            return nil
        end
    end
    function __add(self, other: PositiveNumber | number)
        if class.isinstance(other, PositiveNumber) then
            return PositiveNumber(self.inner + other.inner)
        elseif other >= 0 then
            return PositiveNumber(self.inner + other)
        end
        error(`Attempt to add PositiveNumber to something that isn't positive! (got {other})`)
    end
end
```

Calling a private primary constructor from outside the lexical scope of its class results in a runtime error. Although the parser could generate a syntax error for this, doing so would be inconsistent with calling private `__init` constructors as well as any private class constructors from classes imported from another module.

```luwu
class Account private (
    public holder: User,
    private balance = Money(0)
)
    public id = next_account_id()

    public function user_allowed_to_open_account(user: User)
        if bank.has_any_infractions(user) then
            return false
        end
        const open_accounts = bank.get_open_accounts(user)
        if #open_accounts > 10 then
            return false
        end
        const credit_score = User:check_authorize_credit_score_access()
        if credit_score and not credit_score:is_good() then
            return false
        end
        return true
    end

    public function new(user: User, starting_balance: Money?): Account?
        if Account.user_allowed_to_open_account(user) then
            return Account(user, starting_balance)
        end
        return nil
    end
end
```

An explicit `public` access specifier may be declared in front of the primary constructor parameters list. If any fields or functions on the class are `private`, the user *may* explicitly specify the access specifier of the primary constructor, but they aren't required to.

```luwu
class Seal public (name: string)
end
```

Primary constructor parameters are visible in default field value assignment to allow for transformations upon fields without needing a whole `__init` constructor.

```luwu
const function not_negative(name: string, v: vector): vector
    return if v.x >= 0 and v.y >= 0 then v else error(`{name} should be positive`)
end
class UDim(scale: vector, offset: vector)
    scale = not_negative("scale", scale)
    offset = not_negative("offset", offset)
end
```

Note that fields from the primary constructor will still be included even if they're only used to derive a value;
if the user wants to prevent those fields from being included they should use an `__init` constructor instead:

```luwu
-- this class actually has 3 fields: current, total, and value!
class Percentage(current: number, total: number = 100)
    value = string.format("%.2f%%", current / total * 100)
end
const perc = Percentage(27, 42)
print(perc.value) -- 64.29%
print(perc.current) -- 27
print(perc.total) -- 42
```

An `__init` constructor may not be defined explicitly when a primary constructor is present; doing so will cause a syntax error.

Calling the primary constructor with the wrong number of arguments will result in a `TypeError` in static analysis, but will pass `nil` to the fields at runtime. This may trigger default parameter values or default field values for any relevant parameters or fields.

Any fields that would implicitly be initialized to `nil` by the primary constructor in a way that doesn't match the field's type annotation should raise a `TypeError` in static analysis:

```luwu
class Bottle()
    brand = "Coke"
    top: Instance -- TypeError: this field will always be initialized to `nil` but is not marked as optional; consider providing a default field value, adding a class parameter of the same name, or marking the field as optional with `?`
end
```

Like the default table constructor, primary constructors also implicitly define an `__init` that may be called on the class or as a method on objects of the class. The behavior is identical to an equivalently defined `public/private function __init`. Calling this method is blocked when the class has any `const` fields.

Any calls to the primary constructor that do not match the constructor's type signature should obviously raise a TypeError in static analysis:

```luwu
class Package(owner: User, contents: { Item }) end

const packy = Package {
    owner = user,
    contents = {} :: { Item }
} -- TypeError: Expected this to be 'User', but got '{ owner: User, contents: { Item } }'
```

#### The `__init` constructor

To allow users to customize initialization logic, we propose a constructor function named `__init`. Among other influences, this is inspired by the similarly-named `__init__` from Python as well as the `__init` proposed in upstream Luau.

When `Class(...args)` syntax is used to invoke the class constructor, the "magic box self allocator" in C allocates an uninitialized object of the class and passes it to `Class.__init(self, ...args)` as `self`.

At runtime, all of `self`'s fields will be initialized to the field's default value if one is present, or `nil` if a default value is not specified, irrespective of type annotations.

Once called, the `__init` function *should* then assign to all needed fields in `self` (not checked at runtime), and should not return any values.

Field `const`ness is not enforced between initial allocation and when the `Class()` expression finishes evaluation.

Any values returned by `__init` will be ignored. The `Class()` expression then returns `self` to the caller.

If a user forgets to assign to a field in `__init`, a type error `"TypeError: constructor does not initialize field <name>"` is raised, but at runtime the field will be `nil`. Due to the difficulty of control flow analysis in the existing typesolver, this does not need to be implemented in this initial RFC implementation, and may be reapproached at a later date.

Due to the nature of `__init`, `const` fields may be reassigned during `__init`. To prevent `const` fields from being arbitrarily reassigned after initial object construction, we prevent calling the `__init` constructor explicitly (via `self:__init(...)` or `Class.__init(self, ...)`) if the class has any `const` fields. Attempting to do so raises a runtime error. If a user obtains a class's `__init` using unconventional means, such as by calling `debug.info(1, "f")` to save the `__init` closure and call it later with a fully constructed `self`... just let them do it; the exact behavior of what happens in that case is left unspecified.

#### The Default (POD) Constructor

To facilitate POD-like behavior, the default `__init` implementation will accept a POD-like table of fields.

If a class does not explicitly define a constructor, it is given a default constructor. The default constructor takes a mapping from field name to value and initializes the newly-created object with those fields.

```luwu
class Point
    x: number
    y: number
end

local p = Point { x = 3, y = 4 }
```

With default values:

```luwu
class Point
    x = 0
    y = 0
end

const pointy = Point() -- Point(x = 0, y = 0); no arguments need to be specified 
const pointy2 = Point { x = 4 } -- Point(x = 4, y = 0)
```

There is no runtime check on fields passed to the default constructor: if no argument or `nil` is passed, the default constructor initializes all class fields to each field's default value or `nil` if a default value is unspecified.
If the table does not specify all fields, the fields left unspecified will be initialized in the same way. In either case, we will raise a type error in static analysis specifying the incorrect argument or missing fields for any fields
not explicitly provided or implicitly specified via default value.

The default constructor is always `public`, and there is no way to mark it as private without explicitly
redefining its semantics.

The default constructor is a real function just like any other and so it can be explicitly invoked if desired.

```luwu
class Point
    x: number
    y: number

    function reset(self)
        -- note this modifies `self` in place, it doesn't allocate a new self
        self:__init { x = 0, y = 0 }
    end
end
```

#### `public` and `private` constructors

Users can define the `__init` constructor as `public` or `private`.

```luwu
class User
    public first_name: string
    public last_name: string
    private ssn: string?

    --- A public constructor which initializes the public and private fields.
    --- This is equivalent (but slower!) than using a primary constructor on User.
    public function __init(self, first: string, last: string, ssn: string?) 
        self.first_name = first
        self.last_name = last
        self.ssn = ssn
    end

    public function name(self): string
        return self.first_name .. " " .. self.last_name
    end

    private function get_ssn(self): string
        if self.ssn then
            return self.ssn
        end
        return get_ssn_from_files(self)
    end
end

const user = User("Taz", "Parekh", "126-222-1123")
```

If the `__init` constructor is `private`, then the class must be created via a factory function and cannot
be instantiated otherwise.

If a class has a `private` constructor, but no function in the class instantiates an object from that `private` constructor, a type error is raised:

```luwu
-- TypeError: this class can never be instantiated because its `__init` constructor is private and is never called; did you mean to return an instance of this class from a `public function` instead? Call the constructor to silence.
class User
    public first_name: string
    public last_name: string
    private ssn: string?

    private function __init(self, first, last, ssn)
        self.first_name = first
        self.last_name = last
        self.ssn = ssn
    end

    public function name(self): string
        return self.first_name .. " " .. self.last_name
    end

    private function get_ssn(self): string
        if self.ssn then
            return self.ssn
        end
        return get_ssn_from_files(self)
    end
end
```

Equivalently, with primary constructor syntax instead of an explicit `__init` constructor:

```luwu
-- TypeError: this class can never be instantiated because its constructor is private and is never called; did you mean to return an instance of this class from a `public function` instead? Call the constructor to silence.
class User private (
    public first_name: string,
    public last_name: string,
    private ssn: string?
)
    public function name(self): string
        return self.first_name .. " " .. self.last_name
    end

    private function get_ssn(self): string
        if self.ssn then
            return self.ssn
        end
        return get_ssn_from_files(self)
    end
end
```

Attempting to initialize an object of a class with a `private` constructor outside of its class will raise a runtime error.

By restricting the constructor, a class can require its users to construct it with factory functions that respect the class's
specific invariants.

```luwu
-- Cannot be directly accessed using User() outside this class scope
class User private (
    public id: string,
    public first_name: string,
    public last_name: string,
    private ssn: string?
)
    public function new(id: string): User | Error<string>
        const ssn_for_user = ssns.get(id)
        if typeof(ssn_for_user) == "Error" then
            return Error.new<<string>>(tostring(ssn_for_user))
        end
        const username = usernames.from_id(id)
        return User(username.first, username.last, ssn_for_user)
    end

    public function name(self): string
        return self.first_name .. " " .. self.last_name
    end

    private function get_ssn(self): string
        if self.ssn then
            return self.ssn
        end
        return get_ssn_from_files(self)
    end
end

const user = User.new("12311")
```

### Hoisting

As with upstream's implementation of classes, class declarations may be hoisted so that classes can mutually refer to others.

As long as all used classes are defined before code that constructs them runs, the order in which classes are declared shouldn't matter.

### Type System

Class declarations blocks introduce the class's type into the type environment.

Unlike table types, class/object types are nominally typed; this means two different
classes with identical members are treated as distinct types and unrelated to one another.

Due to the difficulty of doing so, we choose not to try and infer the type of unannotated class fields.
Any such unannotated fields are typed as `any`.
This is fine because users will likely either provide type annotations in this position or not care about static analysis at all.

When a user uses the class's identifier name to annotate a variable, we annotate the variable as an `object` of the class, not the class
itself.

```luwu
class Cat end
-- cats is an array-like table of Cat objects, not an array-like table of multiple copies of the Cat class
const cats: { Cat } = {}
```

Each class is a singleton instance of an unnamed type. If you want to use the class's type instead of the object type, use `typeof(Class)` instead.

The `class.isinstance` function participates in refinement:

```luwu
function foo(p: unknown)
    if class.isinstance(p, Point) then
        return {p.x, p.y} -- no error here
    end
end
```

Attempting to access a private member from outside the class raises a TypeError:

```luwu
class User
    private do_not_use_this_or_i_get_fired: unknown
    -- ...
    private function terminate(self)
    -- ...
    end
end

const user = User.new("deviaze")
-- TypeError: Field 'do_not_use_this_or_i_get_fired' of class 'User' is private; accessing it here will raise a runtime error
print(user.do_not_use_this_or_i_get_fired)

-- TypeError: Function 'terminate' of class 'User' is private; calling it here will raise a runtime error
user:terminate()
```

Classes with generic type parameters should be handled like extern types with generic type parameters. An initial implementation
of this RFC without full type system support may be merged before handling this perfectly.

The type function for `class.fields` will be implemented as a magic function overriding what the type system actually says returns `({ [string]: unknown }, boolean)`

We raise a TypeError if the user attempts to modify a `const` field outside the class's `__init` constructor since doing so is always a hard error at runtime.

## C API

We expose a lightweight C API for interacting with user-defined classes from the embedder side. Classes are a user-facing feature, so embedders shouldn't need to create classes themselves via the C-Stack API (use userdata instead), but embedders should be able to interact with classes passed by users. If an embedder needs to create a class, they should do so by loading Luwu source code that defines and returns/exports a class.

### Existing APIs modified for classes/objects

Embedders may access fields and functions on objects and classes via `lua_getfield` and `lua_setfield`, which importantly bypasses private access. This intentionally allows embedder code to access any field on objects passed to the embedder by users. Trying to access or modify a nonexistent field of a `class` or `object`, or an `object` via the `class`, raises a runtime error. Note that `const` fields and functions on an object or class are immutable, so `lua_setfield` on them raises a runtime error. This is to prevent the C API from accidentally breaking optimizations such as method inlining.

Similarly, `lua_setmetatable` also raises an error when called on an object or class, like `setmetatable` does in user code. This is because otherwise embedders would accidentally override the global metatable for the `object`/`class` primitives which is not intended by the language. Like when called on `number` or `boolean`, `lua_getmetatable` returns 0 and pushes nothing to the stack.

Note that table-expecting functions, including but not limited to `lua_raw*` functions, `lua_cleartable`, `lua_clonetable`, etc., are not meant to be used on `classes` or `objects`--doing so is UB.

The type naming functions, `lua_type`, `lua_typename`, and `luaL_typename`, return the following:

| API             | Primitive | Return        |
| --------------- | --------- | ------------- |
| `lua_type`      | Object    | `LUA_TOBJECT` |
| `lua_type`      | Class     | `LUA_TCLASS`  |
| `lua_typename`  | Object    | `"object"`    |
| `lua_typename`  | Class     | `"class"`     |
| `luaL_typename` | Object    | `"object"`    |
| `luaL_typename` | Class     | `"class"`     |

Note that `luaL_typename` has the exact semantics of `typeof` here in user code, it returns "class" and "object" and not the actual class name or object's class name. This is because classes and objects are not globally unique and therefore their identifier names aren't either. Use the new API `lua_getclassname` instead.

### `int lua_isclass(lua_State* L, int idx);`

Returns 1 if the value at the index is a class or 0 otherwise.

### `int lua_isobject(lua_State* L, int idx);`

Returns 1 if the value at the index is an object or 0 otherwise.

### `void lua_newobject(lua_State* L, int idx);`

Creates and initializes a new `object` from the class at `idx`, calling the class's constructor with all arguments on the stack between top and `idx`. Raises any errors that the constructor raises, including errors from within user-defined `__init` functions. Pops the `class` and all arguments passed to the constructor. Places the new `object` on the top of the stack. This function is allowed to call private constructors.

### `int lua_getmemberaccess(lua_State* L, int idx, const char* membername);`

Returns the access specifier of `membername` on the `class` or `object` at `idx`. Returns `LUA_MEMBERMISSING (0)` if the member doesn't exist, `LUA_MEMBERPUBLIC (1)` if the member exists and is `public`, and `LUA_MEMBERPRIVATE (2)` if the member exists and is `private`. Raises an error if the value at `idx` is not a class nor an object.

### `int lua_ismemberconst(lua_State* L, int idx, const char* membername);`

Returns the constness of `membername` on the `class` or `object` at `idx`. Returns 0 if the member does not exist or is not `const`, and 1 if the member exists and is `const`. Raises an error if the value at `idx` is not a class nor an object. Use `lua_getmemberaccess` to check if a member exists.

### `const char* lua_getclassname(lua_State* L, int idx);`

Returns the class name (the name of the identifier the class binding was declared with) of the class or object at `idx`. If the value at `idx` is an `object`, follows the class pointer to find the class that object is an instance of. The returned string is valid for the lifetime of the class--users should clone it immediately if they plan on keeping it around for a while.

## Compatibility

Classes are backwards compatible with Luau 0.730 and all previous versions of Luwu. Classes are not forward compatible with upstream Luau's own implementation of the classes feature, but there is a narrow subset of classes that would be valid in both languages:

```luau
class Cat
    public name: string
    public age: number

    public function meow(self)
        print(`{self.name} meowed!`)
    end
end
const taz = Cat { name = "Taz", age = 12 }
taz:meow()
```

We are allowing `Cat.__init` to be called outside even though doing so is not very useful in Luwu, to match upstream. Upstream is adding classical inheritance, which we don't want to do at all. We feel our implementation of classes without classical 'extends' style inheritance is simpler (we're adding traits next), is a better paradigm for dynamic and gradually-typed languages, and because it unlocks easier optimization opportunities for us. Unlike upstream, we feel that getting classes out there with full encapsulation is incredibly important.

Private fields are not required to be prefixed with an underscore like in upstream's proposed RFC, where `_` does what `#` does in JavaScript. This is because making `._` and `:_` actually *operators* only in class methods is a horrible and extremely cursed idea that breaks a fundamental expectation of accessing fields on tables everywhere else in the language. Any advantage this could have recouped has already been recouped; private field access is as fast as public field access in our implementation and is significantly faster than tables on read and write.

We are interested in offering a tool to convert Luwu code to Luau code, including translating classes. This will be done through our first class `ast` library with tools written in Luwu.

Unlike in upstream, calls to methods of a class always check that `self` is actually an instance of the class it's supposed to be. We choose to treat classes as an opt-in, more *staticy* feature than tables, so checking `self` and protecting this invariant allows us to take advantage of method inlining for huge performance wins impossible with metatable based OOP.

## Drawbacks

- Implementing classes in a different way from upstream Roblox's Luau may lead to inconsistencies between future code written for upstream Luau vs Luwu. We feel the less complex semantics and implementation of our version of classes is a better long-term goal for the language.
- Allowing multiple ways to declare fields (POD table constructor, class parameters with implicit access specifiers, class parameters with explicit access specifiers, `__init` constructor, default values) can be confusing, especially around classes with class parameters with only implicit public access specifiers. Such classes could make fields introduced by them seem more like function parameters that can be used in the class body instead of fields that can also be used/mutated in the class body. We feel the expressiveness of the syntax is worth it; tooling shows all fields on hover and it's something that can be easily learned.
- Extreme complexity of this feature when simpler implementations (only POD), sugar around metatable OOP, etc. could exist

## Alternatives

- We could remove the half-implemented classes from Luwu
- Remove `public` keyword and have everything public (no access specifiers, no `const`)
- Add a `static` (or `shared`) modifier for fields and methods (instead of requiring users use upvalues for static and `self` meaning method)
- We could remove the half-implemented classes feature and instead add syntactical sugar for the canonical metatable OOP pattern
- We could opt for the old [records proposal by Arseny](https://github.com/luau-lang/luau/pull/205/changes) instead
- Private fields can be enforced only in typechecking and not raise runtime errors
- Implement shared self even with its limitations
- Add syntactical sugar for the current metatable OOP pattern.
- We could instead have "structs" and "implementations" instead of classes and keep things simpler.
- Omit `__init`, and just increase performance of POD table constructor without reserving `new`
- Implement classes exactly as upstream Luau does to maintain compatibility, at the price of choosing a more confusing feature design for no real benefit.
- Wait for traits to release classes.

An example of an alternative design of structs and impls instead of classes:

```rs
struct Cat (
    name: string,
    age: number
) implements {
    function meow(self): string
        print(`{self.name} says meow!`)
    end,
}

struct Dog(name, age, puppies)

// constructor not a table for performance reasons, 
// argument order chosen by struct field order which is known because this is new syntax 
// and only the stuff after implements can be an actual table
const cat = Cat("Taz", 12)
```

## Future work

- Add the planned traits system to allow code reuse between classes:

```luwu
trait Animal
    expect species: string
    expect function is_mammal(self)
end

trait ToJson
    --- you should override this otherwise it just serializes class.fields!!
    function to_json(self)
        const fields = class.fields(self)
        return json.encode(fields)
    end
end

class Header(level) implements ToJson
    level: "h1" | "h2" | "h3"
end

trait Rectangle(position: vector, size: vector) end
trait Frame(color: Color, border: Border?) needs Rectangle end
trait Interactible
    expect enabled: boolean
    expect function on_click?(self)
    expect function on_hover?(self)
end

class TextBox(
    public placeholder_text = "",
    public color = Color("White"),
    public size = vector.create(600, 400),
    public position: vector?
) implements Frame(color), Rectangle(position, size), Interactible
    public text = ""
    public enabled = false
    private is_editing = false

    public function on_click(self)
        -- ...
    end
end
```

- Allow embedders and users to declare classes where they should exist but don't have a physical body.

```luwu
declare export class Path
    components: { PathComponent }
    function __init(self, from: string | { PathComponent })
    function components(self): { PathComponent }
    function is_absolute(self): boolean
    function is_relative(self): boolean
    function is_windows_absolute(self): boolean
end

const Path: class<Path> = luwu.eval(fs.readfile(path_to_list)) :: any
const pathy = Path("./src/main.luwu")
```

- An `is` keyword. Right now we have `typeof`, `type`, `class.isinstance`, and `sometable.discriminantfield == "whatever"` all as ways to refine on identity. The first 3 can be collected into a *real* `is` keyword that figures out which opcodes to use under the hood at runtime and works seamlessly in static analysis.

## Prior art

### Overall influences

Classes take huge inspiration from Kotlin, Rust, upstream Luau, as well as classical OOP languages like C++, C#, Java, etc.

We also acknowledge the similarity to TypeScript classes in syntax though TypeScript surprisingly wasn't a direct source for this RFC; note ours and TypeScript/JavaScript's implementation differ significantly.

### Why not metatable OOP or records

Prototype-based OOP (Lua metatables, Luau typed metatable OOP, JS and TS OOP, etc.) is something we've tried and not succeeded with. Even if classes were a wrapper feature that mostly desugared to metatable OOP, we'd not be able to fix the really hard type system/Analysis components to classes without nominalness. Hovers around metatable OOP (including different ways to represent metatable OOP in the type system) can easily become genuinely horrific and will scare away anybody interested in this language. We felt that the language would be even more amazing than it was before with highly performant, nominally typed classes to accompany the already quite good structural type system so we could be the best of both worlds. Lighter weight alternatives to classes such as Arseny's records were decided against because everyone was going to end up asking for access specifiers, behavior reuse, and users just needed more features than records could provide.

### Access specifiers and encapsulation

The decision to go with hard enforced `private` access specifiers comes from lessons learned in the Roblox ecosystem with the embedder (and OSS library authors) forced to keep private implementation details stable because games would break if they changed. This is a legitimate concern in a public-only language with no way to properly do encapsulation.

Note that like upstream, we use JavaScript's `#private` variables as influence that we actually *need* real, runtime-enforced encapsulation.

This RFC's access specifiers, and specifically how to describe and talk about them, is credited to [Noctua](https://github.com/TenebrisNoctua), who was influenced by his experience in maintaining a Luau classes library [Class++](https://github.com/TenebrisNoctua/ClassPP) and classical OOP usecases found in C++ and C#.

### `const` fields

`const` fields were primarily inspired by the fact that Luau gave us the `const` keyword and having a way to protect immutable field invariants is very useful. We chose to keep `const` completely unenforced in `__init` for an easier implementation (a constructor can reassign to it multiple times, but only within the constructor), a decision backed up by Java and C# which do the same thing for fields with their `final` and `readonly` modifiers.

### Constructor syntax and field defaults

Class field parameters and public/private access specifiers in front of the class field parameter list are inspired by the equivalent syntax in Kotlin for its similarity and synergy with function parameters as well as the obvious "this is how you create one" parallels between the class declaration syntax and class initialization syntax.

The name `__init` is influenced by Python where function call syntax also creates an instance of a class. Default field parameters are also inspired by Python default arguments and similar syntax in Kotlin, as well as our preexisting default (function) parameter values in Luwu. Notably, we choose to avoid the default parameter footgun from Python in both class field parameter defaults as well as function defaults.

The default POD constructor was inspired by upstream as well as by Rust's struct construction syntax.

### Ruby, monkey patching, and constructor naming

We took Ruby's class sigil syntax as something we *shouldn't* do because it leads to significant new/infrequent user confusion of what is a static field vs instance field. We also want to keep classes `const` to prevent any sort of monkey patching like is possible in Ruby and Python because doing so would break our optimizations and the type system. Same reasoning behind not having a separate `def initialize` that gets called as `Classy.new`. Similarly, the Luau team planned on having their class constructor named `__init` but called as `Class.new()` for months but relented after OSS community backlash and considering the confusion of the design.

### Traits instead of inheritance

The decision to go for traits that are allowed to declare state instead of classical inheritance was inspired by Rust, Go (I really wish we could do functions with class receivers but nope we're way too dynamic at runtime for that), Swift (protocols, protocol extensions), Scala 3 (trait parameters), and PHP (which first popularized the concept of stateful traits in dynamic languages), and the concepts of mixins in other languages. We were also inspired by Kotlin here as a modern way to do inheritance (closed by default, more opportunities to do composition instead, class/interface delegation), but we decided that inheritance inherently complicates anything around constructors and `private` ownership.

### Refinement and a future `is` keyword

We take inspiration from Luau's `typeof`, Python's `isinstance`, and TypeScript for a refining `class.isinstance`. We take inspiration from `is` and `is not` from Python for a future `is` keyword that unites all sorts of refinements that are completely different functions in Luau and Luwu today.

### Reflection

The reflection function `class.fields` was inspired by our realization that we need a way to quickly turn possibly-opaque objects into JSON (only serializable through tables). OOP languages in our space like Ruby and Python (`__dict__`) have easy ways to obtain fields and metadata about classes, so there's no reason for us not to. Similarly, `class.name` was inspired by the fact that we already leak class names in error messages; if we don't have a way to get the class's name at runtime people are just going to `pcall(function() return object["i cannot exist"] end)`. We feel that the likelihood that users will use `class.name` for class identity comparison (upstream's motivation for hiding class name including in error messages) is not sufficient motivation to prevent users from easily serializing objects of their classes to strings for their `__tostring` functions (or `Display`-like traits that do so for multiple classes).

### Field layout and performance

Our implementation heavily references V8 and Python `__slots__` optimizations for compiling classes with fields in specific offsets.

### Runtime `self` checking

We choose to enforce that methods were called on objects of the correct class (`self` is actually the right `self`) at runtime because we choose not to support Is-A relationship style inheritance, because it is obviously wrong to call a method of `self` on an object or table or completely unrelated value, and crucially because it opens up in-module method inlining. Method inlining provided our classes implementation its most significant victory over metatable OOP in terms of in-module speedup in O2, even with a mix of `public`, `private`, and `const` fields. We realize Python 2 originally checked this like us and backtracked in Python 3, but we don't have inheritance like Python does and we'd really like to keep this invariant for performance reasons.

## Implementation details

See the [dedicated file](/rfcs/classes/implementation.md).

# Feature name

FFlag: LuauBetterUserDefinedClasses

## Summary

We need to support `__init`, `private` fields, and release the restriction on requiring `public` in front of
class fields. We also need a stable C API for classes, including a way to access class names and fields from the C side.

## Motivation

As of Luau 0.730, upstream Luau has partially implemented the classes feature, however it doesn't support private fields,
has multiple bugs in type inference and analysis, and needs to be completed. We do not want to pursue upstream Luau's
implementation of the feature due to differing views on design.

We need this initial support to following paradigms in the future, in order:

- Nominally typed interfaces
- Inheritance without multi-inheritance.

## Design

For this basic implementation, we are focusing on extending the current in-progress classes.

Right now this is valid syntax (with the classes feature enabled)

```luau
class Cat
    public name: string
    public age: number
    function meow(self, content: string): string
        return `{self.name} says {content}`
    end
end
```

However, we have no way to initialize this outside the default POD syntax.
POD-like constructors `Class { x = x, y = y }` syntax provides a nice default, "named-table" like functionality
that feels great with existing code for POD-like data types. On the other hand, POD-like constructors are not the correct choice
for all flavors of classes. The POD syntax not only allocates a table (fixable in compiler), but there's also no way to customize
our default constructor behavior. This is a problem if we want to add inheritance in the future.

To fix this issue, we propose a constructor function named `__init`, which is always called by the "magic box" self allocator when
constructing a new instance of a class via `Class()` syntax. Among other influences, this is inspired
by the similarly-named `__init__` from Python.

To facilitate current POD-like behavior, we want to preserve the existing default constructor, only overriding it
when the user defines `__init` explicitly.

### `__init` constructor

```luau
class Cat
    public name: string
    public age: number
    function __init(self, name: string, age: number)
        self.name = name
        self.age = age
    end
    function meow(self, content: string): string
        return `{self.name} says {content}`
    end
end

const cat = Cat("Taz", 12)
```

Internally, this works because the `"magic self allocation box"` implied by `Cat.__call` creates a new
uninitialized instance of `Cat` and passes it to the `__init` constructor. Users can still define their
own factory functions named `new`, `create` , etc.

Keep in mind that unlike in Python users do *not* need to define their own `__init` just to create an instance
of their class; all classes get `POD` constructors by default.

If a user forgets to assign to a field in `__init`, a type error `"Forgot to initialize property <name>"` is raised.
At runtime, any uninitialized fields are `nil`.

### `public` and `private` access modifiers

We introduce `private` modifiers for fields and functions. Private fields and functions may only be accessed
within other methods of the same class. If a private function or field is accessed outside the class, a runtime error
is raised. The private field or function's name will not be mentioned in the runtime error message.

Users can initialize private fields by passing values to public constructors. The default POD constructor is public,
so if the user wants to prevent users from using it to initialize private fields they should explicitly define a
`private function __init(self)` constructor instead.

If a class has any private fields, functions, or constructors, then explicit `private` and `public` access specifiers
are required on every field (implicit public is no longer allowed).

```luau
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

const user = User {
    first_name = "Taz",
    last_name = "Parekh",
    ssn = "126-222-1123",
}
```

#### Private constructors

If a class has a private constructor defined, then it is now impossible for users directly instantiate the class using the private
constructor.

You cannot have more than one constructor `__init` at the same time.

```luau
class User -- TypeError: this class can never be instantiated; did you mean to define a public function that returns an instance of this class?
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

Instead, the class must define a factory function to create instances of the class, calling the private constructor.

```luau
class User
    public id: string
    public first_name: string
    public last_name: string
    private ssn: string?

    private function __init(self, first, last, ssn)
        self.first_name = first
        self.last_name = last
        self.ssn = ssn
    end

    public function new(id: string): User | Error<string>
        const ssn_for_user = ssns.get(id)
        if typeof(ssn_for_user) == "error" then
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

If a class only has private members (fields and functions) and has no public functions or fields, then
we raise a type error because such a class will not be usable.

```luau
class UseMe -- TypeError: this class cannot be used because it only has private members
    private please: string
    private uses: number
end
```

## Drawbacks

Implementing classes in a different way from upstream Roblox's Luau may lead to inconsistencies
between future code written for upstream Luau vs our Luau. We feel the less complex semantics and implementation
of our version of classes (not forcing `.new`, more explicit semantics) is a better long-term goal for the language.

Allowing users to define classes with private fields but not require a custom constructor may be a footgun.

## Alternatives (optional)

- We could remove the half-implemented classes from Luau
- We could opt for the old records proposal instead
- We could name the feature "structs" and "implementations" instead of classes.
- Omit `__init`, and just increase performance of POD methods without reserving `new`
- Implement classes exactly as upstream Luau does to maintain compatibility, at the price of reserving `.new`
  and choosing a more confusing feature design for no real benefit.

## C API

-- TODO: make this less rusty

### `lua_pushclassobject(lua_State*, idx: i32)`

- pushes the class index at idx (usually a negative number) calling its constructor with all thingies on stack between top and idx

### `lua_getclassname(lua_State*) -> *const cstr`

# Classes

FFlag: LuauBetterUserDefinedClasses

## Summary

```luau
class Point
    public x: number
    public y

    function length(self)
        return math.sqrt(self.x * self.x + self.y * self.y)
    end

    function __add(self, other: Point)
        return Point.new({ x = self.x + other.x, y = self.y + other.y })
    end

    function __tostring(self)
        return `Point \{ x = {self.x}, y = {self.y} \}`
    end

    function fromAxisLength(theta, length)
        return Point.new({
            x = length * math.cos(theta),
            y = length * math.sin(theta),
        })
    end
end

local p = Point.fromAxisLength(math.pi / 4, 4)
print(`Check out my cool point: {p}  length = {p:length()}`)
```

## Motivation

* People write object-oriented code.  We should afford it in a polished way.
* Accurate type inference of `setmetatable` has proven to be very difficult to get right.  Because of this, the quality of our autocomplete isn't what it could be.
* A construct with a fixed shape and a completely locked-down metatable will open up optimization opportunities that could improve performance:
    * If a value is known to be an instance of a particular class, the bytecode compiler should be able optimize method calls to skip the whole `__index` metamethod process and instead generate code to directly call the correct method.
    * By the same token, method calls can be inlined more aggressively.  Particularly self-method calls eg `self:SomeOtherMethod()`
    * Field accesses can compile to a simple integral table offset so that the VM doesn't need to do a hashtable lookup as the program runs.
    * Since every instance of a class has the same set of properties, we can split the hash table: The set of fields can be associated with the class and instances only need to carry the values of those fields.  We think this can improve performance by improving cache locality.
* Encapsulation at its current state cannot be truly achieved, tables cannot truly be locked-down, and most workarounds for it are too complex for what it's trying to achieve. 

The previous implementation which was done by Luau team has multiple bugs in its type inference and analysis, and is yet to be completed. Completeness aside, the decisions that have been made by the Luau team has mostly considered Roblox as a primary customer, thus preventing a more capable implementation of classes that would be more beneficial to the community.

For this RFC, we will be focusing on a base design that allows us to potentially implement features such as interfaces and inheritance in the future easier.

## Design

The current implementation made by upstream Luau is as follows:

```luau
class Cat
    public name: string
    public age: number
    function meow(self, content: string): string
        return `{self.name} says {content}`
    end
end
```

However, we have no way to initialize this outside of the default POD syntax.
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
of their class; all classes have a default constructor, which can be called with the `POD` initializer syntax.

If the user does not assign a value to a field within the constructor, a type error such as `"Forgot to initialize property <name>"` is raised.
At runtime, any uninitialized fields are `nil`.

### `public` and `private` access specifiers

We introduce the `public` keyword to define fields as public, and accessible from everywhere.

All fields on a class are `public` by default. This means, if all fields on a class are public, the user can emit the `public` keyword in front of the field definitions.
However, to remove ambiguity, if a field is defined with an access specifier other than `public`, then the user must explicitly define all fields with an access specifier.

To achieve full encapsulation, we also introduce the `private` access specifier. Any fields defined with this specifier
will now be private, and now locked to the outside world. Only functions of the same class can access these fields.
Any attempts at accessing these fields outside of the class functions will cause a runtime error to be raised.

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

If a class only has `private` fields, then we raise a type error because such a class will not be usable.

```luau
class UseMe -- TypeError: this class cannot be used because it only has private members
    private please: string
    private uses: number
end
```

#### `public` and `private` constructors

Users can define a constructor as `public` or `private`, which `private` allowing them to create classes with better encapsulation,
allowing the initialization to happen through public factory functions instead. The default constructor however is always public.

```luau
class User
    public first_name: string
    public last_name: string
    private ssn: string?

    public function __init(self, first: string, last: string, ssn: string?) -- A public constructor which initializes the public and private fields.
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

When a constructor is defined with `private` access specifier, they can now never be accessed directly from outside of the class, 
and thus you must define a `public` function for accessing and initializing an object from a `private` constructor: 

```luau
class User
    public id: string
    public first_name: string
    public last_name: string
    private ssn: string?

    private function __init(self, first: string, last: string, ssn: string?)
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

If a class has a `private` constructor, but no `public` function(s) for initializing an object from that `private` constructor, a type error is raised:

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

## Drawbacks

Implementing classes in a different way from upstream Roblox's Luau may lead to inconsistencies
between future code written for upstream Luau vs our Luau. We feel the less complex semantics and implementation
of our version of classes (not forcing `.new`, more explicit semantics) is a better long-term goal for the language.

## Alternatives (optional)

- We could remove the half-implemented classes from Luau
- We could opt for the old [records proposal by Arseny](https://github.com/luau-lang/luau/pull/205/changes) instead
- We could name the feature "structs" and "implementations" instead of classes.
- Omit `__init`, and just increase performance of POD methods without reserving `new`
- Implement classes exactly as upstream Luau does to maintain compatibility, at the price of reserving `.new`
  and choosing a more confusing feature design for no real benefit.

## C API

### `int lua_isclass(lua_State* L, int idx);`

Returns 1 if the value at the index is a class.

### `int lua_isobject(lua_State* L, int idx);`

Returns 1 if the value at the index is an object.

### `int lua_pushobject(lua_State* L, int idx);`

Creates an `object` from the class at the index and calls its constructor with all the field values on the stack between top and the index.

### `const char* lua_getclassname(lua_State* L, int idx)`

Returns the class name at index.

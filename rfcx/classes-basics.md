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
        return Point { x = self.x + other.x, y = self.y + other.y }
    end

    function __tostring(self)
        return `Point \{ x = {self.x}, y = {self.y} \}`
    end

    function fromAxisLength(theta, length)
        return Point {
            x = length * math.cos(theta),
            y = length * math.sin(theta),
        }
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

Class definitions are a block construct. `export class X` is allowed.

Defining two classes with the same name in the same module is forbidden.

Within a class block, two declarations are allowed: Fields and methods.

Fields are introduced with the new access specifier keywords. For now, we plan to only implement `public` and `private`. We may look into implementing other specifiers such as `protected` in the future.

Methods are introduced with the familiar `function` keyword.  `public function f()` is also permitted.

Classes can define the following Luau metamethods. They all work just like they do on a metatable:

* `__init` *
* `__call`
* `__concat`
* `__unm`
* `__add`
* `__sub`
* `__mul`
* `__div`
* `__mod`
* `__pow`
* `__tostring`
* `__eq`
* `__lt`
* `__le`
* `__iter`
* `__len`
* `__idiv`
  
\* `__init` is not a metamethod per se but we call it out here as a valid method to define on a class.

For now, `__index` and `__newindex` are forbidden in classes. We will most likely re-visit this later.
For forward-compatibility, it is a syntax error to define any other method whose name starts with two underscores.

### Class Objects

The action of evaluating a class definition statement introduces a *class object* in the module scope. A class object is a value that serves as a factory for instances of the class and as a namespace for any functions that are defined on the class.

Class objects behave like class instances in most ways, but are always `const` and frozen.

Taking references to class methods via `ClassName.method` syntax is allowed so that classes can easily compose with existing popular APIs:

```luau
local n = pcall(SomeClass.getName, someClassInstance)
```

The top type of all class objects is named `class`.  `type()` and `typeof()` return `"class"` when passed a class object.

### Class Instances

Class instances are a new type of value in the VM. They are similar but not quite the same as tables. They have no array part, for instance.

`pairs`, `ipairs` , `getmetatable`, and `setmetatable` all raise a runtime error when invoked on a class instance. They also cannot be iterated over with the generic `for` loop. (unless the class implements `__iter`)

Reading or writing a nonexistent class property raises an error. This makes it easy to disambiguate between a nonexistent property and a property whose value is nil.

We introduce a new top type for class instances: `object`.  The builtin `type()` and `typeof()` functions return `"object"` for any class instance.  

We chose this over having them return the class name because class names do not have to be globally unique (they must only unique within a single module) and because we do not want to make it possible for classes to impersonate other types.

```luau
class Cls end
local inst = Cls()

type(Cls) == "class"
typeof(Cls) == "class"

type(inst) == "object"
typeof(inst) == "object"
```

Comparisons between object instances are the same as with tables: If `__eq` is not defined, object comparisons use physical (pointer) equality.  `__eq` is only invoked if both operands are the same type.

### The `class` library

We introduce a new global library `class`.  Its contents are:

```luau
local class: {
    isinstance: (o: unknown, C: class) -> boolean,
    classof: (o: unknown) -> class?,
}
```

This library also serves as an obvious extension point for future features like reflection. In the future, we may allow classes to opt-out of reflection using this library.

The function `class.isinstance(o, Class)` returns `true` if the object `o` is an instance of `Class`. At runtime, it raises an error if the second argument is not a class object. If the first argument is not a class instance, `class.isinstance` returns false. (eg `class.isinstance(5, MyClass)`)

The `classof` function returns the class object corresponding to the first argument. If the first argument is not a class instance, the result is `nil`.

### Access Specifiers

Access specifiers allows the user to control access to a specific field within a class. For the scope of this RFC, we will only be introducing the `public` and `private` access specifiers.

#### `public` access specifier

We introduce the `public` keyword to define fields as public, and accessible from everywhere.

All fields on a class are `public` by default. This means, if all fields on a class are public, the user can omit the `public` keyword in front of the field definitions.

However, to remove ambiguity, if a field is defined with an access specifier other than `public`, then the user must explicitly define all fields with an access specifier.

```luau
class Vector3
   x: number -- public keyword can be omitted here, all fields are public.
   y: number
   z: number
end
```

#### `private` access specifier

To achieve full encapsulation, we also introduce the `private` access specifier. 

Any fields defined with this specifier will now be private, and now locked to the outside world. 

Only functions of the same class can access these fields. Any attempts at accessing these fields outside of the class functions will cause a runtime error to be raised.

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

const user = User { -- The default constructor can initialize private fields.
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

### Constructors

To construct an instance of a class, invoke the class object as `MyClass(props)`. The constructor accepts a single optional argument: a table-like value that contains initial values for all the fields. While it will typically be most useful to pass a table literal to this function, that isn't the only use. For example, any class can be shallowly cloned by passing it to its class constructor: `local clone = MyClass(original)`

Let `T` be a class object.  When `T(...args)` is invoked with any arguments, the following happens:

1. A fresh, uninitialized instance of `T` is allocated.  We'll call it `t` here.  All of its fields are initially `nil` irrespective of any type annotations.
2. `T.__init(t, ...args)` is invoked.  Any values returned by `__init` are ignored.
3. `t` is produced as the result of the expression

A class can invoke its parent constructor directly:

```luau
class A extends B
    public x: number

    function __init(self, x, y)
        B.__init(self, x)
        self.x = x * y
    end
end
```

#### The Default Constructor

If a class does not explicitly define a constructor, it is given a default constructor. The default constructor takes an optional mapping from property name to value and initializes the newly-created class instance with those properties. If no argument or `nil` are passed, the default constructor simply initializes all class properties to `nil`.

In strict mode, it is a warning to leave fields uninitialized.

The default constructor is always `public`.

A class must define an explicit constructor if its base class defines one. Attempting to construct such a class will result in a runtime exception. Type inference will also warn in this case.

If a class defines no constructor and inherits from another that also defines no constructor, the default constructor will initialize all fields of the class. For example:

```luau
open class BasePoint
    public x: number
    public y: number
end

class Point3D extends BasePoint
    public z: number
end

local p2 = Point { x=3, y=4 }
local p3 = Point3D { x=1, y=2, z=3 }
```

The default constructor is a real function just like any other and so it can be explicitly invoked if desired.

```luau
class Point
    public x: number
    public y: number

    function reset(self)
        self:__init {x=0, y=0}
    end
end
```

#### `public` and `private` constructors

Users can define a constructor as `public` or `private`, with `private` preventing the user from directly accessing the constructor.

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

Attempting to initialize an instance of a class with a `private` constructor outside of its class will raise a runtime error.

Defining a `private` constructor restricts the user to use a `public` method in order to create an instance from this class. This can be useful in achieving full encapsulation.

```luau
class User
    public id: string
    public first_name: string
    public last_name: string
    private ssn: string?

    private function __init(self, first: string, last: string, ssn: string?) -- Cannot be directly accessed using User() outside of a class function
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

### Type System

Class definitions also introduce a new type to the type environment.

Unlike tables, which are structurally typed, class types are nominal.  Two different classes with identical fields are treated as distinct types.

Inferring the types of class fields is fraught with difficulty, so un-annotated fields are given the type `any`.

The type introduced by a class definition is available anywhere in the source file.

The `class.isinstance` function participates in refinement:

```luau
function foo(p: unknown)
    if class.isinstance(p, Point) then
        return {p.x, p.y} -- no error here
    end
end
```

Each class object is a singleton instance of an unnamed type.  If needed, it is easy to access via `typeof(TheClass)`.  Class object types are all subtypes of the top `class` type.

### Semantics

Class definitions are Luau statements just like function definitions.

The action of a class definition statement is to allocate the class object, define its functions and properties, and freeze it.  Consequently, a class cannot be instantiated before this statement is executed.

Static analysis also considers the class's type to be global to the whole module so that it can appear in any type annotation anywhere in the script.

An example:

```luau
-- illegal: MyClass is not yet defined
local a = MyClass()

-- OK: MyClass can appear in any type annotation anywhere
function use(c: MyClass)
end

function create()
    -- OK as long as this function is invoked after the class definition statement
    return MyClass()
end

-- We can't statically catch this in the general case, but this will fail at runtime!
create()

class MyClass
end

local b = MyClass() -- OK
local c = create() -- OK
```

Because class definition is a statement, class methods can capture upvalues just like ordinary functions do.

```luau
local globalCount = 0

class Counter
    public count: number

    function create()
        local count = globalCount
        globalCount += 1
        return Counter { count = count }
    end
end
```

## C API

### `int lua_isclass(lua_State* L, int idx);`

Returns 1 if the value at the index is a class.

### `int lua_isobject(lua_State* L, int idx);`

Returns 1 if the value at the index is an object.

### `void* lua_newobject(lua_State* L, int idx);`

Places a new `object` from the `class` at the index and calls its constructor with all the field values on the stack between top and the index.
Then places the new `object` on top of the stack.

### `void* lua_newclass(lua_State* L, size_t sz);`

Places a new `class` object with the data size `sz` on top of the stack.

### `const char* lua_getclassname(lua_State* L, int idx)`

Returns the class name at index.

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

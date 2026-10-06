# Feature name

Status: Implemented (Flagged)

FFlag: LuwuTraits, LuwuClasses

## Summary

Adds new `trait` syntax and semantics for classes.

```luwu
trait Entity
    expect name: string
    parent: GameObject? = nil
    id = next_id()

    function __tostring(self)
        return `Entity {class.name(self)}(name = {self.name})`
    end
end
trait Health
    expect hp: Hp
    expect function died?(self, dealer: DamageDealer?)
    function heal(self, amount: number)
        self.hp += amount
    end
    final function is_alive(self)
        return self.hp.current ~= 0
    end
    function take_damage(self, amount: number, dealer: DamageDealer?)
        self.hp -= amount
        if dealer and self.died and not self:is_alive() then
            self:died(dealer)
        end
    end
end
trait DamageDealer needs Entity end
class Npc(name: string) implements Entity end
class Enemy(name: string, hp: Hp) implements DamageDealer, Health end
class Player(name: string, user_id: Id) implements DamageDealer, Health
    hp = Hp(100)
    function died(self, dealer: DamageDealer?)
        if dealer then
            print(`Killed by {dealer}`)
        end
    end
end
```

## Motivation

<!-- Why are we doing this? What use cases does it support? What is the expected outcome? -->

Many nontrivial usecases for classes demand that classes can be related to one another, share functionality, and be substituted
for one another. The ability to define shared code and and shared behavior is integral to abstraction and code reusability in OOP.

Usually, this sort of feature takes shape as classical inheritance and/or some forms of interfaces/mixins/trait system. If we went with classical
inheritance, we would have to figure out how constructors would work, how `private` access could be locked to specific layers,
how to allow multiple 'layers' of classes with their own `private` fields to even exist (that can shadow private fields from base classes they
inherit from), a `protected` modifier, etc.

This seems like quite a lot of trouble for a feature that's primarily meant to allow code reuse and polymorphism, and implementing classical
inheritance in this way would negatively affect implementation complexity, semantic complexity, and performance.

Traits are a form of behavioral inheritance that allows reusable functionality without mixing behavior with identity. Crucially, our implementation of
traits is single-level, which means traits don't actually carry their own data nor functionality. This means that all functionality is resolved by the
class, with any conflicts between implemented traits also resolved by the class. Additionally, traits incentivize users to break up their classes into
smaller, reuseable pieces of functionality instead of making huge base classes they inherit from.

## Design

<!-- This is the bulk of the proposal. Explain the design in enough detail for somebody familiar with the language to understand, and include examples
of how the feature is used.

Although design should be specific, it shouldn't be so technical or theoretic that a moderately experienced Luwu user could not readily understand it.

If this is a user-facing feature that needs type system (`Analysis`) and/or editor (`luwu-lsp`) support, also describe the relevant type system and
editor design in subsections named `### Type system` and/or `### Editor support` or similar. -->

Traits are very similar to classes; in fact, they're classes internally. The main difference between traits and classes is that traits cannot be
constructed and traits can define members that don't exist yet.

### Trait definition syntax

Traits are introduced with a new trait syntax very similar to classes:

```luwu
trait Foo end
trait Animal
    expect species: string
    final function is_living_being(self)
        return true
    end
end
trait Healthed(
    hp = 100
)
    max_hp = hp
    function is_alive(self)
        return self.hp > 0
    end
    function full_heal(self)
        self.hp = self.max_hp
    end
end
trait Iterable<T>
    expect public function __len(self): number
    expect public function __iter(self): () -> (number, T)
end
trait Listable<T> needs Iterable<T>
    expect private inner: Container<T> | { T }
    expect public function front?(self): T?
    expect public function back?(self): T?
end
```

Specifically:

- A trait definition contains the trait header and trait body.
- The trait header starts with the contextual keyword `trait`,
- is followed by the trait's name, which must be a valid identifier unrelated to contextual keywords related to classes and traits,
- may be followed by an optional generic parameters list, just like classes,
- may be followed by a trait primary parameters list, which can contain defaults.
  - Unlike the class primary constructor parameter list, the trait primary parameters list may not be preceded by an access specifier.
  - The trait primary parameters list follows the same syntax as the class primary constructor parameters list.
- The `needs` keyword may follow the optional parameters list, followed by a comma separated list of the traits that this trait requires.
  Trailing commas are not permitted.
- After the trait header starts the trait body.
- The trait body can more-or-less contain the same elements the class body can contain, with some exceptions:
  - Fields and functions without defaults are not allowed.
  - The trait body may `expect` members, which are fields or functions the implementing class must specify to implement the trait.
    - Fields may be expected via `expect fieldname: type` syntax. The type annotation is optional.
    - If a function is optional for the class to implement, its identifier should be followed by a `?` before the function parameter list.
  - A real `function __init` may not be defined by traits, but traits may `expect` a `function __init` of a certain shape.
  - Traits may define a real `function __create`, which allows them to be factories of concrete classes.
  - The `final` modifier may precede any real field or function in the trait.
- The trait body and trait definition ends with the `end` keyword.

Like classes, traits definitions are block constructs and do not evaluate to an expression, and may only be defined at the top level of a module.
Like class definitions, trait values introduced by trait definitions are `const`. All trait values are frozen.

Defining a trait with the same name as a class, or a class with the same name as a trait, or more than one trait with the same name,
is a syntax error.

### The trait value

A trait value defines reusable functionality for a class (or another trait).

A trait value behaves mostly the same as a class value. Even though traits are classes internally, we treat them differently semantically:
`type(t) == "trait"` and `typeof(t) == "trait"`.

All real functions on a trait may be called via the trait value; note that calling them this way (instead of through an object whose class
implements the trait) always resolves to that trait's version of the function and not an overriden version from the class or another trait.
To prevent a real function from being overriden by a class or another trait, prefix it with the `final` modifier.

### Implementing traits and resolution

A class can implement any trait as long as it doesn't conflict with other traits the class implements.

A class must define all fields and functions that the trait expects:

```luwu
trait Named
    expect name: string
    function __tostring(self)
        return `{class.name(self)}(name: {self.name})`
    end
end
trait Meowable
    expect function meow(self): string
end

class Cat implements Named, Meowable
    name: string
    function meow(self)
        return "meow"
    end
end

const cat = Cat {
    name = "Taz"
}
```

An implementing class gets any real functions defined by the trait (or any traits that trait `needs`) without any additional work.

A trait that `needs` another trait is allowed to override any non-`final` fields and functions of the trait it `needs`.
This allows for specialization.

```luwu
trait Animal
    expect species: string
    function __tostring(self)
        return "Animal"
    end
end
trait Cat needs Animal
    expect name: string
    species = "Felis catus"
    function __tostring(self)
        return `Cat({self.name})`
    end
end
class MaineCoon(name: string) implements Cat
    function __tostring(self)
        return `MaineCoon({self.name})`
    end
end
```

If a class implements traits with conflicting field or function types, a TypeError is raised:

```luwu
class Name(first: string, last: string) end
trait Identity
    expect public name: Name
    expect private govt_id: string
    public nickname: string? = nil
end
trait Named
    expect name: string
end

class Person(
    public name: string
) implements Named, Identity --[[
TypeError: Traits 'Named' and 'Identity' conflict on field 'name'.
- 'Named' expects 'name: string'
- 'Identity' expects 'name: Name'

Help (conflicting trait members):
  - These traits cannot be implemented at the same time on class 'Person'.
  - Consider removing one or more of the implementations if a trait is redundant
  - Or factor out the functionality you need into a new trait
  - Or allow one of the traits to override the other via 'needs'
]]
    private govt_id = ""
end
```

In the future, we want to allow for classes to override functions to resolve conflicts, but allowing so raises questions around how to resolve
method arity differences when an object that implements the trait is narrowed via `class.implements` and the concrete class is not known.

Traits can be factories for concrete classes with `__create`.

```luwu
type function SpecificHtmlComponent(t: type) ... end
const component_classes_by_tag: { [string]: class<HtmlComponent> }
trait HtmlComponent<Data>(tag: string)
    expect function __init(self)
    expect function __call(self, data: Data)
    function __create<T>(tag: T | ""): SpecificHtmlComponent<T>
        const class_of_tag = component_classes_by_tag[tag]
        if class_of_tag then
            return class_of_tag() :: SpecificHtmlComponent<T>
        end
        error(`Unknown tag {tag}`)
    end
    final function register(cls: class<HtmlComponent>, tag: string)
        component_classes_by_tag[tag] = cls
    end
end
```

A class (or another trait!) may override any non-final field or function provided by a trait:

```luwu
trait Enemy
    name = "Enemy"
    health = Hp(100)
    function is_hostile(self)
        return true
    end
end

class Bot(health: Hp) implements Enemy
    name = "Bot"
    hostile = true
    function is_hostile(self)
        return self.hostile
    end
    function pacify(self)
        self.hostile = false
    end
    function enrage(self)
        self.hostile = true
    end
end

trait Boss(name: string, health: Hp) needs Enemy
    expect attacks: { Attack }
end

class Karmelita implements Boss("Karmelita", Hp(12_000))
    attacks = create_attacks_for("BOSS: Karmelita")
end
```

Static analysis flags any fields or functions overridden with the wrong type signature. In the example above, redefining the parameter or return types
of `is_hostile` in `Bot` or setting `name = 10` would raise a TypeError.

### Class library

A new function is added: `class.implements(object, trait)`. It returns `true` if the object implements `trait` or false if it doesn't.

### Type system

The standard library function `class.implements` participates in refinement.

The magic type function `class<Trait>` refers to the class value of any class that implements `Trait`. This is useful for factory functions
(and `__create`) that live on traits and need to create objects of a concrete class.

### Editor support

`luwu-lsp` has refactoring actions for traits, allowing one or more functions/methods to be extracted into a trait, converting classes into
traits, etc.

## C API

<!-- If your feature includes new embedder-exposed APIs, document them here with function signatures and a short explanation of each. -->

### `int lua_istrait(lua_State* L, int idx);`

Returns 1 if the value at stack index `idx` is a trait or 0 otherwise.

### `int lua_implements(lua_State* L, int idx);`

Returns 1 if the value at stack index `idx` implements the trait on stack top (index `-1`). Follows same semantics as `class.implements` in user code.

### `int lua_ismemberfinal(lua_State* L, int idx, const char* membername);`

Returns 1 of `membername` on the `trait` or `class` or `object` at `idx`. Returns 0 if the member does not exist or is not `final`.
Raises an error if the value at `idx` is neither a trait nor a class nor an object. Use `lua_getmemberaccess` to check if a member exists.

## Compatibility

We introduce 3 new contextual keywords: `trait`, `implements`, and `needs`. Like with classes, adding traits to the language does not break
any existing Luau 0.730 code. It is not compatible with current upstream Luau and will not be compatible with upstream Luau going forward. Upstream
is interested in classical inheritance with `open class` and `extends`.

## Drawbacks

<!-- Why should we *not* do this? These should note the drawbacks of the current design/implementation, and not overlap with Alternatives. -->

- There's currently no way to resolve conflicts without changing a trait.
- Since all fields/functions are resolved and copied into the class at class initialization time, there is no way to specify fields/functions that
  are only visible inside their traits. There's no way for a trait to have internal implementation details that a class that implements it cannot
  see or access (final fields and functions just prevent overriding).

## Alternatives

- Do classical inheritance
- Do traits with only functions (no fields)

<!-- What other designs have been considered? What is the impact of not doing this? -->

## Future work

- Explicit trait qualification to fix conflicting names: `object:Json:serialize()`.
- An `implements` keyword operator alongside the `is` keyword operator.
- Method delegation via composition as an alternative for inheritance that allows preservation of internal implementation details.

## Prior art (required only for syntax and semantics changing RFCs)

<!-- How has this feature been influenced by other programming languages, theory, and practical use? -->

This feature is heavily influenced by Rust, Scala 3, PHP traits, etc.

## Implementation details (optional)

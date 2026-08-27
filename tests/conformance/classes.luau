--!nocheck

local function expectpass(s, f)
    f()
    print(`ok: {s}`)
end

local function expectfail(s, expected, f)
    local success, actual = pcall(f)
    assert(not success)
    assert(type(actual) == "string", `{s}: error message was not a string, but a {type(actual)}`)
    -- We have pass true for the last argument to string.find to disable pattern matching, since error messages
    -- can have characters in them that would be interpreted as patterns, and we want to match the error message exactly.
    -- e.g. parens in error messages can be interpreted as pattern groups
    assert(string.find(actual, expected, 1, true), `{s} expected:\n\t{expected}\nActual:\n\t{actual}`)
    print(`ok: {s}`)
end

class Point
    public x
    public y

    function magnitude(self)
        return math.sqrt(self.x * self.x + self.y * self.y)
    end

    function __mul(self, other)
        return Point { x = self.x * other.x, y = self.y * other.y }
    end

    function __add(self, other)
        return Point { x = self.x + other.x, y = self.y + other.y }
    end

    function __eq(self, other)
        return self.x == other.x and self.y == other.y
    end

    function zero()
        return Point { x = 0, y = 0 }
    end

    function asserttriple(self)
        local mag = self:magnitude()
        assert(mag == math.ceil(mag), "Not a pythagorean triple!")
    end

    function __tostring(self)
        return `Point(x={self.x}, y={self.y})`
    end

end

class NotAPoint
    public x
end

expectpass("class_first_arg_is_instance", function()
    local p1 = Point({ x = 1, y = 2})
    local p2 = Point({ x = 0, y = 0})
    assert(class.isinstance(p1, Point))
    assert(class.isinstance(p2, Point))
end)

expectpass("class_first_arg_is_not_instance", function()
    local notAPoint = NotAPoint({ x = 1})
    assert(not class.isinstance(notAPoint, Point))
end)

expectpass("class_first_arg_is_not_instance_and_isnt_class", function()
    assert(not class.isinstance(nil, Point))
    assert(not class.isinstance(false, Point))
    assert(not class.isinstance(true, Point))
    assert(not class.isinstance("hello", Point))
    assert(not class.isinstance(5, Point))
    assert(not class.isinstance({}, Point))
end)

expectfail("class_second_arg_is_not_classobject", "invalid argument #2 to 'isinstance' (class expected, got string)", function()
    class.isinstance(nil, "not a class")
end)

expectfail("class_second_arg_is_not_classobject", "invalid argument #2 to 'isinstance' (class expected, got string)", function()
    class.isinstance(Point({x = 1, y = 2}), "not a class")
end)

expectfail("classof raises on missing first argument", "missing argument #1", function()
    class.isinstance()
end)

expectfail("classof raises on missing second argument", "missing argument #2", function()
    class.isinstance(nil)
end)

expectpass("classof returns nil for non-classinstance values", function()
    assert(class.classof(nil) == nil)
    assert(class.classof(false) == nil)
    assert(class.classof(42) == nil)
    assert(class.classof("hello") == nil)
    assert(class.classof({}) == nil)
    -- A class object is not itself a class instance.
    assert(class.classof(Point) == nil)
end)

expectfail("classof raises on missing argument", "missing argument #1", function()
    class.classof()
end)

expectpass("classof returns the class object for a class instance", function()
    local p = Point { x = 1, y = 2 }
    local c = class.classof(p)
    assert(c == Point)
    assert(typeof(c) == "class")
end)

expectpass("classof result is usable as the second arg to isinstance", function()
    local p = Point { x = 3, y = 4 }
    local notAPoint = NotAPoint ({ x = 1 })
    -- classof(notAPoint) is NotAPoint, so a Point instance is not an instance of it.
    assert(not class.isinstance(p, class.classof(notAPoint)))
    -- classof(p) is Point, so p is an instance of it.
    assert(class.isinstance(p, class.classof(p)))
end)

expectpass("basic printing", function ()
    assert(typeof(Point) == "class")
    assert(typeof(Point.zero) == "function")
end)

expectfail(
    "instance methods reject a self that isn't an instance of the class",
    "attempt to call method 'magnitude' with 'self' not being an instance of 'Point'",
    function ()
        Point.magnitude({x = 4, y = 3})
    end
)

expectfail("classobject missing key", "this class does not have a key named 'doesnotexist'", function ()
    return Point.doesnotexist
end)

expectfail("classobject invalid key", "cannot index class with a table", function ()
    return Point[{}]
end)

expectfail("classobject set method", "attempt to index class with 'zero'", function ()
    Point.zero = function () end
end)

expectpass("classobject construction", function ()
    local p = Point({ x = 1, y = 2 })
    assert(p.x == 1 and p.y == 2)
end)

expectfail("classobject bad construction", "attempt to index number with 'x'", function ()
    local _ = Point(42)
end)

expectpass("classobject no construction arg", function ()
    local p = Point()
    assert(not p.x and not p.y)
end)

expectpass("classinstance basics", function()
    local p = Point({ x = 3, y = 4 })
    assert(p.x == 3 and p.y == 4)
    assert(p:magnitude() == 5)
    local pzero = Point.zero()
    assert(pzero.x == 0 and pzero.y == 0)
    assert(pzero:magnitude() == 0)
    local success, res = pcall(Point.magnitude, p)
    assert(success and res == 5)
    local success, res = pcall(Point.magnitude, "wtf")
    assert(not success)
end)

expectpass("classinstance mutable", function ()
    local p = Point()
    p.x = 84
    p.y = 13
    assert(p:magnitude() == 85)
end)

expectpass("classinstance pcall method", function()
    local p = Point({ x = 1, y = 2})
    local success, msg = pcall(Point.asserttriple, p)
    assert(not success, "Expected pcall to fail")
    assert(msg:match("Not a pythagorean triple!"), `Message was: {msg}`)
end)

expectfail("classinstance assign nonexistent prop", "this object does not have a key named 'huh'", function()
    local p = Point({ x = 3, y = 4 })
    p.huh = "???"
end)

expectfail("classinstance get nonexistent prop", "this object does not have a key named 'huh'", function()
    local p = Point {}
    print(p.huh)
end)

expectfail("classinstance assign method", "attempt to index object with 'zero'", function()
    local p = Point({ x = 3, y = 4 })
    p.zero = "???"
end)

expectfail("classinstance call missing method", "this object does not have a key named 'hmm'", function()
    local p = Point {}
    print(p:hmm())
end)

expectpass("classinstance dynamic lookup", function ()
    local p = Point { x = 42, y = 13 }
    local function getfield(s)
        return p[s]
    end
    assert(getfield("x") == 42)
end)

expectpass("classinstance operator overloads", function ()
    local p1 = Point { x = 1, y = 4 }
    local p2 = Point { x = 2, y = 5 }
    local p3 = p1 + p2
    assert(p3.x == 3 and p3.y == 9)
    local p4 = p1 * p2
    assert(p4.x == 2 and p4.y == 20)
end)

expectpass("classinstance partially constructed class", function ()
    local p = Point { y = 99 }
    assert(not p.x and p.y == 99)
    p.x = 20
    assert(p:magnitude() == 101)
end)

class Secret

    function make(s)
        return Secret { cb = function() return s end }
    end

    function __concat(lhs, rhs)
        return Secret {
            cb = function ()
                return lhs:render() .. rhs:render()
            end
        }
    end

    public cb

    function render(self)
        return self.cb()
    end
end

expectpass("classinstance more operator overloads", function()
    local s1 = Secret.make("I am the ")
    local s2 = Secret.make("modren man!")
    local s3 = s1 .. s2
    assert(s3:render() == "I am the modren man!")
end)

expectpass("class instantiate first class function", function ()
    local function makeone(C, args)
        return C(args)
    end

    local p = makeone(Point, {x = 10, y = 2})
    assert(typeof(p) == "object")
    assert(p.x == 10 and p.y == 2)
end)

local count = 0

class Counter
    public count
    function make()
        count += 1
        return Counter { count = count }
    end
end

expectpass("classinstance upvalues", function()
    local c1 = Counter.make()
    assert(c1.count == 1)
    local c2 = Counter.make()
    assert(c2.count == 2 and c1.count == 1)
end)

class Box
    public item
end

expectpass("classes have referential equality", function()
    local b1 = Box { item = "gold coin" }
    local b2 = Box { item = "gold coin" }
    assert(b1 == b1)
    assert(b1 ~= b2)
    assert(Box == Box)
    assert(Point ~= Box)
end)

expectfail("classes do not support <", "attempt to compare class < class", function()
    local _ = Box < Point
end)

expectfail("classes do not support <=", "attempt to compare class <= class", function()
    local _ = Box <= Point
end)

expectpass("classes use __eq", function ()
    local p1 = Point { x = 1, y = 2 }
    local p2 = Point { x = 1, y = 2 }
    assert(p1 == p2)
    assert(p1 ~= Point.zero())
end)

expectpass("classinstance __tostring", function ()
    local p = Point { x = 3, y = 4 }
    assert(tostring(p) == "Point(x=3, y=4)")
    assert(`{p}` == "Point(x=3, y=4)")
end)

expectpass("classinstance __tostring reflects properties", function ()
    local p = Point { x = 10, y = 20 }
    p.x = 99
    assert(tostring(p) == "Point(x=99, y=20)")
end)

expectpass("classinstance without __tostring uses default format", function ()
    local b = Box { item = "sword" }
    local s = tostring(b)
    assert(string.match(s, "^object:") ~= nil, `expected default format, got: {s}`)
end)

expectpass("classobject tostring uses default format", function ()
    local s = tostring(Point)
    assert(string.match(s, "^class:") ~= nil, `expected default format, got: {s}`)
end)

class BadToString
    function __tostring(self)
        return {}
    end
end

expectfail("classinstance __tostring must return string", "'__tostring' must return a string", function ()
    tostring(BadToString {})
end)

class Entry
    public tier: string
    public ordering: number

    function __lt(self, other)
        if self.tier ~= other.tier then
            if self.tier == "S" then
                return true
            elseif other.tier == "S" then
                return false
            else
                -- Assume it's a letter tier.
                return self.tier < other.tier
            end
        end
        return self.ordering < other.ordering
    end

end

expectpass("classes can be sorted", function()
    local e1 = Entry { tier = "A", ordering = 2 }
    local e2 = Entry { tier = "S", ordering = 1 }
    local e3 = Entry { tier = "A", ordering = 1 }
    local e4 = Entry { tier = "B", ordering = 1 }
    local entries = { e1, e2, e3, e4 }
    table.sort(entries)
    assert(entries[1] == e2)
    assert(entries[2] == e3)
    assert(entries[3] == e1)
    assert(entries[4] == e4)
end)

@native
local function istoptier(entry)
    return entry.tier == "S" or entry.tier == "A"
end

@native
local function makeentry(tier, ordering)
    return Entry { tier = tier, ordering = ordering }
end

expectpass("classes gracefully handle NCG", function()
    local hightier = Entry { tier = "A", ordering = 3 }
    local midtier =  Entry { tier = "C", ordering = 1 }
    local lowtier =  Entry { tier = "D", ordering = 2 }
    assert(istoptier(hightier))
    assert(not istoptier(midtier))
    assert(not istoptier(lowtier))
    local fromncg = makeentry("S", 4)
    assert(fromncg.tier == "S" and fromncg.ordering == 4)
end)

expectfail("classes cannot be iterated over", "attempt to iterate over a object value", function ()
    local p = Point { x = 1, y = 2}
    for k, v in p do
        -- This should be unreachable!
        print(k, v)
    end
end)

class Set
    function __iter(self)
        return next, {1, 2, 3, 4}
    end
end

expectpass("classes with __iter can be iterated over", function ()
    local s = Set {}
    for k, v in s do
        assert(typeof(k) == "number" and typeof(v) == "number")
    end
end)

class Test
    public a
    public b
    public c
end

expectpass("instance survives GC during __index construction", function()
    local poison = setmetatable({}, {
        __index = function(_, key)
            collectgarbage()

            if key == "a" then return 1 end
            if key == "b" then return 2 end
            if key == "c" then return 3 end
            return nil
        end
    })

    local v = Test(poison)
    assert(v.a == 1, `expected a=1, got a={v.a}`)
    assert(v.b == 2, `expected b=2, got b={v.b}`)
    assert(v.c == 3, `expected c=3, got c={v.c}`)
end)

class Cls end

expectpass("type reports 'class' for class objects", function()
    assert(type(Cls) == "class", `expected type(Cls) == "class", got {type(Cls)}`)
    assert(typeof(Cls) == "class", `expected typeof(Cls) == "class", got {typeof(Cls)}`)
end)

expectpass("type reports 'object' for class instances", function()
    local inst = Cls {}
    assert(type(inst) == "object", `expected type(inst) == "object", got {type(inst)}`)
    assert(typeof(inst) == "object", `expected typeof(inst) == "object", got {typeof(inst)}`)
end)

expectpass("pcall constructors", function()
    local AlwaysRaises = setmetatable({}, {
        __index=function(self, name)
            error(`Cannot access {name}`)
        end
    })

    local success, res = pcall(Test, AlwaysRaises)
    assert(not success)
    assert("string" == typeof(res))

    local success, res = pcall(function(a) return Test(a) end, AlwaysRaises)
    assert(not success)
    assert("string" == typeof(res))
end)

class Foo
    public x
    public y
    public z
end

expectpass("stack reallocation in creation", function()

    local function deepStack(n)
        local a01, a02, a03, a04, a05, a06, a07, a08, a09, a10 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
        local a11, a12, a13, a14, a15, a16, a17, a18, a19, a20 = 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
        if n > 0 then
            return deepStack(n - 1)+a01+a02+a03+a04+a05+a06+a07+a08+a09+a10+a11+a12+a13+a14+a15+a16+a17+a18+a19+a20
        end
        return 1
    end

    local mt = setmetatable({}, {
        __index = function(t, k)
            local trigger = deepStack(80)
            if k == "x" then return 100 + trigger - trigger end
            if k == "y" then return 200 end
            if k == "z" then return 300 end
        end
    })

    local f = Foo(mt)
    assert(f.x == 100)
    assert(f.y == 200)
    assert(f.z == 300)
end)

class UserWithInit
    public first_name: string
    public last_name: string
    private ssn: string?

    public function __init(self, first, last, ssn)
        self.first_name = first
        self.last_name = last
        self.ssn = ssn
    end

    public function name(self): string
        return self.first_name .. " " .. self.last_name
    end

    private function get_ssn(self): string
        return self.ssn
    end

    public function reveal_ssn(self): string
        -- Calling a private method from within another method of the same class is allowed.
        return self:get_ssn()
    end
end

expectpass("custom __init is used as the constructor", function()
    local user = UserWithInit("Taz", "Parekh", "126-222-1123")
    assert(user:name() == "Taz Parekh")
    assert(user:reveal_ssn() == "126-222-1123")
end)

expectpass("custom __init can be called directly like a normal method", function()
    local user = UserWithInit("a", "b", "c")
    user:__init("Re", "Init", "999-99-9999")
    assert(user:name() == "Re Init")
    assert(user:reveal_ssn() == "999-99-9999")
end)

expectfail(
    "private methods cannot be called from outside the class",
    "'get_ssn' is a private member of 'UserWithInit', it cannot be accessed outside UserWithInit's class scope",
    function()
        local user = UserWithInit("Taz", "Parekh", "126-222-1123")
        user:get_ssn()
    end
)

expectpass("class.fields returns all public fields and values for an instance with no private members", function()
    local p = Point { x = 1, y = 2 }
    local fields, complete = class.fields(p)
    assert(complete == true)
    assert(fields.x == 1)
    assert(fields.y == 2)
    local count = 0
    for _ in pairs(fields) do
        count += 1
    end
    assert(count == 2)
end)

expectpass("class.fields omits private fields and reports complete = false", function()
    local user = UserWithInit("Taz", "Parekh", "126-222-1123")
    local fields, complete = class.fields(user)
    assert(complete == false)
    assert(fields.first_name == "Taz")
    assert(fields.last_name == "Parekh")
    -- The private 'ssn' field must never be exposed via class.fields.
    assert(fields.ssn == nil)
    local count = 0
    for k in pairs(fields) do
        assert(k ~= "ssn", "class.fields leaked a private field")
        count += 1
    end
    assert(count == 2)
end)

expectpass("class.fields never leaks private methods either", function()
    local user = UserWithInit("Taz", "Parekh", "126-222-1123")
    local fields = class.fields(user)
    assert(fields.get_ssn == nil)
end)

expectpass("class.fields works on a class object, not just an instance", function()
    local fields, complete = class.fields(UserWithInit)
    assert(complete == false)
    -- No instance was passed, so public fields are present but unset (none, not nil,
    -- so the keys are still visible via pairs).
    assert(fields.first_name == none)
    assert(fields.last_name == none)
    assert(fields.ssn == nil)
end)

expectpass("class.fields uses 'none' rather than 'nil' so unset fields stay visible via pairs", function()
    local fields = class.fields(Point)
    local count = 0
    for k, v in pairs(fields) do
        assert(v == none)
        count += 1
    end
    assert(count == 2)
end)

expectfail("class.fields raises on a non-class/object argument", "invalid argument #1 to 'fields' (class or object expected)", function()
    class.fields("not a class or object")
end)

expectfail("class.fields raises on a missing argument", "missing argument #1", function()
    class.fields()
end)

class Token
    public const value: string

    public function __init(self, value: string)
        self.value = value
    end

    public function reset(self, value: string)
        -- `const` fields can only be assigned from within `__init`.
        self.value = value
    end
end

expectpass("const fields can be assigned from within __init", function()
    local token = Token("abc")
    assert(token.value == "abc")
end)

expectfail(
    "const fields cannot be assigned outside of __init",
    "'value' is a const member of 'Token' and cannot be assigned outside Token's '__init' constructor",
    function()
        local token = Token("abc")
        token:reset("def")
    end
)

expectfail(
    "const fields cannot be assigned from outside the class either",
    "'value' is a const member of 'Token' and cannot be assigned outside Token's '__init' constructor",
    function()
        local token = Token("abc")
        token.value = "def"
    end
)

class Thingy
    public const cat: string

    private function __init(self, cat: string)
        self.cat = cat
    end

    public function new(cat: string): Thingy
        return Thingy(cat)
    end
end

expectpass("private __init can be called from a public factory function within the class", function()
    local thing = Thingy.new("meow")
    assert(thing.cat == "meow")
end)

expectfail(
    "private __init cannot be called directly from outside the class",
    "'__init' is a private member of 'Thingy', it cannot be accessed outside Thingy's class scope",
    function()
        Thingy("meow")
    end
)

class PointNoInit
    x: number
    y: number
end

expectpass("classes without __init still use the default POD constructor", function()
    local p = PointNoInit({ x = 3, y = 4 })
    assert(p.x == 3 and p.y == 4)
    local p2 = PointNoInit()
    assert(not p2.x and not p2.y)
end)

expectpass("default __init can be called directly like a normal method", function()
    local p = PointNoInit({ x = 3, y = 4 })
    p:__init({ x = 5, y = 6 })
    assert(p.x == 5 and p.y == 6)
    p:__init()
    assert(not p.x and not p.y)
end)

expectpass("default __init can be called directly on the class object", function()
    local p = PointNoInit({ x = 3, y = 4 })
    PointNoInit.__init(p, { x = 7, y = 8 })
    assert(p.x == 7 and p.y == 8)
end)

local widgetIdCounter = 0
local function nextWidgetId()
    widgetIdCounter += 1
    return widgetIdCounter
end

class Widget
    public const id = nextWidgetId()
    public label: string

    function __init(self, label)
        self.label = label
    end
end

expectpass("default field values are set before a custom __init runs", function()
    local w = Widget("hello")
    assert(w.label == "hello")
    assert(type(w.id) == "number")
end)

expectpass("default field values are re-evaluated on every construction", function()
    local w1 = Widget("a")
    local w2 = Widget("b")
    assert(w1.id ~= w2.id, `expected different ids per construction, got {w1.id} and {w2.id}`)
end)

local bareUpvalueBase = 42

class BareUpvalueDefault
    public x = bareUpvalueBase

    function __init(self) end
end

expectpass("default field value referencing a bare outer local (no call) works", function()
    local b = BareUpvalueDefault()
    assert(b.x == 42)
end)

local mutableUpvalue = 1
local function bumpMutableUpvalue()
    mutableUpvalue = mutableUpvalue + 1
end
bumpMutableUpvalue()

class MutableBareUpvalueDefault
    public x = mutableUpvalue

    function __init(self) end
end

expectpass("default field value referencing a mutable bare outer local works", function()
    local m = MutableBareUpvalueDefault()
    assert(m.x == 2, `expected 2, got {m.x}`)
    bumpMutableUpvalue()
    local m2 = MutableBareUpvalueDefault()
    assert(m2.x == 3, `expected 3, got {m2.x}`)
end)

class PodPoint
    x = 0
    y = 0
    public label: string
end

expectpass("POD classes (no custom __init) apply field defaults", function()
    local p = PodPoint()
    assert(p.x == 0 and p.y == 0)
    assert(p.label == nil)
end)

expectpass("POD classes fill explicitly provided fields over their defaults", function()
    local p = PodPoint({ x = 5, label = "hi" })
    assert(p.x == 5)
    assert(p.y == 0)
    assert(p.label == "hi")
end)

local podCounter = 0
local function nextPodId()
    podCounter += 1
    return podCounter
end

class PodWithUpvalueDefault
    public id = nextPodId()
end

expectpass("POD class field defaults are re-evaluated on every construction", function()
    local a = PodWithUpvalueDefault()
    local b = PodWithUpvalueDefault()
    assert(a.id ~= b.id, `expected different ids per construction, got {a.id} and {b.id}`)
end)

class SelfCheckCat
    public name: string

    public function __init(self, name: string)
        self.name = name
    end

    public function meow(self): string
        return `{self.name} says meow!`
    end
end

class SelfCheckDog
    public name: string

    public function __init(self, name: string)
        self.name = name
    end
end

expectpass("calling a method via dot-syntax with the correct self works", function()
    local cat = SelfCheckCat("Taz")
    -- both `Class.method(instance)` (dot-syntax) and `instance:method()` (colon-syntax) should work
    -- when `self` genuinely is an instance of the method's own class.
    assert(SelfCheckCat.meow(cat) == "Taz says meow!")
    assert(cat:meow() == "Taz says meow!")
end)

expectfail(
    "calling a method via dot-syntax with a self from a different class errors",
    "attempt to call method 'meow' with 'self' not being an instance of 'SelfCheckCat'",
    function()
        local dog = SelfCheckDog("Rex")
        -- SelfCheckDog has no `meow` of its own, but SelfCheckCat's `meow` only reads `self.name`,
        -- which SelfCheckDog also happens to have -- without a runtime check this would silently
        -- "work" by accident and print "Rex says meow!", which is exactly the correctness footgun
        -- this check exists to catch.
        SelfCheckCat.meow(dog)
    end
)

expectpass("hoisting: read before declaration is nil", function()
    assert(typeof(Hoisted) == "nil")
end)

class Hoisted
    public x
end

expectpass("hoisting: read after declaration is class", function()
    assert(typeof(Hoisted) == "class")
    local h = Hoisted({ x = 1 })
    assert(h.x == 1)
end)

class Parent
    public children: {Child}

    function createChild(self)
        local child = Child({parent = self}) -- forward reference to Child
        table.insert(self.children, child)
        return child
    end
end

class Child
    public parent: Parent

    function createParent(self)
        local p = Parent({children = {self}})
        return p
    end
end

expectpass("hoisting: mutually referential classes work via capture by ref", function()
    local p = Parent({children = {}})
    local c = p:createChild()
    assert(class.isinstance(c, Child))
    assert(c.parent == p)
    assert(#p.children == 1)
    assert(p.children[1] == c)

    local p2 = c:createParent()
    assert(class.isinstance(p2, Parent))
end)

local function makeLateWidget(id)
    return LateWidget({ id = id }) -- forward reference to LateWidget
end

class LateWidget
    public id
end

expectpass("hoisting: function defined before class works via capture by ref", function()
    local w = makeLateWidget(42)
    assert(class.isinstance(w, LateWidget))
    assert(w.id == 42)
end)

export class ReproCat
    name: string
    age: number
    public function __init(self, name, age)
        self.name = name
        self.age = age
    end
    function meow(self, message)
        message = message or "says meow"
        return `{self.name} {message}`
    end
end

class ReproList<T>
    private const inner: { T }

    public function __init(self, ...: T)
        self.inner = { ... }
    end

    private function into_option(item: T?)
        return item
    end

    public function pop_front(self)
        local res = table.remove(self.inner, 1)
        return ReproList.into_option(res)
    end

    public function push_back(self, item: T)
        table.insert(self.inner, item)
    end

    public function front(self)
        return ReproList.into_option(self.inner[1])
    end

    public function push_front(self, item: T)
        table.insert(self.inner, 1, item)
    end

    public function __len(self)
        return #self.inner
    end
end

expectpass("repro: self-check works when constructed as an argument to a later generic class's constructor", function()
    local cats = ReproList(
        ReproCat("Taz", 12),
        ReproCat("Nanuk", 2)
    )
    local crazy = ReproCat("Crazy", 20)
    cats:push_front(crazy)
end)

class ReproFoo
    public cats: { string }
end

class ReproPerson end

class ReproClass
    public id: string
    public lecturer: ReproPerson
    public tagline: string
    public requirements
    public students: { ReproPerson }
    private password: string

    public function __tostring(self)
        return table.concat({
            self.id,
            tostring(self.lecturer),
            self.tagline,
        }, "\n")
    end
end

expectpass("repro: full sequence like the reported failure", function()
    local comp_4081 = ReproClass {
        id = "4081",
        lecturer = ReproPerson(),
        requirements = "3081",
        tagline = "COMP 4081",
        password = "some password",
        students = {}
    }
    local cat = ReproCat("Taz", 12)
    assert(cat:meow("hehe") == "Taz hehe")
end)

-- OptimizeConstProp: object member access CSE / store-to-load forwarding / invalidation
-- (see OBJECT_CONSTPROP.CLAUDE.md). These must stay correct with native codegen's field caching.
class ConstPropBox
    public v: number
    const c: number = 42

    function bump(self)
        self.v += 100
    end
end

expectpass("constprop: redundant reads of the same field fold but stay correct", function()
    local b = ConstPropBox { v = 3 }
    -- v.v read four times in one expression; must all observe 3
    assert(b.v + b.v + b.v + b.v == 12)
end)

expectpass("constprop: a call between two reads must not reuse a stale cached field", function()
    local function readtwice(b: ConstPropBox)
        local a = b.v
        b:bump() -- mutates b.v, must invalidate the cached read
        local c = b.v
        return a, c
    end
    local a, c = readtwice(ConstPropBox { v = 1 })
    assert(a == 1 and c == 101)
end)

expectpass("constprop: write then read of the same field forwards the stored value", function()
    local function f(b: ConstPropBox, n: number)
        local acc = 0
        for _ = 1, n do
            b.v = b.v + 1
            acc += b.v
        end
        return acc
    end
    assert(f(ConstPropBox { v = 0 }, 5) == 15)
end)

expectfail(
    "constprop: an outside write must not reuse a prior read's weaker (non-const) guard",
    "cannot be assigned outside",
    function()
        local b = ConstPropBox { v = 0 }
        local _ = b.c -- read const (allowed), populates the read-address cache
        b.c = 5       -- illegal outside-__init write: must still enforce const, not reuse the read addr
    end
)

return 'OK'

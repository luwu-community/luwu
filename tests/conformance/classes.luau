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
    x
    y

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
    "attempt to call method 'Point.magnitude' with 'self' of type 'table'",
    function ()
        Point.magnitude({x = 4, y = 3})
    end
)

expectfail("classobject missing member", "class 'Point' does not have a member named 'doesnotexist'; did you mean to access a field of an object of this class instead?", function ()
    return Point.doesnotexist
end)

expectfail(
    "an instance field reached through the class names the field",
    "'x' is a field of objects of class 'Point', not a member of the class itself; did you mean to access it on an object of 'Point' instead?",
    function()
        return Point.x
    end
)

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

expectfail("classinstance assign nonexistent field", "objects of class 'Point' do not have a field named 'huh'", function()
    local p = Point({ x = 3, y = 4 })
    p.huh = "???"
end)

expectfail("classinstance get nonexistent field", "objects of class 'Point' do not have a field named 'huh'", function()
    local p = Point {}
    print(p.huh)
end)

expectfail("classinstance assign method", "attempt to index object with 'zero'", function()
    local p = Point({ x = 3, y = 4 })
    p.zero = "???"
end)

expectfail("classinstance call missing method", "objects of class 'Point' do not have a field named 'hmm'", function()
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

    cb

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
    count
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
    tier: string
    ordering: number

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
    const id = nextWidgetId()
    label: string

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
    x = bareUpvalueBase

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
    x = mutableUpvalue

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
    label: string
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
    "attempt to call method 'SelfCheckCat.meow' with 'self' of class 'SelfCheckDog'",
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
    children: {Child}

    function createChild(self)
        local child = Child({parent = self}) -- forward reference to Child
        table.insert(self.children, child)
        return child
    end
end

class Child
    parent: Parent

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

class ReproCat
    name: string
    age: number
    function __init(self, name, age)
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
    v: number
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

-- class.isinstance fast paths (rfcx/classes.md): the LBF_CLASS_ISINSTANCE builtin / CLASS_ISINSTANCE
-- IR op (value position) and the fused JUMPXISA test-and-branch (condition position). All of these
-- must agree with the plain library semantics for objects, non-objects and both branch polarities.
class IsaFoo public x: number = 0 end
class IsaBar public y: number = 0 end

expectpass("isinstance: value position, objects and non-objects", function()
    local foo = IsaFoo {}
    assert(class.isinstance(foo, IsaFoo) == true)
    assert(class.isinstance(foo, IsaBar) == false)
    assert(class.isinstance(42, IsaFoo) == false)
    assert(class.isinstance("s", IsaFoo) == false)
    assert(class.isinstance(nil, IsaFoo) == false)
    assert(class.isinstance({}, IsaFoo) == false)
end)

expectpass("isinstance: fused condition dispatch matches library semantics", function()
    local function classify(v: any): string
        if class.isinstance(v, IsaFoo) then
            return "foo"
        elseif class.isinstance(v, IsaBar) then
            return "bar"
        end
        return "other"
    end
    assert(classify(IsaFoo {}) == "foo")
    assert(classify(IsaBar {}) == "bar")
    assert(classify(42) == "other")
end)

expectpass("isinstance: negated and short-circuit conditions", function()
    local function notFoo(v: any): boolean
        if not class.isinstance(v, IsaFoo) then
            return true
        end
        return false
    end
    assert(notFoo(IsaBar {}) == true)
    assert(notFoo(IsaFoo {}) == false)

    -- `and` keeps class.isinstance in a value-producing spot rather than a bare condition
    local foo = IsaFoo {}
    assert((class.isinstance(foo, IsaFoo) and "yes" or "no") == "yes")
    assert((class.isinstance(foo, IsaBar) and "yes" or "no") == "no")
end)

expectfail("isinstance: second argument must be a class", "class", function()
    return class.isinstance(IsaFoo {}, 5)
end)

-- Method inlining must remain correct when the inlined method yields: the Luau bytecode inliner is
-- yield-transparent (a yield suspends the whole coroutine; resume continues in the caller's inlined
-- body), so a `self:method()` / typed `obj:method()` call that contains a coroutine.yield can inline
-- without breaking suspend/resume. See rfcx/classes.md.
class YieldGen
    public base: number = 0

    public function step(self, k: number): number
        local fed = coroutine.yield(self.base + k)
        return fed * 10
    end

    public function run(self): number
        local a = self:step(1) -- self:method() inline target, yields mid-body
        local b = self:step(2)
        return a + b
    end
end

expectpass("inlining: a self:method() that yields resumes correctly", function()
    local co = coroutine.wrap(function()
        return (YieldGen { base = 100 }):run()
    end)
    assert(co() == 101)         -- first yield: base + 1
    assert(co(3) == 102)        -- feed 3 -> a = 30; second yield: base + 2
    assert(co(4) == 70)         -- feed 4 -> b = 40; final return a + b = 70
end)

expectpass("inlining: a typed-receiver obj:method() that yields resumes correctly", function()
    local function consume(y: YieldGen): number
        local acc = 0
        acc += y:step(1) -- cross-function inline of an all-public class method that yields
        acc += y:step(2)
        return acc
    end
    local co = coroutine.wrap(function()
        return consume(YieldGen { base = 50 })
    end)
    assert(co() == 51)
    assert(co(100) == 52)       -- step(1) resumes to 100*10 = 1000
    assert(co(200) == 1000 + 2000) -- step(2) resumes to 200*10; acc = 1000 + 2000 = 3000
end)

-- coroutine.resume (not just wrap) must also drive a yielding inlined method correctly.
class ResumeGen
    public base: number = 0
    public function tick(self, k: number): number
        return coroutine.yield(self.base + k)
    end
    public function seq(self): number
        local a = self:tick(1) -- inline target, yields
        local b = self:tick(2)
        return a + b
    end
end

expectpass("inlining: coroutine.resume drives a yielding inlined method", function()
    local co = coroutine.create(function()
        return (ResumeGen { base = 20 }):seq()
    end)
    local ok1, y1 = coroutine.resume(co)     -- yields base + 1 = 21
    assert(ok1 and y1 == 21)
    local ok2, y2 = coroutine.resume(co, 5)  -- a = 5; yields base + 2 = 22
    assert(ok2 and y2 == 22)
    local ok3, r = coroutine.resume(co, 6)   -- b = 6; seq returns a + b = 11
    assert(ok3 and r == 11)
end)

-- An inlined method may itself call coroutine.resume on a child coroutine.
class ChildDriver
    public function drive(self, child: thread): number
        local ok, v = coroutine.resume(child, 7)
        return ok and v or -1
    end
    public function run(self, child: thread): number
        local r = self:drive(child) -- inline target, contains coroutine.resume
        return r + 100
    end
end

expectpass("inlining: a method calling coroutine.resume on a child works", function()
    local child = coroutine.create(function()
        local q = coroutine.yield()
        return q * 2
    end)
    coroutine.resume(child) -- advance child to its yield
    assert((ChildDriver {}):run(child) == 114) -- resumes child with 7 -> 14 -> +100
end)

-- A class method calling a *C* function that yields via the C API (lua_yield). singleYield() and
-- multipleYields() are registered by the Classes conformance harness (Conformance.test.cpp).
class CYielder
    public base: number = 0

    public function fetch(self): number
        return singleYield() + self.base -- C function yields value 2, resumes (continuation) to 4
    end

    public function run(self): number
        local x = self:fetch() -- inline target, carries the yielding C call into run's frame
        return x + 1
    end
end

expectpass("inlining: method calling a yielding C function (lua_yield) resumes correctly", function()
    local co = coroutine.wrap(function()
        return (CYielder { base = 100 }):run()
    end)
    assert(co() == 2)   -- value yielded by the C function
    assert(co() == 105) -- continuation -> 4; fetch = 4 + 100 = 104; run = 105
end)

class MultiCYielder
    public function pull(self, base: number): number
        local m = multipleYields(base) -- C function yields base+1, base+2, base+3, returns base+4
        return m
    end
    public function go(self): number
        local v = self:pull(10) -- inline target
        return v
    end
end

expectpass("inlining: method with a multi-yielding C function resumes each time", function()
    local co = coroutine.wrap(function()
        return (MultiCYielder {}):go()
    end)
    assert(co() == 11) -- base + 1
    assert(co() == 12) -- base + 2
    assert(co() == 13) -- base + 3
    assert(co() == 14) -- final return base + 4 -> go returns 14
end)

-- A custom __init is yieldable (constructor closure carries a continuation; luaR_createobject uses
-- luaL_callyieldable, see lclass.cpp): __init may suspend the coroutine mid-construction and resume,
-- yielding the fully-built object. A `const` field may still be assigned after the resume, because
-- the write is still authorized inside __init's own closure.
class YieldInit
    public v: number = 0
    public const id: number = 0
    public function __init(self, seed: number)
        local fed = coroutine.yield(seed) -- suspend inside the constructor
        self.id = fed        -- const write AFTER resume, still inside __init -> authorized
        self.v = fed * 2
    end
end

expectpass("yielding __init: suspends and resumes into a complete object (via wrap)", function()
    local co = coroutine.wrap(function()
        local o = YieldInit(9)
        return o.id, o.v
    end)
    assert(co() == 9)          -- value yielded from inside __init
    local id, v = co(50)       -- resume: id = 50, v = 100
    assert(id == 50 and v == 100)
end)

expectpass("yielding __init: driven by coroutine.resume", function()
    local co = coroutine.create(function()
        local o = YieldInit(7)
        return o.v
    end)
    local ok1, y = coroutine.resume(co)
    assert(ok1 and y == 7)
    local ok2, v = coroutine.resume(co, 200)
    assert(ok2 and v == 400)
end)

class CInit
    public w: number = 0
    public function __init(self)
        self.w = singleYield() -- C function yields (2), resumes via continuation to 4
    end
end

expectpass("yielding __init: a yielding C function inside the constructor", function()
    local co = coroutine.wrap(function()
        return CInit().w
    end)
    assert(co() == 2)   -- value yielded by the C function
    assert(co() == 4)   -- continuation -> 4; self.w = 4
end)

expectpass("yielding __init: abandoning construction mid-yield does not corrupt state", function()
    for _ = 1, 200 do
        local co = coroutine.wrap(function()
            return YieldInit(1).v
        end)
        co() -- suspend inside __init and never resume; the half-built object must GC cleanly
    end
    collectgarbage()
end)

-- Private-member access from a closure lexically nested inside a class method (see
-- luaR_stampownerclass in VM/src/lclass.cpp): the nested closure gets its own Proto, distinct from
-- the method's, but should still be authorized since it's lexically part of the method body.
class NestedAccessCat
    public name: string
    private unique_id: number

    public function __init(self, name: string)
        self.name = name
        self.unique_id = 12000
    end

    public function meow(self)
        -- direct access from the method itself
        local direct = self.unique_id
        -- access from an immediately-invoked nested closure
        local viaIife = (function()
            return self.unique_id
        end)()
        -- access from a closure nested two levels deep
        local viaDeeplyNested = (function()
            return (function()
                return self.unique_id
            end)()
        end)()
        return direct, viaIife, viaDeeplyNested
    end

    -- a nested closure that escapes the method entirely; access should still be authorized
    -- whenever it's eventually called, since authorization is keyed off the proto, not dynamic
    -- call context
    public function makeGetter(self)
        return function()
            return self.unique_id
        end
    end
end

expectpass("private member access from a closure nested directly inside a method", function()
    local cat = NestedAccessCat("Taz")
    local direct, viaIife, viaDeeplyNested = cat:meow()
    assert(direct == 12000)
    assert(viaIife == 12000)
    assert(viaDeeplyNested == 12000)
end)

expectpass("private member access from a nested closure that escapes and is called later", function()
    local cat = NestedAccessCat("Taz")
    local getter = cat:makeGetter()
    assert(getter() == 12000)
end)

expectfail(
    "private member access from a closure that is NOT lexically nested inside any class method is still rejected",
    "'unique_id' is a private member of 'NestedAccessCat', it cannot be accessed outside NestedAccessCat's class scope",
    function()
        local cat = NestedAccessCat("Taz")
        local function outsideClosure()
            return cat.unique_id
        end
        outsideClosure()
    end
)

-- Runtime checking of `self` for methods (rfcx/classes.md) has to survive method inlining at -O2:
-- `compileInlinedCall` copies the method body into the call site without running the callee's
-- prologue, so it re-emits the CHECKSELFCLASS itself. Without that, a hostile (or merely wrong)
-- annotation would silently run the wrong class's method body instead of erroring.
class InlinePoint2D
    public x: number = 0

    public function __init(self, x: number?)
        if x then
            self.x = x
        end
    end

    public function get_x(self)
        return self.x
    end

    public function double_x(self)
        -- same-class `self:method()`: `self` was already validated by this method's own prologue,
        -- so the inlined copy doesn't need a second check, but it must still be correct
        local v = self:get_x()
        return v * 2
    end
end

class InlinePoint3D
    public x: number = 0

    public function __init(self, x: number?)
        if x then
            self.x = x
        end
    end

    -- deliberately distinguishable from InlinePoint2D's body so that running the wrong one shows up
    public function get_x(self)
        return self.x * 100
    end
end

expectpass("inlinable method calls on a correctly annotated receiver still work", function()
    local p: InlinePoint2D = InlinePoint2D(3)
    local x = p:get_x()
    local d = p:double_x()
    assert(x == 3)
    assert(d == 6)
end)

expectpass("an annotation that lies about a receiver's class never runs the other class's method body", function()
    local p: InlinePoint2D = (InlinePoint3D(3) :: any)

    local ok, res = pcall(function()
        local v = p:get_x()
        return v
    end)

    -- when the call is inlined (-O2) the spliced CHECKSELFCLASS rejects the mismatched receiver;
    -- when it isn't, the call dispatches dynamically to InlinePoint3D's own body. Running
    -- InlinePoint2D's body against an InlinePoint3D (res == 3) is the bug, and is never allowed.
    if ok then
        assert(res == 300, `method call dispatched to the wrong class's body, got {res}`)
    else
        assert(
            string.find(
                res,
                "attempt to call inlined method 'InlinePoint2D:get_x' on an object of class 'InlinePoint3D'",
                1,
                true
            ),
            `unexpected error: {res}`
        )
    end
end)

expectpass("a failed self-check that was not inlined blames the method's own declaration", function()
    local declLine = debug.info(InlinePoint2D.get_x, "l")

    local ok, err = pcall(function()
        -- a dot-call with a bogus `self` is not inlinable, so the callee's own prologue check fires
        return InlinePoint2D.get_x({x = 5})
    end)

    assert(not ok)
    assert(string.find(err, `:{declLine}:`, 1, true), `expected the error at line {declLine}, got: {err}`)
end)

expectpass("a failed self-check that was inlined blames the call site, not the method declaration", function()
    local p: InlinePoint2D = (InlinePoint3D(3) :: any)

    local callLine
    local ok, err = pcall(function()
        callLine = debug.info(1, "l") + 1
        local v = p:get_x()
        return v
    end)

    -- only inlining (-O2) makes this fail at all; when it does, the location must be the call
    if not ok then
        assert(string.find(err, `:{callLine}:`, 1, true), `expected the error at line {callLine}, got: {err}`)
    end
end)


-- Field defaults that are compile-time constants are serialized into the class shape and copied
-- into each new instance, so such a class has no synthesized `__defaults` closure at all. A default
-- that is *not* constant still has to be re-evaluated on every construction, and keeps the closure.
class ConstDefaults
    public n: number = 42
    public s: string = "hi"
    public flag: boolean = true
    public neg: number = -7
    public bare: number
end

expectpass("constant field defaults are applied to every instance", function()
    local a, b = ConstDefaults(), ConstDefaults()
    assert(a.n == 42 and a.s == "hi" and a.flag == true and a.neg == -7)
    assert(a.bare == nil, "a field with no default is still nil")

    -- the class holds the default; instances must not share storage with it or each other
    a.n = 99
    assert(b.n == 42 and ConstDefaults().n == 42, "defaults must not become shared state")
end)

expectpass("a table argument overrides defaults and leaves the rest alone", function()
    local p = ConstDefaults { n = 1 }
    assert(p.n == 1 and p.s == "hi")
    assert(ConstDefaults { n = nil }.n == 42, "an explicitly nil field keeps its default")
end)

class ComputedDefaults
    public fresh = {}
    public n: number = 5
end

expectpass("a non-constant default is re-evaluated for every instance", function()
    local a, b = ComputedDefaults(), ComputedDefaults()
    assert(type(a.fresh) == "table" and type(b.fresh) == "table")
    assert(a.fresh ~= b.fresh, "a `= {}` default must hand out a fresh table per instance")
    assert(a.n == 5 and b.n == 5)
end)

local sideEffectSeen = 0
local function bumpSideEffect()
    sideEffectSeen += 1
    return sideEffectSeen
end

class SideEffect
    public v = bumpSideEffect()
end

expectpass("a default that observes side effects runs once per construction", function()
    assert(SideEffect().v == 1)
    assert(SideEffect().v == 2)
    assert(sideEffectSeen == 2)
end)

-- Constructing an object allocates its fields inline with the object header, and __init is fetched
-- from a fixed class offset and called with a directly-built frame; check the arg plumbing holds up
-- at the edges.
class ManyArgs
    public first: number = 0
    public last: number = 0
    public total: number = 0

    public function __init(self, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t)
        self.first = a
        self.last = t
        self.total = a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t
    end
end

expectpass("a constructor with enough arguments to grow the stack", function()
    local m = ManyArgs(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20)
    assert(m.first == 1 and m.last == 20 and m.total == 210, `got {m.first}/{m.last}/{m.total}`)
end)

expectpass("a table argument carrying an __index metatable still goes through the generic path", function()
    local proxied = setmetatable({}, {
        __index = function(_, key)
            return key == "n" and 100 or nil
        end,
    })

    local p = ConstDefaults(proxied)
    assert(p.n == 100, `expected __index to be consulted, got {p.n}`)
    assert(p.s == "hi", "fields the proxy doesn't supply keep their defaults")
end)


-- A call whose callee is a statically resolved class using the default constructor is compiled to a
-- single NEWOBJECT, bypassing the class's __call metamethod and its C constructor. Classes that need
-- a real call -- a user `__init`, or non-constant defaults behind `__defaults` -- must still get one.
class NewObj
    public x: number = 1
    public y: number = 2
end

class NewObjInit
    public x: number = 0
    public function __init(self, x: number)
        self.x = x
    end
end

class NewObjComputed
    public fresh = {}
    public n: number = 3
end

expectpass("a class with a default constructor builds correctly with and without an argument", function()
    assert(NewObj().x == 1 and NewObj().y == 2)
    assert(NewObj {}.x == 1)
    assert(NewObj { x = 7 }.x == 7 and NewObj { x = 7 }.y == 2)
    assert(NewObj { x = 7, y = 8 }.y == 8)
    assert(class.isinstance(NewObj(), NewObj), "still an instance of its class")
end)

expectpass("classes that need a real constructor call still get one", function()
    assert(NewObjInit(5).x == 5)
    local a, b = NewObjComputed(), NewObjComputed()
    assert(a.n == 3 and a.fresh ~= b.fresh, "non-constant defaults keep re-evaluating")
end)

expectpass("constructing in a multret position yields exactly one value", function()
    local function make()
        return NewObj { x = 4 }
    end

    assert(make().x == 4)
    assert(select("#", make()) == 1, "a constructor returns one value, not a multret tail")
    assert(select("#", NewObj()) == 1)

    -- as a trailing call argument, the constructor must not spread
    local function count(...)
        return select("#", ...)
    end
    assert(count(NewObj()) == 1)
    assert(count(1, 2, NewObj()) == 3)
end)

expectpass("a constructor argument carrying __index is honoured", function()
    local proxied = setmetatable({}, {
        __index = function(_, key)
            -- collect during the lookup: the half-built object must stay reachable
            collectgarbage()
            return key == "x" and 42 or nil
        end,
    })

    local p = NewObj(proxied)
    assert(p.x == 42, `expected __index to be consulted, got {p.x}`)
    assert(p.y == 2, "fields the proxy doesn't supply keep their defaults")
end)

expectfail(
    "the default constructor still rejects too many arguments",
    "expected zero or one arguments",
    function()
        return NewObj(1, 2)
    end
)


-- With a user-defined `__init`, NEWOBJECT allocates the instance and lays out `__init`'s frame for a
-- plain CALL, so `__init` runs as an ordinary Lua call rather than through a C constructor.
class InitArgs
    public a = 0
    public b = 0
    public extra = 0

    public function __init(self, a, b, ...)
        self.a = a
        self.b = b
        self.extra = select("#", ...)
    end
end

expectpass("__init receives its arguments, including varargs and missing ones", function()
    local q = InitArgs(1, 2)
    assert(q.a == 1 and q.b == 2 and q.extra == 0)
    assert(InitArgs(1, 2, 3, 4).extra == 2, "varargs reach __init")

    local short = InitArgs(1)
    assert(short.a == 1 and short.b == nil, "arguments __init doesn't receive are nil")
end)

class InitDefaulted
    public x: number = 11
    public y: number = 22

    public function __init(self, y)
        if y then
            self.y = y
        end
    end
end

expectpass("__init runs with field defaults already applied", function()
    assert(InitDefaulted().x == 11 and InitDefaulted().y == 22)
    assert(InitDefaulted(5).x == 11 and InitDefaulted(5).y == 5)
end)

class InitInner
    public k = 0
    public function __init(self, k)
        self.k = k
    end
end

class InitOuter
    public inner = nil
    public sum = 0

    public function __init(self, k)
        -- constructing inside a constructor nests the frames
        self.inner = InitInner(k + 1)
        self.sum = self.inner.k + k
    end
end

expectpass("constructing inside a constructor", function()
    local o = InitOuter(4)
    assert(o.inner.k == 5 and o.sum == 9, `got {o.inner.k}/{o.sum}`)
end)

class InitYields
    public v = 0
    public function __init(self, v)
        coroutine.yield("paused")
        self.v = v
    end
end

expectpass("a yielding __init suspends and resumes to the finished instance", function()
    local co = coroutine.create(function()
        return InitYields(7)
    end)

    local _, msg = coroutine.resume(co)
    assert(msg == "paused")

    local ok, obj = coroutine.resume(co)
    assert(ok and obj.v == 7, "the constructor returns the instance after resuming")
end)

class InitBoom
    public function __init(self)
        error("boom")
    end
end

expectfail("an error raised by __init propagates out of the constructor", "boom", function()
    return InitBoom()
end)

class InitPrivate
    public x = 0
    private function __init(self)
        self.x = 1
    end
end

expectpass("a private __init is still not constructible from outside its class", function()
    assert(not pcall(function()
        return InitPrivate()
    end))
end)


-- `ClassName { field = value }` whose keys all name declared fields is compiled with the values
-- passed positionally, so no argument table is built. Anything the compiler can't place that way
-- falls back to the table-argument form, which must stay observably identical.
class Fields
    public x: number = 1
    public y: number = 2
    public z: number = 3
end

expectpass("a field literal sets what it names and defaults the rest", function()
    local a = Fields { x = 10, y = 20, z = 30 }
    assert(a.x == 10 and a.y == 20 and a.z == 30)

    local b = Fields { y = 99 }
    assert(b.x == 1 and b.y == 99 and b.z == 3, "omitted fields keep their defaults")

    assert(Fields {}.x == 1, "an empty literal is all defaults")
    assert(Fields { x = nil }.x == 1, "an explicitly nil field keeps its default")
    assert(Fields { x = false }.x == false, "false is a value, not an omission")
end)

expectpass("field values are evaluated in source order, not declaration order", function()
    local order = {}
    local function mark(name, value)
        order[#order + 1] = name
        return value
    end

    -- z is written first but declared last
    local p = Fields { z = mark("z", 7), x = mark("x", 8) }
    assert(p.z == 7 and p.x == 8 and p.y == 2)
    assert(table.concat(order, ",") == "z,x", `expected z,x got {table.concat(order, ",")}`)
end)

expectpass("literals the positional form can't take still work", function()
    -- a computed key, an array part, and a key that isn't a declared field
    assert(Fields { [("x")] = 5 }.x == 5)
    assert(Fields { 1, 2 }.x == 1, "an array part is ignored, as before")
    assert(Fields { nope = 1 }.x == 1, "an unknown key is ignored, as before")
end)

-- past kMaxNewObjectFields the positional form stops paying for itself and the table form is used;
-- this class is deliberately wider than that cap
class WideFields
    public f1: number = 0  public f2: number = 0  public f3: number = 0  public f4: number = 0
    public f5: number = 0  public f6: number = 0  public f7: number = 0  public f8: number = 0
    public f9: number = 0  public f10: number = 0 public f11: number = 0 public f12: number = 0
    public f13: number = 0 public f14: number = 0 public f15: number = 0 public f16: number = 0
    public f17: number = 9
end

expectpass("a class wider than the positional-form cap still constructs correctly", function()
    local w = WideFields { f1 = 1, f17 = 17 }
    assert(w.f1 == 1 and w.f17 == 17 and w.f2 == 0, `got {w.f1}/{w.f17}/{w.f2}`)
    assert(WideFields {}.f17 == 9, "defaults still apply")
end)

-- The self-check error names the receiver's *actual* class or type, and distinguishes how the call
-- was spelled: a `.` call passed the wrong receiver, while a `:` call can only reach the wrong
-- method body via a type annotation that lied and let the compiler inline it.
expectfail(
    "a dot-call with an object of the wrong class names that class",
    "attempt to call method 'InlinePoint2D.get_x' with 'self' of class 'InlinePoint3D'",
    function()
        return InlinePoint2D.get_x(InlinePoint3D(1))
    end
)

expectfail(
    "a dot-call with a non-object names its type instead",
    "attempt to call method 'InlinePoint2D.get_x' with 'self' of type 'number'",
    function()
        return InlinePoint2D.get_x(42)
    end
)

expectpass("a colon-call reaching the wrong body points at the annotation", function()
    local p: InlinePoint2D = (InlinePoint3D(3) :: any)

    local ok, err = pcall(function()
        local v = p:get_x()
        return v
    end)

    -- only inlining (-O2) makes this fail at all
    if not ok then
        assert(
            string.find(
                err,
                "attempt to call inlined method 'InlinePoint2D:get_x' on an object of class 'InlinePoint3D'; " ..
                    "this occurred because inlining optimizations are enabled and the passed 'self' did not match the " ..
                    "expected type annotation 'InlinePoint2D'",
                1,
                true
            ),
            `unexpected error: {err}`
        )
    end
end)

-- `class.isinstance` compiles to a fastcall that native codegen inlines as a tag-guarded class
-- pointer compare. Inside a method the tag of `self` is already known (the prologue's
-- CHECKSELFCLASS established it), so constant propagation replaces the tag operand with a
-- constant -- which the X64/A64 lowering has to accept rather than assume a register.
class IsinstanceSelfCheck
    public x: number = 7

    public function get_x(self)
        assert(class.isinstance(self, IsinstanceSelfCheck))
        -- a value whose tag is statically known *not* to be an object folds the other way
        local n = 42
        assert(not class.isinstance(n, IsinstanceSelfCheck))
        return self.x
    end
end

expectpass("class.isinstance on a value with a statically known tag", function()
    local v = IsinstanceSelfCheck()
    assert(v:get_x() == 7)
end)

--
-- Primary constructors (rfcx/classes.md). Each parameter implicitly declares a public field, and the
-- whole header compiles to a synthesized `__init`.
--

class PrimaryCat(name: string, age: number)
    function describe(self)
        return `{self.name} is {self.age}`
    end
end

expectpass("a primary constructor initializes a field per parameter", function()
    local cat = PrimaryCat("Taz", 14)
    assert(cat.name == "Taz")
    assert(cat.age == 14)
    assert(cat:describe() == "Taz is 14")
    assert(class.classof(cat) == PrimaryCat)
end)

expectpass("a primary constructor works when the class isn't statically resolved", function()
    -- reached through a table field, so the compiler can't compile this to NEWOBJECT
    local classes = { cat = PrimaryCat }
    local cat = classes.cat("Oreo", 3)
    assert(cat.name == "Oreo" and cat.age == 3)
end)

-- a parameter default fires only for an argument that was left out (or passed as nil), and a field
-- initializer may derive a value from the parameters
class PrimaryPercentage(current: number, total = 100)
    value = ((current / total) * 100) // 1
end

expectpass("primary constructor parameter defaults", function()
    local a = PrimaryPercentage(27, 42)
    assert(a.value == 64 and a.current == 27 and a.total == 42)

    local b = PrimaryPercentage(27)
    assert(b.value == 27 and b.current == 27 and b.total == 100)

    local c = PrimaryPercentage(27, nil)
    assert(c.total == 100)
end)

-- like default field values, a parameter default is re-evaluated on every construction
local primaryCounterCalls = 0
local function nextPrimaryCounter()
    primaryCounterCalls += 1
    return primaryCounterCalls
end

class PrimaryCounter(current = nextPrimaryCounter())
end

expectpass("primary constructor parameter defaults are re-evaluated per construction", function()
    local before = primaryCounterCalls
    local first = PrimaryCounter()
    local second = PrimaryCounter()

    assert(second.current == first.current + 1)
    assert(primaryCounterCalls == before + 2)

    -- a supplied argument doesn't evaluate the default at all
    local third = PrimaryCounter(99)
    assert(third.current == 99)
    assert(primaryCounterCalls == before + 2)
end)

-- restating a parameter in the class body is how its field gets an access specifier or a modifier;
-- the `= hash` may be omitted, in which case the field is initialized from the parameter
class PrimaryCard(public userid: number, hash: string)
    private const hash

    public function same_hash(self, other)
        return self.hash == other.hash
    end
end

expectpass("a bare restatement makes a parameter's field private", function()
    local a = PrimaryCard(1, "abc")
    local b = PrimaryCard(2, "abc")

    assert(a.userid == 1)
    assert(a:same_hash(b))
end)

expectfail(
    "a restated parameter's field is private outside the class",
    "'hash' is a private member of 'PrimaryCard'",
    function()
        return PrimaryCard(1, "abc").hash
    end
)

-- a parameter may carry the access specifier and `const` modifier of the field it declares, instead
-- of restating the field in the class body (Kotlin-style, rfcx/classes.md)
class PrimaryKey private (
    public const name: string,
    private const secret: string
)
    public function make(name)
        return PrimaryKey(name, `secret-{name}`)
    end

    public function matches(self, other)
        return self.secret == other.secret
    end
end

expectpass("qualified parameters declare their fields' access and constness", function()
    local k = PrimaryKey.make("taz")
    assert(k.name == "taz")
    assert(k:matches(PrimaryKey.make("taz")))
    assert(not k:matches(PrimaryKey.make("nanuk")))
end)

expectfail(
    "a parameter marked private is private outside the class",
    "'secret' is a private member of 'PrimaryKey'",
    function()
        return PrimaryKey.make("taz").secret
    end
)

expectfail(
    "a parameter marked const cannot be assigned outside the constructor",
    "'name' is a const member of 'PrimaryKey'",
    function()
        PrimaryKey.make("taz").name = "nanuk"
    end
)

expectfail("a private primary constructor is private outside the class", "'__init' is a private member of 'PrimaryKey'", function()
    return PrimaryKey("taz", "nope")
end)

-- `const` with no access specifier leaves the field public, and does not make the class one that
-- has to spell out access specifiers everywhere
class PrimaryNonNegative(const inner: number)
    function doubled(self)
        return self.inner * 2
    end
end

expectpass("an unqualified const parameter declares a public const field", function()
    assert(PrimaryNonNegative(21):doubled() == 42)
end)

expectfail("... that is still const", "'inner' is a const member of 'PrimaryNonNegative'", function()
    PrimaryNonNegative(1).inner = 2
end)

-- any expression may be applied to a parameter by restating it
local function primaryNotNegative(name, v)
    return if v >= 0 then v else error(`{name} should be positive`)
end

class PrimaryUDim(scale: number, offset: number)
    scale = primaryNotNegative("scale", scale)
end

expectpass("an expression applied to a parameter", function()
    local d = PrimaryUDim(3, -4)
    assert(d.scale == 3)
    -- only `scale` is validated; `offset` keeps the argument as passed
    assert(d.offset == -4)
end)

expectfail("an expression applied to a parameter can raise", "scale should be positive", function()
    return PrimaryUDim(-1, 0)
end)

-- fields are laid out class-body-first, then the parameters the body doesn't restate
class PrimaryFrame(public name: string, size: number)
    private id = 7
    public size = size * 2

    public function get_id(self)
        return self.id
    end
end

expectpass("body fields and parameter fields coexist", function()
    local f = PrimaryFrame("main", 10)
    assert(f.name == "main")
    assert(f.size == 20)
    assert(f:get_id() == 7)
end)

-- evaluation order is: argument expressions, then defaults for the arguments left out, then field
-- initializers in declaration order
local primaryOrderLog = {}
local function primaryNote(s, v)
    table.insert(primaryOrderLog, s)
    return v
end

class PrimaryOrder(a = primaryNote("default-a", 1), b = primaryNote("default-b", 2))
    c = primaryNote("field-c", 3)
end

expectpass("primary constructor evaluation order", function()
    primaryOrderLog = {}
    local o = PrimaryOrder(primaryNote("arg-a", 10))

    assert(table.concat(primaryOrderLog, ",") == "arg-a,default-b,field-c")
    assert(o.a == 10 and o.b == 2 and o.c == 3)
end)

-- a class may construct itself from within its own methods
class PrimaryVec(x: number, y: number)
    function add(self, other)
        return PrimaryVec(self.x + other.x, self.y + other.y)
    end
end

expectpass("constructing a class from inside one of its own methods", function()
    local v = PrimaryVec(1, 2):add(PrimaryVec(3, 4))
    assert(v.x == 4 and v.y == 6)
end)

-- a private primary constructor may only be called from within its own class
class PrimaryAccount private (public holder: string, public balance: number)
    public function open(holder: string)
        return PrimaryAccount(holder, 0)
    end
end

expectpass("a private primary constructor is callable from inside its class", function()
    local account = PrimaryAccount.open("taz")
    assert(account.holder == "taz" and account.balance == 0)
end)

expectfail(
    "a private primary constructor is not callable from outside its class",
    "'__init' is a private member of 'PrimaryAccount'",
    function()
        return PrimaryAccount("hax", 99)
    end
)

-- the synthesized `__init` is a real member, so it checks `self` like any other method
class PrimaryPoint(x: number, y: number) end
class PrimaryOther(x: number) end

expectfail(
    "the synthesized '__init' rejects a receiver of the wrong class",
    "attempt to call method 'PrimaryPoint.__init' with 'self' of class 'PrimaryOther'",
    function()
        return PrimaryPoint.__init(PrimaryOther(1), 1, 2)
    end
)

expectpass("the synthesized '__init' can be called on an instance of its own class", function()
    local p = PrimaryPoint(1, 2)
    p:__init(3, 4)
    assert(p.x == 3 and p.y == 4)
end)

-- a class with a primary constructor has no table constructor: the table is just argument one
class PrimaryPackage(owner: string, contents: number) end

expectpass("a table passed to a primary constructor is an ordinary argument", function()
    local pkg = PrimaryPackage({ owner = "x", contents = 1 } :: any)
    assert(type(pkg.owner) == "table")
    assert(pkg.contents == nil)
end)

-- an empty parameter list is not the same as no parameter list: it takes away the table constructor
local primarySymbolCalls = 0
local function nextPrimarySymbol()
    primarySymbolCalls += 1
    return primarySymbolCalls
end

class PrimarySymbol()
    const id = nextPrimarySymbol()
end

expectpass("a primary constructor with no parameters", function()
    local a = PrimarySymbol()
    local b = PrimarySymbol()
    assert(b.id == a.id + 1)
end)

--
-- A C function is never an authority for private access. Everything a default embedding exposes to
-- Luwu code -- pcall, xpcall, a coroutine body, a table.sort comparator, a string.gsub callback --
-- runs with a Lua frame below it, and the check looks through the C frames to find it. Only the
-- embedder's own C API calls, which have no Lua frame at all, still bypass.
--

class PrivateBypass private (secret: string)
    private const secret

    public function make(s: string)
        return PrivateBypass(s)
    end

    public function reveal(self)
        return self.secret
    end
end

expectpass("a private constructor and field are reachable from inside the class", function()
    local v = PrivateBypass.make("shh")
    assert(v:reveal() == "shh")
end)

expectpass("pcall cannot launder a private constructor", function()
    local ok, err = pcall(PrivateBypass, "hax")
    assert(not ok, "pcall constructed a class with a private constructor")
    assert(string.find(err, "'__init' is a private member of 'PrivateBypass'", 1, true), err)
end)

expectpass("xpcall cannot launder a private constructor", function()
    local ok, err = xpcall(PrivateBypass, function(e)
        return e
    end, "hax")
    assert(not ok, "xpcall constructed a class with a private constructor")
    assert(string.find(err, "'__init' is a private member of 'PrivateBypass'", 1, true), err)
end)

expectpass("a coroutine cannot launder a private constructor", function()
    local ok, err = coroutine.resume(coroutine.create(function()
        return PrivateBypass("hax")
    end))
    assert(not ok, "a coroutine constructed a class with a private constructor")
    assert(string.find(err, "'__init' is a private member of 'PrivateBypass'", 1, true), err)
end)

expectpass("a C function callback cannot launder a private field read", function()
    local v = PrivateBypass.make("shh")

    local sorted, sortErr = pcall(function()
        local t = {v, v}
        table.sort(t, function(a, b)
            return a.secret < b.secret
        end)
    end)
    assert(not sorted, "a table.sort comparator read a private field")
    assert(string.find(sortErr, "'secret' is a private member of 'PrivateBypass'", 1, true), sortErr)

    local gsubbed, gsubErr = pcall(function()
        return (string.gsub("a", "a", function()
            return v.secret
        end))
    end)
    assert(not gsubbed, "a string.gsub callback read a private field")
    assert(string.find(gsubErr, "'secret' is a private member of 'PrivateBypass'", 1, true), gsubErr)
end)

expectpass("native code reading a private member through the C API is allowed", function()
    local v = PrivateBypass.make("shh")

    -- Deliberate, and the counterpart to the tests above: native code that performs the access itself
    -- is trusted, whether that is the embedder calling in or a cdylib plugin Luwu code called. It
    -- holds the C API and the LuauObject pointer besides, so `private` was never a boundary it could
    -- be held to; an embedder whose own internals are partly Luwu (and a debugger plugin) depends on
    -- this working. What is *not* allowed is Luwu code reaching a private member through a builtin,
    -- since a builtin performs no access of its own.
    assert(pluginReadMember(v, "secret") == "shh")
    assert(pluginReadMemberOnNewThread(v, "secret") == "shh")

    -- public members read the same way, of course
    assert(pluginReadMember(PrimaryCat("Taz", 14), "name") == "Taz")
end)

--
-- A statically resolved construction of a class with a primary constructor doesn't call the
-- synthesized `__init` at all: the parameters are evaluated into registers, each field initializer is
-- compiled at the construction site, and NEWOBJECT's positional FIELDS form finishes the instance.
-- These are the cases where that inlining could plausibly diverge from the call it replaces.
--

class PrimaryInlineDefaults(x = 5, y = 6) end

expectpass("an argument that is nil at runtime still takes the parameter's default", function()
    -- not a constant the compiler can see through: the default has to be applied at runtime
    local absent = ({})[1]
    local d = PrimaryInlineDefaults(absent, 9)

    assert(d.x == 5)
    assert(d.y == 9)
end)

class PrimaryInlineNest(v: number)
    doubled = v * 2
end

expectpass("constructing a class inside its own constructor's arguments", function()
    local n = PrimaryInlineNest(PrimaryInlineNest(3).doubled)

    assert(n.v == 6)
    assert(n.doubled == 12)
end)

-- a parameter read by more than one field initializer, and one restated by the class body: neither
-- can be evaluated straight into a field register the way a plain parameter field is
class PrimaryInlineMulti(w: number, public h: number)
    public area = w * h
    private const w
    public perim = 2 * (w + h)

    public function get_w(self)
        return self.w
    end
end

expectpass("a parameter read by several field initializers", function()
    local m = PrimaryInlineMulti(3, 4)

    assert(m.area == 12)
    assert(m.perim == 14)
    assert(m.h == 4)
    assert(m:get_w() == 3)
end)

class PrimaryInlineMaybe(v: number?)
    got = v
end

expectpass("a field initializer that evaluates to nil stays nil", function()
    assert(PrimaryInlineMaybe(nil).got == nil)
    assert(PrimaryInlineMaybe(7).got == 7)
end)

-- wider than the positional form's field cap, so this one falls back to calling `__init`
class PrimaryInlineWide(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18) end

expectpass("a class too wide for the positional form still constructs", function()
    local w = PrimaryInlineWide(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18)

    assert(w.a1 == 1)
    assert(w.a18 == 18)
end)

-- an initializer that reads an upvalue can't be compiled at the construction site (the site has no
-- way to name it), so this one falls back to calling `__init` too
local primaryInlineFactor = 10

class PrimaryInlineCapture(v: number)
    scaled = v * primaryInlineFactor
end

expectpass("a field initializer that captures an upvalue still constructs", function()
    assert(PrimaryInlineCapture(3).scaled == 30)
end)

return 'OK'

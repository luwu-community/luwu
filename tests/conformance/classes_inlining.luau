--!nocheck
-- Luwu Classes (rfcs/classes.md): method inlining must never relocate a private access into a
-- closure that does not own it.
--
-- At O2 the compiler inlines `obj:method()` bodies. Every runtime-checked `private` access -- a field, a
-- method, a static, the constructor -- is authorized against the *executing closure's*
-- `Proto::ownerclass` (luaR_closureownsprivateaccess), so splicing such a body into a foreign closure
-- makes the check judge it against the wrong class and reject code that was legal where it was written.
--
-- A method of a class with private members may still be inlined into outside code when its body's private
-- access doesn't depend on the running closure: it touches private fields only through its own `self`,
-- which the inline site's CHECKSELFCLASS proves, so they compile to constant offsets with no runtime check
-- (InlinedPrivateAccessVisitor in Compiler.cpp). Anything else keeps the call. Cases 1-4 are bodies that
-- must keep the call; 7 onwards cover bodies that inline and must behave exactly like the call.
--
-- MUST BE RUN AT -O2. At O1 the inliner never runs and every case here passes vacuously.

local function expectpass(s, f)
    f()
    print(`ok: {s}`)
end

-- 1. private primary constructor
class PrimaryCtor private (public x: number)
    public function make(x: number): PrimaryCtor
        return PrimaryCtor(x)
    end

    public function clone(self): PrimaryCtor
        -- constructing Foo inside Foo is a private-__init access; if this body is inlined into
        -- another class it is judged against that class instead
        local c = PrimaryCtor(self.x)
        return c
    end
end

class PrimaryHolder(public inner: PrimaryCtor)
    public function dup(self): number
        local c = self.inner:clone()
        return c.x
    end
end

expectpass("a private primary constructor blocks cross-class inlining of its methods", function()
    local h = PrimaryHolder(PrimaryCtor.make(7))
    assert(h:dup() == 7)
end)

-- 2. explicit `private function __init`
class InitCtor
    public x: number

    private function __init(self, x: number)
        self.x = x
    end

    public function make(x: number): InitCtor
        return InitCtor(x)
    end

    public function clone(self): InitCtor
        local c = InitCtor(self.x)
        return c
    end
end

class InitHolder
    inner: InitCtor

    function __init(self, inner: InitCtor)
        self.inner = inner
    end

    function dup(self): number
        local c = self.inner:clone()
        return c.x
    end
end

expectpass("an explicit private __init blocks cross-class inlining of its methods", function()
    local h = InitHolder(InitCtor.make(9))
    assert(h:dup() == 9)
end)

-- 3. private instance method reached through a public one
class PrivateMethod
    public x: number

    private function secret(self): number
        return self.x * 2
    end

    public function pub(self): number
        local v = self:secret()
        return v
    end
end

class MethodHolder
    inner: PrivateMethod

    function __init(self, inner: PrivateMethod)
        self.inner = inner
    end

    function go(self): number
        local v = self.inner:pub()
        return v
    end
end

expectpass("a private method blocks cross-class inlining of the public method that calls it", function()
    local h = MethodHolder(PrivateMethod { x = 21 })
    assert(h:go() == 42)
end)

-- 4. private static reached through a public method
class PrivateStatic
    public x: number

    private function helper(v: number): number
        return v + 100
    end

    public function pub(self): number
        local v = PrivateStatic.helper(self.x)
        return v
    end
end

class StaticHolder
    inner: PrivateStatic

    function __init(self, inner: PrivateStatic)
        self.inner = inner
    end

    function go(self): number
        local v = self.inner:pub()
        return v
    end
end

expectpass("a private static blocks cross-class inlining of the public method that calls it", function()
    local h = StaticHolder(PrivateStatic { x = 5 })
    assert(h:go() == 105)
end)

-- 5. an all-public class must STILL be inlinable across classes -- the gate has to stay narrow, or
-- it would cost the hot path in real code (vecphysics inlines Vector2 methods into Particle's)
class Vec2(x: number, y: number)
    function add(self, other: Vec2): Vec2
        return Vec2(self.x + other.x, self.y + other.y)
    end
end

class Body(public pos: Vec2)
    public function shift(self, by: Vec2): number
        local p = self.pos:add(by)
        return p.x + p.y
    end
end

expectpass("an all-public class stays inlinable into another class's body", function()
    local b = Body(Vec2(1, 2))
    assert(b:shift(Vec2(10, 20)) == 33)
end)

-- 6. same-class inlining is always allowed, private members or not
class SameClass
    public n: number

    private function bump(self): number
        return self.n + 1
    end

    public function twice(self): number
        local a = self:bump()
        local b = self:bump()
        return a + b
    end
end

expectpass("same-class inlining still works when the class has private members", function()
    assert(SameClass { n = 4 }:twice() == 10)
end)

-- 7. private fields through the inlined method's own `self`: inlined into outside code, and still correct
class Counter private (private count: number)
    public function new(): Counter
        return Counter(0)
    end

    public function bump(self): number
        self.count += 1
        return self.count
    end

    public function peek(self): number
        return self.count
    end
end

expectpass("a method touching private fields only through self inlines into outside code", function()
    local c: Counter = Counter.new()
    for _ = 1, 5 do
        c:bump()
    end
    assert(c:peek() == 5)
end)

-- 8. an annotation that lies about the receiver still can't reach a private field at a constant offset:
-- the inline site's CHECKSELFCLASS rejects the object before the body runs, even when the impostor has
-- the same layout
class Impostor(public count: number)
end

local function peekCounter(counter: Counter): number
    local count = counter:peek()
    return count
end

expectpass("a lying annotation is rejected before an inlined body reads a private field", function()
    local ok, result = pcall(peekCounter, Impostor(41))
    assert(not ok, `reading through a lying annotation must fail, got {result}`)
    assert(string.find(result, "Counter", 1, true), result)
end)

-- 9. bodies whose private access stays runtime-checked must keep the call and behave as written
class Config private (private token: string, public name: string)
    public function new(token: string, name: string): Config
        return Config(token, name)
    end

    -- a runtime key could name a private member
    public function get(self, key: string): string
        return self[key]
    end

    -- a private field on something other than `self`
    public function sameToken(self, other: Config): boolean
        return self.token == other.token
    end

    -- a private field reached from a nested function
    public function tokenReader(self): () -> string
        return function()
            return self.token
        end
    end
end

expectpass("reflective, other-receiver and nested private access keep working at O2", function()
    local a: Config = Config.new("abc", "a")
    local b: Config = Config.new("abc", "b")
    assert(a:get("token") == "abc")
    assert(a:get("name") == "a")
    assert(a:sameToken(b))
    assert(a:tokenReader()() == "abc")
end)

-- 10. a runtime key on a field declared as a table is fine to inline
class Bag private (private items: { number })
    public function new(): Bag
        return Bag({ 10, 20, 30 })
    end

    public function at(self, i: number): number
        return self.items[i]
    end
end

expectpass("a runtime index into a table-typed private field inlines and reads correctly", function()
    local bag: Bag = Bag.new()
    local sum = 0
    for i = 1, 3 do
        sum += bag:at(i)
    end
    assert(sum == 60)
end)

-- 11. a method calling another method on its own `self` inside an inlined body
class Pair private (private a: number, private b: number)
    public function new(a: number, b: number): Pair
        return Pair(a, b)
    end

    public function first(self): number
        return self.a
    end

    public function total(self): number
        return self:first() + self.b
    end
end

expectpass("nested method calls on an inlined self keep private access", function()
    local p: Pair = Pair.new(3, 4)
    assert(p:total() == 7)
end)

-- 12. inlining a function that contains a closure into a class method must not hand that closure's
-- (shared) proto the class's private access: the closure is outside code wherever it is created
local function makeSecretReader()
    return function(o)
        return o.secret
    end
end

class Vault private (private secret: string)
    public function new(): Vault
        return Vault("hunter2")
    end

    public function touch(self): number
        local _reader = makeSecretReader()
        return 1
    end
end

expectpass("a closure inlined into a class method doesn't gain private access elsewhere", function()
    local v = Vault.new()
    assert(v:touch() == 1)
    local reader = makeSecretReader()
    local ok, err = pcall(reader, v)
    assert(not ok, "outside code read a private field")
    assert(string.find(err, "private", 1, true), err)
end)

-- 13. a private method called through a typed receiver from outside the class must still raise: the call itself
-- is the private access, so inlining it would skip the check (found by tests/classes_fuzz)
class Guarded
    public n: number

    private function secret(self): number
        return self.n
    end
end

local function callSecret(g: Guarded): number
    local r = g:secret()
    return r
end

expectpass("a private method can't be called from outside through an inlined typed call", function()
    local ok, err = pcall(callSecret, Guarded { n = 5 })
    assert(not ok, "a private method was callable from outside at O2")
    assert(string.find(err, "private", 1, true), err)
end)

return 'OK'

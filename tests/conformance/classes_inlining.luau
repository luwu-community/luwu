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

-- 14. a local initialized by constructing a class is that class's instance with no annotation, so its
-- method calls inline (classFromConstruction). Behaviour must be identical to the annotated and the
-- reassigned cases, and the privacy rules must not loosen just because the receiver resolved without
-- the user writing a type.
class Counted
    public n: number

    private function bump(self, by: number): number
        self.n += by
        return self.n
    end

    public function add(self, by: number): number
        local r = self:bump(by)
        return r
    end
end

expectpass("an unannotated construction-initialized local inlines its method calls", function()
    local c = Counted { n = 0 }
    local a = c:add(2)
    local b = c:add(3)
    assert(a == 2 and b == 5, `{a}, {b}`)
    assert(c.n == 5, tostring(c.n))

    -- the same class reached through a reassigned local, which does not resolve statically
    local d = Counted { n = 0 }
    d = Counted { n = 10 }
    assert(d:add(1) == 11, tostring(d.n))
end)

expectpass("resolving a receiver through its construction does not grant private access", function()
    local c = Counted { n = 0 }
    local ok, err = pcall(function()
        return c:bump(1)
    end)
    assert(not ok, "a private method was callable from outside through an unannotated local")
    assert(string.find(err, "private", 1, true), err)
end)

expectpass("a constructed instance passed to an inlined function resolves there too", function()
    local function twice(counter, by: number): number
        counter:add(by)
        local r = counter:add(by)
        return r
    end

    assert(twice(Counted { n = 0 }, 4) == 8)
end)

-- 15. a `class.isinstance` branch proves the receiver's exact class at runtime, so method calls inside it
-- inline *and* skip the inline site's CHECKSELFCLASS (JUMPXISA already did that work on this path).
-- Because the check is gone, the proof itself is now load-bearing: an object of another class with the
-- same layout must never reach the branch.
class Refined
    public n: number

    private function hidden(self): number
        return self.n * 3
    end

    public function twice(self): number
        return self.n * 2
    end
end

class RefinedImpostor
    public n: number

    public function twice(self): number
        return self.n * 100
    end
end

local function describe(p): number
    if class.isinstance(p, Refined) then
        local r = p:twice()
        return r + p.n
    elseif class.isinstance(p, RefinedImpostor) then
        local r = p:twice()
        return r
    end
    return -1
end

expectpass("a class.isinstance-proven receiver inlines its method calls", function()
    assert(describe(Refined { n = 4 }) == 12, tostring(describe(Refined { n = 4 })))
    assert(describe(RefinedImpostor { n = 4 }) == 400, tostring(describe(RefinedImpostor { n = 4 })))
    assert(describe("not an object") == -1)
    assert(describe(nil) == -1)
    assert(describe({ n = 4 }) == -1)
end)

expectpass("a proven receiver still can't call a private method from outside", function()
    local ok, err = pcall(function()
        local p = Refined { n = 1 }
        if class.isinstance(p, Refined) then
            return p:hidden()
        end
        return 0
    end)
    assert(not ok, "a private method was callable from outside a proven branch")
    assert(string.find(err, "private", 1, true), err)
end)

expectpass("the proof does not escape into a nested closure", function()
    local function later(p)
        if class.isinstance(p, Refined) then
            return function()
                return p:twice()
            end
        end
        return function()
            return -1
        end
    end

    assert(later(Refined { n = 3 })() == 6)
    assert(later(RefinedImpostor { n = 3 })() == -1)
end)

-- 16. the two receiver tiers. A *proven* receiver (construction, `class.isinstance`, a method's own
-- checked `self`) inlines with no CHECKSELFCLASS at the site; a *trusted* one (a declared local or field
-- type) inlines but keeps it, and that check is what makes a lying annotation raise. Both must produce
-- the same answers as an ordinary call.
class Tier(public n: number)
    public function double(self): number
        return self.n * 2
    end
end

class TierImpostor(public n: number)
    public function double(self): number
        return self.n * 1000
    end
end

class TierHolder(public inner: Tier)
    public function viaField(self): number
        local r = self.inner:double()
        return r
    end
end

expectpass("a proven receiver inlines correctly without a self check", function()
    local t = Tier(21)
    assert(t:double() == 42)

    -- an inlined function's parameter is proven by the argument it was given
    local function twice(v, by: number): number
        local a = v:double()
        return a + by
    end
    assert(twice(Tier(5), 1) == 11)
end)

expectpass("a trusted receiver keeps the check that makes a lying annotation raise", function()
    local h = TierHolder(Tier(3))
    assert(h:viaField() == 6)

    -- nothing checks what a field actually holds, so the inline site's check has to
    h.inner = TierImpostor(3) :: any
    local ok, err = pcall(h.viaField, h)
    assert(not ok, "a lying field type reached an inlined body")
    assert(string.find(err, "Tier", 1, true), err)
end)

-- 17. the `class.isinstance` proof is bounded by the branch it guards, not by whether the local is ever
-- written. A write the proof cannot see coming -- inside the branch, or from a closure -- must unprove it;
-- a write that cannot reach a use inside the branch must not cost the optimization. Since a proven
-- receiver inlines with no CHECKSELFCLASS, getting this wrong reads constant field offsets off whatever
-- the local now holds, so every case here checks the *answer*, not just that it ran.
class Meower(public sound: string)
    public function speak(self): string
        return self.sound
    end
end

class OtherMeower(public sound: string)
    public function speak(self): string
        return "other:" .. self.sound
    end
end

-- write after the branch: the proof stands inside it
local function writeAfter(c): string
    local out = "none"
    if class.isinstance(c, Meower) then
        out = c:speak() .. "/" .. c.sound
    end
    c = nil
    return out
end

-- write before the branch: the check tests whatever the local holds by then
local function writeBefore(c, replacement): string
    c = replacement
    if class.isinstance(c, Meower) then
        local r = c:speak()
        return r
    end
    return "none"
end

-- the write is outside the branch but inside a loop, so the check re-runs every iteration
local function writeInEnclosingLoop(a, b): string
    local c = a
    local out = ""
    for _ = 1, 4 do
        if class.isinstance(c, Meower) then
            local r = c:speak()
            out ..= r
        else
            out ..= "-"
        end
        c = if c == a then b else a
    end
    return out
end

-- the write is inside the branch, so the second iteration must dispatch to the new value's class
local function writeInsideBranch(c, other): string
    if class.isinstance(c, Meower) then
        local out = ""
        for _ = 1, 2 do
            local r = c:speak()
            out ..= r
            c = other
        end
        return out
    end
    return "none"
end

-- a closure can run at any point, so a local it writes is never proven
local function writeFromClosure(c, other): string
    if class.isinstance(c, Meower) then
        local swap = function()
            c = other
        end
        swap()
        local r = c:speak()
        return r
    end
    return "none"
end

-- the same, but the closure is declared *outside* the branch, so its write is not in the region and only
-- the "written by a nested function" fact rules it out. Compiled without that fact this fails an assert.
local function writeFromOuterClosure(c, other): string
    local swap = function()
        c = other
    end
    if class.isinstance(c, Meower) then
        swap()
        local r = c:speak()
        return r
    end
    return "none"
end

expectpass("a write that cannot reach the branch does not cost the proof", function()
    assert(writeAfter(Meower("mew")) == "mew/mew", writeAfter(Meower("mew")))
    assert(writeAfter(OtherMeower("mew")) == "none")
    assert(writeAfter(nil) == "none")

    assert(writeBefore(Meower("a"), Meower("b")) == "b")
    assert(writeBefore(Meower("a"), OtherMeower("b")) == "none")
    assert(writeBefore(Meower("a"), nil) == "none")

    assert(writeInEnclosingLoop(Meower("x"), OtherMeower("y")) == "x-x-", writeInEnclosingLoop(Meower("x"), OtherMeower("y")))
    assert(writeInEnclosingLoop(OtherMeower("y"), Meower("x")) == "-x-x")
    assert(writeInEnclosingLoop(Meower("x"), nil) == "x-x-")
end)

expectpass("a write the proof cannot see coming unproves the receiver", function()
    -- the second iteration must reach OtherMeower's body, not Meower's at a constant offset
    assert(writeInsideBranch(Meower("a"), OtherMeower("b")) == "aother:b", writeInsideBranch(Meower("a"), OtherMeower("b")))

    local ok = pcall(writeInsideBranch, Meower("a"), nil)
    assert(not ok, "a nil assigned inside the branch was called as if still proven")

    assert(writeFromClosure(Meower("a"), OtherMeower("b")) == "other:b", writeFromClosure(Meower("a"), OtherMeower("b")))

    local closureOk = pcall(writeFromClosure, Meower("a"), nil)
    assert(not closureOk, "a nil assigned by a closure was called as if still proven")

    assert(writeFromOuterClosure(Meower("a"), OtherMeower("b")) == "other:b", writeFromOuterClosure(Meower("a"), OtherMeower("b")))

    local outerOk = pcall(writeFromOuterClosure, Meower("a"), nil)
    assert(not outerOk, "a nil assigned by a closure declared outside the branch was called as if still proven")
end)

-- 18. `assert(class.isinstance(c, C))` proves `c` for the rest of the block, with or without a message:
-- assert raises on a falsy condition, so the statements after it run only when the same check a branch
-- performs passed. The receiver is proven, so it inlines with no self check -- and the same two kinds of
-- write must take that away.
class Asserted(public tone: string)
    public function speak(self): string
        return self.tone
    end
end

class AssertedOther(public tone: string)
    public function speak(self): string
        return "other:" .. self.tone
    end
end

local function afterAssert(c): string
    assert(class.isinstance(c, Asserted))
    local r = c:speak()
    return r .. "/" .. c.tone
end

local function afterAssertWithMessage(c): string
    assert(class.isinstance(c, Asserted), "expected an Asserted")
    local r = c:speak()
    return r
end

local function writeAfterAssert(c, other): string
    assert(class.isinstance(c, Asserted))
    c = other
    local r = c:speak()
    return r
end

local function writeFromClosureAfterAssert(c, other): string
    local swap = function()
        c = other
    end
    assert(class.isinstance(c, Asserted))
    swap()
    local r = c:speak()
    return r
end

local function assertInsideIf(c, flag): string
    if flag then
        assert(class.isinstance(c, Asserted))
        local r = c:speak()
        return r
    end
    local r = c:speak()
    return r
end

expectpass("assert(class.isinstance(...)) proves the receiver for the rest of the block", function()
    assert(afterAssert(Asserted("mew")) == "mew/mew", afterAssert(Asserted("mew")))
    assert(afterAssertWithMessage(Asserted("mew")) == "mew")

    -- a value that fails the check never reaches the proven code
    local ok, err = pcall(afterAssert, AssertedOther("mew"))
    assert(not ok and string.find(err, "assertion failed", 1, true), tostring(err))
    assert(not (pcall(afterAssert, nil)))
    assert(not (pcall(afterAssert, 5)))

    local msgOk, msgErr = pcall(afterAssertWithMessage, AssertedOther("mew"))
    assert(not msgOk and string.find(msgErr, "expected an Asserted", 1, true), tostring(msgErr))
end)

expectpass("a fused assert raises exactly as the call it replaces would", function()
    -- the check compiles to a JUMPXISA over the assert call, so the failure path is the original call:
    -- same message, same custom message, and the arguments must survive being read twice
    local ok, err = pcall(function()
        local c = 5
        assert(class.isinstance(c, Asserted))
        return "unreachable"
    end)
    assert(not ok and string.find(err, "assertion failed", 1, true), tostring(err))

    local msgOk, msgErr = pcall(function()
        local c = nil
        assert(class.isinstance(c, Asserted), "custom message")
        return "unreachable"
    end)
    assert(not msgOk and string.find(msgErr, "custom message", 1, true), tostring(msgErr))

    -- an operand with side effects is evaluated exactly once, pass or fail
    local calls = 0
    local function counted(v)
        calls += 1
        return v
    end

    assert(pcall(function()
        assert(class.isinstance(counted(Asserted("x")), Asserted))
    end))
    assert(calls == 1, `evaluated {calls} times on success`)

    calls = 0
    assert(not (pcall(function()
        assert(class.isinstance(counted(5), Asserted))
    end)))
    assert(calls == 1, `evaluated {calls} times on failure`)
end)

expectpass("a write after the assert takes the proof away again", function()
    assert(writeAfterAssert(Asserted("a"), AssertedOther("b")) == "other:b", writeAfterAssert(Asserted("a"), AssertedOther("b")))
    assert(not (pcall(writeAfterAssert, Asserted("a"), nil)))

    assert(
        writeFromClosureAfterAssert(Asserted("a"), AssertedOther("b")) == "other:b",
        writeFromClosureAfterAssert(Asserted("a"), AssertedOther("b"))
    )
    assert(not (pcall(writeFromClosureAfterAssert, Asserted("a"), nil)))

    -- the proof ends with the block the assert is in
    assert(assertInsideIf(Asserted("a"), true) == "a")
    assert(assertInsideIf(AssertedOther("a"), false) == "other:a")
    assert(not (pcall(assertInsideIf, AssertedOther("a"), true)))
end)

return 'OK'

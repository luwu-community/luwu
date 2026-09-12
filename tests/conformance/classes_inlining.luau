--!nocheck
-- Luwu Classes (rfcx/classes.md): method inlining must never relocate a private access into a
-- closure that does not own it.
--
-- At O2 the compiler inlines `obj:method()` bodies. Every `private` access -- a field, a method, a
-- static, the constructor -- is authorized at runtime against the *executing closure's*
-- `Proto::ownerclass` (luaR_closureownsprivateaccess), so splicing such a body into a foreign
-- closure makes the check judge it against the wrong class and reject code that was legal where it
-- was written. `tryResolveMethodCall`'s gate exists to prevent that: a class with anything private
-- is not an inline target for a *different* class's body (same-class inlining is always fine,
-- because the ownerclass is preserved).
--
-- The gate used to be keyed on private *properties* only, so a class whose sole private thing was
-- its constructor or a method sailed through and failed at runtime. Every class below therefore has
-- NO private fields -- that is the whole point; adding one would mask the bug being tested.
--
-- MUST BE RUN AT -O2. At O1 the inliner never runs and every case here passes vacuously.

local function expectpass(s, f)
    f()
    print(`ok: {s}`)
end

-- 1. private primary constructor
class PrimaryCtor private (x: number)
    public x: number

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

class PrimaryHolder(inner: PrimaryCtor)
    public inner: PrimaryCtor

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

class Body(pos: Vec2)
    public pos: Vec2

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

return 'OK'

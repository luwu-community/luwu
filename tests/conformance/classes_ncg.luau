--!nocheck
-- Native codegen coverage for class construction and class declaration. This lives in its own file
-- rather than in classes.luau because that file is past codegen's total-IR-instruction budget: its
-- protos are never natively compiled, so it cannot catch a lowering bug at all.
--
-- Neither LOP_NEWOBJECT nor LOP_NEWCLASSMEMBER has a machine code lowering; both run through a C
-- fallback. They used to be unconditional exits to the interpreter instead, which carried no register
-- liveness information: stores to registers the rest of the bytecode still reads -- a numeric for
-- loop's limit/step/index, established before the loop body and never touched by construction -- were
-- eliminated as dead, and the interpreter resumed on top of garbage. That showed up as an endless
-- loop, a bogus loop variable, or an assertion failure in FORNLOOP.

local function expectpass(s, f)
    f()
    print(`ok: {s}`)
end

class Point
    x: number
    y: number
end

local function constructInLoop(count: number)
    local seen = 0
    local last = nil

    for i = 1, count do
        local p = Point{ x = i, y = i * 2 }

        seen += 1
        last = p

        assert(i == seen, `loop index was clobbered: expected {seen}, got {i}`)
        assert(seen <= count, "loop ran past its limit")
    end

    return seen, last
end

expectpass("constructing inside a numeric for loop keeps the loop registers intact", function()
    local seen, last = constructInLoop(4)

    assert(seen == 4)
    assert(last.x == 4)
    assert(last.y == 8)
end)

-- the table form (an argument the constructor reads fields off) rather than the positional one
local function constructFromTableInLoop(count: number)
    local seen = 0
    local fields = { x = 0, y = 0 }

    for i = 1, count do
        fields.x = i
        local p = Point(fields)

        seen += 1
        assert(i == seen, `loop index was clobbered: expected {seen}, got {i}`)
        assert(p.x == i)
    end

    return seen
end

expectpass("constructing from a table argument inside a numeric for loop is unaffected too", function()
    assert(constructFromTableInLoop(4) == 4)
end)

class Counted
    n: number

    function __init(self, n: number)
        self.n = n * 2
    end

    function get(self)
        return self.n
    end
end

local function constructWithInitInLoop(count: number)
    local seen = 0

    for i = 1, count do
        local c = Counted(i)

        seen += 1
        assert(i == seen, `loop index was clobbered: expected {seen}, got {i}`)
        assert(c:get() == i * 2)
    end

    return seen
end

expectpass("constructing through a custom __init inside a numeric for loop is unaffected too", function()
    assert(constructWithInitInLoop(4) == 4)
end)

-- LOP_NEWCLASSMEMBER runs in the main chunk, so the registers it must not disturb are the chunk's
-- own locals, live across the declaration below.
local before1 = 11
local before2 = "twelve"
local before3 = { 13 }

class Declared
    v: number = 1

    function bump(self)
        return self.v + 1
    end
end

expectpass("declaring a class with methods leaves the surrounding locals intact", function()
    assert(before1 == 11)
    assert(before2 == "twelve")
    assert(before3[1] == 13)
    assert(Declared{ v = 41 }:bump() == 42)
end)

-- LOP_NEWOBJECT is now emitted for a class with a private constructor too, which means the opcode --
-- not luaR_createobject's C frame -- is what enforces "a private constructor is only callable from
-- inside its own class". The authority is the executing closure's Proto::ownerclass. NEWOBJECT is
-- only emitted for a direct, statically resolved `ClassName(...)` callee, so pcall-laundered and
-- aliased calls still go through luaR_createobject; both must stay denied either way.
class Acct private (public owner: string, balance: number)
    private balance

    public function new(o: string, b: number): Acct
        return Acct(o, b)
    end

    public function nested(o: string, b: number): Acct
        -- a closure lexically inside a method inherits ownerclass (luaR_stampownerclass is recursive)
        local f = function()
            return Acct(o, b)
        end
        return f()
    end

    public function bal(self): number
        return self.balance
    end
end

class ExplicitInit
    public x: number

    private function __init(self, x: number)
        self.x = x
    end

    public function new(x: number): ExplicitInit
        return ExplicitInit(x)
    end
end

expectpass("a private constructor is callable from inside its own class", function()
    assert(Acct.new("a", 10):bal() == 10)
    assert(Acct.nested("b", 20):bal() == 20)
    assert(ExplicitInit.new(5).x == 5)
end)

local function denied(f, what)
    local ok, err = pcall(f)
    assert(not ok, `{what} was ALLOWED`)
    assert(type(err) == "string" and string.find(err, "private", 1, true), `{what}: unexpected error {err}`)
end

expectpass("a private constructor is not callable from outside, by any route", function()
    -- the direct call is the one that compiles to NEWOBJECT
    denied(function()
        return Acct("c", 30)
    end, "direct call from outside")

    denied(function()
        return ExplicitInit(1)
    end, "direct call to a private __init from outside")

    -- these never become NEWOBJECT sites; they must stay denied on the luaR_createobject path.
    -- pcall is checked directly rather than through `denied`: it catches the error instead of
    -- propagating it, so wrapping it in another pcall would report success either way.
    local laundered, launderErr = pcall(Acct, "d", 40)
    assert(not laundered, "pcall laundering was ALLOWED")
    assert(type(launderErr) == "string" and string.find(launderErr, "private", 1, true), `pcall laundering: unexpected error {launderErr}`)

    local alias = Acct
    denied(function()
        return alias("e", 50)
    end, "aliased call")

    local function outsideHelper()
        return Acct("f", 60)
    end
    denied(outsideHelper, "a plain local function outside the class")
end)

return 'OK'

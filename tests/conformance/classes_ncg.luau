--!nocheck
-- Native codegen coverage for classes. This lives in its own file
-- rather than in classes.luau because that file contains `@native` functions, and a module with any
-- `@native` function natively compiles only those (gatherFunctionsHelper in CodeGenLower.h): the rest of
-- its protos stay interpreted even under --codegen, so it cannot catch a lowering bug in them. Don't add
-- `@native` here.
--
-- LOP_NEWCLASSMEMBER and non-native LOP_NEWOBJECT forms run through a C fallback. They used to be
-- unconditional exits to the interpreter instead, which carried no register liveness information: stores
-- to registers the rest of the bytecode still reads -- a numeric for loop's limit/step/index,
-- established before the loop body and never touched by construction -- were eliminated as dead, and the
-- interpreter resumed on top of garbage. That showed up as an endless loop, a bogus loop variable, or an
-- assertion failure in FORNLOOP.

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

-- A class with `const` fields blocks reading its `__init` (rfcs/classes.md). From natively compiled code
-- too, and from the class's own methods, which are authorized for its private members.
class Frozen
    public const id: number
    private secret: number

    public function __init(self, id: number)
        self.id = id
        self.secret = id
    end

    public function reinit(self, id: number)
        self:__init(id)
    end

    public function peekInit(self)
        return Frozen.__init
    end

    public function peekSecret(self): number
        return self.secret
    end
end

local function blocked(f, what)
    for _ = 1, 3 do
        local ok, err = pcall(f)
        assert(not ok, `{what} was ALLOWED`)
        assert(type(err) == "string" and string.find(err, "cannot be accessed or called explicitly", 1, true), `{what}: unexpected error {err}`)
    end
end

expectpass("a blocked __init is not readable, even from inside its own class", function()
    local f = Frozen(1)
    blocked(function() f:reinit(2) end, "self:__init from a method")
    blocked(function() return f:peekInit() end, "Frozen.__init from a method")
    blocked(function() return f.__init end, "reading __init off an object from outside")
    blocked(function() return Frozen.__init end, "reading __init off the class from outside")
    assert(f.id == 1)
    -- private members of the same class are still authorized natively
    assert(f:peekSecret() == 1)
end)

-- A field access or method call on an untyped receiver lowers with a table path and an object path. When
-- a multret call sits between the receiver and the call consuming its results -- `v:add(a:scale(dt))`
-- evaluates `a:scale(dt)` first and resolves `add` afterwards -- the object path's block has to agree with
-- the table path about where those pending results start, or native compilation of the function fails an
-- IR liveness assertion. Covered for both receiver kinds so both paths of each instruction actually run.
local MtVec = {}
MtVec.__index = MtVec
local function mtvec(x, y) return setmetatable({ x = x, y = y }, MtVec) end
function MtVec.add(self, o) return mtvec(self.x + o.x, self.y + o.y) end
function MtVec.scale(self, s) return mtvec(self.x * s, self.y * s) end

class ObjVec(x: number, y: number)
    function add(self, o) return ObjVec(self.x + o.x, self.y + o.y) end
    function scale(self, s) return ObjVec(self.x * s, self.y * s) end
end

local function step(holder, force, dt)
    holder.velocity = holder.velocity:add(force:scale(dt))
    holder.position = holder.position:add(holder.velocity:scale(dt))
    return holder.position.x + holder.position.y
end

expectpass("untyped field access and method calls around a multret call, on tables and objects", function()
    local mtHolder = { velocity = mtvec(0, 0), position = mtvec(0, 0) }
    local objHolder = { velocity = ObjVec(0, 0), position = ObjVec(0, 0) }
    local mtSum, objSum = 0, 0
    for _ = 1, 50 do
        mtSum += step(mtHolder, mtvec(1, 2), 0.5)
        objSum += step(objHolder, ObjVec(1, 2), 0.5)
    end
    assert(mtSum == objSum and mtSum > 0)
end)

-- `class.isinstance` branches. Same module: the compiler reads fields of the proven local at a constant
-- offset (GETOBJECTMEMBER). Class not statically known (as with an imported class): codegen's type analysis
-- still knows the value is an object in the branch. Private and const rules must hold on both paths.
class IsaNum
    public value: number
    private secret: number = 7
    public const fixed: number = 1

    public function peek(other): number
        -- a method of the class may read a proven local's private field
        if class.isinstance(other, IsaNum) then
            return other.secret
        end
        return -1
    end
end

class IsaAdd
    left: object
    right: object
end

local function isaEval(node): number
    if class.isinstance(node, IsaNum) then
        return node.value
    elseif class.isinstance(node, IsaAdd) then
        return isaEval(node.left) + isaEval(node.right)
    elseif type(node) == "table" then
        return node.value
    end
    return 0
end

local aliases = { Num = IsaNum, Add = IsaAdd }

local function isaEvalAliased(node): number
    if class.isinstance(node, aliases.Num) then
        return node.value
    elseif class.isinstance(node, aliases.Add) then
        return isaEvalAliased(node.left) + isaEvalAliased(node.right)
    elseif type(node) == "table" then
        return node.value
    end
    return 0
end

expectpass("fields read through a class.isinstance branch, known and aliased classes, mixed with tables", function()
    local tree = IsaAdd { left = IsaNum { value = 2 }, right = IsaAdd { left = { value = 3 }, right = IsaNum { value = 4 } } }
    for _ = 1, 50 do
        assert(isaEval(tree) == 9)
        assert(isaEvalAliased(tree) == 9)
    end
end)

expectpass("a proven local's private field stays private outside the class, and readable inside it", function()
    local n = IsaNum { value = 1 }
    for _ = 1, 3 do
        local ok, err = pcall(function(m)
            if class.isinstance(m, IsaNum) then
                return m.secret
            end
        end, n)
        assert(not ok and string.find(err, "private", 1, true), `private read through isinstance: {err}`)
        assert(IsaNum.peek(n) == 7)
    end
end)

expectpass("a proven local's const field still can't be assigned", function()
    local n = IsaNum { value = 1 }
    for _ = 1, 3 do
        local ok, err = pcall(function(m)
            if class.isinstance(m, IsaNum) then
                m.fixed = 2
            end
        end, n)
        assert(not ok and string.find(err, "const", 1, true), `const write through isinstance: {err}`)
    end
end)

expectpass("a proven local's field writes land, and reassigning the local disables the proof", function()
    local n = IsaNum { value = 1 }
    if class.isinstance(n, IsaNum) then
        n.value = 5
    end
    assert(n.value == 5)

    local function swap(x, replacement)
        if class.isinstance(x, IsaNum) then
            x = replacement
            return x.value
        end
        return nil
    end
    for _ = 1, 3 do
        assert(swap(IsaNum { value = 1 }, { value = "table" }) == "table")
    end
end)

expectpass("a nested closure capturing a proven local reads it the ordinary way", function()
    local n = IsaNum { value = 11 }
    for _ = 1, 3 do
        if class.isinstance(n, IsaNum) then
            local read = function()
                return n.value
            end
            assert(read() == 11)
        end
    end
end)

-- Native construction (NEWOBJECT's FIELDS form lowered to NEW_OBJECT + member stores) and every case its
-- guard sends back to the fallback. Each constructs in a loop so the function is natively compiled and
-- both paths run repeatedly.
class NcgConstPrimary(const id: number, label: string) end

class NcgDefaulted
    x: number
    y: number = 5
end

class NcgPrivatePrimary private (public const code: number)
    public function make(code: number): NcgPrivatePrimary
        return NcgPrivatePrimary(code)
    end
end

local function constructMany(count: number): number
    local sum = 0
    for i = 1, count do
        local p = NcgConstPrimary(i, "label")
        local d = NcgDefaulted { x = i }
        local q = NcgPrivatePrimary.make(i)
        local v = Point { x = i, y = i + 1 }
        sum += p.id + d.x + d.y + q.code + v.x + v.y
    end
    return sum
end

expectpass("native construction: primary with const fields, defaulted fields, private primary, POD", function()
    local expected = 0
    for i = 1, 200 do
        expected += i + i + 5 + i + i + (i + 1)
    end
    for _ = 1, 3 do
        assert(constructMany(200) == expected)
    end
end)

expectpass("native construction: const fields stay const, and a private primary constructor stays private", function()
    local p = NcgConstPrimary(1, "a")
    local ok, err = pcall(function(o)
        o.id = 2
    end, p)
    assert(not ok and string.find(err, "const", 1, true), `const write after native construction: {err}`)

    for _ = 1, 3 do
        local okc, errc = pcall(function()
            return NcgPrivatePrimary(1)
        end)
        assert(not okc and string.find(errc, "private", 1, true), `private primary constructor from outside: {errc}`)
    end
end)

expectpass("native construction: values survive a collection right after construction", function()
    local keep = {}
    for i = 1, 300 do
        keep[i] = NcgConstPrimary(i, `label {i}`)
        if i % 50 == 0 then
            collectgarbage("collect")
        end
    end
    for i = 1, 300 do
        assert(keep[i].id == i and keep[i].label == `label {i}`)
    end
end)

-- `class.isinstance(x, C)` as a condition with a class the compiler can't resolve (JUMPXISA CHECKCLASS)
-- must behave exactly like the builtin, including its error for a non-class second argument.
local function isaAgainst(value, maybeClass)
    if class.isinstance(value, maybeClass) then
        return "yes"
    end
    return "no"
end

expectpass("a fused isinstance against a runtime class value matches the builtin", function()
    local n = IsaNum { value = 1 }
    for _ = 1, 3 do
        assert(isaAgainst(n, aliases.Num) == "yes")
        assert(isaAgainst(n, aliases.Add) == "no")
        assert(isaAgainst({}, aliases.Num) == "no")
        assert(isaAgainst(nil, aliases.Num) == "no")
        assert(isaAgainst(IsaNum, aliases.Num) == "no")

        for _, notClass in { "Num", {}, 5, nil, n } do
            local ok, err = pcall(isaAgainst, n, notClass)
            local expected = `invalid argument #2 to 'isinstance' (class expected, got {typeof(notClass)})`
            assert(not ok and string.find(err, expected, 1, true), `fused isinstance error: {err}`)
        end
    end
end)

-- A private primary constructor constructs natively from inside its class -- a method, or a closure
-- nested in one -- and still raises from anywhere else, including another class's methods.
class NcgSecret private (public const id: number)
    public function many(count: number): number
        local sum = 0
        for i = 1, count do
            sum += NcgSecret(i).id
        end
        return sum
    end

    public function viaClosure(count: number): number
        local make = function(i)
            return NcgSecret(i)
        end
        local sum = 0
        for i = 1, count do
            sum += make(i).id
        end
        return sum
    end
end

class NcgIntruder
    public function steal(count: number)
        local sum = 0
        for i = 1, count do
            sum += NcgSecret(i).id
        end
        return sum
    end
end

expectpass("a private primary constructor works natively from its class and nested closures", function()
    for _ = 1, 3 do
        assert(NcgSecret.many(100) == 5050)
        assert(NcgSecret.viaClosure(100) == 5050)
    end
end)

expectpass("a private primary constructor still raises from another class's method", function()
    for _ = 1, 3 do
        local ok, err = pcall(NcgIntruder.steal, 10)
        assert(not ok and string.find(err, "private", 1, true), `private constructor from another class: {err}`)
    end
end)

-- A multret last argument filling a primary constructor's last parameter constructs at the call site
-- truncated to one value; one that falls short of the last parameter still spreads, as a call would.
class NcgSpread(public a: number, public b: number?, public c: number?)
end

local function three()
    return 1, 2, 3
end

class NcgWrapped private (public inner: { number })
    public function make(count: number): NcgWrapped
        return NcgWrapped(table.create(count, 7))
    end
end

expectpass("multret arguments to a primary constructor keep call semantics", function()
    for _ = 1, 3 do
        local full = NcgSpread(9, 8, three())
        assert(full.a == 9 and full.b == 8 and full.c == 1)

        local spread = NcgSpread(three())
        assert(spread.a == 1 and spread.b == 2 and spread.c == 3)

        local middle = NcgSpread(9, three())
        assert(middle.a == 9 and middle.b == 1 and middle.c == 2)

        local early = NcgSpread(three(), 5)
        assert(early.a == 1 and early.b == 5 and early.c == nil)

        local w = NcgWrapped.make(4)
        assert(#w.inner == 4 and w.inner[4] == 7)
    end
end)

-- One NAMECALL site fed alternately by metatable-OOP tables and objects: both paths cache a slot in the same
-- operand, so each call can find the other path's slot there. A table slot hint beyond an object's member
-- count must be rejected by the bounds check rather than read, and every call must still resolve correctly.
local NcgLegacy = {}
NcgLegacy.__index = NcgLegacy

-- enough methods that `describe` can hash to a slot past NcgWidget's few members
for i = 1, 40 do
    NcgLegacy[`filler{i}`] = function()
        return i
    end
end

function NcgLegacy.describe(self)
    return "legacy " .. self.name
end

class NcgWidget(public name: string)
    public function describe(self): string
        return "widget " .. self.name
    end
end

class NcgGadget(public name: string, public extra: number)
    public function other(self): number
        return self.extra
    end

    public function describe(self): string
        return "gadget " .. self.name
    end
end

local function describeAll(items)
    local out = {}
    for i, item in items do
        out[i] = item:describe()
    end
    return out
end

expectpass("a method call site alternating tables and objects resolves every call", function()
    local items = {}
    for i = 1, 30 do
        local kind = i % 3
        if kind == 0 then
            items[i] = setmetatable({ name = `l{i}` }, NcgLegacy)
        elseif kind == 1 then
            items[i] = NcgWidget(`w{i}`)
        else
            items[i] = NcgGadget(`g{i}`, i)
        end
    end

    for _ = 1, 3 do
        local out = describeAll(items)
        for i = 1, 30 do
            local kind = i % 3
            local expected = if kind == 0 then `legacy l{i}` elseif kind == 1 then `widget w{i}` else `gadget g{i}`
            assert(out[i] == expected, `item {i}: {out[i]}`)
        end
    end
end)

return 'OK'

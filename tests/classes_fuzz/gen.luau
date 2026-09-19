--[[
    Random Luwu class program generator for differential and crash fuzzing (see run.luau).

    Written for seal 0.8.1 in the Luau dialect *it* understands -- no Luwu classes here: the fuzzer must not run
    on the build it is testing.

    A generated program is a pair of modules: `main` requires `mod`, so cross-module paths get exercised too.
    `main` prints one line per operation: `<label>\tok\t<value>\t<value>` or `<label>\terr`. The runner compares
    those lines across optimization levels and codegen modes. A label starting with `~` marks an operation whose
    error *status* may legitimately differ between optimization levels -- the documented O2 divergences in
    CLASSES_IMPL_NOTES.CLAUDE.md section 8 (lying receiver annotations, private access laundered by inlining) --
    so for those only values are compared, and only when both sides succeed.

    Programs contain two placeholders the runner fills in per configuration: OPTLEVEL and MODNAME.
]]

local str = require("@std/str")

type Rng = {
    next: () -> number,
    int: (lo: number, hi: number) -> number,
    chance: (p: number) -> boolean,
    pick: <T>(list: { T }) -> T,
    sample: <T>(list: { T }, count: number) -> { T },
}

-- xorshift32: deterministic for a given seed regardless of the host's math.random
local function newRng(seed: number): Rng
    local state = (seed * 2654435761) % 4294967296
    if state == 0 then
        state = 1
    end

    local rng = {} :: Rng

    function rng.next(): number
        state = bit32.bxor(state, bit32.lshift(state, 13))
        state = bit32.bxor(state, bit32.rshift(state, 17))
        state = bit32.bxor(state, bit32.lshift(state, 5))
        return state
    end

    function rng.int(lo: number, hi: number): number
        return lo + rng.next() % (hi - lo + 1)
    end

    function rng.chance(p: number): boolean
        return rng.next() / 4294967296 < p
    end

    function rng.pick<T>(list: { T }): T
        return list[rng.int(1, #list)]
    end

    function rng.sample<T>(list: { T }, count: number): { T }
        local copy = table.clone(list)
        local out = {}
        for _ = 1, math.min(count, #copy) do
            table.insert(out, table.remove(copy, rng.int(1, #copy)))
        end
        return out
    end

    return rng
end

type Field = {
    name: string,
    private: boolean,
    const: boolean,
    kind: FieldKind,
    default: string?,
}

type Method = {
    name: string,
    private: boolean,
    static: boolean,
    body: { string },
    params: { string },
    sketchy: boolean,
    calls: { string },
}

type Metamethod = { name: string, params: { string }, body: { string } }

type FieldKind = "number" | "string" | "table"
type CtorKind = "pod" | "primary" | "init"

type Cls = {
    name: string,
    module: boolean,
    fields: { Field },
    methods: { Method },
    ctor: CtorKind,
    ctorPrivate: boolean,
    generic: boolean,
    primaryParams: { Field },
    metamethods: { Metamethod },
}

const FIELD_NAMES = { "a", "b", "c", "d", "count", "total", "name", "items", "tag", "hp" }
-- "primary" twice: primary constructors are the recommended form, so weight them
const CTOR_KINDS: { CtorKind } = { "pod", "primary", "primary", "init" }
const SHARED_METHODS = { "get", "bump", "peek", "sum", "twice", "label", "size" }
const METHOD_SHAPES = {
    "getter", "bump", "selfcall", "reflect", "other", "construct", "closure", "loop", "isinstance", "error",
    "foreign", "yield", "sumtable", "staticcall", "lying",
}
const OP_KINDS = {
    "construct", "ctor_outside", "read", "write", "call_typed", "call_untyped", "call_dot", "wrong_self", "lying",
    "mixed_site", "isinstance", "classlib", "meta", "init_explicit", "coroutine", "gc", "dynamic_key", "raw",
    "iterate", "private_static", "loop_calls", "fields", "alias_ctor", "table_key",
}

local function lit(rng: Rng, kind: string): string
    if kind == "number" then
        return rng.pick({ "0", "1", "2", "3", "7", "-4", "10", "0.5", "100" })
    elseif kind == "string" then
        return rng.pick({ '"x"', '"hello"', '"Z"', '""' })
    end
    local items = {}
    for _ = 1, rng.int(0, 3) do
        table.insert(items, tostring(rng.int(0, 9)))
    end
    return "{ " .. table.concat(items, ", ") .. " }"
end

local function contains<T>(list: { T }, value: T): boolean
    return table.find(list, value) ~= nil
end

local function filter<T>(list: { T }, keep: (T) -> boolean): { T }
    local out = {}
    for _, v in list do
        if keep(v) then
            table.insert(out, v)
        end
    end
    return out
end

local function method(name: string, private: boolean, static: boolean, body: { string }, params: { string }?, sketchy: boolean?, calls: { string }?): Method
    return {
        name = name,
        private = private,
        static = static,
        body = body,
        params = params or {},
        sketchy = sketchy or false,
        calls = calls or {},
    }
end

local function generate(seed: number): (string, string)
    local rng = newRng(seed)
    local classes: { Cls } = {}
    local ops: { { label: string, lines: { string } } } = {}
    local opCount = 0

    -- how `target` is named from code in `from` (nil: top level of main)
    local function ref(target: Cls, from: Cls?): string
        if target.module and not (from and from.module) then
            return `mod.{target.name}`
        end
        return target.name
    end

    local function publicFields(c: Cls): { Field }
        return filter(c.fields, function(f)
            return not f.private
        end)
    end

    local function privateFields(c: Cls): { Field }
        return filter(c.fields, function(f)
            return f.private
        end)
    end

    local function instanceMethods(c: Cls, publicOnly: boolean): { Method }
        return filter(c.methods, function(m)
            return not m.static and (not publicOnly or not m.private)
        end)
    end

    local function ctorCall(c: Cls, from: Cls?): string
        local name = ref(c, from)
        if c.ctor == "pod" then
            if rng.chance(0.2) then
                return `{name}()`
            end
            local kv = {}
            for _, f in c.fields do
                if rng.chance(0.8) then
                    table.insert(kv, `{f.name} = {lit(rng, f.kind)}`)
                end
            end
            return `{name} \{ {table.concat(kv, ", ")} }`
        end
        local args = {}
        for _, f in if c.ctor == "primary" then c.primaryParams else c.fields do
            table.insert(args, if rng.chance(0.85) then lit(rng, f.kind) else "nil")
        end
        if c.ctor == "primary" and #args > 0 and rng.chance(0.2) then
            args[#args] = "multi()"
        end
        return `{name}({table.concat(args, ", ")})`
    end

    local function makeClass(index: number, module: boolean): Cls
        local c: Cls = {
            name = (if module then "M" else "C") .. index,
            module = module,
            fields = {},
            methods = {},
            ctor = rng.pick(CTOR_KINDS),
            ctorPrivate = false,
            generic = false,
            primaryParams = {},
            metamethods = {},
        }
        c.ctorPrivate = c.ctor ~= "pod" and rng.chance(0.4)
        c.generic = c.ctor == "primary" and rng.chance(0.2)
        for _, name in rng.sample(FIELD_NAMES, rng.int(1, 4)) do
            local kind: FieldKind = if name == "name" or name == "tag" then "string" elseif name == "items" then "table" else "number"
            local f: Field = {
                name = name,
                private = rng.chance(0.45),
                const = rng.chance(0.25),
                kind = kind,
                default = nil,
            }
            if rng.chance(0.4) then
                f.default = if kind == "number" and rng.chance(0.33) then "nextId()" else lit(rng, kind)
            end
            if f.const and c.ctor == "pod" and f.default == nil then
                f.default = lit(rng, kind)
            end
            table.insert(c.fields, f)
        end
        if c.ctor == "primary" then
            c.primaryParams = filter(c.fields, function()
                return rng.chance(0.8)
            end)
            if #c.primaryParams == 0 then
                c.primaryParams = { c.fields[1] }
            end
        end
        return c
    end

    local function methodBody(c: Cls, shape: string): ({ string }?, { string }?, boolean?, { string }?)
        local others = filter(classes, function(o)
            return o ~= c and (not c.module or o.module)
        end)
        local numFields = filter(c.fields, function(f)
            return f.kind == "number"
        end)
        local mutableNum = filter(numFields, function(f)
            return not f.const
        end)
        local tables = filter(c.fields, function(f)
            return f.kind == "table"
        end)

        if shape == "getter" then
            return { `return self.{rng.pick(c.fields).name}` }, {}, false, {}
        elseif shape == "bump" and #mutableNum > 0 then
            local f = rng.pick(mutableNum)
            return { `self.{f.name} = (self.{f.name} or 0) + 1`, `return self.{f.name}` }, {}, false, {}
        elseif shape == "selfcall" then
            return { "local g = self:get()", "return tostring(g) .. self:describe()" }, {}, false, { "get", "describe" }
        elseif shape == "reflect" then
            return { "return self[key]" }, { "key" }, false, {}
        elseif shape == "other" then
            local f = rng.pick(c.fields)
            return {
                `if class.isinstance(other, {c.name}) then`,
                `    return other.{f.name}`,
                "end",
                "return nil",
            }, { `other: {c.name}` }, false, {}
        elseif shape == "construct" then
            return { `local o = {ctorCall(c, c)}`, "return o:get()" }, {}, false, { "get" }
        elseif shape == "closure" then
            return { `local fn = function() return self.{rng.pick(c.fields).name} end`, "return fn()" }, {}, false, {}
        elseif shape == "loop" and #numFields > 0 then
            local f = rng.pick(numFields)
            return { "local s = 0", "for i = 1, 20 do", `    s += (self.{f.name} or 0) + i`, "end", "return s" }, {}, false, {}
        elseif shape == "isinstance" and #others > 0 then
            local o = rng.pick(others)
            local pub = publicFields(o)
            local read = if #pub > 0 then `x.{pub[1].name}` else "1"
            return { `if class.isinstance(x, {ref(o, c)}) then`, `    return {read}`, "end", "return -1" }, { "x" }, false, {}
        elseif shape == "error" then
            return { 'if n == 3 then error("boom") end', "return n" }, { "n" }, false, {}
        elseif shape == "foreign" then
            -- another class's private field: denied at O1, laundered at O2 if inlined into that class
            local withPrivate = filter(others, function(o)
                return #privateFields(o) > 0
            end)
            if #withPrivate > 0 then
                local o = rng.pick(withPrivate)
                return { `return x.{privateFields(o)[1].name}` }, { "x" }, true, {}
            end
        elseif shape == "yield" then
            -- only yield inside a coroutine the program made: the CLI's main thread is yieldable too, and a yield
            -- there passes through pcall and ends the program. `shared` is one table for both modules (each module
            -- gets its own sandboxed globals, so a global wouldn't be visible in the other)
            return { "if coroutine.running() ~= shared.mainThread then coroutine.yield(self:get()) end", "return 5" }, {}, false, { "get" }
        elseif shape == "sumtable" and #tables > 0 then
            local f = tables[1]
            return {
                `local t = self.{f.name}`,
                'if type(t) ~= "table" then return 0 end',
                "local s = 0",
                "for i = 1, #t do",
                "    s += t[i]",
                "end",
                "table.insert(t, s)",
                "return s",
            }, {}, false, {}
        elseif shape == "staticcall" then
            return { `return {c.name}.make():get()` }, {}, false, { "make", "get" }
        elseif shape == "lying" and #others > 0 then
            -- a receiver annotation that can lie: O2 inlines and raises, O1 dispatches dynamically
            local o = rng.pick(others)
            return { `local y: {o.name} = x`, "return y:get()" }, { "x" }, true, {}
        end
        return nil, nil, nil, nil
    end

    local function addMethods(c: Cls)
        local first = c.fields[1]
        table.insert(c.methods, method("describe", false, false, { `return "{c.name}:" .. tostring(self.{first.name})` }))
        table.insert(c.methods, method("get", false, false, { `return self.{first.name}` }))
        -- a public factory, so a private constructor is still reachable from outside
        table.insert(c.methods, method("make", false, true, { `return {ctorCall(c, c)}` }))

        for _ = 1, rng.int(1, 5) do
            local name = if rng.chance(0.7) then rng.pick(SHARED_METHODS) else `m{rng.int(0, 99)}`
            local taken = filter(c.methods, function(m)
                return m.name == name
            end)
            if #taken == 0 then
                local body, params, sketchy, calls = methodBody(c, rng.pick(METHOD_SHAPES))
                if body then
                    table.insert(c.methods, method(name, rng.chance(0.3), false, body, params, sketchy, calls))
                end
            end
        end

        if rng.chance(0.4) then
            table.insert(c.methods, method("helper", true, true, { "return 41 + (x or 0)" }, { "x" }))
            table.insert(c.methods, method("useHelper", false, false, { `return {c.name}.helper(1)` }, {}, false, { "helper" }))
        end

        local numFields = filter(c.fields, function(f)
            return f.kind == "number"
        end)
        if #numFields > 0 and rng.chance(0.5) then
            local f = numFields[1].name
            table.insert(c.metamethods, {
                name = "__add",
                params = { "other" },
                body = {
                    `if class.isinstance(other, {c.name}) then`,
                    `    return (self.{f} or 0) + (other.{f} or 0)`,
                    "end",
                    `return (self.{f} or 0) + (other or 0)`,
                },
            })
        end
        if rng.chance(0.4) then
            table.insert(c.metamethods, { name = "__tostring", params = {}, body = { `return "<{c.name}>"` } })
        end
        if rng.chance(0.3) then
            table.insert(c.metamethods, { name = "__len", params = {}, body = { "return 3" } })
        end
        if #numFields > 0 and rng.chance(0.3) then
            local f = numFields[1].name
            table.insert(c.metamethods, { name = "__eq", params = { "other" }, body = { `return self.{f} == other.{f}` } })
            table.insert(c.metamethods, {
                name = "__lt",
                params = { "other" },
                body = { `return (self.{f} or 0) < (other.{f} or 0)` },
            })
        end
    end

    local function emitClass(c: Cls): { string }
        local out = {}
        local head = `class {c.name}` .. (if c.generic then "<T>" else "")
        if c.ctor == "primary" then
            local params = {}
            for _, f in c.primaryParams do
                local p = `{if f.private then "private" else "public"} {if f.const then "const " else ""}{f.name}`
                if f.default and rng.chance(0.5) then
                    p ..= ` = {f.default}`
                end
                table.insert(params, p)
            end
            head ..= (if c.ctorPrivate then " private " else " ") .. "(" .. table.concat(params, ", ") .. ")"
        end
        table.insert(out, head)
        for _, f in c.fields do
            if not (c.ctor == "primary" and contains(c.primaryParams, f)) then
                local line = `    {if f.private then "private" else "public"} {if f.const then "const " else ""}{f.name}`
                if f.default then
                    line ..= ` = {f.default}`
                end
                table.insert(out, line)
            end
        end
        if c.ctor == "init" then
            local params = {}
            for i in c.fields do
                table.insert(params, `p{i}`)
            end
            table.insert(out, `    {if c.ctorPrivate then "private" else "public"} function __init(self, {table.concat(params, ", ")})`)
            for i, f in c.fields do
                table.insert(out, `        self.{f.name} = p{i}`)
            end
            table.insert(out, "    end")
        end
        local function emitFunction(access: string, name: string, params: { string }, body: { string })
            table.insert(out, `    {access} function {name}({table.concat(params, ", ")})`)
            for _, line in body do
                table.insert(out, "        " .. line)
            end
            table.insert(out, "    end")
        end
        for _, m in c.methods do
            local params = table.clone(m.params)
            if not m.static then
                table.insert(params, 1, "self")
            end
            emitFunction(if m.private then "private" else "public", m.name, params, m.body)
        end
        for _, mm in c.metamethods do
            local params = table.clone(mm.params)
            table.insert(params, 1, "self")
            emitFunction("public", mm.name, params, mm.body)
        end
        table.insert(out, "end")
        return out
    end

    local function isSketchy(c: Cls, name: string, seen: { [string]: boolean }?): boolean
        local visited = seen or {}
        if visited[name] then
            return false
        end
        visited[name] = true
        for _, m in c.methods do
            if m.name == name then
                if m.sketchy then
                    return true
                end
                for _, called in m.calls do
                    if isSketchy(c, called, visited) then
                        return true
                    end
                end
            end
        end
        return false
    end

    local function callArgs(m: Method): string
        local args = {}
        for _, p in m.params do
            if p == "key" then
                table.insert(args, '"' .. rng.pick(FIELD_NAMES) .. '"')
            elseif p == "n" then
                table.insert(args, rng.pick({ "1", "3", "5" }))
            elseif str.startswith(p, "other") then
                table.insert(args, rng.pick({ "obj", "obj2", "nil", "5" }))
            else
                table.insert(args, rng.pick({ "obj", "obj2", "tbl", "7", "nil" }))
            end
        end
        return table.concat(args, ", ")
    end

    local function op(kind: string, lines: { string }, divergent: boolean?)
        opCount += 1
        table.insert(ops, { label = `{if divergent then "~" else ""}{opCount}_{kind}`, lines = lines })
    end

    local function genOps()
        for _ = 1, rng.int(25, 60) do
            local c = rng.pick(classes)
            local c2 = rng.pick(classes)
            local r, r2 = ref(c), ref(c2)
            local make = `{r}.make()`
            local kind = rng.pick(OP_KINDS)

            if kind == "construct" then
                op(kind, { `local o = {make}`, "return o:describe()" })
            elseif kind == "ctor_outside" then
                op(kind, { `local o = {ctorCall(c)}`, "return o:get()" })
            elseif kind == "read" then
                op(kind, { `local o = {make}`, `return o.{rng.pick(c.fields).name}` })
            elseif kind == "write" then
                local f = rng.pick(c.fields)
                op(kind, { `local o = {make}`, `o.{f.name} = {lit(rng, f.kind)}`, `return o.{f.name}` })
            elseif kind == "call_typed" or kind == "call_untyped" or kind == "loop_calls" then
                local m = rng.pick(instanceMethods(c, rng.chance(0.85)))
                local annotation = if kind ~= "call_untyped" and not c.generic then `: {r}` else ""
                local divergent = isSketchy(c, m.name)
                if kind == "loop_calls" then
                    op(kind, {
                        `local o{annotation} = {make}`,
                        "local last",
                        "for i = 1, 30 do",
                        `    last = o:{m.name}({callArgs(m)})`,
                        "end",
                        "return last",
                    }, divergent)
                else
                    op(kind, { `local o{annotation} = {make}`, `local result = o:{m.name}({callArgs(m)})`, "return result" }, divergent)
                end
            elseif kind == "call_dot" then
                local m = rng.pick(instanceMethods(c, true))
                local args = callArgs(m)
                op(kind, { `return {r}.{m.name}(obj{if args ~= "" then ", " .. args else ""})` }, isSketchy(c, m.name))
            elseif kind == "wrong_self" then
                local m = rng.pick(instanceMethods(c, true))
                op(kind, { `local o = {r2}.make()`, `return {r}.{m.name}(o)` })
            elseif kind == "lying" then
                op(kind, { `local o: {r} = {r2}.make()`, `local result = o:{rng.pick({ "get", "describe" })}()`, "return result" }, true)
            elseif kind == "mixed_site" then
                op(kind, { `return describeAll(\{ {make}, legacy, {r2}.make(), legacy })` })
            elseif kind == "isinstance" then
                local pub = publicFields(c)
                local read = if #pub > 0 then `x.{pub[1].name}` else "true"
                local form = rng.pick({ "fused", "alias", "upvalue", "badclass" })
                if form == "fused" then
                    op("isa", { `local x = pick({make}, {r2}.make())`, `if class.isinstance(x, {r}) then`, `    return {read}`, "end", "return false" })
                elseif form == "alias" then
                    op("isa_alias", { `local K = {r}`, `local x = pick({make}, 3)`, "if class.isinstance(x, K) then", `    return {read}`, "end", "return 0" })
                elseif form == "upvalue" then
                    op("isa_up", { `local x = pick({make}, tbl)`, "return isaUp(x)" })
                else
                    op("isa_bad", { `local x = {make}`, "return class.isinstance(x, pick(tbl, 5))" })
                end
            elseif kind == "classlib" then
                local arg = rng.pick({ make, r, "tbl", "5", "nil", "legacy" })
                local fn = rng.pick({ "of", "name", "isinstance", "fields" })
                if fn == "of" then
                    op("class_of", { `local k = class.of({arg})`, "return k and class.name(k)" })
                elseif fn == "name" then
                    op("class_name", { `return class.name({arg})` })
                elseif fn == "isinstance" then
                    op("class_isa", { `return class.isinstance({arg}, {rng.pick({ r, r2, "tbl" })})` })
                else
                    op("class_fields", { `local t, complete = class.fields({arg})`, "return sortedKeys(t), complete" })
                end
            elseif kind == "meta" then
                local expr = rng.pick({
                    "o + 1", "o + o", "#o", "o == o2", "o < o2", "tostring(o)", "-o", "o .. 'x'", "getmetatable(o)",
                    "setmetatable(o, {})", "type(o)", "typeof(o)", "o()",
                })
                op(kind, { `local o, o2 = {make}, {make}`, `return {expr}` })
            elseif kind == "init_explicit" then
                op(kind, { `local o = {make}`, "o:__init()", "return o:get()" })
            elseif kind == "coroutine" then
                op(kind, {
                    `local o = {make}`,
                    "local co = coroutine.wrap(function()",
                    `    local ok, result = pcall(function() return o:{rng.pick(instanceMethods(c, true)).name}({""}) end)`,
                    "    coroutine.yield(ok)",
                    "    return if ok then result else 'err'",
                    "end)",
                    "local a = co()",
                    "local b = co()",
                    "return a, b",
                })
            elseif kind == "gc" then
                op(kind, {
                    "local keep = {}",
                    "for i = 1, 3000 do",
                    `    keep[i % 50 + 1] = {make}`,
                    "    local junk = \{ i, i + 1 }",
                    "end",
                    "local s = 0",
                    "for _, o in keep do",
                    "    s += #tostring(o:get())",
                    "end",
                    "return s",
                })
            elseif kind == "dynamic_key" then
                local key = rng.pick(c.fields).name
                op(kind, { `local o = {make}`, `local k = pick("{key}", "{key}")`, "return o[k]" })
            elseif kind == "raw" then
                local call = rng.pick({ "rawget(o, 'a')", "rawset(o, 'a', 1)", "rawlen(o)", "rawequal(o, o)", "next(o)" })
                op(kind, { `local o = {make}`, `return {call}` })
            elseif kind == "iterate" then
                local iterable = rng.pick({ "pairs(o)", "ipairs(o)", "o" })
                op(kind, { `local o = {make}`, "local n = 0", `for _ in {iterable} do`, "    n += 1", "    if n > 5 then break end", "end", "return n" })
            elseif kind == "private_static" then
                op(kind, { `return {r}.helper(1)` })
            elseif kind == "fields" then
                op(kind, { `local t = class.fields({r})`, "return sortedKeys(t)" })
            elseif kind == "alias_ctor" then
                op(kind, { `local K = {r}`, "local ok, o = pcall(K)", "return ok and class.name(o)" })
            elseif kind == "table_key" then
                op(kind, { `local o = {make}`, "local t = {}", "t[o] = 1", "return t[o]" })
            end
        end
    end

    local modClasses: { Cls } = {}
    for i = 0, rng.int(0, 3) - 1 do
        local c = makeClass(i, true)
        table.insert(modClasses, c)
        table.insert(classes, c)
    end
    for i = 0, rng.int(2, 6) - 1 do
        table.insert(classes, makeClass(i, false))
    end
    for _, c in classes do
        addMethods(c)
    end
    genOps()

    const prelude = {
        "local shared = {}",
        "local idCounter = 0",
        "local function nextId()",
        "    idCounter += 1",
        "    return idCounter",
        "end",
        "local function multi()",
        "    return 1, 2, 3",
        "end",
    }

    local mod = { "--!nocheck", "--!optimize OPTLEVEL" }
    table.move(prelude, 1, #prelude, #mod + 1, mod)
    local exports = {}
    for _, c in modClasses do
        local lines = emitClass(c)
        table.move(lines, 1, #lines, #mod + 1, mod)
        table.insert(exports, `{c.name} = {c.name}`)
    end
    table.insert(exports, "__shared = shared")
    table.insert(mod, "return { " .. table.concat(exports, ", ") .. " }")

    local main = { "--!nocheck", "--!optimize OPTLEVEL" }
    table.move(prelude, 1, #prelude, #main + 1, main)
    for _, line in {
        'local mod = require("./MODNAME")',
        "shared = mod.__shared",
        "shared.mainThread = coroutine.running()",
        "local function norm(v)",
        "    local t = typeof(v)",
        "    if t == 'number' then",
        "        if v ~= v then return 'nan' end",
        "        return string.format('%.6g', v)",
        "    elseif t == 'string' then",
        "        if string.find(v, '0x') then return 'addr' end",
        "        return string.format('%q', v)",
        "    elseif t == 'boolean' or t == 'nil' then",
        "        return tostring(v)",
        "    elseif t == 'object' then",
        "        return 'object:' .. class.name(v)",
        "    elseif t == 'class' then",
        "        return 'class:' .. class.name(v)",
        "    end",
        "    return t",
        "end",
        "local function run(label, fn)",
        "    local r = table.pack(pcall(fn))",
        "    if r[1] then",
        "        print(label, 'ok', norm(r[2]), norm(r[3]))",
        "    else",
        "        print(label, 'err')",
        "    end",
        "end",
        "local function pick(a, b)",
        "    return if idCounter % 2 == 0 then a else b",
        "end",
        "local function sortedKeys(t)",
        "    if type(t) ~= 'table' then return t end",
        "    local keys = {}",
        "    for k in t do table.insert(keys, tostring(k)) end",
        "    table.sort(keys)",
        "    return table.concat(keys, ',')",
        "end",
        "local legacy = setmetatable({ name = 'legacy' }, { __index = {",
        "    describe = function(self) return 'legacy' end,",
        "    get = function(self) return 0 end,",
        "} })",
        "local tbl = { a = 1 }",
        "local function describeAll(items)",
        "    local out = {}",
        "    for i, item in items do",
        "        out[i] = item:describe()",
        "    end",
        "    return table.concat(out, '|')",
        "end",
    } do
        table.insert(main, line)
    end
    for _, c in classes do
        if not c.module then
            local lines = emitClass(c)
            table.move(lines, 1, #lines, #main + 1, main)
        end
    end
    local target = rng.pick(classes)
    for _, line in {
        "local function isaUp(x)",
        `    if class.isinstance(x, {ref(target)}) then`,
        "        return class.name(x)",
        "    end",
        "    return 'no'",
        "end",
        "local okObj, obj = pcall(function() return " .. ref(rng.pick(classes)) .. ".make() end)",
        "if not okObj then obj = nil end",
        "local okObj2, obj2 = pcall(function() return " .. ref(rng.pick(classes)) .. ".make() end)",
        "if not okObj2 then obj2 = nil end",
    } do
        table.insert(main, line)
    end
    for _, o in ops do
        table.insert(main, `run("{o.label}", function()`)
        for _, line in o.lines do
            table.insert(main, "    " .. line)
        end
        table.insert(main, "end)")
    end
    table.insert(main, `print("__done", {opCount})`)

    return table.concat(main, "\n") .. "\n", table.concat(mod, "\n") .. "\n"
end

return {
    generate = generate,
}

--[[
    Random Luwu class program generator for differential and crash fuzzing (see run.luau).

    Written for seal 0.8.1 in the Luau dialect *it* understands -- no Luwu classes here: the fuzzer must not run
    on the build it is testing.

    A generated program is a pair of modules: `main` requires `mod`, so cross-module paths get exercised too.
    `main` prints one line per operation: `<label>\tok\t<value>\t<value>` or `<label>\terr`. The runner compares
    those lines across optimization levels and codegen modes. A label starting with `~` marks an operation whose
    error *status* may legitimately differ between optimization levels. Two do: a receiver annotation that
    lies about its class, and private access laundered by inlining a free function into a class method.
    For those only values are compared, and only when both sides succeed.

    Programs contain two placeholders the runner fills in per configuration: OPTLEVEL and MODNAME.

    Programs also declare traits (TRAITS.CLAUDE.md) that classes implement, and the generator knows what must be true
    of them: which classes implement which traits, what a call dispatched through a trait returns, which reads must
    fail. Such an operation calls `check(cond, what)`, which prints `__wrong\t<what>` when the fact doesn't hold. The
    runner reports those lines as WRONG in whichever configuration printed them, since a runtime rule that is wrong at
    every optimization level would never diverge. Operations on traits are built to succeed and are labeled `!`: an
    error from one is WRONG too, for the same reason.
]]

const str = require("@std/str")

type Rng = {
    next: () -> number,
    int: (lo: number, hi: number) -> number,
    chance: (p: number) -> boolean,
    pick: <T>(list: { T }) -> T,
    sample: <T>(list: { T }, count: number) -> { T },
}

-- xorshift32: deterministic for a given seed regardless of the host's math.random
const function new_rng(seed: number): Rng
    local state = (seed * 2654435761) % 4294967296
    if state == 0 then
        state = 1
    end

    const rng = {} :: Rng

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
        const copy = table.clone(list)
        const out = {}
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
    ctor_private: boolean,
    generic: boolean,
    primary_params: { Field },
    metamethods: { Metamethod },
    -- the traits the class lists after `implements`, with the trait arguments it passes (nil: no argument list)
    implements: { { trait: Trait, args: string? } },
    -- every trait it implements: the listed ones and the ones their `needs` imply
    trait_set: { Trait },
    -- trait defaults the class defines itself
    overrides: { string },
    -- trait fields the class declares itself, each with a constant value; kept out of `fields`, so no constructor argument
    -- sets them and the generator knows the value a new object ends up with
    field_overrides: { Field },
    defines_maybe: boolean,
}

-- A trait's own members are named after it (`t2_f1`, `t2_m3`), so the traits a class implements never clash with each
-- other or with the class; clashes are covered by the conformance tests. The exception is an override: a trait may
-- redefine its needed trait's `m1`, which it wins for every class implementing it (each `m1` is overridden by at most one
-- trait per program, so no class meets two). What traits *expect* uses the class-side names
-- (`hp`, `get`), with one access specifier and constness per name for the whole program, so every expectation of a
-- name can be met at once.
type TraitFn = {
    name: string,
    private: boolean,
    static: boolean,
    final: boolean,
    params: { string },
    body: { string },
    -- the value a call returns for an object, if the generator knows it without running anything
    constant: string?,
}

type Trait = {
    name: string,
    index: number,
    module: boolean,
    -- trait parameters, each a provided field of the trait
    params: { { name: string, default: string? } },
    fields: { { name: string, private: boolean, const: boolean, default: string } },
    expect_fields: { string },
    expect_fns: { string },
    expects_maybe: boolean,
    expects_init: boolean,
    fns: { TraitFn },
    needs: { Trait },
    -- the needed traits' functions this trait overrides, by name, with the value its override returns
    overrides: { [string]: string },
    -- the needed traits' fields this trait declares again, by name, with the value they end up with
    field_overrides: { [string]: string },
    rank: number,
    create: boolean,
    tostring: boolean,
    implementors: { Cls },
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
    "iterate", "private_static", "loop_calls", "fields", "alias_ctor", "table_key", "yield_construct", "expressive",
    "expressive",
}

const function lit(rng: Rng, kind: string): string
    if kind == "number" then
        return rng.pick({ "0", "1", "2", "3", "7", "-4", "10", "0.5", "100" })
    elseif kind == "string" then
        return rng.pick({ '"x"', '"hello"', '"Z"', '""' })
    end
    const items = {}
    for _ = 1, rng.int(0, 3) do
        table.insert(items, tostring(rng.int(0, 9)))
    end
    return "{ " .. table.concat(items, ", ") .. " }"
end

const function contains<T>(list: { T }, value: T): boolean
    return table.find(list, value) ~= nil
end

const function filter<T>(list: { T }, keep: (T) -> boolean): { T }
    const out = {}
    for _, v in list do
        if keep(v) then
            table.insert(out, v)
        end
    end
    return out
end

const function method(name: string, private: boolean, static: boolean, body: { string }, params: { string }?, sketchy: boolean?, calls: { string }?): Method
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

const function generate(seed: number): (string, string)
    const rng = new_rng(seed)
    const classes: { Cls } = {}
    const ops: { { label: string, lines: { string } } } = {}
    local op_count = 0

    -- how `target` is named from code in `from` (nil: top level of main)
    const function ref(target: Cls, from: Cls?): string
        if target.module and not (from and from.module) then
            return `mod.{target.name}`
        end
        return target.name
    end

    const function public_fields(c: Cls): { Field }
        return filter(c.fields, function(f)
            return not f.private
        end)
    end

    const function private_fields(c: Cls): { Field }
        return filter(c.fields, function(f)
            return f.private
        end)
    end

    const function instance_methods(c: Cls, public_only: boolean): { Method }
        return filter(c.methods, function(m)
            return not m.static and (not public_only or not m.private)
        end)
    end

    const function ctor_call(c: Cls, from: Cls?): string
        const name = ref(c, from)
        if c.ctor == "pod" then
            if rng.chance(0.2) then
                return `{name}()`
            end
            const kv = {}
            for _, f in c.fields do
                if rng.chance(0.8) then
                    table.insert(kv, `{f.name} = {lit(rng, f.kind)}`)
                end
            end
            return `{name} \{ {table.concat(kv, ", ")} }`
        end
        const args = {}
        for _, f in if c.ctor == "primary" then c.primary_params else c.fields do
            table.insert(args, if rng.chance(0.85) then lit(rng, f.kind) else "nil")
        end
        if c.ctor == "primary" and #args > 0 and rng.chance(0.2) then
            args[#args] = "multi()"
        end
        return `{name}({table.concat(args, ", ")})`
    end

    const function make_class(index: number, module: boolean): Cls
        const c: Cls = {
            name = (if module then "M" else "C") .. index,
            module = module,
            fields = {},
            methods = {},
            ctor = rng.pick(CTOR_KINDS),
            ctor_private = false,
            generic = false,
            primary_params = {},
            metamethods = {},
            implements = {},
            trait_set = {},
            overrides = {},
            field_overrides = {},
            defines_maybe = false,
        }
        c.ctor_private = c.ctor ~= "pod" and rng.chance(0.4)
        c.generic = c.ctor == "primary" and rng.chance(0.2)
        for _, name in rng.sample(FIELD_NAMES, rng.int(1, 4)) do
            const kind: FieldKind = if name == "name" or name == "tag" then "string" elseif name == "items" then "table" else "number"
            const f: Field = {
                name = name,
                private = rng.chance(0.45),
                const = rng.chance(0.25),
                kind = kind,
                default = nil,
            }
            if rng.chance(0.4) then
                f.default = if kind == "number" and rng.chance(0.33) then "nextId()" else lit(rng, kind)
                -- a default that suspends construction when it runs in a coroutine (see `pause` in the prelude)
                if rng.chance(0.15) then
                    f.default = `{rng.pick({ "pause", "gpause" })}({f.default})`
                end
            end
            if f.const and c.ctor == "pod" and f.default == nil then
                f.default = lit(rng, kind)
            end
            table.insert(c.fields, f)
        end
        if c.ctor == "primary" then
            c.primary_params = filter(c.fields, function()
                return rng.chance(0.8)
            end)
            if #c.primary_params == 0 then
                c.primary_params = { c.fields[1] }
            end
        end
        return c
    end

    const function method_body(c: Cls, shape: string): ({ string }?, { string }?, boolean?, { string }?)
        const others = filter(classes, function(o)
            return o ~= c and (not c.module or o.module)
        end)
        const num_fields = filter(c.fields, function(f)
            return f.kind == "number"
        end)
        const mutable_num = filter(num_fields, function(f)
            return not f.const
        end)
        const tables = filter(c.fields, function(f)
            return f.kind == "table"
        end)

        if shape == "getter" then
            return { `return self.{rng.pick(c.fields).name}` }, {}, false, {}
        elseif shape == "bump" and #mutable_num > 0 then
            const f = rng.pick(mutable_num)
            return { `self.{f.name} = (self.{f.name} or 0) + 1`, `return self.{f.name}` }, {}, false, {}
        elseif shape == "selfcall" then
            return { "local g = self:get()", "return tostring(g) .. self:describe()" }, {}, false, { "get", "describe" }
        elseif shape == "reflect" then
            return { "return self[key]" }, { "key" }, false, {}
        elseif shape == "other" then
            const f = rng.pick(c.fields)
            return {
                `if class.isinstance(other, {c.name}) then`,
                `    return other.{f.name}`,
                "end",
                "return nil",
            }, { `other: {c.name}` }, false, {}
        elseif shape == "construct" then
            -- bound to a local: a multret `return o:get()` never inlines, and this is a construction-proven receiver
            return { `local o = {ctor_call(c, c)}`, "local g = o:get()", "return g" }, {}, false, { "get" }
        elseif shape == "closure" then
            return { `local fn = function() return self.{rng.pick(c.fields).name} end`, "return fn()" }, {}, false, {}
        elseif shape == "loop" and #num_fields > 0 then
            const f = rng.pick(num_fields)
            return { "local s = 0", "for i = 1, 20 do", `    s += (self.{f.name} or 0) + i`, "end", "return s" }, {}, false, {}
        elseif shape == "isinstance" and #others > 0 then
            const o = rng.pick(others)
            const pub = public_fields(o)
            const read = if #pub > 0 then `x.{pub[1].name}` else "1"
            return { `if class.isinstance(x, {ref(o, c)}) then`, `    return {read}`, "end", "return -1" }, { "x" }, false, {}
        elseif shape == "error" then
            return { 'if n == 3 then error("boom") end', "return n" }, { "n" }, false, {}
        elseif shape == "foreign" then
            -- another class's private field: denied at O1, laundered at O2 if inlined into that class
            const with_private = filter(others, function(o)
                return #private_fields(o) > 0
            end)
            if #with_private > 0 then
                const o = rng.pick(with_private)
                return { `return x.{private_fields(o)[1].name}` }, { "x" }, true, {}
            end
        elseif shape == "yield" then
            -- only yield inside a coroutine the program made: the CLI's main thread is yieldable too, and a yield
            -- there passes through pcall and ends the program. `shared` is one table for both modules (each module
            -- gets its own sandboxed globals, so a global wouldn't be visible in the other)
            return {
                "if coroutine.running() ~= shared.mainThread then",
                "    local g = self:get()",
                "    coroutine.yield(g)",
                "end",
                "return 5",
            }, {}, false, { "get" }
        elseif shape == "sumtable" and #tables > 0 then
            const f = tables[1]
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
            const o = rng.pick(others)
            return { `local y: {o.name} = x`, "local g = y:get()", "return g" }, { "x" }, true, {}
        end
        return nil, nil, nil, nil
    end

    const function add_methods(c: Cls)
        const first = c.fields[1]
        table.insert(c.methods, method("describe", false, false, { `return "{c.name}:" .. tostring(self.{first.name})` }))
        table.insert(c.methods, method("get", false, false, { `return self.{first.name}` }))
        -- a public factory, so a private constructor is still reachable from outside
        table.insert(c.methods, method("make", false, true, { `return {ctor_call(c, c)}` }))

        for _ = 1, rng.int(1, 5) do
            const name = if rng.chance(0.7) then rng.pick(SHARED_METHODS) else `m{rng.int(0, 99)}`
            const taken = filter(c.methods, function(m)
                return m.name == name
            end)
            if #taken == 0 then
                const body, params, sketchy, calls = method_body(c, rng.pick(METHOD_SHAPES))
                if body then
                    table.insert(c.methods, method(name, rng.chance(0.3), false, body, params, sketchy, calls))
                end
            end
        end

        if rng.chance(0.4) then
            table.insert(c.methods, method("helper", true, true, { "return 41 + (x or 0)" }, { "x" }))
            table.insert(c.methods, method("useHelper", false, false, { `return {c.name}.helper(1)` }, {}, false, { "helper" }))
        end

        const num_fields = filter(c.fields, function(f)
            return f.kind == "number"
        end)
        if #num_fields > 0 and rng.chance(0.5) then
            const f = num_fields[1].name
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
        if #num_fields > 0 and rng.chance(0.3) then
            const f = num_fields[1].name
            table.insert(c.metamethods, { name = "__eq", params = { "other" }, body = { `return self.{f} == other.{f}` } })
            table.insert(c.metamethods, {
                name = "__lt",
                params = { "other" },
                body = { `return (self.{f} or 0) < (other.{f} or 0)` },
            })
        end
    end

    const function emit_class(c: Cls): { string }
        const out = {}
        local head = `class {c.name}` .. (if c.generic then "<T>" else "")
        if c.ctor == "primary" then
            const params = {}
            for _, f in c.primary_params do
                local p = `{if f.private then "private" else "public"} {if f.const then "const " else ""}{f.name}`
                if f.default and rng.chance(0.5) then
                    p ..= ` = {f.default}`
                end
                table.insert(params, p)
            end
            head ..= (if c.ctor_private then " private " else " ") .. "(" .. table.concat(params, ", ") .. ")"
        end
        if #c.implements > 0 then
            const entries = {}
            for _, use in c.implements do
                local entry = if use.trait.module and not c.module then `mod.{use.trait.name}` else use.trait.name
                if use.args then
                    entry ..= `({use.args})`
                end
                table.insert(entries, entry)
            end
            head ..= " implements " .. table.concat(entries, ", ")
        end
        table.insert(out, head)
        for _, f in c.fields do
            if not (c.ctor == "primary" and contains(c.primary_params, f)) then
                local line = `    {if f.private then "private" else "public"} {if f.const then "const " else ""}{f.name}`
                if f.default then
                    line ..= ` = {f.default}`
                end
                table.insert(out, line)
            end
        end
        for _, f in c.field_overrides do
            table.insert(out, `    {if f.private then "private" else "public"} {if f.const then "const " else ""}{f.name} = {f.default}`)
        end
        if c.ctor == "init" then
            const params = {}
            for i in c.fields do
                table.insert(params, `p{i}`)
            end
            table.insert(out, `    {if c.ctor_private then "private" else "public"} function __init(self, {table.concat(params, ", ")})`)
            for i, f in c.fields do
                table.insert(out, `        self.{f.name} = p{i}`)
            end
            table.insert(out, "    end")
        end
        const function emit_function(access: string, name: string, params: { string }, body: { string })
            table.insert(out, `    {access} function {name}({table.concat(params, ", ")})`)
            for _, line in body do
                table.insert(out, "        " .. line)
            end
            table.insert(out, "    end")
        end
        for _, m in c.methods do
            const params = table.clone(m.params)
            if not m.static then
                table.insert(params, 1, "self")
            end
            emit_function(if m.private then "private" else "public", m.name, params, m.body)
        end
        for _, mm in c.metamethods do
            const params = table.clone(mm.params)
            table.insert(params, 1, "self")
            emit_function("public", mm.name, params, mm.body)
        end
        table.insert(out, "end")
        return out
    end

    const function is_sketchy(c: Cls, name: string, seen: { [string]: boolean }?): boolean
        const visited = seen or {}
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
                    if is_sketchy(c, called, visited) then
                        return true
                    end
                end
            end
        end
        return false
    end

    const function call_args(m: Method): string
        const args = {}
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

    -- `divergent`: may raise at some levels only (label `~`). `mustSucceed`: raising at all is wrong (label `!`), for
    -- operations built to succeed, where an error would otherwise look the same at every level and go unnoticed.
    const function op(kind: string, lines: { string }, divergent: boolean?, must_succeed: boolean?)
        op_count += 1
        const prefix = if divergent then "~" elseif must_succeed then "!" else ""
        table.insert(ops, { label = `{prefix}{op_count}_{kind}`, lines = lines })
    end

    const function gen_ops()
        for _ = 1, rng.int(25, 60) do
            const c = rng.pick(classes)
            const c2 = rng.pick(classes)
            const r, r2 = ref(c), ref(c2)
            const make = `{r}.make()`
            const kind = rng.pick(OP_KINDS)

            if kind == "construct" then
                op(kind, { `local o = {make}`, "return o:describe()" })
            elseif kind == "yield_construct" then
                -- a construction whose defaults pause (see `pause`) is suspended and resumed until it completes; how
                -- often it pauses and what it builds are the same at every level
                op(kind, {
                    `local co = coroutine.create(function() return {make} end)`,
                    "local ok, r = coroutine.resume(co)",
                    "local pauses = 0",
                    "while ok and coroutine.status(co) ~= 'dead' do",
                    "    pauses += 1",
                    "    ok, r = coroutine.resume(co)",
                    "end",
                    "if not ok then",
                    "    error(r, 0)",
                    "end",
                    "return pauses, r:describe()",
                })
            elseif kind == "ctor_outside" then
                -- A local initialized by constructing a class in this module is proven to be that class, so its
                -- method calls inline with no self check at O2. Bound to a local, since a multret return never
                -- inlines. The rebind forms write the local where the construction can no longer vouch for it:
                -- any write, including one from a closure, must unprove it, or `c`'s body runs against the new
                -- value unchecked.
                const m = rng.pick(instance_methods(c, rng.chance(0.85)))
                const call = `o:{m.name}({call_args(m)})`
                const divergent = is_sketchy(c, m.name)
                const form = rng.pick({ "plain", "plain", "rebind", "rebind_closure", "rebind_after" })
                const elsewhere = filter(classes, function(o)
                    return o ~= c
                end)
                const other = if #elsewhere > 0 and rng.chance(0.75)
                    then `{ref(rng.pick(elsewhere))}.make()`
                    else rng.pick({ "tbl", "nil", "legacy" })

                if form == "plain" then
                    op(kind, { `local o = {ctor_call(c)}`, `local r = {call}`, "return r" }, divergent)
                elseif form == "rebind" then
                    op("ctor_rebind", { `local o = {ctor_call(c)}`, `o = {other}`, `local r = {call}`, "return r" }, divergent)
                elseif form == "rebind_closure" then
                    op("ctor_rebind_closure", {
                        `local o = {ctor_call(c)}`,
                        "local swap = function()",
                        `    o = {other}`,
                        "end",
                        "swap()",
                        `local r = {call}`,
                        "return r",
                    }, divergent)
                else
                    op("ctor_rebind_after", {
                        `local o = {ctor_call(c)}`,
                        `local r = {call}`,
                        `o = {other}`,
                        `local r2 = {call}`,
                        "return r, r2",
                    }, divergent)
                end
            elseif kind == "expressive" then
                -- Luwu If Local and Do Expressions (EXPRESSIVE_CONTROL_FLOW): the same class operations, run through
                -- `do ... give`, `x or return`, if local chains and do expressions in loop headers, where a jump out
                -- of the middle of an expression has to leave registers, upvalues and proofs as -O0 does.
                const m = rng.pick(instance_methods(c, true))
                const divergent = if m then is_sketchy(c, m.name) else false
                const fields = public_fields(c)
                const function use(receiver: string): string
                    if m then
                        return `{receiver}:{m.name}({call_args(m)})`
                    elseif #fields > 0 then
                        return `{receiver}.{fields[1].name}`
                    end
                    return `{receiver}:describe()`
                end
                const skip_at, stop_at = rng.int(1, 5), rng.int(2, 6)
                const form = rng.pick({
                    "give_call", "give_loop", "or_return", "if_local", "if_local_expr", "do_receiver", "do_header",
                    "do_closures", "do_yield", "inlined_return",
                })

                if form == "give_call" then
                    op("do_give_call", { "local v = do", `    local o = {make}`, `    give {use("o")}`, "return v" }, divergent)
                elseif form == "give_loop" then
                    op("do_give_loop", {
                        "local acc = {}",
                        "for i = 1, 6 do",
                        "    local v = do",
                        `        local o = {make}`,
                        `        if i == {skip_at} then continue end`,
                        `        if i == {stop_at} then break end`,
                        `        give norm({use("o")})`,
                        "    table.insert(acc, v)",
                        "end",
                        "return table.concat(acc, ',')",
                    }, divergent)
                elseif form == "or_return" then
                    op("do_or_return", {
                        "local function f(o)",
                        "    local got = o or return 'none'",
                        `    local r = {use("got")}`,
                        "    return r",
                        "end",
                        `return f(pick({make}, nil)), f(pick(nil, {r2}.make()))`,
                    }, divergent)
                elseif form == "if_local" then
                    op("if_local_isa", {
                        `if local o = pick({make}, {r2}.make()) when class.isinstance(o, {r}) when local d = o:describe() then`,
                        `    local got = {use("o")}`,
                        "    return got, d",
                        "end",
                        "return -1",
                    }, divergent)
                elseif form == "if_local_expr" then
                    op("if_local_expr", {
                        `local v = if local o = pick({make}, nil) when local d = o:describe() then d else 'none'`,
                        `local w = if local o = pick(nil, {make}) then {use("o")} elseif local p = {r2}.make() then p:describe() else 0`,
                        "return v, norm(w)",
                    }, divergent)
                elseif form == "do_receiver" then
                    op("do_receiver", {
                        `local a = (do give {make}):describe()`,
                        `local b = (do local o = {make} give o):describe()`,
                        `local got = {use(`(do give {make})`)}`,
                        "return a .. b, got",
                    }, divergent)
                elseif form == "do_header" then
                    op("do_header", {
                        "local n = 0",
                        `local o = {make}`,
                        "while do n += 1 give n < 4 and o ~= nil do",
                        `    o = if n == {skip_at} then {r2}.make() else o`,
                        "end",
                        "local log = {}",
                        "for outer = 1, 3 do",
                        "    for i = 1, do if outer == 2 then break end give 2 do",
                        `        table.insert(log, ({make}):describe())`,
                        "    end",
                        "end",
                        "return n, o:describe(), table.concat(log, '|')",
                    })
                elseif form == "do_closures" then
                    op("do_closures", {
                        "local fns = {}",
                        "for i = 1, 4 do",
                        "    local d = do",
                        `        local o = {make}`,
                        "        table.insert(fns, function() return o:describe() end)",
                        `        if i == {skip_at} then continue end`,
                        "        give o:describe()",
                        "    local _ = d",
                        "end",
                        "local out = {}",
                        "for _, fn in fns do table.insert(out, fn()) end",
                        "return table.concat(out, '|')",
                    })
                elseif form == "do_yield" then
                    op("do_yield", {
                        "local co = coroutine.wrap(function()",
                        "    local v = do",
                        `        local o = {make}`,
                        "        local again = coroutine.yield(o:describe())",
                        `        if again then give {use("o")} end`,
                        "        give 'stopped'",
                        "    return v",
                        "end)",
                        "local first = co()",
                        `local second = co({if rng.chance(0.5) then "true" else "false"})`,
                        "return first, norm(second)",
                    }, divergent)
                else
                    op("do_inlined_return", {
                        "local function g(o)",
                        "    return do",
                        "        if o == nil then return 'nil' end",
                        "        give o:describe()",
                        "end",
                        "local function h(o)",
                        "    local _ = o or (do return)",
                        "    return o:describe()",
                        "end",
                        "local out = {}",
                        "for _ = 1, 3 do",
                        `    table.insert(out, g(pick({make}, nil)))`,
                        `    table.insert(out, tostring(select('#', h(pick(nil, {make})))))`,
                        "end",
                        "return table.concat(out, ',')",
                    })
                end
            elseif kind == "read" then
                op(kind, { `local o = {make}`, `return o.{rng.pick(c.fields).name}` })
            elseif kind == "write" then
                const f = rng.pick(c.fields)
                op(kind, { `local o = {make}`, `o.{f.name} = {lit(rng, f.kind)}`, `return o.{f.name}` })
            elseif kind == "call_typed" or kind == "call_untyped" or kind == "loop_calls" then
                const m = rng.pick(instance_methods(c, rng.chance(0.85)))
                const annotation = if kind ~= "call_untyped" and not c.generic then `: {r}` else ""
                const divergent = is_sketchy(c, m.name)
                if kind == "loop_calls" then
                    op(kind, {
                        `local o{annotation} = {make}`,
                        "local last",
                        "for i = 1, 30 do",
                        `    last = o:{m.name}({call_args(m)})`,
                        "end",
                        "return last",
                    }, divergent)
                else
                    op(kind, { `local o{annotation} = {make}`, `local result = o:{m.name}({call_args(m)})`, "return result" }, divergent)
                end
            elseif kind == "call_dot" then
                const m = rng.pick(instance_methods(c, true))
                const args = call_args(m)
                op(kind, { `return {r}.{m.name}(obj{if args ~= "" then ", " .. args else ""})` }, is_sketchy(c, m.name))
            elseif kind == "wrong_self" then
                const m = rng.pick(instance_methods(c, true))
                op(kind, { `local o = {r2}.make()`, `return {r}.{m.name}(o)` })
            elseif kind == "lying" then
                op(kind, { `local o: {r} = {r2}.make()`, `local result = o:{rng.pick({ "get", "describe" })}()`, "return result" }, true)
            elseif kind == "mixed_site" then
                op(kind, { `return describeAll(\{ {make}, legacy, {r2}.make(), legacy })` })
            elseif kind == "isinstance" then
                const pub = public_fields(c)
                const read = if #pub > 0 then `x.{pub[1].name}` else "true"
                const form = rng.pick({
                    "fused",
                    "alias",
                    "upvalue",
                    "badclass",
                    "rebind_in",
                    "rebind_after",
                    "rebind_before",
                    "rebind_closure",
                    "assert",
                    "assert_msg",
                    "assert_rebind",
                    "assert_closure",
                })
                if form == "fused" then
                    op("isa", { `local x = pick({make}, {r2}.make())`, `if class.isinstance(x, {r}) then`, `    return {read}`, "end", "return false" })
                elseif form == "alias" then
                    op("isa_alias", { `local K = {r}`, `local x = pick({make}, 3)`, "if class.isinstance(x, K) then", `    return {read}`, "end", "return 0" })
                elseif form == "upvalue" then
                    op("isa_up", { `local x = pick({make}, tbl)`, "return isaUp(x)" })
                elseif form == "rebind_in" or form == "rebind_after" or form == "rebind_before" or form == "rebind_closure" then
                    -- A `class.isinstance` branch proves its local's class and the compiler drops the
                    -- inline site's self check, so a write that can reach a use inside the branch must
                    -- unprove it. These four place a write in each position the rule distinguishes: the
                    -- ones that can reach the use must dispatch (or raise) exactly as at O0.
                    const m = rng.pick(instance_methods(c, true))
                    const use = if m then `x:{m.name}()` else read

                    -- A stale proof would run `c`'s body against whatever the local holds after the write.
                    -- So the new value must not be a `c`. Rebinding to another `c` (which `r2` may well be)
                    -- would run the right body anyway and test nothing.
                    -- The new value is usually another class when the module has one (75% of the time), and
                    -- otherwise a non-object. Another class is the more useful target. A stale proof then
                    -- runs the wrong body and returns a wrong value, which the runner catches by comparing
                    -- against -O0. A non-object usually just errors at every level, and when both runs fail
                    -- the runner compares only their statuses.
                    const elsewhere = filter(classes, function(o)
                        return o ~= c
                    end)
                    const other = if #elsewhere > 0 and rng.chance(0.75)
                        then `{ref(rng.pick(elsewhere), c)}.make()`
                        else rng.pick({ "tbl", "nil", "legacy" })

                    if form == "rebind_in" then
                        op("isa_rebind_in", {
                            `local x = pick({make}, {r2}.make())`,
                            `if class.isinstance(x, {r}) then`,
                            `    x = {other}`,
                            `    local got = {use}`,
                            "    return got",
                            "end",
                            "return -1",
                        })
                    elseif form == "rebind_after" then
                        op("isa_rebind_after", {
                            `local x = pick({make}, {r2}.make())`,
                            "local out = -1",
                            `if class.isinstance(x, {r}) then`,
                            `    local got = {use}`,
                            "    out = got",
                            "end",
                            `x = {other}`,
                            "return out",
                        })
                    elseif form == "rebind_before" then
                        op("isa_rebind_before", {
                            `local x = pick({make}, tbl)`,
                            `x = pick({make}, {r2}.make())`,
                            `if class.isinstance(x, {r}) then`,
                            `    local got = {use}`,
                            "    return got",
                            "end",
                            "return -1",
                        })
                    else
                        -- the closure is declared outside the branch, so its write is not in the region:
                        -- only "this local is written by a nested function" rules the proof out
                        op("isa_rebind_closure", {
                            `local x = pick({make}, {r2}.make())`,
                            "local swap = function()",
                            `    x = {other}`,
                            "end",
                            `if class.isinstance(x, {r}) then`,
                            "    swap()",
                            `    local got = {use}`,
                            "    return got",
                            "end",
                            "return -1",
                        })
                    end
                elseif form == "assert" or form == "assert_msg" or form == "assert_rebind" or form == "assert_closure" then
                    -- `assert(class.isinstance(x, C))` proves x for the rest of the block, so the same
                    -- writes that unprove a branch's local must unprove this one. The asserts that fail
                    -- must raise, not run proven code against the wrong value.
                    const m = rng.pick(instance_methods(c, true))
                    const use = if m then `x:{m.name}()` else read
                    const elsewhere = filter(classes, function(o)
                        return o ~= c
                    end)
                    const other = if #elsewhere > 0 and rng.chance(0.75)
                        then `{ref(rng.pick(elsewhere), c)}.make()`
                        else rng.pick({ "tbl", "nil", "legacy" })

                    if form == "assert" then
                        op("isa_assert", {
                            `local x = pick({make}, {r2}.make())`,
                            `assert(class.isinstance(x, {r}))`,
                            `local got = {use}`,
                            "return got",
                        })
                    elseif form == "assert_msg" then
                        op("isa_assert_msg", {
                            `local x = pick({make}, {r2}.make())`,
                            `assert(class.isinstance(x, {r}), "not a {c.name}")`,
                            `local got = {use}`,
                            "return got",
                        })
                    elseif form == "assert_rebind" then
                        op("isa_assert_rebind", {
                            `local x = pick({make}, {r2}.make())`,
                            `assert(class.isinstance(x, {r}))`,
                            `x = {other}`,
                            `local got = {use}`,
                            "return got",
                        })
                    else
                        op("isa_assert_closure", {
                            `local x = pick({make}, {r2}.make())`,
                            "local swap = function()",
                            `    x = {other}`,
                            "end",
                            `assert(class.isinstance(x, {r}))`,
                            "swap()",
                            `local got = {use}`,
                            "return got",
                        })
                    end
                else
                    op("isa_bad", { `local x = {make}`, "return class.isinstance(x, pick(tbl, 5))" })
                end
            elseif kind == "classlib" then
                const arg = rng.pick({ make, r, "tbl", "5", "nil", "legacy" })
                const fn = rng.pick({ "of", "name", "isinstance", "fields" })
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
                const expr = rng.pick({
                    "o + 1", "o + o", "#o", "o == o2", "o < o2", "tostring(o)", "-o", "o .. 'x'", "getmetatable(o)",
                    "setmetatable(o, {})", "type(o)", "typeof(o)", "o()",
                })
                op(kind, { `local o, o2 = {make}, {make}`, `return {expr}` })
            elseif kind == "init_explicit" then
                -- `__init` is never readable by name, so this must raise the same error at every level and in every
                -- codegen mode; the result reports whether it raised and with which message, which the runner compares
                op(kind, {
                    `local o = {make}`,
                    `local ok, err = pcall(function() o:__init() end)`,
                    `return ok, not ok and string.find(tostring(err), "'__init' of '{c.name}' cannot be accessed or called explicitly", 1, true) ~= nil`,
                })
            elseif kind == "coroutine" then
                op(kind, {
                    `local o = {make}`,
                    "local co = coroutine.wrap(function()",
                    `    local ok, result = pcall(function() return o:{rng.pick(instance_methods(c, true)).name}({""}) end)`,
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
                const key = rng.pick(c.fields).name
                op(kind, { `local o = {make}`, `local k = pick("{key}", "{key}")`, "return o[k]" })
            elseif kind == "raw" then
                const call = rng.pick({ "rawget(o, 'a')", "rawset(o, 'a', 1)", "rawlen(o)", "rawequal(o, o)", "next(o)" })
                op(kind, { `local o = {make}`, `return {call}` })
            elseif kind == "iterate" then
                const iterable = rng.pick({ "pairs(o)", "ipairs(o)", "o" })
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

    -- traits ------------------------------------------------------------------------------------------------------

    const traits: { Trait } = {}
    -- one access specifier and constness per expected field name, shared by every trait expecting it
    const expect_flags: { [string]: { private: boolean, const: boolean } } = {}

    const function trait_ref(t: Trait, from_module: boolean): string
        if t.module and not from_module then
            return `mod.{t.name}`
        end
        return t.name
    end

    const function field_kind(name: string): FieldKind
        return if name == "name" or name == "tag" then "string" elseif name == "items" then "table" else "number"
    end

    const function trait_fn(t: Trait, name: string): TraitFn?
        for _, fn in t.fns do
            if fn.name == name then
                return fn
            end
        end
        return nil
    end

    const function public_instance_fns(t: Trait): { TraitFn }
        return filter(t.fns, function(fn)
            return not fn.static and not fn.private
        end)
    end

    const function implements_trait(c: Cls, t: Trait): boolean
        return contains(c.trait_set, t)
    end

    const function make_traits()
        if not rng.chance(0.85) then
            return
        end

        for i = 0, rng.int(1, 5) - 1 do
            const module = rng.chance(0.3)
            const t: Trait = {
                name = (if module then "U" else "T") .. i,
                index = i,
                module = module,
                params = {},
                fields = {},
                expect_fields = {},
                expect_fns = {},
                expects_maybe = rng.chance(0.3),
                expects_init = false,
                fns = {},
                needs = {},
                overrides = {},
                field_overrides = {},
                rank = rng.int(1, 1000),
                create = false,
                tostring = rng.chance(0.2),
                implementors = {},
            }

            if rng.chance(0.3) then
                for k = 1, rng.int(1, 2) do
                    table.insert(t.params, { name = `t{i}_p{k}`, default = if rng.chance(0.6) then lit(rng, "number") else nil })
                end
            end

            for k = 1, rng.int(0, 3) do
                table.insert(t.fields, {
                    name = `t{i}_f{k}`,
                    private = rng.chance(0.35),
                    const = rng.chance(0.25),
                    default = rng.pick({ lit(rng, "number"), "{}", "nextId()", '"s"', "pause({})", "gpause(nextId())" }),
                })
            end

            for _, name in rng.sample(FIELD_NAMES, rng.int(0, 2)) do
                if not expect_flags[name] then
                    expect_flags[name] = { private = rng.chance(0.4), const = rng.chance(0.3) }
                end
                table.insert(t.expect_fields, name)
            end

            t.expect_fns = rng.sample({ "get", "describe", "secret" }, rng.int(0, 2))
            t.create = #t.params == 0 and rng.chance(0.35)
            table.insert(traits, t)
        end

        -- `needs` follows a random order unrelated to declaration order, so a trait may need one declared after it.
        -- Only traits without parameters are needed: a needed trait with parameters would have to be listed by every
        -- implementing class. A trait in the required module can't name one in main.
        for _, t in traits do
            const candidates = filter(traits, function(o)
                return o ~= t and o.rank < t.rank and #o.params == 0 and (not t.module or o.module)
            end)
            t.needs = rng.sample(candidates, rng.int(0, 2))
        end
    end

    -- the functions and fields some trait already overrides; each is overridden at most once per program, so no class
    -- meets two traits overriding the same member without one needing the other
    const overridden_fns: { [string]: boolean } = {}
    const overridden_fields: { [string]: boolean } = {}

    -- a trait's functions; after `needs` is known, since a default may call a needed trait's
    const function make_trait_fns(t: Trait)
        const i = t.index

        -- a needed trait's field declared again, which classes implementing this trait get instead: as a constant, or
        -- computed (`(n + 0)` isn't a constant default) so it also goes through the trait's initializer
        for _, needed in t.needs do
            for _, f in needed.fields do
                if not overridden_fields[f.name] and rng.chance(0.3) then
                    overridden_fields[f.name] = true
                    const value = tostring(3000 + i * 100 + #t.fields)
                    const default = if rng.chance(0.5) then value else `({value} + 0)`
                    table.insert(t.fields, { name = f.name, private = f.private, const = f.const, default = default })
                    t.field_overrides[f.name] = value
                end
            end
        end

        const own = table.clone(t.fields)
        for _, p in t.params do
            table.insert(own, { name = p.name, private = false, const = false, default = p.default or "nil" })
        end
        const mutable = filter(own, function(f)
            return not f.const
        end)

        -- m1 returns a constant, so what dispatch returns is known
        table.insert(t.fns, { name = `t{i}_m1`, private = false, static = false, final = rng.chance(0.2), params = {}, body = { `return "t{i}m1"` }, constant = `"t{i}m1"` })

        -- an override of a needed trait's m1, which classes implementing this trait run instead
        for _, needed in t.needs do
            const base = trait_fn(needed, `t{needed.index}_m1`)
            if base and not base.final and not overridden_fns[base.name] and rng.chance(0.4) then
                overridden_fns[base.name] = true
                const value = `"ov{i}:{base.name}"`
                table.insert(t.fns, { name = base.name, private = false, static = false, final = false, params = {}, body = { `return {value}` }, constant = value })
                t.overrides[base.name] = value
            end
        end
        table.insert(t.fns, { name = `t{i}_s1`, private = false, static = true, final = false, params = { "x" }, body = { `return (x or 0) + {i}` } })
        if rng.chance(0.4) then
            table.insert(t.fns, { name = `t{i}_pm`, private = true, static = false, final = false, params = {}, body = { `return "private{i}"` } })
        end

        for k = 2, rng.int(2, 6) do
            const name = `t{i}_m{k}`
            const shape = rng.pick({
                "field", "bump", "expectfield", "expectfn", "maybe", "own", "needed", "closure", "static", "implements",
                "yield", "private", "upvalue", "selfname",
            })
            local body: { string }? = nil
            if shape == "field" and #own > 0 then
                body = { `return self.{rng.pick(own).name}` }
            elseif shape == "bump" and #mutable > 0 then
                const f = rng.pick(mutable).name
                body = { `self.{f} = (tonumber(self.{f}) or 0) + 1`, `return self.{f}` }
            elseif shape == "expectfield" and #t.expect_fields > 0 then
                body = { `return tostring(self.{rng.pick(t.expect_fields)})` }
            elseif shape == "expectfn" and #t.expect_fns > 0 then
                body = { `return tostring(self:{rng.pick(t.expect_fns)}())` }
            elseif shape == "maybe" and t.expects_maybe then
                body = { "if self.maybe then", "    return self:maybe()", "end", 'return "nomaybe"' }
            elseif shape == "own" then
                body = { `return "own:" .. tostring(self:t{i}_m1())` }
            elseif shape == "needed" and #t.needs > 0 then
                const needed = rng.pick(t.needs)
                body = { `return "need:" .. tostring(self:t{needed.index}_m1())` }
            elseif shape == "closure" and #own > 0 then
                body = { `local f = function() return self.{rng.pick(own).name} end`, "return f()" }
            elseif shape == "static" then
                body = { `return {t.name}.t{i}_s1(2)` }
            elseif shape == "implements" then
                body = { `return class.implements(self, {t.name})` }
            elseif shape == "yield" then
                body = {
                    "if coroutine.running() ~= shared.mainThread then",
                    `    coroutine.yield(self:t{i}_m1())`,
                    "end",
                    "return 6",
                }
            elseif shape == "private" and trait_fn(t, `t{i}_pm`) then
                body = { `return self:t{i}_pm()` }
            elseif shape == "upvalue" then
                body = { "return nextId() > 0" }
            elseif shape == "selfname" then
                body = { "return class.name(self)" }
            end

            if body then
                table.insert(t.fns, { name = name, private = rng.chance(0.15), static = false, final = rng.chance(0.2), params = {}, body = body })
            end
        end
    end

    -- which traits a class implements; before addMethods, since meeting an expectation can change a field's flags
    const function implement_traits(c: Cls)
        const candidates = filter(traits, function(t)
            return not c.module or t.module
        end)
        if #candidates == 0 then
            return
        end

        for _ = 1, 4 do
            const listed = rng.sample(candidates, rng.int(0, 3))

            -- the listed traits and the ones their `needs` imply, each once
            const set: { Trait } = {}
            const queue = table.clone(listed)
            while #queue > 0 do
                const t = table.remove(queue, 1) :: Trait
                if not contains(set, t) then
                    table.insert(set, t)
                    for _, needed in t.needs do
                        table.insert(queue, needed)
                    end
                end
            end

            -- two traits providing `__tostring` would clash; clashes are the conformance tests' job
            const with_tostring = filter(set, function(t)
                return t.tostring
            end)
            if #with_tostring <= 1 then
                for _, t in listed do
                    local args: string? = nil
                    if #t.params > 0 then
                        const form = rng.pick({ "none", "empty", "constants", "params" })
                        if form == "empty" then
                            args = ""
                        elseif form == "constants" or (form == "params" and c.ctor ~= "primary") then
                            const values = {}
                            for _ in t.params do
                                table.insert(values, rng.pick({ lit(rng, "number"), "nextId()", "pause(nextId())" }))
                            end
                            args = table.concat(values, ", ")
                        elseif form == "params" then
                            const values = {}
                            for _ in t.params do
                                table.insert(values, `(tonumber({rng.pick(c.primary_params).name}) or 0) + 1`)
                            end
                            args = table.concat(values, ", ")
                        end
                    end
                    table.insert(c.implements, { trait = t, args = args })
                end
                c.trait_set = set
                break
            end
        end

        for _, t in c.trait_set do
            table.insert(t.implementors, c)

            for _, name in t.expect_fields do
                const flags = expect_flags[name]
                local found: Field? = nil
                for _, f in c.fields do
                    if f.name == name then
                        found = f
                    end
                end
                if found then
                    found.private = flags.private
                    found.const = flags.const
                else
                    found = { name = name, private = flags.private, const = flags.const, kind = field_kind(name), default = if rng.chance(0.5) then lit(rng, field_kind(name)) else nil }
                    table.insert(c.fields, found :: Field)
                end
                const f = found :: Field
                if f.const and c.ctor == "pod" and f.default == nil then
                    f.default = lit(rng, f.kind)
                end
            end

            if t.expects_maybe and rng.chance(0.5) then
                c.defines_maybe = true
            end
        end
    end

    -- what implementing adds to a class besides fields: expected functions, and overrides of trait defaults
    const function add_trait_members(c: Cls)
        const function has(name: string): boolean
            return #filter(c.methods, function(m)
                return m.name == name
            end) > 0
        end

        local needs_secret = false
        for _, t in c.trait_set do
            if contains(t.expect_fns, "secret") then
                needs_secret = true
            end
        end
        if needs_secret and not has("secret") then
            table.insert(c.methods, method("secret", true, false, { `return "secret:{c.name}"` }))
        end
        if c.defines_maybe and not has("maybe") then
            table.insert(c.methods, method("maybe", false, false, { `return "maybe:{c.name}"` }))
        end

        for _, t in c.trait_set do
            for _, fn in t.fns do
                const overridable = not fn.static and not fn.final and not fn.private
                if overridable and rng.chance(0.15) and not has(fn.name) then
                    table.insert(c.methods, method(fn.name, false, false, { `return "override:{c.name}"` }))
                    table.insert(c.overrides, fn.name)
                end
            end

            -- a trait field declared again by the class, which keeps the trait's access and constness
            for _, f in t.fields do
                const already = #filter(c.field_overrides, function(o)
                    return o.name == f.name
                end) > 0
                if not already and rng.chance(0.12) then
                    const value = tostring(5000 + #c.field_overrides)
                    table.insert(c.field_overrides, { name = f.name, private = f.private, const = f.const, kind = "number", default = value })
                end
            end
        end
    end

    -- The value a new object of `c` has in the trait field `name`, when the generator knows it: the class's own value
    -- if it declares the field again, else the value of the trait overriding it, else the providing trait's constant
    const function known_trait_field(c: Cls, name: string): string?
        for _, o in c.field_overrides do
            if o.name == name then
                return o.default
            end
        end

        for _, t in c.trait_set do
            const value = t.field_overrides[name]
            if value then
                return value
            end
        end

        for _, t in c.trait_set do
            for _, f in t.fields do
                if f.name == name and tonumber(f.default) then
                    return f.default
                end
            end
        end

        return nil
    end

    const function emit_trait(t: Trait): { string }
        const out = {}
        local head = `trait {t.name}`
        if #t.params > 0 then
            const params = {}
            for _, p in t.params do
                table.insert(params, `public {p.name}` .. (if p.default then ` = {p.default}` else ""))
            end
            head ..= "(" .. table.concat(params, ", ") .. ")"
        end
        if #t.needs > 0 then
            const names = {}
            for _, needed in t.needs do
                table.insert(names, trait_ref(needed, t.module))
            end
            head ..= " needs " .. table.concat(names, ", ")
        end
        table.insert(out, head)

        for _, name in t.expect_fields do
            const flags = expect_flags[name]
            table.insert(out, `    expect {if flags.private then "private" else "public"} {if flags.const then "const " else ""}{name}`)
        end
        for _, name in t.expect_fns do
            table.insert(out, `    expect {if name == "secret" then "private" else "public"} function {name}(self)`)
        end
        if t.expects_maybe then
            table.insert(out, "    expect public function maybe?(self)")
        end
        for _, f in t.fields do
            table.insert(out, `    {if f.private then "private" else "public"} {if f.const then "const " else ""}{f.name} = {f.default}`)
        end
        for _, fn in t.fns do
            const params = table.clone(fn.params)
            if not fn.static then
                table.insert(params, 1, "self")
            end
            table.insert(out, `    {if fn.private then "private" else "public"} {if fn.final then "final " else ""}function {fn.name}({table.concat(params, ", ")})`)
            for _, line in fn.body do
                table.insert(out, "        " .. line)
            end
            table.insert(out, "    end")
        end
        if t.tostring then
            table.insert(out, `    public function __tostring(self)`)
            table.insert(out, `        return "<{t.name}>"`)
            table.insert(out, "    end")
        end
        if t.create then
            -- a factory: constructs its implementing classes, through their private constructors too
            const makeable = filter(t.implementors, function(c)
                return not t.module or c.module
            end)
            table.insert(out, "    public function __create(n)")
            for index, c in makeable do
                table.insert(out, `        if n == {index} then`)
                table.insert(out, `            return {ctor_call(c, ({ module = t.module } :: any) :: Cls)}`)
                table.insert(out, "        end")
            end
            table.insert(out, "        return nil")
            table.insert(out, "    end")
        end
        table.insert(out, "end")
        return out
    end

    -- operations on traits; `check` states what the generator knows must hold
    const function gen_trait_ops()
        if #traits == 0 then
            return
        end

        for _ = 1, rng.int(10, 30) do
            const t = rng.pick(traits)
            const T = trait_ref(t, false)
            const c = rng.pick(classes)
            const C = ref(c)
            const implements = implements_trait(c, t)
            const fns = public_instance_fns(t)
            const kind = rng.pick({
                "call", "dispatch", "dispatch_wrong", "implements", "refine", "type", "blocked", "ctor", "field",
                "maybe", "isolated", "gc", "coroutine", "static", "tostring", "fields", "annotated", "private_fn", "overridden",
                "settle",
            })

            if kind == "overridden" then
                -- a class implementing a trait that overrides a needed trait's function runs the override, unless it
                -- defines the function itself
                const overriding = filter(c.trait_set, function(o)
                    return next(o.overrides) ~= nil
                end)
                if #overriding > 0 then
                    const ov = rng.pick(overriding)
                    const names = {}
                    for name in ov.overrides do
                        table.insert(names, name)
                    end
                    table.sort(names)
                    const name = rng.pick(names)
                    if not contains(c.overrides, name) then
                        op("t_overridden", {
                            `local o = {C}.make()`,
                            `local r = o:{name}()`,
                            `check(r == {ov.overrides[name]}, "{c.name}:{name}() returned " .. tostring(r) .. " instead of {ov.name}'s override")`,
                            "return r",
                        }, false, true)
                    end
                end
            elseif kind == "call" and implements then
                const fn = rng.pick(fns)
                op("t_call", { `local o = {C}.make()`, `local r = o:{fn.name}()`, "return r" }, false, true)
            elseif kind == "dispatch" and implements then
                -- through the trait, the trait's default runs even when the class overrides it
                const fn = rng.pick(fns)
                const lines = { `local o = {C}.make()`, `local viaTrait = {T}.{fn.name}(o)` }
                if contains(c.overrides, fn.name) then
                    table.insert(lines, `check(viaTrait ~= "override:{c.name}", "{T}.{fn.name} on {c.name} ran the override over the default")`)
                end
                if fn.constant then
                    table.insert(lines, `check(viaTrait == {fn.constant}, "{T}.{fn.name} on {c.name} returned " .. tostring(viaTrait))`)
                end
                table.insert(lines, "return viaTrait")
                op("t_dispatch", lines, false, true)
            elseif kind == "dispatch_wrong" and #fns > 0 then
                const fn = rng.pick(fns)
                const receiver = if implements then rng.pick({ "tbl", "nil", "5", "legacy" }) else `{C}.make()`
                op("t_dispatch_wrong", {
                    `local ok = pcall({T}.{fn.name}, {receiver})`,
                    `check(not ok, "{T}.{fn.name} accepted a receiver that doesn't implement {t.name}")`,
                    "return ok",
                }, false, true)
            elseif kind == "implements" then
                op("t_implements", {
                    `local o = {C}.make()`,
                    `local got = class.implements(o, {T})`,
                    `check(got == {tostring(implements)}, "class.implements({c.name}, {t.name}) is " .. tostring(got))`,
                    `check(class.isinstance(o, {T}) == false, "class.isinstance with a trait was true")`,
                    "return got",
                }, false, true)
            elseif kind == "refine" and #fns > 0 then
                const c2 = rng.pick(classes)
                const fn = rng.pick(fns)
                op("t_refine", {
                    `local x = pick({C}.make(), {ref(c2)}.make())`,
                    `if class.implements(x, {T}) then`,
                    `    local r = x:{fn.name}()`,
                    "    return r",
                    "end",
                    'return "no"',
                }, false, true)
            elseif kind == "type" then
                op("t_type", {
                    `local t = {T}`,
                    `check(type(t) == "trait" and typeof(t) == "trait", "type of trait {t.name} is " .. type(t))`,
                    `check(type({C}) == "class", "type of class {c.name} is " .. type({C}))`,
                    `check(class.name(t) == "{t.name}", "class.name of trait {t.name}")`,
                    "return type(t)",
                }, false, true)
            elseif kind == "blocked" then
                const lines = {}
                for _, f in t.fields do
                    table.insert(lines, `check(not pcall(function() return {T}.{f.name} end), "read field {f.name} through {t.name}")`)
                end
                for _, name in t.expect_fns do
                    table.insert(lines, `check(not pcall(function() return {T}.{name} end), "read expected {name} through {t.name}")`)
                end
                table.insert(lines, `check({T}.t{t.index}_s1(2) == {2 + t.index}, "static through {t.name}")`)
                table.insert(lines, `return {T}.t{t.index}_s1(2)`)
                op("t_blocked", lines, false, true)
            elseif kind == "ctor" then
                if t.create then
                    const makeable = filter(t.implementors, function(o)
                        return not t.module or o.module
                    end)
                    op("t_ctor", {
                        `local r = {T}({rng.int(1, #makeable + 1)})`,
                        `check(r == nil or class.implements(r, {T}), "{t.name}'s factory made something that doesn't implement it")`,
                        "return if r == nil then 'nil' else class.name(r)",
                    }, false, true)
                else
                    op("t_ctor_none", { `local ok = pcall({T})`, `check(not ok, "called {t.name}, which has no __create")`, "return ok" }, false, true)
                end
            elseif kind == "field" and implements and #t.fields > 0 then
                const f = rng.pick(t.fields)
                if f.private then
                    op("t_field_private", { `local o = {C}.make()`, `check(not pcall(function() return o.{f.name} end), "read private trait field {f.name} from outside")`, "return 1" }, false, true)
                elseif f.const then
                    op("t_field_const", { `local o = {C}.make()`, `check(not pcall(function() o.{f.name} = 1 end), "wrote const trait field {f.name} from outside")`, `return o.{f.name}` }, false, true)
                else
                    op("t_field", { `local o = {C}.make()`, `o.{f.name} = 42`, `check(o.{f.name} == 42, "trait field {f.name} didn't keep a write")`, `return o.{f.name}` }, false, true)
                end
            elseif kind == "maybe" and implements and t.expects_maybe then
                op("t_maybe", {
                    `local o = {C}.make()`,
                    `check((o.maybe ~= nil) == {tostring(c.defines_maybe)}, "optional maybe on {c.name}")`,
                    "return o.maybe ~= nil",
                }, false, true)
            elseif kind == "isolated" and implements and #t.fields > 0 then
                -- trait state lives in each object: a table default is fresh per object, and a write stays in its object
                const f = rng.pick(t.fields)
                const lines = { `local a, b = {C}.make(), {C}.make()` }
                if not f.private and f.default == "{}" and known_trait_field(c, f.name) == nil then
                    table.insert(lines, `check(a.{f.name} ~= b.{f.name}, "objects share trait table field {f.name}")`)
                end
                if not f.private and not f.const then
                    table.insert(lines, `local before = b.{f.name}`)
                    table.insert(lines, `a.{f.name} = "changed"`)
                    table.insert(lines, `check(b.{f.name} == before, "a write to one object's trait field reached another")`)
                end
                table.insert(lines, "return true")
                op("t_isolated", lines, false, true)
            elseif kind == "gc" and implements then
                op("t_gc", {
                    "local keep = {}",
                    "for i = 1, 2000 do",
                    `    keep[i % 40 + 1] = {C}.make()`,
                    "    local junk = { i }",
                    "end",
                    "local s = ''",
                    "for _, o in keep do",
                    `    s ..= tostring(o:t{t.index}_m1())`,
                    "end",
                    "return #s",
                }, false, true)
            elseif kind == "coroutine" and implements and #fns > 0 then
                const fn = rng.pick(fns)
                op("t_coroutine", {
                    `local o = {C}.make()`,
                    "local co = coroutine.wrap(function()",
                    `    local ok, r = pcall({T}.{fn.name}, o)`,
                    "    coroutine.yield(ok)",
                    "    return if ok then r else 'err'",
                    "end)",
                    "local a = co()",
                    "local b = co()",
                    "return a, b",
                }, false, true)
            elseif kind == "static" and implements then
                op("t_static", {
                    `local viaClass = {C}.t{t.index}_s1(2)`,
                    `check(viaClass == {T}.t{t.index}_s1(2), "a trait static differs through {c.name}")`,
                    "return viaClass",
                }, false, true)
            elseif kind == "tostring" and implements and t.tostring then
                const own_tostring = #filter(c.metamethods, function(mm)
                    return mm.name == "__tostring"
                end) > 0
                const lines = { `local o = {C}.make()`, "local s = tostring(o)" }
                if not own_tostring then
                    table.insert(lines, `check(s == "<{t.name}>", "{t.name}'s __tostring on {c.name} gave " .. s)`)
                end
                table.insert(lines, "return s")
                op("t_tostring", lines, false, true)
            elseif kind == "fields" and implements then
                const lines = { `local fields = class.fields({C}.make())` }
                -- a public trait field is one of the object's fields (a nil one is listed as `none`)
                for _, f in t.fields do
                    if not f.private then
                        table.insert(lines, `check(fields.{f.name} ~= nil, "class.fields of {c.name} lacks trait field {f.name}")`)
                    end
                end
                table.insert(lines, "return sortedKeys(fields)")
                op("t_fields", lines, false, true)
            elseif kind == "settle" and implements then
                -- every public trait field whose value the generator knows has it in a new object: the most specific
                -- declaration's (the class over a trait, a trait over one it needs), whatever order they're written in
                const lines = { `local o = {C}.make()` }
                const seen: { [string]: boolean } = {}
                for _, owner in c.trait_set do
                    for _, f in owner.fields do
                        const value = known_trait_field(c, f.name)
                        if value and not f.private and not seen[f.name] then
                            seen[f.name] = true
                            const message = `"{c.name}.{f.name} is " .. tostring(o.{f.name}) .. ", expected {value}"`
                            table.insert(lines, `check(o.{f.name} == {value}, {message})`)
                        end
                    end
                end
                table.insert(lines, "return true")
                op("t_settle", lines, false, true)
            elseif kind == "annotated" and implements and #fns > 0 then
                const fn = rng.pick(fns)
                op("t_annotated", { `local o: {T} = {C}.make()`, `local r = o:{fn.name}()`, "return r" }, false, true)
            elseif kind == "private_fn" and implements and trait_fn(t, `t{t.index}_pm`) then
                op("t_private_fn", {
                    `local o = {C}.make()`,
                    `check(not pcall(function() return o:t{t.index}_pm() end), "called private trait function from outside")`,
                    "return 1",
                }, false, true)
            end
        end
    end

    const mod_classes: { Cls } = {}
    for i = 0, rng.int(0, 3) - 1 do
        const c = make_class(i, true)
        table.insert(mod_classes, c)
        table.insert(classes, c)
    end
    for i = 0, rng.int(2, 6) - 1 do
        table.insert(classes, make_class(i, false))
    end
    make_traits()
    -- a needed trait has the lower rank, so its functions exist by the time a trait needing it overrides one
    const by_rank = table.clone(traits)
    table.sort(by_rank, function(a, b)
        return a.rank < b.rank
    end)
    for _, t in by_rank do
        make_trait_fns(t)
    end
    for _, c in classes do
        implement_traits(c)
    end
    for _, c in classes do
        add_methods(c)
    end
    for _, c in classes do
        add_trait_members(c)
    end
    gen_ops()
    gen_trait_ops()

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
        -- Yields inside a coroutine the program made, then returns `v`: a default that calls it suspends construction
        -- partway, and the value it ends up with doesn't change. Never on the main thread (the CLI's is yieldable, and a
        -- yield there ends the program) or while the module loads. `gpause` is the same through a global, which a
        -- construction site can name, so trait initializers calling it can be inlined; ones calling `pause` can't.
        "local function pause(v)",
        "    if shared.mainThread ~= nil and coroutine.running() ~= shared.mainThread and coroutine.isyieldable() then",
        "        coroutine.yield()",
        "    end",
        "    return v",
        "end",
        "function gpause(v)",
        "    return pause(v)",
        "end",
    }

    -- `--!trust` only takes effect in the configs that set DebugLuwuCompilerTrustsTypeAnnotations
    const mod = { "--!nocheck", "--!optimize OPTLEVEL", "--!trust" }
    table.move(prelude, 1, #prelude, #mod + 1, mod)
    const exports = {}
    for _, t in traits do
        if t.module then
            const lines = emit_trait(t)
            table.move(lines, 1, #lines, #mod + 1, mod)
            table.insert(exports, `{t.name} = {t.name}`)
        end
    end
    for _, c in mod_classes do
        const lines = emit_class(c)
        table.move(lines, 1, #lines, #mod + 1, mod)
        table.insert(exports, `{c.name} = {c.name}`)
    end
    table.insert(exports, "__shared = shared")
    table.insert(mod, "return { " .. table.concat(exports, ", ") .. " }")

    const main = { "--!nocheck", "--!optimize OPTLEVEL", "--!trust" }
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
        "    elseif t == 'class' or t == 'trait' then",
        "        return t .. ':' .. class.name(v)",
        "    end",
        "    return t",
        "end",
        "local function check(cond, what)",
        "    if not cond then",
        "        print('__wrong', what)",
        "    end",
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
    for _, t in traits do
        if not t.module then
            const lines = emit_trait(t)
            table.move(lines, 1, #lines, #main + 1, main)
        end
    end
    for _, c in classes do
        if not c.module then
            const lines = emit_class(c)
            table.move(lines, 1, #lines, #main + 1, main)
        end
    end
    const target = rng.pick(classes)
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
    table.insert(main, `print("__done", {op_count})`)

    return table.concat(main, "\n") .. "\n", table.concat(mod, "\n") .. "\n"
end

return {
    generate = generate,
}

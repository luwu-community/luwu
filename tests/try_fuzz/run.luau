--!strict
--[=[
    Differential fuzzer for `try` expressions. Each generated program evaluates expressions twice on the same line, once
    as `try E` and once as `pcall(function() return E end)`, and checks the two agree: the same success, value and error.

    The expressions are built to stress what a catch can get wrong: recursion deep enough to reallocate the stack and
    the call frames inside a range, stack overflow, errors from C callbacks and metamethods, coroutines that yield inside
    a range, `pcall` inside `try`, nested `try`s, and the GC running while an error is raised. Results are read both
    through the object and through the forms the compiler keeps in registers.

    Usage: seal tests/try_fuzz/run.luau --luwu build-assert/luwu [--count 300] [--seed 1] [--out dir]
]=]

const fs = require("@std/fs")
const env = require("@std/env")
const process = require("@std/process")

type Options = {
    luwu: string,
    count: number,
    seed: number,
    out: string,
}

local function parse_options(): Options
    const options: Options = {
        luwu = "build-assert/luwu",
        count = 300,
        seed = 1,
        out = "try_fuzz_failures",
    }
    local i = 1
    while i <= #env.args do
        const flag = env.args[i]
        const value = env.args[i + 1]
        if flag == "--luwu" then
            options.luwu = value
        elseif flag == "--count" then
            options.count = assert(tonumber(value), "--count takes a number")
        elseif flag == "--seed" then
            options.seed = assert(tonumber(value), "--seed takes a number")
        elseif flag == "--out" then
            options.out = value
        else
            error(`unknown option {flag}`)
        end
        i += 2
    end
    return options
end

-- the depths a call chain recurses to: small, then enough to grow the stack and the frame array
const DEPTHS = { 0, 1, 5, 40, 400, 3000 }
-- what `raise(kind)` does: kinds 1 to 7 raise in different ways, the rest return kind * 10
const KINDS = 10

const PRELUDE = [[
local result = collections.result

local function deep(n, f, ...)
    local a, b, c, d = n, n + 1, n + 2, n + 3
    if n <= 0 then
        return f(...)
    end
    local r = deep(n - 1, f, ...)
    if a + b + c + d < 0 then
        return nil
    end
    return r
end

local function raise(kind)
    if kind == 1 then
        error("boom")
    elseif kind == 2 then
        error({ code = 7 })
    elseif kind == 3 then
        error(42)
    elseif kind == 4 then
        local t = nil
        return t.x
    elseif kind == 5 then
        return string.rep()
    elseif kind == 6 then
        collectgarbage("step")
        error("after gc")
    elseif kind == 7 then
        return deep(1e7, raise, 0)
    end
    return kind * 10
end

-- yields once, then raises or returns like `raise`
local function yielding(kind)
    coroutine.yield()
    return raise(kind)
end

local function equal(a, b)
    if rawequal(a, b) then
        return true
    end
    if type(a) == "table" and type(b) == "table" then
        return a.code == b.code
    end
    return a ~= a and b ~= b
end

local function describe(value)
    if type(value) == "table" then
        return `table code {value.code}`
    end
    return tostring(value)
end

-- the result as an object
local function check_object(case, ok, value, r)
    if ok then
        assert(class.isinstance(r, result.ok), `case {case}: pcall succeeded with {describe(value)}, try gave {r}`)
        assert(equal(r.value, value), `case {case}: value {describe(r.value)} vs {describe(value)}`)
    else
        assert(class.isinstance(r, result.err), `case {case}: pcall failed with {describe(value)}, try gave {r}`)
        assert(equal(r.error, value), `case {case}: error {describe(r.error)} vs {describe(value)}`)
        assert(type(r.traceback) == "string", `case {case}: no traceback`)
    end
end

-- a nested try's value, through the object
local function unwrap(r)
    return r:expect("inner")
end

-- the result read through `ok` and `err`, which the compiler keeps in registers
local function check_methods(case, ok, value, got, raised)
    if ok then
        assert(equal(got, value) and raised == none, `case {case}: ok() {describe(got)} err() {describe(raised)} vs {describe(value)}`)
    else
        assert(got == none and equal(raised, value), `case {case}: ok() {describe(got)} err() {describe(raised)} vs error {describe(value)}`)
    end
end

]]

type Rng = { next: (self: Rng, lo: number, hi: number) -> number }

local function new_rng(seed: number): Rng
    local state = seed
    return {
        next = function(_, lo: number, hi: number): number
            -- an LCG is enough to pick shapes, and gives the same programs on every platform
            state = (state * 1103515245 + 12345) % 2147483648
            return lo + state % (hi - lo + 1)
        end,
    }
end

local function pick<T>(rng: Rng, list: { T }): T
    return list[rng:next(1, #list)]
end

-- An expression that may raise, as source text. `inner` adds a nesting level.
local function gen_expr(rng: Rng, depth: number): string
    const n = pick(rng, DEPTHS)
    const kind = rng:next(1, KINDS)
    -- stack overflow is slow, so it's rare
    const k = if kind == 7 and rng:next(1, 4) > 1 then 8 else kind
    const call = `deep({n}, raise, {k})`

    -- none of these starts with `(`, which would make `try (` a call
    const shape = rng:next(1, if depth < 2 then 8 else 6)
    if shape == 1 then
        return call
    elseif shape == 2 then
        return 'string.gsub("ab", "%w", function() return ' .. call .. " end)"
    elseif shape == 3 then
        return "setmetatable({}, { __index = function() return " .. call .. " end }).field"
    elseif shape == 4 then
        return "table.sort({ 3, 1, 2 }, function(a, b) local _ = " .. call .. "; return a < b end)"
    elseif shape == 5 then
        return call .. " + 1"
    elseif shape == 6 then
        return `select(2, pcall(raise, {k}))`
    elseif shape == 7 then
        -- a nested try, unwrapped through the object
        return `unwrap(try {gen_expr(rng, depth + 1)})`
    else
        return `#tostring({gen_expr(rng, depth + 1)})`
    end
end

local function gen_case(rng: Rng, case: number): string
    const form = rng:next(1, 5)
    const expr = gen_expr(rng, 0)
    const gc = if rng:next(1, 6) == 1 then "    collectgarbage(\"collect\")\n" else ""

    if form == 1 then
        -- the object
        return `do\n{gc}    local ok, v = pcall(function() return {expr} end); local r = try {expr}\n    check_object({case}, ok, v, r)\nend\n`
    elseif form == 2 then
        -- `ok` and `err`, kept in registers
        return `do\n{gc}    local ok, v = pcall(function() return {expr} end); local r = try {expr}\n    check_methods({case}, ok, v, r:ok(), r:err())\nend\n`
    elseif form == 3 then
        -- fields kept in registers, including the other variant's field, which raises
        return `do\n{gc}    local ok, v = pcall(function() return {expr} end); local r = try {expr}\n`
            .. `    if ok then\n        assert(equal(r.value, v), "case {case}: value")\n`
            .. `        local wrong = try r.error\n        assert(class.isinstance(wrong, result.err), "case {case}: ok had an error field")\n`
            .. `    else\n        assert(equal(r.error, v) and r.line ~= nil and r.traceback ~= nil, "case {case}: error")\n`
            .. `        local wrong = try r.value\n        assert(class.isinstance(wrong, result.err), "case {case}: err had a value field")\n`
            .. `    end\nend\n`
    elseif form == 4 then
        -- inside a vararg function, with `...` in the range
        const n = pick(rng, DEPTHS)
        const k = rng:next(1, KINDS)
        return `do\n{gc}    local function v(...)\n`
            .. `        local ok, val = pcall(function(...) return deep({n}, raise, ...) end, ...); local r = try deep({n}, raise, ...)\n`
            .. `        check_object({case}, ok, val, r)\n    end\n    v({k})\nend\n`
    else
        -- inside a coroutine, around a call that yields first
        const n = pick(rng, DEPTHS)
        const k = rng:next(1, 6)
        const call = `deep({n}, yielding, {k})`
        return `do\n{gc}    local co = coroutine.wrap(function()\n`
            .. `        local ok, v = pcall(function() return {call} end); local r = try {call}\n`
            .. `        check_object({case}, ok, v, r)\n        return "done"\n    end)\n`
            .. `    co(); co(); assert(co() == "done", "case {case}: coroutine")\nend\n`
    end
end

local function gen_program(seed: number): string
    const rng = new_rng(seed)
    const parts = { `--!optimize {rng:next(0, 2)}\n`, PRELUDE }
    for case = 1, rng:next(10, 30) do
        table.insert(parts, gen_case(rng, case))
    end
    table.insert(parts, 'print("OK")\n')
    return table.concat(parts)
end

local function main()
    const options = parse_options()
    const programs = `{options.out}/programs`
    fs.makedir(programs, { create_missing = true, error_if_exists = false })

    local failures = 0
    for i = 0, options.count - 1 do
        const seed = options.seed + i
        const source = gen_program(seed)
        const path = `{programs}/{seed}.luwu`
        fs.writefile(path, source)

        for _, mode in { {}, { "--codegen" } } do
            const args = table.clone(mode)
            table.insert(args, "--fflags=DebugLuwuCollections=true")
            table.insert(args, path)
            const run = process.run({ program = options.luwu, args = args })
            if not run.ok or not string.find(run.stdout, "OK", 1, true) then
                failures += 1
                const label = if #mode > 0 then "codegen" else "interp"
                const output = if run.ok then run.stdout else run.err
                fs.writefile(`{options.out}/fail-{seed}-{label}.txt`, output .. "\n" .. run.stderr)
                print(`seed {seed} ({label}) failed: {string.sub(output, 1, 200)}`)
            end
        end
    end

    print(`{options.count} programs, {failures} failures`)
    if failures > 0 then
        process.exit(1)
    end
end

main()

--!nocheck

-- Regression test: an `export class` referenced later in the same module (including through
-- constructing another class from it) previously risked seeing the exported class as nil due to
-- export hoisting ordering. `export` statements force the module's implicit return to be the
-- (frozen) export table, which conflicts with an explicit top-level `return`. So (as in
-- exportclasses.luau) the exported class under test lives in its own chunk, loaded and run via
-- loadstring, while this outer chunk stays export-free and can `return 'OK'`. Since the inner
-- chunk can't `return` its result either, it stashes the result in a global instead.
local source = [[
    export class ReproCat
        name: string
        age: number
        public function __init(self, name, age)
            self.name = name
            self.age = age
        end
        function meow(self, message = "says meow")
            return `{self.name} {message}`
        end
    end

    class ReproList<T>
        private const inner: { T }

        public function __init(self, ...: T)
            self.inner = { ... }
        end
    end

    reproTestResult = ReproList(
        ReproCat("Taz", 12),
        ReproCat("Nanuk", 2)
    )
]]

local chunk, compileErr = loadstring(source)
assert(chunk, `failed to compile export hoisting repro chunk: {tostring(compileErr)}`)

local ok, err = pcall(chunk)
assert(ok, `expected ReproList/ReproCat construction to succeed, got error: {tostring(err)}`)

local result = reproTestResult
assert(typeof(result) == "object", `expected typeof(result) == "object", got {typeof(result)}`)

print("ok: exported class referenced by another class in the same module is not nil")

return 'OK'

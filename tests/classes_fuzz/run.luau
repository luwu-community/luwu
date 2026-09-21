--[[
    Differential and crash fuzzer for Luwu classes. Requires seal 0.8.x at >= 0.8.1 (ChildProcess:status);
    seal 0.9.0 breaks the std APIs used here.

        seal tests/classes_fuzz/run.luau --luau build-assert/luau [--start 1] [--count 100] [--jobs 16] [--work DIR] [--keep]

    For every seed, gen.luau writes a random class program (a main module plus a required module) and this script
    runs it with the given `luau` CLI at -O0/-O1/-O2, each interpreted, with --codegen and with --codegen-cold. Use an
    assert-enabled build (build-assert/) so internal invariants are checked too.

    Reported problems:
      CRASH        the process died from a signal or dumped core (asserts land here)
      TIMEOUT      the process ran longer than the per-run limit
      EXIT         nonzero exit code: an error escaped the program's top level
      INVALID      the generated program failed to parse or compile (a generator bug)
      INCOMPLETE   the program exited cleanly without printing its final marker
      VACUOUS      no operation succeeded at -O0 (a generator bug; nothing was really compared)
      DIVERGE      an operation's result differs from the -O0 interpreter
    Seeds with problems keep their files under the work directory, and the report prints how to rerun each.

    Exits 1 if any seed had a problem, 0 otherwise.
]]

local fs = require("@std/fs")
local process = require("@std/process")
local args = require("@std/args")
local str = require("@std/str")
local time = require("@std/time")

local gen = require("./gen")

const FLAGS = "--fflags="
    .. table.concat({
        "DebugLuauUserDefinedClasses=true",
        "DebugLuauUserDefinedClassesRuntime=true",
        "LuwuBetterUserDefinedClasses=true",
        "LuwuGenericNominals=true",
        "LuauExportValueSyntax=true",
        "LuauExportedClassIsNilWorkaround=true",
        "LuwuDefaultArguments=true",
        "LuwuNonePrimitive=true",
    }, ",")
-- Acting on a receiver's declared type is opt-in (see Compiler.cpp). It only changes O2, where inlining
-- runs, so it is a variant of the O2 configs rather than a second copy of the whole matrix.
const TRUST_ANNOTATIONS_FLAG = ",DebugLuwuCompilerTrustsTypeAnnotations=true"
const RUN_TIMEOUT_SECONDS = 60
const OUTPUT_TAIL_BYTES = 800

type DateTime = typeof(time.datetime.now())

type ProblemKind = "CRASH" | "TIMEOUT" | "EXIT" | "INVALID" | "INCOMPLETE" | "VACUOUS" | "DIVERGE"

type Config = {
    opt: number,
    mode: "" | "--codegen" | "--codegen-cold",
    trustAnnotations: boolean,
    tag: string,
}

type Outcome = { ok: true, stdout: string } | { ok: false, kind: ProblemKind, detail: string }

type Job = {
    seed: number,
    config: Config,
    child: process.PipedChild,
    deadline: DateTime,
    stdout: { string },
    stderr: { string },
}

-- per operation label, what a generated program printed for it: "ok" plus values, or "err"
type OpResults = { [string]: string }

const parsed = args.parse("classes_fuzz", "differential and crash fuzzing for Luwu classes"):simple(
    args.named("--luau", "path to the luau CLI to test (use an assert-enabled build)"):required(),
    args.named("--start", "first seed"):default("1"),
    args.named("--count", "number of programs to generate"):default("100"),
    args.named("--jobs", "processes to run at once"):default("16"),
    args.named("--work", "directory for generated programs"):default("classes_fuzz_work"),
    args.flag("--keep", "keep every seed's files, not just the ones with problems")
)

-- seal 0.8.1 only applies `:validate` to positional arguments, so named numbers are checked here
local function positiveInteger(name: string): number
    const raw = parsed:expect<<string>>(name)
    const n = tonumber(raw)
    if n == nil or n < 1 or n ~= math.floor(n) then
        print(`classes_fuzz: {name} expects a positive integer, got '{raw}'`)
        process.exit(2)
    end
    return n :: number
end

const luau = fs.path.canonicalize(parsed:expect<<string>>("--luau"))
const firstSeed = positiveInteger("--start")
const seedCount = positiveInteger("--count")
const maxJobs = positiveInteger("--jobs")
const work = fs.path.absolutize(parsed:expect<<string>>("--work"))
const keepAll = parsed.flags["keep"] == true

const configs: { Config } = {}
for opt = 0, 2 do
    table.insert(configs, { opt = opt, mode = "", trustAnnotations = false, tag = `O{opt}` })
    table.insert(configs, { opt = opt, mode = "--codegen", trustAnnotations = false, tag = `O{opt}cg` })
    table.insert(configs, { opt = opt, mode = "--codegen-cold", trustAnnotations = false, tag = `O{opt}cgcold` })
end
-- annotation-driven inlining only exists at O2
table.insert(configs, { opt = 2, mode = "", trustAnnotations = true, tag = "O2trust" })
table.insert(configs, { opt = 2, mode = "--codegen", trustAnnotations = true, tag = "O2trustcg" })
table.insert(configs, { opt = 2, mode = "--codegen-cold", trustAnnotations = true, tag = "O2trustcgcold" })

local function contains(haystack: string, needle: string): boolean
    return string.find(haystack, needle, 1, true) ~= nil
end

local function tail(text: string, bytes: number): string
    return if #text > bytes then "..." .. string.sub(text, -bytes) else text
end

local function seedDir(seed: number): string
    return fs.path.join(work, `seed{seed}`)
end

local function mainFile(config: Config): string
    return `main_{config.tag}.luau`
end

local function luauArgs(config: Config): { string }
    local list = { `-O{config.opt}`, if config.trustAnnotations then FLAGS .. TRUST_ANNOTATIONS_FLAG else FLAGS }
    if config.mode ~= "" then
        table.insert(list, config.mode)
    end
    table.insert(list, mainFile(config))
    return list
end

local function rerunCommand(seed: number, config: Config): string
    return `cd {seedDir(seed)} && {luau} {table.concat(luauArgs(config), " ")}`
end

-- generate --------------------------------------------------------------------------------------------------------

type PendingRun = { seed: number, config: Config }

local pending: { PendingRun } = {}

fs.makedir(work, { create_missing = true, error_if_exists = false })

for seed = firstSeed, firstSeed + seedCount - 1 do
    local main, mod = gen.generate(seed)
    local dir = seedDir(seed)
    fs.makedir(dir, { create_missing = true, error_if_exists = false })

    for _, config in configs do
        local modName = `mod_{config.tag}`
        local opt = tostring(config.opt)
        fs.writefile(fs.path.join(dir, modName .. ".luau"), str.replace(mod, "OPTLEVEL", opt))
        fs.writefile(fs.path.join(dir, mainFile(config)), str.replace(str.replace(main, "OPTLEVEL", opt), "MODNAME", modName))
        table.insert(pending, { seed = seed, config = config })
    end
end

-- run -------------------------------------------------------------------------------------------------------------

const outcomes: { [number]: { [string]: Outcome } } = {}
for seed = firstSeed, firstSeed + seedCount - 1 do
    outcomes[seed] = {}
end

local function drain(job: Job, timeout: number)
    while true do
        local out = job.child.stdout:read(nil, timeout)
        local err = job.child.stderr:read(nil, 0)
        if out then
            table.insert(job.stdout, out)
        end
        if err then
            table.insert(job.stderr, err)
        end
        if out == nil and err == nil then
            return
        end
    end
end

local function classify(job: Job, status: process.ChildStatus?): Outcome
    -- the reader threads may still hold the last of the output when the child exits
    drain(job, 0.05)
    const stdout = table.concat(job.stdout)
    const stderr = table.concat(job.stderr)

    if status == nil then
        return { ok = false, kind = "TIMEOUT", detail = `killed after {RUN_TIMEOUT_SECONDS}s` }
    elseif status.signal ~= nil or status.core_dumped then
        return {
            ok = false,
            kind = "CRASH",
            detail = `signal {status.signal}, core dumped: {status.core_dumped}\n{tail(stderr, OUTPUT_TAIL_BYTES)}`,
        }
    elseif status.code ~= 0 then
        const output = stderr .. stdout
        const invalid = contains(output, "SyntaxError") or contains(output, "CompileError")
        return {
            ok = false,
            kind = if invalid then "INVALID" else "EXIT",
            detail = `exit {status.code}\n{tail(output, OUTPUT_TAIL_BYTES)}`,
        }
    elseif not contains(stdout, "__done") then
        return { ok = false, kind = "INCOMPLETE", detail = tail(stdout, OUTPUT_TAIL_BYTES) }
    end
    return { ok = true, stdout = stdout }
end

const startedAt = time.datetime.now()
const running: { Job } = {}

while #pending > 0 or #running > 0 do
    while #running < maxJobs and #pending > 0 do
        local run = table.remove(pending) :: PendingRun
        table.insert(running, {
            seed = run.seed,
            config = run.config,
            child = process.spawn({
                program = luau,
                args = luauArgs(run.config),
                cwd = seedDir(run.seed),
                stream = { stdout_capacity = 16 * 1024 * 1024, stderr_capacity = 1024 * 1024 },
            }) :: process.PipedChild,
            deadline = time.datetime.now() + time.datetime.seconds(RUN_TIMEOUT_SECONDS),
            stdout = {},
            stderr = {},
        })
    end

    for index = #running, 1, -1 do
        local job = running[index]
        drain(job, 0)

        local status = job.child:status()
        if status then
            outcomes[job.seed][job.config.tag] = classify(job, status)
            table.remove(running, index)
        elseif time.datetime.now() > job.deadline then
            job.child:kill()
            outcomes[job.seed][job.config.tag] = classify(job, nil)
            table.remove(running, index)
        end
    end

    time.wait(time.milliseconds(2))
end

-- compare ---------------------------------------------------------------------------------------------------------

local function parseOps(stdout: string): OpResults
    local results: OpResults = {}
    for _, line in str.splitlines(stdout) do
        local label, result = table.unpack(str.splitfront(line, "\t", 1))
        if result and label ~= "__done" then
            results[label] = result
        end
    end
    return results
end

local function succeeded(result: string): boolean
    return str.startswith(result, "ok")
end

-- A `~` label marks an operation whose error status may legitimately differ between optimization levels (see gen.luau):
-- only its values are compared, and only when both runs succeeded.
local function resultsMatch(label: string, expected: string, actual: string?): boolean
    if actual == nil then
        return false
    elseif str.startswith(label, "~") then
        return not (succeeded(expected) and succeeded(actual)) or expected == actual
    end
    return expected == actual
end

type Problem = { config: Config, kind: ProblemKind, detail: string }

local function findProblems(seed: number): { Problem }
    const problems: { Problem } = {}
    const byTag = outcomes[seed]

    for _, config in configs do
        const outcome = byTag[config.tag]
        if not outcome.ok then
            table.insert(problems, { config = config, kind = outcome.kind, detail = outcome.detail })
        end
    end

    const reference = byTag[configs[1].tag]
    if not reference.ok then
        return problems
    end

    const expected = parseOps(reference.stdout)
    local anySucceeded = false
    for _, result in expected do
        anySucceeded = anySucceeded or succeeded(result)
    end
    if not anySucceeded then
        table.insert(problems, { config = configs[1], kind = "VACUOUS", detail = "no operation succeeded" })
    end

    for index = 2, #configs do
        const config = configs[index]
        const outcome = byTag[config.tag]
        if outcome.ok then
            const actual = parseOps(outcome.stdout)
            for label, want in expected do
                if not resultsMatch(label, want, actual[label]) then
                    table.insert(problems, {
                        config = config,
                        kind = "DIVERGE",
                        detail = `{label}: {configs[1].tag} '{want}' vs '{actual[label]}'`,
                    })
                end
            end
        end
    end

    return problems
end

-- report ----------------------------------------------------------------------------------------------------------

const MAX_PROBLEMS_PER_SEED = 6

local seedsWithProblems = 0
const countsByKind: { [ProblemKind]: number } = {}

for seed = firstSeed, firstSeed + seedCount - 1 do
    const problems = findProblems(seed)

    if #problems > 0 then
        seedsWithProblems += 1
        print(`seed {seed}:`)
        for index, problem in problems do
            countsByKind[problem.kind] = (countsByKind[problem.kind] or 0) + 1
            if index <= MAX_PROBLEMS_PER_SEED then
                print(`  [{problem.config.tag}] {problem.kind}: {problem.detail}`)
                print(`    rerun: {rerunCommand(seed, problem.config)}`)
            end
        end
        if #problems > MAX_PROBLEMS_PER_SEED then
            print(`  ... {#problems - MAX_PROBLEMS_PER_SEED} more`)
        end
    elseif not keepAll then
        fs.removetree(seedDir(seed))
    end
end

const runCount = seedCount * #configs
print(`\n{seedCount} programs x {#configs} configs ({runCount} runs) in {time.datetime.now():since(startedAt)}: {seedsWithProblems} seeds with problems`)
for kind, count in countsByKind do
    print(`  {kind}: {count}`)
end

process.exit(if seedsWithProblems > 0 then 1 else 0)

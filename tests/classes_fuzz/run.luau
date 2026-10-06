--[[
    Differential and crash fuzzer for Luwu classes. Requires seal 0.8.x at >= 0.8.1 (ChildProcess:status);
    seal 0.9.0 breaks the std APIs used here.

        seal tests/classes_fuzz/run.luau --luau build-assert/luwu [--start 1] [--count 100] [--jobs 16] [--work DIR] [--keep] [--no-codegen]

    For every seed, gen.luau writes a random class program (a main module plus a required module, both .luwu) and this script
    runs it with the given `luau` CLI at -O0/-O1/-O2, each interpreted, with --codegen and with --codegen-cold. Use an
    assert-enabled build (build-assert/) so internal invariants are checked too. Pass --no-codegen on a platform
    without native code generation (e.g. 32-bit Windows) to run only the interpreted configs.

    Reported problems:
      CRASH        the process died from a signal or dumped core (asserts land here)
      TIMEOUT      the process ran longer than the per-run limit
      EXIT         nonzero exit code: an error escaped the program's top level
      INVALID      the generated program failed to parse or compile (a generator bug)
      INCOMPLETE   the program exited cleanly without printing its final marker
      VACUOUS      no operation succeeded at -O0 (a generator bug; nothing was really compared)
      DIVERGE      an operation's result differs from the -O0 interpreter
      WRONG        a fact the generator knows (say, which classes implement a trait) didn't hold; see gen.luau
    Seeds with problems keep their files under the work directory, and the report prints how to rerun each.

    Exits 1 if any seed had a problem, 0 otherwise.
]]

const fs = require("@std/fs")
const process = require("@std/process")
const args = require("@std/args")
const str = require("@std/str")
const time = require("@std/time")

const gen = require("./gen")

const FLAGS = "--fflags="
    .. table.concat({
        "LuwuClasses=true",
        "LuwuGenericNominals=true",
        "LuauExportValueSyntax=true",
        "LuwuExportedClassIsNilWorkaround=true",
        "LuwuDefaultArguments=true",
        "LuwuNonePrimitive=true",
        "LuwuTraits=true",
        "LuwuIfLocal=true",
        "DebugLuwuDoExpr=true",
    }, ",")
-- Acting on a receiver's declared type is opt-in (see Compiler.cpp): every generated file says `--!trust`,
-- and this flag decides whether that directive counts. It only changes O2, where inlining runs, so it is a
-- variant of the O2 configs rather than a second copy of the whole matrix.
const TRUST_ANNOTATIONS_FLAG = ",DebugLuwuCompilerTrustsTypeAnnotations=true"
const RUN_TIMEOUT_SECONDS = 60
const OUTPUT_TAIL_BYTES = 800

type DateTime = typeof(time.datetime.now())

type ProblemKind = "CRASH" | "TIMEOUT" | "EXIT" | "INVALID" | "INCOMPLETE" | "VACUOUS" | "DIVERGE" | "WRONG"

type Config = {
    opt: number,
    mode: "" | "--codegen" | "--codegen-cold",
    trust_annotations: boolean,
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
    args.flag("--keep", "keep every seed's files, not just the ones with problems"),
    args.flag("--no-codegen", "run only the interpreted configs, for a platform without native code generation")
)

-- seal 0.8.1 only applies `:validate` to positional arguments, so named numbers are checked here
const function positive_integer(name: string): number
    const raw = parsed:expect<<string>>(name)
    const n = tonumber(raw)
    if n == nil or n < 1 or n ~= math.floor(n) then
        print(`classes_fuzz: {name} expects a positive integer, got '{raw}'`)
        process.exit(2)
    end
    return n :: number
end

const luau = fs.path.canonicalize(parsed:expect<<string>>("--luau"))
const first_seed = positive_integer("--start")
const seed_count = positive_integer("--count")
const max_jobs = positive_integer("--jobs")
const work = fs.path.absolutize(parsed:expect<<string>>("--work"))
const keep_all = parsed.flags["--keep"] == true
const with_codegen = parsed.flags["--no-codegen"] ~= true

const configs: { Config } = {}
for opt = 0, 2 do
    table.insert(configs, { opt = opt, mode = "", trust_annotations = false, tag = `O{opt}` })
    if with_codegen then
        table.insert(configs, { opt = opt, mode = "--codegen", trust_annotations = false, tag = `O{opt}cg` })
        table.insert(configs, { opt = opt, mode = "--codegen-cold", trust_annotations = false, tag = `O{opt}cgcold` })
    end
end
-- annotation-driven inlining only exists at O2
table.insert(configs, { opt = 2, mode = "", trust_annotations = true, tag = "O2trust" })
if with_codegen then
    table.insert(configs, { opt = 2, mode = "--codegen", trust_annotations = true, tag = "O2trustcg" })
    table.insert(configs, { opt = 2, mode = "--codegen-cold", trust_annotations = true, tag = "O2trustcgcold" })
end

const function contains(haystack: string, needle: string): boolean
    return string.find(haystack, needle, 1, true) ~= nil
end

const function tail(text: string, bytes: number): string
    return if #text > bytes then "..." .. string.sub(text, -bytes) else text
end

const function seed_dir(seed: number): string
    return fs.path.join(work, `seed{seed}`)
end

const function main_file(config: Config): string
    return `main_{config.tag}.luwu`
end

const function luau_args(config: Config): { string }
    const list = { `-O{config.opt}`, if config.trust_annotations then FLAGS .. TRUST_ANNOTATIONS_FLAG else FLAGS }
    if config.mode ~= "" then
        table.insert(list, config.mode)
    end
    table.insert(list, main_file(config))
    return list
end

-- canonicalize returns a verbatim `\\?\C:\...` path on Windows; process.spawn takes it, but it reads badly in a
-- command meant for copying
const function display_path(path: string): string
    return if str.startswith(path, "\\\\?\\") then string.sub(path, 5) else path
end

const function rerun_command(seed: number, config: Config): string
    return `cd {display_path(seed_dir(seed))} && {display_path(luau)} {table.concat(luau_args(config), " ")}`
end

-- generate --------------------------------------------------------------------------------------------------------

type PendingRun = { seed: number, config: Config }

const pending: { PendingRun } = {}

fs.makedir(work, { create_missing = true, error_if_exists = false })

for seed = first_seed, first_seed + seed_count - 1 do
    const main, mod = gen.generate(seed)
    const dir = seed_dir(seed)
    fs.makedir(dir, { create_missing = true, error_if_exists = false })

    for _, config in configs do
        const mod_name = `mod_{config.tag}`
        const opt = tostring(config.opt)
        fs.writefile(fs.path.join(dir, mod_name .. ".luwu"), str.replace(mod, "OPTLEVEL", opt))
        fs.writefile(fs.path.join(dir, main_file(config)), str.replace(str.replace(main, "OPTLEVEL", opt), "MODNAME", mod_name))
        table.insert(pending, { seed = seed, config = config })
    end
end

-- run -------------------------------------------------------------------------------------------------------------

const outcomes: { [number]: { [string]: Outcome } } = {}
for seed = first_seed, first_seed + seed_count - 1 do
    outcomes[seed] = {}
end

const function drain(job: Job, timeout: number)
    while true do
        const out = job.child.stdout:read(nil, timeout)
        const err = job.child.stderr:read(nil, 0)
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

const function classify(job: Job, status: process.ChildStatus?): Outcome
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

const started_at = time.datetime.now()
const running: { Job } = {}

while #pending > 0 or #running > 0 do
    while #running < max_jobs and #pending > 0 do
        const run = table.remove(pending) :: PendingRun
        table.insert(running, {
            seed = run.seed,
            config = run.config,
            child = process.spawn({
                program = luau,
                args = luau_args(run.config),
                cwd = seed_dir(run.seed),
                stream = { stdout_capacity = 16 * 1024 * 1024, stderr_capacity = 1024 * 1024 },
            }) :: process.PipedChild,
            deadline = time.datetime.now() + time.datetime.seconds(RUN_TIMEOUT_SECONDS),
            stdout = {},
            stderr = {},
        })
    end

    for index = #running, 1, -1 do
        const job = running[index]
        drain(job, 0)

        const status = job.child:status()
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

const function parse_ops(stdout: string): OpResults
    const results: OpResults = {}
    for _, line in str.splitlines(stdout) do
        const label, result = table.unpack(str.splitfront(line, "\t", 1))
        if result and label ~= "__done" and label ~= "__wrong" then
            results[label] = result
        end
    end
    return results
end

const function succeeded(result: string): boolean
    return str.startswith(result, "ok")
end

-- A `~` label marks an operation whose error status may legitimately differ between optimization levels (see gen.luau):
-- only its values are compared, and only when both runs succeeded.
const function results_match(label: string, expected: string, actual: string?): boolean
    if actual == nil then
        return false
    elseif str.startswith(label, "~") then
        return not (succeeded(expected) and succeeded(actual)) or expected == actual
    end
    return expected == actual
end

type Problem = { config: Config, kind: ProblemKind, detail: string }

const function find_problems(seed: number): { Problem }
    const problems: { Problem } = {}
    const by_tag = outcomes[seed]

    for _, config in configs do
        const outcome = by_tag[config.tag]
        if not outcome.ok then
            table.insert(problems, { config = config, kind = outcome.kind, detail = outcome.detail })
        else
            -- a failed check, in whichever configuration it failed
            for _, line in str.splitlines(outcome.stdout) do
                if str.startswith(line, "__wrong\t") then
                    table.insert(problems, { config = config, kind = "WRONG", detail = string.sub(line, 9) })
                end
            end
        end
    end

    const reference = by_tag[configs[1].tag]
    if not reference.ok then
        return problems
    end

    const expected = parse_ops(reference.stdout)

    -- a `!` operation is built to succeed (see gen.luau)
    for _, config in configs do
        const outcome = by_tag[config.tag]
        if outcome.ok then
            for label, result in parse_ops(outcome.stdout) do
                if str.startswith(label, "!") and not succeeded(result) then
                    table.insert(problems, { config = config, kind = "WRONG", detail = `{label} raised` })
                end
            end
        end
    end

    local any_succeeded = false
    for _, result in expected do
        any_succeeded = any_succeeded or succeeded(result)
    end
    if not any_succeeded then
        table.insert(problems, { config = configs[1], kind = "VACUOUS", detail = "no operation succeeded" })
    end

    for index = 2, #configs do
        const config = configs[index]
        const outcome = by_tag[config.tag]
        if outcome.ok then
            const actual = parse_ops(outcome.stdout)
            for label, want in expected do
                if not results_match(label, want, actual[label]) then
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

local seeds_with_problems = 0
const counts_by_kind: { [ProblemKind]: number } = {}

for seed = first_seed, first_seed + seed_count - 1 do
    const problems = find_problems(seed)

    if #problems > 0 then
        seeds_with_problems += 1
        print(`seed {seed}:`)
        for index, problem in problems do
            counts_by_kind[problem.kind] = (counts_by_kind[problem.kind] or 0) + 1
            if index <= MAX_PROBLEMS_PER_SEED then
                print(`  [{problem.config.tag}] {problem.kind}: {problem.detail}`)
                print(`    rerun: {rerun_command(seed, problem.config)}`)
            end
        end
        if #problems > MAX_PROBLEMS_PER_SEED then
            print(`  ... {#problems - MAX_PROBLEMS_PER_SEED} more`)
        end
    elseif not keep_all then
        fs.removetree(seed_dir(seed))
    end
end

-- the work directory itself goes too once nothing in it is worth keeping
if not keep_all and #fs.listdir(work) == 0 then
    fs.removetree(work)
end

const run_count = seed_count * #configs
print(`\n{seed_count} programs x {#configs} configs ({run_count} runs) in {time.datetime.now():since(started_at)}: {seeds_with_problems} seeds with problems`)
for kind, count in counts_by_kind do
    print(`  {kind}: {count}`)
end

process.exit(if seeds_with_problems > 0 then 1 else 0)

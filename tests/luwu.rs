// `cargo test` entry point for Luwu's test suites (the doctest binaries built from tests/*.cpp).
//
// This uses a custom harness (`harness = false` in Cargo.toml) rather than libtest so that the runs happen
// one at a time, in the order listed in `runs()`: every suite with no fflags first, then every suite with
// all fflags, then the extra conformance configurations CI runs (.github/workflows/build.yml). libtest
// would run them in parallel and in alphabetical order, which puts "all fflags" before "no fflags".
//
// Suites mirror the CMake test targets: `unit` (Luau.UnitTest), `conformance` (Luau.Conformance, the VM
// tests) and `cli` (Luau.CLI.Test). On Linux/macOS the Makefile links all of them into one `luau-tests`
// binary, so each suite is selected there with doctest's `-ts`/`-tse` test-suite filters instead.
//
// Filtering works like libtest: `cargo test -- unit`, `cargo test -- "no fflags"`, `--exact`, `--skip`,
// `--list`.
//
// Uses the same OUT_DIR-backed build directory as build.rs (see the comment there on why: it's what makes
// `cargo clean` actually clean the C++ side too). Test binaries don't get OUT_DIR as a runtime env var the
// way build scripts do, so it's read here via the `env!` compile-time macro instead.
#[path = "../rust-build-stub/progress.rs"]
mod progress;

use progress::{stream_and_report, Progress};
use std::path::PathBuf;
use std::process::{Command, ExitCode, Stdio};
use std::time::Instant;

#[derive(Clone, Copy)]
enum Suite {
    Unit,
    Conformance,
    Cli,
}

impl Suite {
    fn name(self) -> &'static str {
        match self {
            Suite::Unit => "unit",
            Suite::Conformance => "conformance",
            Suite::Cli => "cli",
        }
    }

    /// doctest test suites belonging to this suite's CMake target (see Sources.cmake). Unit is "everything
    /// that isn't conformance or cli", so it has no list of its own.
    fn doctest_suites(self) -> &'static [&'static str] {
        match self {
            Suite::Unit => &[],
            Suite::Conformance => &[
                "ExternalBuffers",
                "ExternalStrings",
                "FatCClosure",
                "Conformance",
                "DirectFieldAccess",
                "FeedbackVector",
                "IrLowering",
                "SharedCodeAllocator",
            ],
            Suite::Cli => &["ReplPrettyPrint", "ReplCodeCompletion", "RegressionTests", "RequireByStringTests", "ExportValueTests"],
        }
    }

    #[cfg(windows)]
    fn cmake_target(self) -> &'static str {
        match self {
            Suite::Unit => "Luau.UnitTest",
            Suite::Conformance => "Luau.Conformance",
            Suite::Cli => "Luau.CLI.Test",
        }
    }
}

struct Run {
    name: String,
    suite: Suite,
    args: Vec<&'static str>,
}

fn runs() -> Vec<Run> {
    let mut runs = Vec::new();
    let mut add = |suite: Suite, config: &str, extra: &[&'static str], all_fflags: bool| {
        let mut args = extra.to_vec();
        if all_fflags {
            args.push("--fflags=true");
        }
        let fflags = if all_fflags { "all fflags" } else { "no fflags" };
        runs.push(Run { name: format!("{}{config}: {fflags}", suite.name()), suite, args });
    };

    for all_fflags in [false, true] {
        for suite in [Suite::Unit, Suite::Conformance, Suite::Cli] {
            add(suite, "", &[], all_fflags);
        }
    }

    for (config, extra) in [(" -O2", &["-O2"][..]), (" --codegen", &["--codegen"][..]), (" --codegen -O2", &["--codegen", "-O2"][..])] {
        for all_fflags in [false, true] {
            add(Suite::Conformance, config, extra, all_fflags);
        }
    }

    runs
}

struct Options {
    filters: Vec<String>,
    skips: Vec<String>,
    exact: bool,
    list: bool,
    ignored: bool,
}

fn parse_args() -> Options {
    let mut options = Options { filters: Vec::new(), skips: Vec::new(), exact: false, list: false, ignored: false };
    let mut args = std::env::args().skip(1);

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--exact" => options.exact = true,
            "--list" => options.list = true,
            "--ignored" => options.ignored = true,
            "--skip" => options.skips.extend(args.next()),
            // libtest options that take a value; accepted and ignored so `cargo test -- --test-threads 1` etc. still work.
            "--test-threads" | "--format" | "--color" | "--logfile" | "-Z" => {
                args.next();
            }
            _ if arg.starts_with("--skip=") => options.skips.push(arg["--skip=".len()..].to_string()),
            _ if arg.starts_with('-') => {}
            _ => options.filters.push(arg),
        }
    }

    options
}

fn selected(options: &Options, name: &str) -> bool {
    let matches = |filter: &String| if options.exact { name == filter } else { name.contains(filter.as_str()) };
    // There are no #[ignore]d runs, so `--ignored` selects nothing.
    !options.ignored && (options.filters.is_empty() || options.filters.iter().any(matches)) && !options.skips.iter().any(matches)
}

fn manifest_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn jobs() -> String {
    std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8).to_string()
}

#[cfg(not(windows))]
fn build_dir() -> PathBuf {
    PathBuf::from(env!("OUT_DIR")).join("build")
}

#[cfg(not(windows))]
fn build() -> bool {
    fn extract_make_source(line: &str) -> Option<String> {
        if !line.contains("-MMD -MP -o") {
            return None;
        }
        line.split_whitespace()
            .find(|tok| (tok.ends_with(".cpp") || tok.ends_with(".c")) && !tok.starts_with('-'))
            .map(|s| s.to_string())
    }

    let mut child = Command::new("make")
        .current_dir(manifest_dir())
        .arg(format!("-j{}", jobs()))
        .arg(format!("BUILD={}", build_dir().display()))
        // Warnings are errors, as in CI; see the matching note in build.rs.
        .arg("werror=1")
        .arg("luau-tests")
        .stdout(Stdio::piped())
        .spawn()
        .expect("failed to invoke make");

    let mut progress = Progress::new("Compiling tests", 0);
    stream_and_report(&mut child, &mut progress, extract_make_source);
    let status = child.wait().expect("failed to wait on make");
    progress.finish(if status.success() { "Test build finished" } else { "Test build failed" });
    status.success()
}

#[cfg(not(windows))]
fn command(run: &Run) -> Command {
    // The Makefile's `test` target builds this as $(BUILD)/luau-tests; the repo-root `luau-tests` is just a symlink to it.
    let mut cmd = Command::new(build_dir().join("luau-tests"));

    let unit_excludes: Vec<&str> = [Suite::Conformance, Suite::Cli].iter().flat_map(|s| s.doctest_suites().iter().copied()).collect();
    match run.suite {
        Suite::Unit => cmd.arg(format!("-tse={}", unit_excludes.join(","))),
        suite => cmd.arg(format!("-ts={}", suite.doctest_suites().join(","))),
    };

    cmd.args(&run.args);
    cmd
}

#[cfg(windows)]
fn build_type() -> &'static str {
    if cfg!(debug_assertions) {
        "Debug"
    } else {
        "RelWithDebInfo"
    }
}

#[cfg(windows)]
fn build_dir() -> PathBuf {
    PathBuf::from(env!("OUT_DIR")).join("cmake")
}

#[cfg(windows)]
fn build() -> bool {
    fn extract_cmake_source(line: &str) -> Option<String> {
        if !line.starts_with("Building") {
            return None;
        }
        line.rsplit('/').next().map(|s| s.trim_end_matches(".o").to_string())
    }

    let mut cmd = Command::new("cmake");
    cmd.arg("--build").arg(build_dir()).arg("--config").arg(build_type()).arg("-j").arg(jobs());
    for suite in [Suite::Unit, Suite::Conformance, Suite::Cli] {
        cmd.arg("--target").arg(suite.cmake_target());
    }

    let mut child = cmd.stdout(Stdio::piped()).spawn().expect("failed to invoke cmake");

    let mut progress = Progress::new("Compiling tests", 0);
    stream_and_report(&mut child, &mut progress, extract_cmake_source);
    let status = child.wait().expect("failed to wait on cmake");
    progress.finish(if status.success() { "Test build finished" } else { "Test build failed" });
    status.success()
}

#[cfg(windows)]
fn command(run: &Run) -> Command {
    let mut cmd = Command::new(build_dir().join(build_type()).join(format!("{}.exe", run.suite.cmake_target())));
    cmd.args(&run.args);
    cmd
}

fn main() -> ExitCode {
    let options = parse_args();
    let all = runs();
    let total = all.len();
    let selected: Vec<Run> = all.into_iter().filter(|run| selected(&options, &run.name)).collect();

    if options.list {
        for run in &selected {
            println!("{}: test", run.name);
        }
        return ExitCode::SUCCESS;
    }

    let filtered_out = total - selected.len();
    let start = Instant::now();

    if !selected.is_empty() && !build() {
        println!("\nerror: failed to build the test binaries");
        return ExitCode::FAILURE;
    }

    println!("\nrunning {} tests", selected.len());

    let mut failed = Vec::new();
    for run in &selected {
        println!("\ntest {} ...", run.name);
        let run_start = Instant::now();

        let mut cmd = command(run);
        // RequireByString and the conformance tests resolve paths relative to the repo root.
        cmd.current_dir(manifest_dir());
        let ok = match cmd.status() {
            Ok(status) => status.success(),
            Err(e) => {
                println!("failed to run {:?}: {e}", cmd);
                false
            }
        };

        println!("test {} ... {} ({:.1}s)", run.name, if ok { "ok" } else { "FAILED" }, run_start.elapsed().as_secs_f64());
        if !ok {
            failed.push(run.name.as_str());
        }
    }

    if !failed.is_empty() {
        println!("\nfailures:");
        for name in &failed {
            println!("    {name}");
        }
    }

    println!(
        "\ntest result: {}. {} passed; {} failed; 0 ignored; 0 measured; {} filtered out; finished in {:.2}s\n",
        if failed.is_empty() { "ok" } else { "FAILED" },
        selected.len() - failed.len(),
        failed.len(),
        filtered_out,
        start.elapsed().as_secs_f64()
    );

    if failed.is_empty() {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

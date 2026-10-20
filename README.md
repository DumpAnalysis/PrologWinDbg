# PrologWinDbg

A native x64 WinDbg extension with a persistent SWI-Prolog worker. It exposes
threads, stack frames, symbols, loaded modules, and memory from a user-mode dump
or a stopped native x64 process. Load your own Prolog files and run queries from
the debugger command window.

**Version:** 0.1.1 (experimental; licensing and packaging update).

**Build status:** the release DLL and Windows test target are cross-compiled
Windows x64 binaries. The real SWI-Prolog worker, sample rules, and portable
protocol tests were run on Linux. This version has not been loaded into WinDbg
in the build environment. Run `scripts/smoke-windows.ps1` to validate it on your
Windows installation. See `docs/VALIDATION.md` for the exact checks.

## Quick start

The source repository does not contain compiled binaries. Build it using the
Windows instructions below to populate `dist/`, or extract the
`PrologWinDbg-v0.1.1-win-x64.zip` release archive, which includes `dist/`.

1. Install the x64 Windows console version of [SWI-Prolog](https://www.swi-prolog.org/download/stable).
   Version 1 uses the SWI 9.x API and has been tested with SWI-Prolog 9.0.4 on
   Linux. Other SWI releases, including Windows builds, need the smoke checks.
2. Keep `dist/prolog.dll` together
   with the `dist/prolog` and `dist/examples` directories.
3. Open a native x64 user-mode dump in WinDbg, or attach to a native x64 process
   and break into it. For a reproducible dump, run this in PowerShell:

   ```powershell
   .\dist\prologdbg_target.exe --dump C:\Temp\prologdbg-sample.dmp
   ```

4. In WinDbg, replace these paths with your actual project directory:

   ```text
   .load C:\PrologWinDbg\dist\prolog.dll
   !prolog.runtime "C:\Program Files\swipl\bin\swipl.exe"
   !prolog.info
   !prolog.consult "C:\PrologWinDbg\dist\examples\patterns.pl"
   !prolog.query wd:current_snapshot(S), wd:thread_id(S,T,WindowsTid).
   !prolog.query wd:current_snapshot(S), wd:stack_frame(S,T,Depth,IP).
   !prolog.query wd:current_snapshot(S), patterns:stack_contains(S,T,Module,Function).
   ```

The extension uses the explicit runtime path, `PROLOGDBG_SWIPL` environment
variable, or the standard `Program Files\swipl\bin\swipl.exe` installation.
It starts the worker when first needed. All communication uses local pipes.

For the supplied target, inspect the known eight-byte marker:

```text
!prolog.query wd:current_snapshot(S), wd:lookup(S,'prologdbg_target!g_magic',ok(A)), patterns:inspect_magic(S,A,Verdict).
```

Expected verdict: `matched(prologdb, Address)`. The marker bytes spell
`PROLOGDB`. The target exports these names, so this example does not require
a PDB. Private PDBs are useful for other functions and applications.

## Live debugging

Launch `prologdbg_target.exe --live` under WinDbg. Continue past the initial
loader breakpoint. The target then breaks at `CapturePoint` after two worker
threads have started. Use the same queries as for a dump.

Alternatively, run `prologdbg_target.exe --wait`, attach to its printed PID,
and break in. This mode exits after ten minutes if not stopped earlier.

Use normal WinDbg `bp`, `g`, `p`, and `t` commands to control execution. Every
`!prolog.query` collects a fresh snapshot. Registered Prolog breakpoint
actions and automatic continuation are planned for a later version.

## Commands

| Command | Purpose |
|---|---|
| `!prolog.help` | Display command help |
| `!prolog.runtime "absolute path to swipl.exe"` | Select runtime and stop any existing worker |
| `!prolog.info` | Start the worker and report its version |
| `!prolog.consult "absolute path to rules.pl"` | Load or reload Prolog source |
| `!prolog.query Goal.` | Collect evidence and enumerate solutions |
| `!prolog.queryfile "absolute path to goal.txt"` | Read a single UTF-8 goal from a file, then query |
| `!prolog.eval Goal.` | Evaluate Prolog with no debugger evidence |
| `!prolog.limits` | Display current limits |
| `!prolog.limits 5000 100 128 10000000` | Set milliseconds, answer count, frames per thread, and inference limit |
| `!prolog.reset` | Stop the worker; loaded rules and custom runtime state are lost |

Simple goals are passed directly after the command. Standard Prolog single
quotes delimit atoms. You may omit the final period. Use one goal per command,
joining subgoals with commas. For Prolog disjunction (`;`), enclose the whole
goal in double quotes to protect it from WinDbg's command separator:

```text
!prolog.eval "(X=a;X=b), writeln(X)."
```

For complex quoting, Unicode text, or multiline goals, put the goal in a UTF-8
text file and use `!prolog.queryfile "C:\rules\goal.txt"`. Its contents go
directly to Prolog, bypassing WinDbg's command-text parsing. The file limit is
1 MiB. For reusable logic, define predicates in a consulted `.pl` file.

Example without a target:

```text
!prolog.eval member(X,[1,2,3]), Y is X*X.
```

Answers are printed as `bindings([Name=Value, ...], ResidualGoals)`. The second
list preserves residual constraints. A final status distinguishes exhausted
search, failure, answer limits, inference limits, timeouts, and exceptions.
Reaching an answer limit does not assert that more solutions exist.

Consulting a file runs normal Prolog loading and initialization. Load errors
are reported, but Prolog loading is not transactional: some clauses can have
loaded before an error. Correct and reload the file, or reset the worker.

## Write a rule

```prolog
:- module(my_rules, [in_function/4]).
:- use_module('C:/PrologWinDbg/dist/prolog/wd.pl').

in_function(S, T, Module, Function) :-
    wd:stack_frame(S, T, _, IP),
    wd:symbol(S, IP, Module, Function, _).
```

Then:

```text
!prolog.consult "C:\rules\my_rules.pl"
!prolog.query wd:current_snapshot(S), my_rules:in_function(S,T,myapp,'ProcessRequest').
```

See `docs/API.md` for predicate modes and evidence semantics. The initial
rules provide stack matching, shared functions across threads, explicit stack
evidence, unreadable addresses, and a memory-signature diagnosis with an
inconclusive result.

## Bounds and cancellation

The default query budget is 5 seconds, 100 answers, 128 frames per thread, and
10 million inferences. Snapshot collection receives its own time budget of the
same size. Initial symbol loading can be slow: warm the symbol cache or raise
the first value with `!prolog.limits`.

Memory requests are at most 64 KiB each and 16 MiB total per query. Snapshot
capture is capped at 4,096 threads, 4,096 loaded modules, and 8 MiB of fact text.
The worker uses a 512 MiB Prolog stack limit and a 768 MiB Windows process-memory
limit. The native bridge also limits output, RPC count, and IPC wait time.

Use WinDbg's Break action / Ctrl+Break to interrupt a command. While waiting on
the worker or walking snapshot items, the extension checks `GetInterrupt`.
Interrupted IPC resets the worker; consult your rules again afterward. Normal
Prolog time/inference limits preserve the worker. A `!prolog.reset` command is
usable after the current command has returned; it is not a concurrent cancel
command.

Individual DbgEng calls, especially symbol loading and remote reads, are
synchronous. The extension checks elapsed time and interruption between calls;
it cannot promise a hard deadline while DbgEng itself is blocked.

Manual interruption check:

```text
!prolog.limits 600000 100 128 1000000000
!prolog.eval sleep(300).
```

Press Ctrl+Break. Confirm the prompt returns, then run `!prolog.info` and
`!prolog.eval X is 2+2.`. Restore normal limits afterward.

## Build with Visual Studio on Windows

Install Visual Studio 2022 (Desktop development with C++), CMake, and the
Windows SDK with **Debugging Tools for Windows**. SWI-Prolog and Python 3 are
needed to run all portable tests. Add `swipl.exe` and `python.exe` to PATH.

From a Visual Studio Developer PowerShell:

```powershell
.\scripts\build-windows.ps1
.\scripts\smoke-windows.ps1
```

If your debugger SDK is elsewhere, pass `-DebuggingTools`. It should contain
`sdk\inc\dbgeng.h` (or `inc\dbgeng.h`). If CDB or SWI-Prolog is elsewhere,
pass `-Cdb` and `-Swipl` to the smoke script. The build script writes `dist`
and copies MSVC-generated PDBs for the test target there.

The smoke script creates its own target process, full dump, debugger command
files, and logs in a new temporary directory. It tests both dump and live
sessions, thread/scope restoration, memory evidence, timeout recovery, worker
reset, and DLL unload/reload. It retains its logs and dump and prints their
directory. It does not attach to an existing unrelated process.

## Portable tests and cross build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The optional MinGW cross build uses `cmake/mingw-x64.cmake` and a current
Microsoft DbgEng header supplied separately through `DEBUGGING_TOOLS_ROOT`.
MinGW's bundled header is too old for the wide-character interfaces used here.
The small `dbgeng_compat.hpp` wrapper handles missing SAL annotations without
changing Microsoft's header or interface layout.

```sh
cmake -S . -B build-cross -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x64.cmake \
  -DDEBUGGING_TOOLS_ROOT=/path/to/debugger-sdk
cmake --build build-cross
cmake --install build-cross --prefix dist
```

## Version 1 scope and limits

- Native AMD64 user-mode targets only. WOW64, ARM64, kernel-specific data,
  managed-runtime objects, general register predicates, typed fields, exception
  providers, and rule-triggered execution actions are not implemented here.
- The current process is captured. Other processes in a multi-process session
  can be selected with normal debugger commands before the next query.
- The bridge restores the selected thread and saved symbol scope. Custom
  exception/register-context overrides need explicit Windows validation; the
  bridge does not write target registers to restore them.
- Stack facts are captured prefixes. Even a successful unwind does not prove
  completeness, and optimized/inlined/tail-called frames may be absent.
- Facts and cached reads exist for one query only. Resuming or changing the
  target invalidates that query; the next query recollects facts. Avoid changing
  the target through another debugger client while a query is active.
- Rule files are ordinary trusted Prolog programs running as your Windows user.
  Process separation provides runtime recovery, not a security sandbox. The
  debugger API itself offers read-only operations. Background Prolog threads
  are outside the supported rule model; memory/symbol RPC is restricted to the
  worker's main thread. Interactive Prolog input sees end-of-file.
- Rule files and worker text support UTF-8. WinDbg's extension-command input ABI
  is ANSI; non-ASCII command text or file paths depend on the Windows code page.
  Native output and symbol APIs use their wide-character forms.
- SWI-Prolog is installed separately. It is not bundled with this project.

See `THIRD_PARTY_NOTICES.txt` for build dependencies and runtime notices.

## Licensing

Copyright © 2026 Dmitry Vostokov. The original PrologWinDbg project code and
documentation are licensed under the [PolyForm Strict License 1.0.0](LICENSE.md),
the same license used by [VDL Studio](https://github.com/DumpAnalysis/VDLStudio/blob/main/LICENSE.md).
The license also applies to the original project code in release v0.1.0.

You may download and use it for personal and other noncommercial purposes;
redistribution, modifications, derivative works, and commercial use require
a separate license from the author. See LICENSE.md for the full terms.
Third-party components retain their own licenses, documented in
`THIRD_PARTY_NOTICES.txt` and `licenses/`.

# PrologWinDbg v0.1.0

Initial experimental release of a native x64 WinDbg extension backed by a
persistent, separate SWI-Prolog process.

## Included

- Prolog queries over threads, stack frames, symbols, modules, and memory.
- Analysis of native AMD64 user-mode dumps and stopped live processes.
- Loading and reloading user rules, bounded query execution, and worker reset.
- Explicit evidence for complete, partial, and unavailable memory reads.
- Sample diagnostic rules and a Windows target for reproducible checks.
- Full source, Windows build and smoke-test scripts, and portable tests.

## Validation

The original validation run passed 22 C++ protocol checks, 12 Prolog rule tests,
and 23 worker integration tests (57 total). The DLL and diagnostic target
cross-compiled successfully. See [VALIDATION.md](VALIDATION.md) for details.

Execution inside Windows WinDbg/CDB, MSVC compilation, and the PowerShell
scripts have not yet been validated. Treat this as a prerelease.

## Release archive

`PrologWinDbg-v0.1.0-win-x64.zip` contains the source tree, compiled x64 binaries
in `dist/`, runtime Prolog files, examples, documentation, and third-party
notices. Install SWI-Prolog separately. Read the root README before loading the
extension.

Kernel targets, WOW64, ARM64, managed-runtime providers, and automatic
Prolog-triggered execution actions are outside this release's scope.

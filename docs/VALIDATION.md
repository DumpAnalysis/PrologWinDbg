# v0.2.0 ARM64 validation

Validated on 20 October 2026. The [native Windows CI run](https://github.com/DumpAnalysis/PrologWinDbg/actions/runs/39478127987)
passed for source commit `e2106574e13a6881f01a7fccbdce3fa088fcbc8e`.
Subsequent release changes only document these results and retain the logs;
the packaged DLLs and diagnostic executables are the tested CI binaries.

| Check | Native x64 | Native ARM64 |
|---|---|---|
| MSVC compiler | 19.44.35229.0 | 19.51.36260.0 |
| C++ protocol checks | 22 passed | 22 passed |
| Architecture pairing checks | 36 passed | 36 passed |
| Prolog rule tests | 16 passed | 16 passed |
| Worker integration tests | 25 passed | 25 passed |
| SWI-Prolog runtime | 10.1.18-1 x64 | 10.1.18-1 ARM64 |
| Native CDB | 10.0.26100.4202 | 10.0.26100.6584 |
| Full user-mode dump smoke test | Passed | Passed |
| Stopped live-process smoke test | Passed | Passed |
| PE machine: DLL and diagnostic target | 0x8664 | 0xaa64 |

Both CDB modes verified arithmetic, multiple answers, architecture metadata,
thread enumeration, symbol lookup, shared stack functions, 64-bit pointer
reads, partial/unavailable memory evidence, selected-thread and frame-scope
restoration, timeout recovery, worker reset, and DLL unload/reload.
The native tests also exercised the Windows process, pipe, and job-object path.
Quoted debugger file paths were corrected during validation; README examples
use forward slashes to avoid DbgEng's backslash escape processing.

The same 99 portable checks also passed locally on Linux x64 with SWI-Prolog
9.0.4. Worker integration uses a simulated debugger peer; CDB smoke tests use
the real extension and the supplied diagnostic process.

Raw native logs are retained in `validation/arm64-{dump,live,portable}.log`
and `validation/x64-{dump,live,portable}.log`. The older protocol/build reports
in that directory describe 0.1.x, not the new MSVC binaries.

Interactive WinDbg Ctrl+Break, custom exception/register-context overrides,
and additional application dump formats remain manual validation tasks.
The supported matrix is a matching native debugger, DLL, and AMD64 or ARM64
target. WOW64, ARM32, ARM64EC, and cross-architecture scope contexts are not
claimed as supported.

## Historical validation (0.1.x)

The following is the original record, retained for the earlier releases.
It does not describe v0.2.0.


Date: 30 September 2026.

### Licensing and packaging update — v0.1.1

On 20 October 2026, the project adopted the PolyForm Strict License 1.0.0.
The v0.1.1 archive includes LICENSE.md in the root and dist/ directories.
Runtime source, tests, and the two Windows binaries are byte-for-byte identical
to v0.1.0. Only licensing, documentation, the CMake project version and license
installation list, and package manifests changed. The original validation below
is retained; the licensing update does not represent a new Windows validation run.

### Completed here

| Check | Result |
|---|---|
| Portable C++ protocol tests | 22 checks passed |
| Prolog rule tests | 12 tests passed |
| Worker integration tests with real SWI-Prolog | 23 tests passed |
| CTest | All 3 registered suites passed |
| x64 Windows extension | Cross-compiled and linked successfully |
| x64 Windows diagnostic test target | Cross-compiled and linked successfully |
| DLL exports | Initialization, notification, cleanup, and all command exports present |
| DLL imports | KERNEL32.dll and msvcrt.dll; no separate MinGW runtime DLL required |

The worker tests use a simulated debugger peer and the real Prolog runtime.
They cover multiple answers, arithmetic, consulting and reloading files,
compile errors, ordinary query errors, UTF-8, standard output/error routing,
symbol lookups, unsigned 64-bit addresses, exact/partial/unavailable reads,
per-query caching, stale snapshot rejection, invalid snapshot rejection,
answer limits, inference limits, timeouts, process exit/restart, restricted
background-thread RPC, empty snapshots, and complete single-goal parsing.

Timeouts during a delayed memory reply and 25 inference-budget boundaries
around memory RPC were checked for protocol desynchronization. Subsequent
queries completed normally.

Portable protocol tests check binary-safe hex transport, empty fields,
unsigned-integer overflow, memory-range wraparound, atom escaping, Windows
process-argument quoting, and outer quoting of debugger command goals.

### Build environment

- Linux x86-64, Ubuntu 24.04.
- SWI-Prolog 9.0.4 for x86_64-linux.
- GNU C++ 13.3.0 for native portable tests.
- x86_64-w64-mingw32-g++ 13-posix for Windows binaries.
- CMake 3.28.3 and Ninja.
- DbgEng header from Microsoft's win32metadata repository:
  `generation/WinSDK/RecompiledIdlHeaders/um/DbgEng.h`.
- SHA-256 of the unmodified external header:
  `a9037bf9a096700082ec5d4d96a98d1d98dee6c8ed71b3eac8de64c1fd9a0f4f`.

The Microsoft header is a build dependency and is not included in the package.
The MinGW wrapper only supplies missing annotation macros around the include;
it does not change the interface declarations or vtable layout.

### Windows validation still required

The Linux build environment has no Windows WinDbg/CDB session. The included
DLL is therefore **compiled but not verified inside WinDbg**. Windows process
startup, pipe behavior, job assignment, DbgEng thread/scope restoration, symbol
resolution, Ctrl+Break behavior, and DLL unload/reload need execution on Windows.
MSVC compilation and the PowerShell automation have also not been run here.

Run `scripts/smoke-windows.ps1` on Windows after installing x64 SWI-Prolog and
Debugging Tools for Windows. It checks a generated full dump and its own live
test process. It verifies success markers, partial/unavailable memory evidence,
restored thread and scope, timeout recovery, worker reset, and DLL reload.
It writes logs and a dump into a new temporary directory and prints that path.

Then perform the manual Ctrl+Break test in README.md. The Prolog timeout tests
performed here do not establish WinDbg keyboard-interrupt behavior.

Follow-up tests should include a small dump with omitted application data,
a dump with unavailable private symbols, and a real application with its PDBs.
Custom `.ecxr` / `.cxr` contexts need specific inspection; this version does
not write target registers when restoring selection and symbol scope.

Kernel debugging, WOW64, ARM64, managed-runtime providers, and automatic live
execution actions are outside Version 1's implemented scope.

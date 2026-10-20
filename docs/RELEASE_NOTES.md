# PrologWinDbg v0.2.0

Experimental native ARM64 support, retaining native x64 support and the
PolyForm Strict 1.0.0 license.

- Build a native ARM64 DLL and diagnostic target with Visual Studio.
- Match the DLL, debugger, and effective target architecture before capturing
  architecture-specific scope contexts. Invalidate a query if the effective
  processor changes; never restore a saved context into another architecture.
- Expose `wd:architecture(S,arm64)` or `wd:architecture(S,amd64)` without
  changing the existing `target/4` or memory APIs.
- Select x64 or ARM64 builds and smoke tests explicitly, or use the native
  Windows host default. Keep build outputs separate and check PE machine types.
- Add native x64 and ARM64 Windows CI builds, portable tests, CDB dump/live
  smoke tests, and architecture-specific packages.

SWI-Prolog is installed separately. Native ARM64 builds are available from
its development download page. See README.md for setup and
VALIDATION.md for the exact verification status.

## Previous releases

### v0.1.1

Licensing and packaging update to the experimental v0.1.0 release.

- Adopt PolyForm Strict License 1.0.0, matching VDL Studio, with copyright
  © 2026 Dmitry Vostokov. The license also applies to original project code
  in v0.1.0. Third-party components retain their existing terms.
- Include LICENSE.md in the source tree, Windows archive, and dist/ directory;
  install it with future CMake builds.
- Update the README and release documentation to state the license.
- Preserve the runtime implementation, tests, and compiled Windows binaries
  byte-for-byte from v0.1.0. This release adds no runtime functionality.

Download `PrologWinDbg-v0.1.1-win-x64.zip` and verify it using `SHA256SUMS.txt`.
See [LICENSE.md](../LICENSE.md) for the full terms and
[VALIDATION.md](VALIDATION.md) for the validation record. Execution inside
Windows WinDbg/CDB remains untested; this is still an experimental prerelease.

### v0.1.0

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

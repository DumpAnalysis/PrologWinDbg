param(
    [string]$DebuggingTools = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers",
    [string]$Configuration = "Release",
    [ValidateSet('x64','ARM64')][string]$Architecture,
    [string]$Dist
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\windows-architecture.ps1"
$NativeArchitecture = Get-NativeWindowsArchitecture
if (-not $Architecture) { $Architecture = $NativeArchitecture }
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build-windows-$($Architecture.ToLowerInvariant())"
if (-not $Dist) { $Dist = Join-Path $Root "dist\$($Architecture.ToLowerInvariant())" }
# The Windows SDK also provides dbgeng.h through the compiler's normal include paths.
& cmake -S $Root -B $Build -A $Architecture "-DDEBUGGING_TOOLS_ROOT=$DebuggingTools"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$TestArguments = @('--test-dir', $Build, '-C', $Configuration, '--output-on-failure')
if ($Architecture -eq 'ARM64' -and $NativeArchitecture -ne 'ARM64') {
    Write-Warning 'Cross-building ARM64: native C++ tests must be run on ARM64 Windows.'
    $TestArguments += @('-E', '^(protocol|architecture)$')
}
& ctest @TestArguments
if ($LASTEXITCODE -ne 0) { throw 'Portable tests failed' }
& cmake --install $Build --config $Configuration --prefix $Dist
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
Get-ChildItem (Join-Path $Build $Configuration) -Filter '*.pdb' | Copy-Item -Destination $Dist
Assert-PEArchitecture (Join-Path $Dist 'prolog.dll') $Architecture
Assert-PEArchitecture (Join-Path $Dist 'prologdbg_target.exe') $Architecture
Write-Host "Built package: $Dist"

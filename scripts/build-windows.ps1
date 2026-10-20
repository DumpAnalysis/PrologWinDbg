param(
    [string]$DebuggingTools = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers",
    [string]$Configuration = "Release"
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root 'build-windows'
$Dist = Join-Path $Root 'dist'
if (-not (Test-Path (Join-Path $DebuggingTools 'sdk\inc\dbgeng.h')) -and
    -not (Test-Path (Join-Path $DebuggingTools 'inc\dbgeng.h'))) {
    throw 'Install Debugging Tools for Windows from the Windows SDK, or pass -DebuggingTools.'
}
& cmake -S $Root -B $Build -A x64 "-DDEBUGGING_TOOLS_ROOT=$DebuggingTools"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ctest --test-dir $Build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Portable tests failed' }
& cmake --install $Build --config $Configuration --prefix $Dist
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
Get-ChildItem (Join-Path $Build $Configuration) -Filter '*.pdb' | Copy-Item -Destination $Dist
Write-Host "Built package: $Dist"

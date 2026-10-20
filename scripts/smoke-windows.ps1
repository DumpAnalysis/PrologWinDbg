param(
    [string]$Dist = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [string]$Cdb = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
    [string]$Swipl = "$env:ProgramFiles\swipl\bin\swipl.exe"
)
$ErrorActionPreference = 'Stop'
$Dist = (Resolve-Path $Dist).Path
$Dll = Join-Path $Dist 'prolog.dll'
$Target = Join-Path $Dist 'prologdbg_target.exe'
$Rules = Join-Path $Dist 'examples\patterns.pl'
foreach ($Path in @($Cdb, $Swipl, $Dll, $Target, $Rules)) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing: $Path" }
}
$Run = Join-Path ([IO.Path]::GetTempPath()) ('prologdbg-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Run | Out-Null
$Dump = Join-Path $Run 'sample.dmp'
& $Target --dump $Dump
if ($LASTEXITCODE -ne 0) { throw 'Test target could not write its dump' }

$Body = @'
.load "__DLL__"
!prolog.runtime "__SWIPL__"
!prolog.info
!prolog.consult "__RULES__"
!prolog.limits 30000 100 128 10000000
!prolog.eval X is 6*7, X=42, writeln('PD_ARITHMETIC_OK').
!prolog.eval "(X=a;X=b), nonvar(X), writeln('PD_DISJUNCTION_OK')."
~1s
.frame 1
.printf "PD_THREAD_BEFORE:%x\n", @$tid
.printf "PD_SCOPE_BEFORE:%p\n", @$scopeip
!prolog.query wd:current_snapshot(S), findall(T,wd:thread(S,T),Ts), length(Ts,N), N>=3, writeln('PD_THREADS_OK').
!prolog.query wd:current_snapshot(S), wd:lookup(S,'prologdbg_target!g_magic',ok(A)), patterns:inspect_magic(S,A,matched(prologdb,A)), writeln('PD_MAGIC_OK').
!prolog.query wd:current_snapshot(S), once(patterns:shared_function(S,A,B,prologdbg_target,'WaitForRelease')), writeln('PD_STACKS_OK').
!prolog.query wd:current_snapshot(S), wd:lookup(S,'prologdbg_target!g_partial',ok(V)), wd:pointer(S,V,A), wd:bytes(S,A,4,[84,65,73,76]), wd:memory(S,A,8,R), functor(R,Shape,_), memberchk(Shape,[partial,unavailable]), writeln('PD_INCOMPLETE_OK').
.printf "PD_THREAD_AFTER:%x\n", @$tid
.printf "PD_SCOPE_AFTER:%p\n", @$scopeip
!prolog.limits 100 100 128 10000000
!prolog.eval sleep(2).
!prolog.limits 5000 100 128 10000000
!prolog.eval writeln('PD_AFTER_TIMEOUT_OK').
!prolog.reset
!prolog.info
!prolog.eval writeln('PD_RESTART_OK').
.unload "__DLL__"
.load "__DLL__"
!prolog.runtime "__SWIPL__"
!prolog.eval writeln('PD_RELOAD_OK').
!prolog.reset
q
'@
$Body = $Body.Replace('__DLL__', $Dll).Replace('__SWIPL__', $Swipl).Replace('__RULES__', $Rules)
foreach ($Mode in @('dump','live')) {
    $Commands = Join-Path $Run "$Mode.txt"
    $Log = Join-Path $Run "$Mode.log"
    $Text = $Body
    if ($Mode -eq 'live') { $Text = "g`r`n" + $Text }
    # DbgEng's extension-command ABI is ANSI. ASCII paths are simplest for this smoke script.
    [IO.File]::WriteAllText($Commands, $Text, [Text.Encoding]::Default)
    if ($Mode -eq 'dump') {
        & $Cdb -logo $Log -cf $Commands -z $Dump | Out-Host
    } else {
        & $Cdb -logo $Log -cf $Commands $Target --live | Out-Host
    }
    if ($LASTEXITCODE -ne 0) { throw "CDB $Mode session failed; see $Log" }
    $Result = Get-Content -Raw -LiteralPath $Log
    foreach ($Marker in @('PD_ARITHMETIC_OK','PD_DISJUNCTION_OK','PD_THREADS_OK','PD_MAGIC_OK','PD_STACKS_OK',
            'PD_INCOMPLETE_OK','PD_AFTER_TIMEOUT_OK','PD_RESTART_OK','PD_RELOAD_OK')) {
        # Anchor output markers so an echoed command does not count as success.
        if ($Result -notmatch "(?m)^$Marker\r?$" ) { throw "Missing $Marker in $Log" }
    }
    if ($Result -notmatch '\[prolog\] timeout;') { throw "Timeout was not observed in $Log" }
    foreach ($Item in @('THREAD','SCOPE')) {
        $Before = [regex]::Match($Result,"(?m)^PD_${Item}_BEFORE:([0-9a-fA-F``]+)\r?$")
        $After = [regex]::Match($Result,"(?m)^PD_${Item}_AFTER:([0-9a-fA-F``]+)\r?$")
        if (-not $Before.Success -or -not $After.Success -or $Before.Groups[1].Value -ne $After.Groups[1].Value) {
            throw "Debugger $Item was not restored; see $Log"
        }
    }
    Write-Host "$Mode smoke checks passed. Log: $Log"
}
Write-Host "All automated Windows smoke checks passed. Logs and dump retained at: $Run"
Write-Host 'Also run the manual WinDbg Ctrl+Break check described in README.md.'

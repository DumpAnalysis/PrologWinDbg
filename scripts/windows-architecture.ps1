function Get-NativeWindowsArchitecture {
    $Native = $env:PROCESSOR_ARCHITEW6432
    if (-not $Native) { $Native = $env:PROCESSOR_ARCHITECTURE }
    switch ($Native) {
        'AMD64' { return 'x64' }
        'ARM64' { return 'ARM64' }
        default { throw "Unsupported Windows architecture: $Native" }
    }
}

function Assert-PEArchitecture([string]$Path, [string]$Architecture) {
    $Stream = [IO.File]::OpenRead($Path)
    $Reader = [IO.BinaryReader]::new($Stream)
    try {
        if ($Reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $Path" }
        $Stream.Position = 0x3c
        $Offset = $Reader.ReadInt32()
        if ($Offset -lt 0x40 -or $Offset -gt $Stream.Length - 6) { throw "Invalid PE header: $Path" }
        $Stream.Position = $Offset
        if ($Reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        $Machine = $Reader.ReadUInt16()
        $Expected = if ($Architecture -eq 'ARM64') { 0xaa64 } else { 0x8664 }
        if ($Machine -ne $Expected) {
            throw ('Architecture mismatch: {0} has machine 0x{1:x4}; expected {2}' -f $Path, $Machine, $Architecture)
        }
    } finally { $Reader.Dispose() }
}

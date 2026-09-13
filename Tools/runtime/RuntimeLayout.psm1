Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-EngineFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant() }
    finally { $stream.Dispose() }
}

function Assert-EngineChildPath([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the owned output root: $full ($base)"
    }
    $cursor = $full
    while ($cursor.Length -ge $base.TrimEnd('\').Length) {
        $attributes = $null
        try { $attributes = [IO.File]::GetAttributes($cursor) }
        catch [IO.FileNotFoundException] {}
        catch [IO.DirectoryNotFoundException] {}
        if ($null -ne $attributes -and ($attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse point is not allowed in generated output: $cursor"
        }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor) { break }
        $cursor = $parent
    }
    $full
}

# Read imports directly: deployment/validation does not require dumpbin or Visual Studio.
function Get-EnginePeImports([string]$Path) {
    $data = [IO.File]::ReadAllBytes($Path)
    function U16([int]$at) { [BitConverter]::ToUInt16($data, $at) }
    function U32([int]$at) { [BitConverter]::ToUInt32($data, $at) }
    if ($data.Length -lt 64 -or (U16 0) -ne 0x5a4d) { throw "Not a PE image: $Path" }
    $pe = [int](U32 60)
    if ((U32 $pe) -ne 0x4550) { throw "Invalid PE signature: $Path" }
    $count = U16 ($pe + 6)
    $optional = $pe + 24
    $magic = U16 $optional
    $directory = if ($magic -eq 0x20b) { $optional + 112 } elseif ($magic -eq 0x10b) { $optional + 96 } else { throw "Unsupported PE: $Path" }
    $sectionStart = $optional + (U16 ($pe + 20))
    $sections = @()
    for ($i = 0; $i -lt $count; $i++) {
        $s = $sectionStart + 40 * $i
        $sections += @{ rva = U32 ($s + 12); size = [Math]::Max((U32 ($s + 8)), (U32 ($s + 16))); raw = U32 ($s + 20) }
    }
    function Offset([uint32]$rva) {
        foreach ($s in $sections) {
            if ($rva -ge $s.rva -and $rva -lt ($s.rva + $s.size)) { return [int]($s.raw + $rva - $s.rva) }
        }
        throw "PE RVA is outside sections: $Path : $rva"
    }
    function CString([int]$at) {
        $end = $at
        while ($end -lt $data.Length -and $data[$end] -ne 0) { $end++ }
        if ($end -eq $data.Length) { throw "Unterminated PE import: $Path" }
        [Text.Encoding]::ASCII.GetString($data, $at, $end - $at)
    }
    $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($table in @(@{ index = 1; stride = 20; name = 12 }, @{ index = 13; stride = 32; name = 4 })) {
        $rva = U32 ($directory + 8 * $table.index)
        if ($rva -eq 0) { continue }
        $at = Offset $rva
        while ((U32 ($at + $table.name)) -ne 0) {
            if ($table.index -eq 13 -and ((U32 $at) -band 1) -eq 0) { throw "VA delay imports are unsupported: $Path" }
            [void]$imports.Add((CString (Offset (U32 ($at + $table.name)))))
            $at += $table.stride
        }
    }
    @($imports | Sort-Object)
}

function Get-EngineEntries([string]$Root, [string[]]$Paths) {
    foreach ($relative in @($Paths | Sort-Object -Unique)) {
        $path = Assert-EngineChildPath (Join-Path $Root $relative) $Root
        $file = Get-Item -LiteralPath $path
        [pscustomobject][ordered]@{ path = $relative.Replace('\', '/'); bytes = $file.Length; sha256 = Get-EngineFileHash $path }
    }
}

function Get-EngineDigest([object[]]$Entries) {
    # Ordinal ordering makes identities independent of the publisher/verifier's locale.
    $ordered = [Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $unique = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Entries) {
        if (-not $unique.Add($entry.path) -or $entry.path.Contains("`n") -or $entry.path.Contains("`0")) { throw 'Duplicate or invalid engine payload path.' }
        $ordered.Add($entry.path, $entry.sha256)
    }
    $lines = @($ordered.GetEnumerator() | ForEach-Object { "$($_.Key)`0$($_.Value)" })
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))).ToLowerInvariant()
}

function Test-EngineEntries([string]$Root, [object[]]$Entries) {
    foreach ($entry in $Entries) {
        $path = Assert-EngineChildPath (Join-Path $Root $entry.path) $Root
        $file = [IO.FileInfo]::new($path)
        if (-not $file.Exists -or $file.Length -ne $entry.bytes -or (Get-EngineFileHash $path) -ne $entry.sha256) {
            throw "Engine file missing or changed: $($entry.path)"
        }
    }
}

function Write-EngineJson([string]$Path, $Value) {
    $parent = Split-Path -Parent $Path
    [void][IO.Directory]::CreateDirectory($parent)
    $temporary = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 20) + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary, $Path, $true)
}

Export-ModuleMember -Function *-Engine*

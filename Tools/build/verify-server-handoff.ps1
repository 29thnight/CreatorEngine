[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactRoot,
    [string]$LockPath = ''
)

$ErrorActionPreference = 'Stop'

if (!$LockPath) {
    $adjacentLock = Join-Path $PSScriptRoot 'ServerHandoffLock20261024.json'
    $LockPath = if (Test-Path -LiteralPath $adjacentLock) {
        $adjacentLock
    } else {
        Join-Path $PSScriptRoot '../../docs/analysis/ServerHandoffLock20261024.json'
    }
}

$root = [IO.Path]::GetFullPath($ArtifactRoot).TrimEnd('\', '/')
$lock = Get-Content -LiteralPath $LockPath -Raw -Encoding UTF8 | ConvertFrom-Json

if ($lock.schemaVersion -ne 1 -or @($lock.artifacts).Count -eq 0) {
    throw 'Unsupported or empty server handoff lock'
}

$seen = @{}
$bytes = [long]0

foreach ($entry in $lock.artifacts) {
    if ([IO.Path]::IsPathRooted($entry.path)) {
        throw "Artifact path must be relative: $($entry.path)"
    }

    $path = [IO.Path]::GetFullPath((Join-Path $root $entry.path))
    $prefix = $root + [IO.Path]::DirectorySeparatorChar

    if (!$path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or $seen.ContainsKey($path)) {
        throw "Outside-root or duplicate artifact: $($entry.path)"
    }

    $seen[$path] = $true
    $file = Get-Item -LiteralPath $path

    if ($file.Length -ne [long]$entry.bytes) {
        throw "Artifact size mismatch: $($entry.path)"
    }

    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash

    if ($hash -ne $entry.sha256) {
        throw "Artifact SHA256 mismatch: $($entry.path)"
    }

    $bytes += $file.Length
}

[pscustomobject]@{
    result = 'SERVER_HANDOFF_ARTIFACTS_OK'
    sourceRevision = $lock.source.revision
    fsharpRevision = $lock.fsharp.revision
    sdkBuildId = $lock.sdk.buildId
    artifacts = $seen.Count
    bytes = $bytes
} | ConvertTo-Json

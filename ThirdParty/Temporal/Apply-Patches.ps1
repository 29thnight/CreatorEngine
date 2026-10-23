#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$versions = Get-Content "$PSScriptRoot/SDKVersions.json" -Raw | ConvertFrom-Json
$sdk = Join-Path $PSScriptRoot 'FidelityFX-SDK'
$revision = & git -C $sdk rev-parse HEAD
if ($LASTEXITCODE -or $revision -ne $versions.fsr.revision) {
    throw 'FidelityFX revision differs from SDKVersions.json.'
}
function Get-NormalizedHash([string]$Path) {
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text))).ToLowerInvariant()
}
foreach ($patch in $versions.fsr.patches) {
    $source = Join-Path $sdk $patch.source
    $patchFile = Join-Path $PSScriptRoot $patch.path
    if ((Get-FileHash $patchFile).Hash.ToLowerInvariant() -ne $patch.sha256) {
        throw "SDK patch hash changed: $($patch.path)"
    }
    $current = Get-NormalizedHash $source
    if ($current -eq $patch.patchedNormalizedSha256) {
        continue
    }
    if ($current -ne $patch.originalNormalizedSha256) {
        throw "SDK source contains unrecognized changes: $($patch.source)"
    }
    & git -C $sdk apply --check --ignore-space-change $patchFile
    if ($LASTEXITCODE) {
        throw "SDK patch check failed: $($patch.path)"
    }
    & git -C $sdk apply --ignore-space-change $patchFile
    if ($LASTEXITCODE -or (Get-NormalizedHash $source) -ne $patch.patchedNormalizedSha256) {
        throw "SDK patch application failed: $($patch.path)"
    }
}

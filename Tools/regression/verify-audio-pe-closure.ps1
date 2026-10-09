param(
    [Parameter(Mandatory = $true)][string]$BinaryRoot,
    [Parameter(Mandatory = $true)][string]$EvidencePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../runtime/RuntimeLayout.psm1')
$forbidden = '^(fmod(?:L|studio|studioL)?|miniaudio)\.dll$'
$records = @()
foreach ($file in @(Get-ChildItem -LiteralPath $BinaryRoot -Recurse -File | Where-Object Extension -In '.exe', '.dll'))
{
    if ($file.Name -match $forbidden)
    {
        throw "Forbidden audio runtime file: $($file.FullName)"
    }
    $imports = @(Get-EnginePeImports $file.FullName)
    foreach ($name in $imports)
    {
        if ($name -match $forbidden)
        {
            throw "Forbidden PE import: $($file.FullName) -> $name"
        }
    }
    $records += [pscustomobject]@{ path = $file.FullName; sha256 = Get-EngineFileHash $file.FullName; imports = $imports }
}
if ($records.Count -eq 0)
{
    throw 'No PE images were checked.'
}
$parent = Split-Path -Parent ([IO.Path]::GetFullPath($EvidencePath))
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $EvidencePath -Encoding utf8
"AUDIO_PE_CLOSURE_OK images=$($records.Count) root=$BinaryRoot evidence=$EvidencePath"

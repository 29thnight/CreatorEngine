[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$Work
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new fixture directory.' }
$modelDirectory = Join-Path $Work 'Project/Assets/Models'
$shaderDirectory = Join-Path $Work 'Project/Assets/Shaders/DefaultPassShader'
$settingsDirectory = Join-Path $Work 'Project/ProjectSetting'
New-Item -ItemType Directory -Force $modelDirectory,$shaderDirectory,$settingsDirectory | Out-Null
$model = Join-Path $modelDirectory 'Prim_Cube.glb'
$meta = "$model.meta"
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Models/Prim_Cube.glb') -Destination $model
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Models/Prim_Cube.glb.meta') -Destination $meta
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/ProjectSetting/AssetIdentity.asset') -Destination $settingsDirectory
Get-ChildItem -LiteralPath (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') -File -Filter '*.shadermeta.meta' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $shaderDirectory
}
$originalMeta = [IO.File]::ReadAllBytes($meta)
$originalSourceHash = (Get-FileHash -LiteralPath $model).Hash
function Read-Sidecar {
    $text = [IO.File]::ReadAllText($meta)
    [pscustomobject]@{
        Generation = [UInt64]([regex]::Match($text, '(?m)^generation:\s*(\d+)\s*$').Groups[1].Value)
        ModelId = [regex]::Match($text, '(?m)^assetId:\s*([^\r\n]+)').Groups[1].Value.Trim()
        Ids = @([regex]::Matches($text, '(?m)^\s*assetId:\s*([^\r\n]+)') | ForEach-Object { $_.Groups[1].Value.Trim() }) -join ','
    }
}
$initial = Read-Sidecar
$cooker = Join-Path $repo "Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
$generations = Join-Path $Work 'Project/Library/ModelAssetGenerations'
function Invoke-Author([string]$Label, [string[]]$Extra = @()) {
    $arguments = @('--author-model-asset','--asset-root',(Join-Path $Work 'Project/Assets'),'--output',$generations,'--model',$model) + $Extra
    $process = Start-Process -FilePath $cooker -WorkingDirectory (Split-Path $cooker) -WindowStyle Hidden -PassThru -ArgumentList $arguments -RedirectStandardOutput (Join-Path $Work "$Label.out") -RedirectStandardError (Join-Path $Work "$Label.err")
    $null = $process.Handle
    if (-not $process.WaitForExit(45000)) { $process.Kill(); $process.WaitForExit(); throw "$Label timed out" }
    $process.WaitForExit()
    return $process.ExitCode
}
if ((Invoke-Author 'first') -ne 0) { throw 'Initial authoring failed.' }
$first = Read-Sidecar
$firstDirectory = Join-Path $generations "$($first.ModelId)/$($first.Generation)"
$firstHashes = @{}
Get-ChildItem -LiteralPath $firstDirectory -Recurse -File | ForEach-Object { $firstHashes[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName).Hash }
# Reproduce a canonical sidecar rollback while the newer immutable generation remains.
[IO.File]::WriteAllBytes($meta, $originalMeta)
if ((Invoke-Author 'canonical-rollback') -ne 0) { throw 'Recovery collided with an existing generation after sidecar rollback.' }
$recovered = Read-Sidecar
if ($recovered.Generation -ne ($first.Generation + 1) -or $recovered.Ids -ne $initial.Ids) { throw 'Recovery did not preserve identity and advance beyond the occupied generation.' }
foreach ($entry in $firstHashes.GetEnumerator()) {
    if ((Get-FileHash -LiteralPath $entry.Key).Hash -ne $entry.Value) { throw 'Recovery modified an existing generation.' }
}
$beforeFailureHash = (Get-FileHash -LiteralPath $meta).Hash
$beforeDirectories = @(Get-ChildItem -LiteralPath (Join-Path $generations $first.ModelId) -Directory).Count
if ((Invoke-Author 'failed-commit' @('--model-authoring-fail','after-generation-publish')) -eq 0 -or [IO.File]::ReadAllText((Join-Path $Work 'failed-commit.err')) -notmatch 'inject.after-generation-publish') { throw 'Failure injection was not reached.' }
if ((Get-FileHash -LiteralPath $meta).Hash -ne $beforeFailureHash -or @(Get-ChildItem -LiteralPath (Join-Path $generations $first.ModelId) -Directory).Count -ne $beforeDirectories) { throw 'Failed publication changed the canonical sidecar or retained a partial generation.' }
$maximumDirectory = Join-Path $generations "$($first.ModelId)/18446744073709551615"
New-Item -ItemType Directory -Path $maximumDirectory | Out-Null
[IO.File]::WriteAllText((Join-Path $maximumDirectory 'sentinel.txt'), 'preserve')
if ((Invoke-Author 'overflow') -eq 0 -or (Get-FileHash -LiteralPath $meta).Hash -ne $beforeFailureHash -or [IO.File]::ReadAllText((Join-Path $Work 'overflow.err')) -notmatch 'identity.generation.*overflow' -or [IO.File]::ReadAllText((Join-Path $maximumDirectory 'sentinel.txt')) -ne 'preserve') { throw 'Generation exhaustion did not preserve the canonical sidecar and existing generation.' }
if ((Get-FileHash -LiteralPath $model).Hash -ne $originalSourceHash) { throw 'Authoring changed the source model.' }
$verdict = [ordered]@{ configuration=$Configuration; initialGeneration=$initial.Generation; firstGeneration=$first.Generation; recoveredGeneration=$recovered.Generation; identitiesPreserved=$true; existingGenerationPreserved=$true; failedCommitAtomic=$true; overflowRejected=$true }
$verdict | ConvertTo-Json | Set-Content (Join-Path $Work 'verdict.json')
"MODEL_GENERATION_RESERVATION_OK $($verdict | ConvertTo-Json -Compress)"

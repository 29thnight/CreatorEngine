param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$model = Join-Path $root 'Dynamic_CPP\Assets\Models\Prim_Cube.glb'
if (-not (Test-Path -LiteralPath $model -PathType Leaf)) {
    "실자산 fixture가 없다: $model"
    exit 1
}

$sourcePaths = @($model, ($model + '.meta'))
$sourceHashes = @{}
foreach ($path in $sourcePaths) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        "실자산 target/meta pair가 없다: $path"
        exit 1
    }
    $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

$run = Join-Path $Work ("CE_D5CookedIdentity_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@(
    'experiment.cooked'
    "experiment.cooked $($model.Replace('\', '/'))"
    'quit'
) | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout -Raw
} else { '' }
$errorLines = if (Test-Path -LiteralPath $stderr) {
    @(Get-Content -LiteralPath $stderr)
} else { @() }
$knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
$unexpectedErrors = @($errorLines | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning
})

$cliPasses = ([regex]::Matches($text,
    '\[CLI\] experiment\.cooked 통과')).Count
$identityRejectPasses = ([regex]::Matches($text,
    'cooked identity 게시 거부: 5/5')).Count
$subassetRejectPasses = ([regex]::Matches($text,
    'model subasset identity 거부: 4/4')).Count
$manifestRejectPasses = ([regex]::Matches($text,
    'cooked asset manifest 거부: 9/9')).Count
$summaries = @([regex]::Matches($text,
    '단정 (\d+)건 중 통과 (\d+) · 실패 (\d+)'))
$allAssertionsPassed = $summaries.Count -eq 3 -and
    @($summaries | Where-Object { $_.Groups[3].Value -ne '0' }).Count -eq 0
$realIdentity = [regex]::Match($text,
    'cooked identity: model=yes materials=(\d+)/(\d+) shaders=(\d+)/(\d+) textures=(\d+)/(\d+)')
$realSidecar = [regex]::Match($text,
    'sidecar identity: model=yes materials=(\d+)/(\d+) embedded=(\d+)/(\d+)')
$realManifest = [regex]::Match($text,
    'manifest identity: entries=(\d+) model=yes materials=(\d+)/(\d+) sha256=yes')

$realIdentityComplete = $false
$realSidecarComplete = $false
$realManifestComplete = $false
$materialCount = 0
$textureCount = 0
if ($realIdentity.Success) {
    $materialValid = [int]$realIdentity.Groups[1].Value
    $materialCount = [int]$realIdentity.Groups[2].Value
    $shaderValid = [int]$realIdentity.Groups[3].Value
    $shaderCount = [int]$realIdentity.Groups[4].Value
    $textureValid = [int]$realIdentity.Groups[5].Value
    $textureCount = [int]$realIdentity.Groups[6].Value
    $realIdentityComplete = $materialCount -gt 0 -and
        $materialValid -eq $materialCount -and
        $shaderValid -eq $shaderCount -and
        $shaderCount -eq $materialCount -and
        $textureCount -gt 0 -and $textureValid -eq $textureCount
}
if ($realSidecar.Success) {
    $realSidecarComplete = [int]$realSidecar.Groups[1].Value -gt 0 -and
        $realSidecar.Groups[1].Value -eq $realSidecar.Groups[2].Value -and
        [int]$realSidecar.Groups[3].Value -gt 0 -and
        $realSidecar.Groups[3].Value -eq $realSidecar.Groups[4].Value
}
if ($realManifest.Success) {
    $realManifestComplete = [int]$realManifest.Groups[1].Value -ge 2 -and
        [int]$realManifest.Groups[2].Value -gt 0 -and
        $realManifest.Groups[2].Value -eq $realManifest.Groups[3].Value
}

$sourceMutations = [System.Collections.Generic.List[string]]::new()
foreach ($path in $sourceHashes.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $sourceMutations.Add("삭제됨: $path")
        continue
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $sourceHashes[$path]) {
        $sourceMutations.Add("변경됨: $path")
    }
}

"experiment-cooked-identities exit=$($process.ExitCode) output=$run"
"cliPasses=$cliPasses identityRejectPasses=$identityRejectPasses subassetRejectPasses=$subassetRejectPasses manifestRejectPasses=$manifestRejectPasses assertionSummaries=$($summaries.Count) allAssertionsPassed=$allAssertionsPassed materials=$materialCount textures=$textureCount sidecarComplete=$realSidecarComplete manifestComplete=$realManifestComplete sourceMutations=$($sourceMutations.Count) unexpectedStderr=$($unexpectedErrors.Count)"

$passed = 0 -eq $process.ExitCode -and
    $cliPasses -eq 2 -and
    $identityRejectPasses -eq 2 -and
    $subassetRejectPasses -eq 2 -and
    $manifestRejectPasses -eq 2 -and
    $allAssertionsPassed -and
    $realIdentityComplete -and
    $realSidecarComplete -and
    $realManifestComplete -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedErrors.Count -eq 0
if (-not $passed) {
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    if ($unexpectedErrors.Count -gt 0) { '예상하지 않은 stderr:'; $unexpectedErrors }
    exit 1
}

'전체 통과 — sidecar UUIDv4 subasset identity와 GUID-addressed Derived manifest가 checked .cemc 게시/왕복에서 보존됐다'
exit 0

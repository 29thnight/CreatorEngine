param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [int]$ExpectedScenes = 8,
    [int]$ExpectedPrefabs = 9,
    [int]$ExpectedMaterials = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = Join-Path $root 'Dynamic_CPP\Assets'
$scenes = @(Get-ChildItem -LiteralPath (Join-Path $assets 'Scenes') -File -Filter '*.creator' |
    Sort-Object FullName)
$prefabs = @(Get-ChildItem -LiteralPath (Join-Path $assets 'Prefabs') -File -Filter '*.prefab' |
    Sort-Object FullName)
$materials = @(Get-ChildItem -LiteralPath (Join-Path $assets 'Materials') -File -Filter '*.asset' |
    Sort-Object FullName)

if ($scenes.Count -ne $ExpectedScenes -or
    $prefabs.Count -ne $ExpectedPrefabs -or
    $materials.Count -ne $ExpectedMaterials) {
    "corpus count mismatch scenes=$($scenes.Count)/$ExpectedScenes prefabs=$($prefabs.Count)/$ExpectedPrefabs materials=$($materials.Count)/$ExpectedMaterials"
    exit 1
}

$corpus = @($scenes) + @($prefabs) + @($materials)
$sourceHashes = @{}
foreach ($item in $corpus) {
    foreach ($path in @($item.FullName, ($item.FullName + '.meta'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            "target/meta pair가 없다: $path"
            exit 1
        }
        $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}

$run = Join-Path $Work ("CE_D5DocumentParity_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$commands = [Collections.Generic.List[string]]::new()
foreach ($item in @($scenes) + @($prefabs)) {
    $commands.Add(('experiment.scenecook {0} {1}' -f
        $assets.Replace('\', '/'), $item.FullName.Replace('\', '/')))
}
foreach ($item in $materials) {
    $commands.Add(('experiment.matcook {0} {1}' -f
        $assets.Replace('\', '/'), $item.FullName.Replace('\', '/')))
}
$commands.Add('quit')
$commands | Set-Content -LiteralPath $scenario -Encoding UTF8

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

$sceneCliPasses = ([regex]::Matches($text,
    '\[CLI\] experiment\.scenecook 통과')).Count
$materialCliPasses = ([regex]::Matches($text,
    '\[CLI\] experiment\.matcook 통과')).Count
$sceneSummaries = @([regex]::Matches($text,
    '실자산 parity 단정 (\d+)/(\d+)'))
$overrideSummaries = @([regex]::Matches($text,
    'override CEDO (\d+)'))
$cookedOverrideValues = 0
foreach ($summary in $overrideSummaries) {
    $cookedOverrideValues += [int]$summary.Groups[1].Value
}
$authoredOverrideValues = @(
    @($scenes) + @($prefabs) | Select-String -Pattern '^\s*m_valueYaml:'
).Count
$materialSummaries = @([regex]::Matches($text,
    'material parity 단정 (\d+)/(\d+)'))
$sceneParity = $sceneSummaries.Count -eq ($scenes.Count + $prefabs.Count) -and
    @($sceneSummaries | Where-Object {
        $_.Groups[1].Value -ne $_.Groups[2].Value
    }).Count -eq 0
$overrideParity = $overrideSummaries.Count -eq ($scenes.Count + $prefabs.Count) -and
    $cookedOverrideValues -eq $authoredOverrideValues
$materialParity = $materialSummaries.Count -eq $materials.Count -and
    @($materialSummaries | Where-Object {
        $_.Groups[1].Value -ne $_.Groups[2].Value
    }).Count -eq 0
$failureMarkers = ([regex]::Matches($text,
    '(?:\[실패\]|\[CLI\] experiment\.(?:scenecook|matcook) 실패)')).Count

$sourceMutations = [Collections.Generic.List[string]]::new()
foreach ($path in $sourceHashes.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $sourceMutations.Add("삭제됨: $path")
        continue
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $sourceHashes[$path]) {
        $sourceMutations.Add("변경됨: $path")
    }
}

"document-cook-parity exit=$($process.ExitCode) output=$run"
"corpus scenes=$($scenes.Count) prefabs=$($prefabs.Count) materials=$($materials.Count) sceneCli=$sceneCliPasses materialCli=$materialCliPasses sceneParity=$sceneParity materialParity=$materialParity overrideCedo=$cookedOverrideValues/$authoredOverrideValues failures=$failureMarkers sourceMutations=$($sourceMutations.Count) unexpectedStderr=$($unexpectedErrors.Count)"

$passed = $process.ExitCode -eq 0 -and
    $sceneCliPasses -eq ($scenes.Count + $prefabs.Count) -and
    $materialCliPasses -eq $materials.Count -and
    $sceneParity -and $materialParity -and $overrideParity -and
    $failureMarkers -eq 0 -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedErrors.Count -eq 0
if (-not $passed) {
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    if ($unexpectedErrors.Count -gt 0) { '예상하지 않은 stderr:'; $unexpectedErrors }
    exit 1
}

'전체 통과 — 현존 scene/prefab/material의 authoring 문서와 GUID-addressed CEDO payload가 구조적으로 동일하다'
exit 0

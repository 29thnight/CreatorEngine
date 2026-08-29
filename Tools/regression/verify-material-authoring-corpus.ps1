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
$materialRoot = Join-Path $root 'Dynamic_CPP\Assets\Materials'
$materials = @(Get-ChildItem -LiteralPath $materialRoot -File -Filter '*.asset' |
    Sort-Object Name)
$expectedMaterialCount = 2
if ($materials.Count -ne $expectedMaterialCount) {
    "standalone material 코퍼스가 예상과 다르다: expected=$expectedMaterialCount actual=$($materials.Count)"
    $materials.Name
    exit 1
}

$sourceHashes = @{}
foreach ($material in $materials) {
    foreach ($path in @($material.FullName, ($material.FullName + '.meta'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            "material target/meta pair가 없다: $path"
            exit 1
        }
        $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}

$run = Join-Path $Work ("CE_D2MaterialCorpus_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$names = @($materials | ForEach-Object { $_.BaseName })
@(
    "material.corpus.probe $($names -join ' ')"
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

$perMaterialPasses = ([regex]::Matches($text,
    '\[material\.corpus\] \S+ pass identity=yes shader=yes textures=\d+/valid stable=yes')).Count
$summary = [regex]::Match($text,
    '\[material\.corpus\] pass materials=(\d+)/(\d+) textureRefs=(\d+)')
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

$textureReferences = if ($summary.Success) { $summary.Groups[3].Value } else { '?' }
"material-authoring-corpus exit=$($process.ExitCode) output=$run"
"materials=$($materials.Count) passed=$perMaterialPasses/$expectedMaterialCount summary=$($summary.Success) textureRefs=$textureReferences sourceMutations=$($sourceMutations.Count) unexpectedStderr=$($unexpectedErrors.Count)"

$passed = 0 -eq $process.ExitCode -and
    $perMaterialPasses -eq $expectedMaterialCount -and
    $summary.Success -and
    $summary.Groups[1].Value -eq $expectedMaterialCount -and
    $summary.Groups[2].Value -eq $expectedMaterialCount -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedErrors.Count -eq 0
if (-not $passed) {
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    if ($unexpectedErrors.Count -gt 0) { '예상하지 않은 stderr:'; $unexpectedErrors }
    exit 1
}

'전체 통과 — standalone material 2개의 identity·ShaderMeta·texture reference와 canonical payload가 보존됐다'
exit 0

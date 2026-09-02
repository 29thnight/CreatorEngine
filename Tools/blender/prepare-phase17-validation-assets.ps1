[CmdletBinding()]
param(
    [string]$Blender = '',
    [string]$InfinianRoot = (Join-Path $env:USERPROFILE 'Downloads\infinian-lineage-series'),
    [string]$SponzaRoot = (Join-Path $env:USERPROFILE 'Downloads\sponza'),
    [string]$OutDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot 'artifacts\phase17\validation-assets'
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
$InfinianRoot = [IO.Path]::GetFullPath($InfinianRoot)
$SponzaRoot = [IO.Path]::GetFullPath($SponzaRoot)

function Find-Blender {
    $candidates = [Collections.Generic.List[IO.FileInfo]]::new()
    foreach ($pattern in @(
        "$env:ProgramFiles\Blender Foundation\*\blender.exe",
        "${env:ProgramFiles(x86)}\Blender Foundation\*\blender.exe",
        "$env:LOCALAPPDATA\Programs\Blender*\blender.exe"
    )) {
        foreach ($candidate in @(Get-ChildItem $pattern -File -ErrorAction SilentlyContinue)) {
            $candidates.Add($candidate)
        }
    }
    $onPath = Get-Command blender -ErrorAction SilentlyContinue
    if ($null -ne $onPath) { $candidates.Add((Get-Item -LiteralPath $onPath.Source)) }
    if ($candidates.Count -eq 0) { return $null }
    return ($candidates | Sort-Object FullName -Descending | Select-Object -First 1).FullName
}

if ([string]::IsNullOrWhiteSpace($Blender)) { $Blender = Find-Blender }
if ([string]::IsNullOrWhiteSpace($Blender) -or
    -not (Test-Path -LiteralPath $Blender -PathType Leaf)) {
    throw 'Blender를 찾지 못했다. -Blender <blender.exe>로 지정하라.'
}

$requiredInputs = @(
    (Join-Path $InfinianRoot 'source\Infinian\Mon_Infinian_001_Skeleton.FBX'),
    (Join-Path $SponzaRoot 'source\Sponza.fbx')
)
foreach ($inputPath in $requiredInputs) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
        throw "PHASE 17 입력 모델이 없다: $inputPath"
    }
}
foreach ($textureRoot in @(
    (Join-Path $InfinianRoot 'textures'),
    (Join-Path $SponzaRoot 'textures')
)) {
    if (-not (Test-Path -LiteralPath $textureRoot -PathType Container)) {
        throw "PHASE 17 texture 폴더가 없다: $textureRoot"
    }
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$script = Join-Path $PSScriptRoot 'prepare_phase17_validation_assets.py'
$log = Join-Path $OutDir 'blender-prepare.log'

"Blender: $Blender"
"Infinian: $InfinianRoot"
"Sponza: $SponzaRoot"
"Output: $OutDir"

$output = & $Blender --background --factory-startup --python $script -- `
    --infinian-root $InfinianRoot --sponza-root $SponzaRoot --out $OutDir 2>&1
$exitCode = $LASTEXITCODE
$output | Set-Content -LiteralPath $log -Encoding utf8NoBOM
$output | Where-Object {
    $_ -match '^\[PHASE17\]' -or $_ -match 'Error|Traceback|Exception'
} | ForEach-Object { $_ }

if ($exitCode -ne 0) {
    $output | Select-Object -Last 80
    throw "Blender PHASE 17 asset 준비 실패 (exit=$exitCode, log=$log)"
}

$manifest = Join-Path $OutDir 'phase17-model-corpus.json'
$expectedOutputs = @(
    $manifest,
    (Join-Path $OutDir 'Phase17_Infinian.blend'),
    (Join-Path $OutDir 'Phase17_Infinian.glb'),
    (Join-Path $OutDir 'Phase17_Sponza.blend'),
    (Join-Path $OutDir 'Phase17_Sponza.glb')
)
foreach ($outputPath in $expectedOutputs) {
    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        throw "Blender 산출물이 없다: $outputPath"
    }
}

# Older runs made with interactive Blender defaults can leave .blend1 backups.
# They are generated only inside this explicit output root and are not part of
# the accepted corpus recorded by the manifest.
foreach ($backup in @(Get-ChildItem -LiteralPath $OutDir -File -Filter 'Phase17_*.blend1' `
    -ErrorAction SilentlyContinue)) {
    Remove-Item -LiteralPath $backup.FullName -Force
}

$manifestData = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json -Depth 100
foreach ($asset in @($manifestData.assets)) {
    "{0}: sourceVertices={1:N0} sourcePolygons={2:N0} materials={3} animations={4} glbVertices={5:N0} glbBytes={6:N0}" -f `
        $asset.name, $asset.scene.source_vertices, $asset.scene.source_polygons,
        $asset.scene.materials, @($asset.scene.actions).Count,
        $asset.glb.vertices, $asset.glb.bytes
}

"완료 — 원본 무변형·비압축 Blender/GLB 검증 자산과 manifest를 만들었다: $OutDir"

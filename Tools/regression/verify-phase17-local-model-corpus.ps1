[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$AssetRoot = (Join-Path $PSScriptRoot '..\..\artifacts\phase17\validation-assets'),
    [string]$Work = $env:TEMP,
    [ValidateRange(60, 7200)]
    [int]$TimeoutSeconds = 1800,
    [switch]$SkipEngine
)

# Local-only PHASE 17 acceptance corpus:
#   * Infinian proves skin + multi-clip animation import.
#   * Sponza proves that a multi-million-vertex source is not replaced by an
#     optimized or compressed fixture before reaching the engine importer.
#
# The large third-party inputs and generated Blender/GLB files stay under the
# ignored artifacts tree.  The reproducible Blender recipe and this gate are the
# tracked contract.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$AssetRoot = [IO.Path]::GetFullPath($AssetRoot)
$manifestPath = Join-Path $AssetRoot 'phase17-model-corpus.json'
$infinianPath = Join-Path $AssetRoot 'Phase17_Infinian.glb'
$sponzaPath = Join-Path $AssetRoot 'Phase17_Sponza.glb'
$failures = [Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-GlbDocument([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        $magic = $reader.ReadUInt32()
        $version = $reader.ReadUInt32()
        $declaredLength = $reader.ReadUInt32()
        if ($magic -ne 0x46546c67 -or $version -ne 2) {
            throw "glTF 2 GLB 헤더가 아니다: $Path"
        }
        if ($declaredLength -ne $stream.Length) {
            throw "GLB 선언 길이와 파일 길이가 다르다: $Path"
        }
        $jsonLength = $reader.ReadUInt32()
        $jsonType = $reader.ReadUInt32()
        if ($jsonType -ne 0x4e4f534a) { throw "GLB JSON chunk가 없다: $Path" }
        $jsonBytes = $reader.ReadBytes($jsonLength)
        $json = [Text.Encoding]::UTF8.GetString($jsonBytes).TrimEnd(" `t`r`n`0")
        return $json | ConvertFrom-Json -Depth 100
    } finally {
        $stream.Dispose()
    }
}

function Get-GlbSummary([string]$Path) {
    $document = Read-GlbDocument -Path $Path
    $extensions = if ($null -ne $document.PSObject.Properties['extensionsUsed']) {
        @($document.extensionsUsed)
    } else { @() }
    $forbidden = @($extensions | Where-Object {
        $_ -in @('KHR_draco_mesh_compression', 'EXT_meshopt_compression',
            'KHR_mesh_quantization')
    })
    $vertices = [long]0
    $indices = [long]0
    $primitives = 0
    $positionComponentTypes = [Collections.Generic.HashSet[int]]::new()
    foreach ($mesh in @($document.meshes)) {
        foreach ($primitive in @($mesh.primitives)) {
            ++$primitives
            $position = $primitive.attributes.PSObject.Properties['POSITION']
            if ($null -ne $position) {
                $accessor = $document.accessors[[int]$position.Value]
                $vertices += [long]$accessor.count
                [void]$positionComponentTypes.Add([int]$accessor.componentType)
            }
            $indexProperty = $primitive.PSObject.Properties['indices']
            if ($null -ne $indexProperty) {
                $indices += [long]$document.accessors[[int]$indexProperty.Value].count
            }
        }
    }
    $skins = @(if ($null -ne $document.PSObject.Properties['skins']) { $document.skins })
    $animations = @(if ($null -ne $document.PSObject.Properties['animations']) { $document.animations })
    $jointMeasure = $skins | ForEach-Object { @($_.joints).Count } | Measure-Object -Sum
    $channelMeasure = $animations | ForEach-Object { @($_.channels).Count } | Measure-Object -Sum
    return [pscustomobject]@{
        Path = $Path
        Bytes = (Get-Item -LiteralPath $Path).Length
        Extensions = $extensions
        ForbiddenExtensions = $forbidden
        Meshes = @($document.meshes).Count
        Primitives = $primitives
        Vertices = $vertices
        Indices = $indices
        PositionComponentTypes = @($positionComponentTypes)
        Materials = @($document.materials).Count
        Textures = @($document.textures).Count
        Images = @($document.images).Count
        Skins = $skins.Count
        Joints = [long]$jointMeasure.Sum
        Animations = $animations.Count
        AnimationChannels = [long]$channelMeasure.Sum
    }
}

foreach ($path in @($manifestPath, $infinianPath, $sponzaPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Failure "산출물이 없다: $path"
    }
}
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "실패: $_" }
    exit 1
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -Depth 100
if ([int]$manifest.schema -ne 1) { Add-Failure "manifest schema가 1이 아니다" }
if (@($manifest.assets).Count -ne 2) { Add-Failure "manifest asset 수가 2가 아니다" }

$mustBeFalse = @(
    'geometry_modifiers_applied', 'weld_vertices', 'vertex_cache_optimization',
    'mesh_decimation', 'draco_compression', 'gltfpack', 'mesh_quantization',
    'sparse_accessors', 'animation_key_reduction', 'source_tangents_exported',
    'blend_file_compression'
)
foreach ($propertyName in $mustBeFalse) {
    $property = $manifest.export_policy.PSObject.Properties[$propertyName]
    if ($null -eq $property -or [bool]$property.Value) {
        Add-Failure "비압축/비최적화 정책 위반 또는 누락: $propertyName"
    }
}
if ([int]$manifest.export_policy.animation_frame_step -ne 1) {
    Add-Failure 'animation frame step이 1이 아니다'
}
if (-not [bool]$manifest.export_policy.engine_mikktspace_generation) {
    Add-Failure 'engine MikkTSpace tangent 생성 정책이 켜져 있지 않다'
}

$manifestByName = @{}
foreach ($asset in @($manifest.assets)) { $manifestByName[[string]$asset.name] = $asset }
foreach ($name in @('Infinian', 'Sponza')) {
    if (-not $manifestByName.ContainsKey($name)) { Add-Failure "manifest asset 누락: $name" }
}

if ($manifestByName.ContainsKey('Infinian')) {
    $asset = $manifestByName.Infinian
    if ([int]$asset.scene.source_vertices -ne 37685) { Add-Failure 'Infinian source vertex 기준선이 37,685가 아니다' }
    if ([int]$asset.scene.source_polygons -ne 46223) { Add-Failure 'Infinian source polygon 기준선이 46,223이 아니다' }
    if ([int]$asset.scene.bones -ne 190) { Add-Failure 'Infinian bone 기준선이 190이 아니다' }
    if (@($asset.scene.actions).Count -ne 14) { Add-Failure 'Infinian animation clip 기준선이 14가 아니다' }
    if ([int]$asset.scene.materials -ne 3) { Add-Failure 'Infinian material 기준선이 3이 아니다' }
}
if ($manifestByName.ContainsKey('Sponza')) {
    $asset = $manifestByName.Sponza
    if ([int]$asset.scene.source_vertices -ne 1932514) { Add-Failure 'Sponza source vertex 기준선이 1,932,514가 아니다' }
    if ([int]$asset.scene.source_polygons -ne 2408209) { Add-Failure 'Sponza source polygon 기준선이 2,408,209가 아니다' }
    if ([int]$asset.scene.materials -ne 28) { Add-Failure 'Sponza material 기준선이 28이 아니다' }
    if (@($asset.scene.actions).Count -ne 0) { Add-Failure 'Sponza 정적 씬에 animation action이 남아 있다' }
}

foreach ($asset in @($manifest.assets)) {
    if (-not (Test-Path -LiteralPath $asset.source.fbx -PathType Leaf)) {
        Add-Failure "원본 FBX가 없다: $($asset.source.fbx)"
        continue
    }
    $sourceHash = (Get-FileHash -LiteralPath $asset.source.fbx -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -ne [string]$asset.source.fbx_sha256) {
        Add-Failure "원본 FBX가 준비 이후 변경됐다: $($asset.source.fbx)"
    }
    foreach ($texture in @($asset.source.textures)) {
        if (-not (Test-Path -LiteralPath $texture.path -PathType Leaf)) {
            Add-Failure "원본 texture가 없다: $($texture.path)"
            continue
        }
        $textureHash = (Get-FileHash -LiteralPath $texture.path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($textureHash -ne [string]$texture.sha256) {
            Add-Failure "원본 texture가 준비 이후 변경됐다: $($texture.path)"
        }
    }
}

$infinian = Get-GlbSummary -Path $infinianPath
$sponza = Get-GlbSummary -Path $sponzaPath
foreach ($summary in @($infinian, $sponza)) {
    if (@($summary.ForbiddenExtensions).Count -ne 0) {
        Add-Failure "압축/양자화 확장이 있다: $($summary.Path) -> $($summary.ForbiddenExtensions -join ',')"
    }
    if (@($summary.PositionComponentTypes).Count -ne 1 -or
        $summary.PositionComponentTypes[0] -ne 5126) {
        Add-Failure "POSITION accessor가 float32 단일 계약이 아니다: $($summary.Path)"
    }
    $manifestAsset = $manifestByName[[IO.Path]::GetFileNameWithoutExtension($summary.Path).Replace('Phase17_', '')]
    $hash = (Get-FileHash -LiteralPath $summary.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne [string]$manifestAsset.glb.sha256) {
        Add-Failure "GLB가 manifest 생성 이후 변경됐다: $($summary.Path)"
    }
}
if ($infinian.Skins -lt 1 -or $infinian.Joints -lt 190 -or $infinian.Animations -lt 14) {
    Add-Failure "Infinian GLB skin/animation 축이 줄었다: skins=$($infinian.Skins) joints=$($infinian.Joints) animations=$($infinian.Animations)"
}
if ($infinian.Materials -lt 3 -or $infinian.Textures -lt 6) {
    Add-Failure "Infinian GLB material 축이 줄었다: materials=$($infinian.Materials) textures=$($infinian.Textures)"
}
if ($sponza.Vertices -lt 1932514 -or $sponza.Materials -lt 28 -or $sponza.Textures -lt 70) {
    Add-Failure "Sponza GLB 대형 vertex/material 축이 줄었다: vertices=$($sponza.Vertices) materials=$($sponza.Materials) textures=$($sponza.Textures)"
}

"phase17-local-corpus root=$AssetRoot blender=$($manifest.blender_version)"
"Infinian glbBytes=$($infinian.Bytes) meshes=$($infinian.Meshes) primitives=$($infinian.Primitives) vertices=$($infinian.Vertices) indices=$($infinian.Indices) materials=$($infinian.Materials) textures=$($infinian.Textures) skins=$($infinian.Skins) joints=$($infinian.Joints) animations=$($infinian.Animations) channels=$($infinian.AnimationChannels)"
"Sponza glbBytes=$($sponza.Bytes) meshes=$($sponza.Meshes) primitives=$($sponza.Primitives) vertices=$($sponza.Vertices) indices=$($sponza.Indices) materials=$($sponza.Materials) textures=$($sponza.Textures)"

if (-not $SkipEngine -and $failures.Count -eq 0) {
    if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
        Add-Failure "Editor 실행 파일이 없다: $Exe"
    } else {
        $run = Join-Path $Work ('CE_Phase17LocalModels_' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $run -Force | Out-Null
        $scenario = Join-Path $run 'commands.txt'
        $stdout = Join-Path $run 'stdout.txt'
        $stderr = Join-Path $run 'stderr.txt'
        @(
            "experiment.phase17model animated $($infinianPath.Replace('\', '/'))"
            "experiment.phase17model large $($sponzaPath.Replace('\', '/'))"
            'quit'
        ) | Set-Content -LiteralPath $scenario -Encoding utf8NoBOM

        $process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
            -WorkingDirectory $repoRoot -WindowStyle Hidden `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        $process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
        if (-not $process.HasExited) {
            $process.Kill()
            Add-Failure "engine import 검증 시간 초과: $run"
        } else {
            $text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
            $animatedPasses = @([regex]::Matches($text,
                '\[CLI\] experiment\.phase17model 통과 mode=animated(?:\s|$)')).Count
            $largePasses = @([regex]::Matches($text,
                '\[CLI\] experiment\.phase17model 통과 mode=large(?:\s|$)')).Count
            $corpusPasses = $animatedPasses + $largePasses
            if ($process.ExitCode -ne 0) { Add-Failure "Editor exit code=$($process.ExitCode): $run" }
            if ($animatedPasses -ne 1 -or $largePasses -ne 1) {
                Add-Failure "engine PHASE 17 corpus 모드별 통과가 아니다 (animated=$animatedPasses, large=$largePasses): $run"
            }
            $errorLines = @(if (Test-Path -LiteralPath $stderr) {
                Get-Content -LiteralPath $stderr | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
            })
            if ($errorLines.Count -gt 0) {
                Add-Failure "engine stderr가 비어 있지 않다 ($($errorLines.Count)줄): $run"
            }
            "engine phase17Corpus=$corpusPasses/2 exit=$($process.ExitCode) output=$run"
        }
    }
}

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

if ($SkipEngine) {
    '전체 통과 — 원본 무변형·비압축 PHASE 17 local corpus 구조와 해시가 유효하다 (engine 실행 생략)'
} else {
    '전체 통과 — 원본 무변형·비압축 corpus와 CreatorEngine source/animation 경로가 PHASE 17 로컬 입력을 수용했다'
}
exit 0

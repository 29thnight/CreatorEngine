param(
    [string]$EditorExe = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

function Read-Source([string]$relativePath) {
    Get-Content -LiteralPath (Join-Path $repoRoot $relativePath) -Raw
}

function Assert-Matches([string]$relativePath, [string]$pattern) {
    $text = Read-Source $relativePath
    if ($text -notmatch $pattern) {
        throw "required asset-authoring route is missing: $relativePath / $pattern"
    }
}

function Assert-DoesNotMatch([string]$relativePath, [string]$pattern) {
    $text = Read-Source $relativePath
    if ($text -match $pattern) {
        throw "runtime source owns a forbidden asset writer: $relativePath / $pattern"
    }
}

# ModelLoader may build an in-memory cache payload, but filesystem mutation and
# embedded-image encoding belong to the Editor adapter.
Assert-DoesNotMatch "RenderEngine\ModelLoader.cpp" `
    'std::ofstream|SaveToWICFile|create_directories\s*\('
Assert-Matches "RenderEngine\ModelLoader.cpp" `
    'AssetAuthoringPort::WriteModelCache'
Assert-Matches "RenderEngine\ModelLoader.cpp" `
    'AssetAuthoringPort::WriteEmbeddedTexture'

# Terrain produces only a value snapshot. PNG/texture/descriptor publication and
# transaction lifetime belong to the Editor adapter.
Assert-DoesNotMatch "ScriptBinder\Terrain.cpp" `
    'std::ofstream|stbi_write_png|create_directories\s*\(|copy_file\s*\(|WorkerPools'
Assert-Matches "ScriptBinder\Terrain.cpp" `
    'AssetAuthoringPort::WriteTerrain'
Assert-DoesNotMatch "ScriptBinder\Terrain.cpp" `
    'BuildOutTrrain|SaveEditorHeightMap|SaveEditorSplatMap'

Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'InstallModelCacheWriter'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'InstallEmbeddedTextureWriter'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'InstallTerrainWriter'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'SaveToWICFile'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'file::copy_file'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'stbi_write_png'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'file::rename\(stagingDirectory, finalGeneration'
Assert-Matches "EngineEntry\EditorAssetDatabase.cpp" `
    'MoveFileExW\(descriptorTemporary\.c_str\(\), descriptorPath\.c_str\(\)'

$playerSources = Get-ChildItem -LiteralPath (Join-Path $repoRoot "Player") `
    -Recurse -File -Include *.cpp,*.h | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Raw
    }
$playerText = $playerSources -join "`n"
if ($playerText -match 'Install(?:ModelCacheWriter|EmbeddedTextureWriter|TerrainWriter)') {
    throw "Player installs an Editor asset-authoring writer"
}

if ([string]::IsNullOrWhiteSpace($EditorExe)) {
    $EditorExe = Join-Path $repoRoot "Bin\Editor\Academy_4Q.exe"
}
if (-not (Test-Path -LiteralPath $EditorExe)) {
    throw "Editor executable is missing: $EditorExe"
}
$editorRuntimeDirectory = Split-Path $EditorExe -Parent
$editorWatcherRuntime = Join-Path $editorRuntimeDirectory "efsw.dll"
if (-not (Test-Path -LiteralPath $editorWatcherRuntime)) {
    throw "Editor asset database runtime is missing: $editorWatcherRuntime"
}

$probeName = "CE_AssetWriterProbe_" + [guid]::NewGuid().ToString("N")
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempRoot = [IO.Path]::GetFullPath((Join-Path $tempBase $probeName))
$modelRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "Dynamic_CPP\Assets\Models"))
if (-not $tempRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "probe temp path escaped the temp root: $tempRoot"
}

$sourceDirectory = Join-Path $tempRoot "source"
$source = Join-Path $sourceDirectory ($probeName + ".glb")
$commandFile = Join-Path $tempRoot "import.txt"
$destination = Join-Path $modelRoot ($probeName + ".glb")
$cache = Join-Path $modelRoot ($probeName + ".asset")
$materialRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "Dynamic_CPP\Assets\Materials"))
$embeddedName = "CEProbe_" + $probeName.Substring($probeName.Length - 11)
$embeddedTexture = Join-Path $materialRoot ($embeddedName + ".png")
$terrainRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "Dynamic_CPP\Assets\Terrain"))
$terrainName = "CE_TerrainWriterProbe_" + $probeName.Substring($probeName.Length - 12)
$terrainDescriptor = Join-Path $terrainRoot ($terrainName + ".terrain")
$terrainData = Join-Path $terrainRoot ($terrainName + ".terrain-data")
$terrainTexture = Join-Path $materialRoot "Plane_Mat_BaseColor.png"
$createdModelAssets = @(
    $destination
    ($destination + ".meta")
    $cache
    ($cache + ".meta")
    ($cache + ".tmp")
)
$createdMaterialAssets = @(
    $embeddedTexture
    ($embeddedTexture + ".meta")
    ($embeddedTexture + ".tmp")
)

function Replace-ByteSequence(
    [byte[]]$bytes,
    [byte[]]$needle,
    [byte[]]$replacement
) {
    if ($needle.Length -ne $replacement.Length) {
        throw "GLB probe replacement must preserve the JSON chunk length"
    }
    $matches = 0
    for ($offset = 0; $offset -le $bytes.Length - $needle.Length; $offset++) {
        $equal = $true
        for ($index = 0; $index -lt $needle.Length; $index++) {
            if ($bytes[$offset + $index] -ne $needle[$index]) {
                $equal = $false
                break
            }
        }
        if (-not $equal) { continue }
        [Array]::Copy($replacement, 0, $bytes, $offset, $replacement.Length)
        $matches++
        $offset += $needle.Length - 1
    }
    if ($matches -eq 0) {
        throw "embedded texture name was not found in the GLB fixture"
    }
}

function Invoke-Import([string]$suffix) {
    $stdout = Join-Path $tempRoot ("stdout-" + $suffix + ".txt")
    $stderr = Join-Path $tempRoot ("stderr-" + $suffix + ".txt")
    $process = Start-Process -FilePath $EditorExe `
        -ArgumentList "--console", "--script", $commandFile `
        -WorkingDirectory $editorRuntimeDirectory `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        throw "asset-authoring probe timed out ($suffix)"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        $errorText = if (Test-Path -LiteralPath $stderr) {
            Get-Content -LiteralPath $stderr -Raw
        } else { "" }
        throw "asset-authoring probe failed ($suffix): exit=$($process.ExitCode) $errorText"
    }
    return Get-Content -LiteralPath $stdout -Raw
}

try {
    New-Item -ItemType Directory -Path $sourceDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $modelRoot "Prim_Plane.glb") -Destination $source
    $sourceBytes = [IO.File]::ReadAllBytes($source)
    Replace-ByteSequence $sourceBytes `
        ([Text.Encoding]::UTF8.GetBytes("Plane_Mat_BaseColor")) `
        ([Text.Encoding]::UTF8.GetBytes($embeddedName))
    [IO.File]::WriteAllBytes($source, $sourceBytes)
    [IO.File]::WriteAllLines($commandFile, @("model.load $source", "quit"))

    $null = Invoke-Import "first"
    if (-not (Test-Path -LiteralPath $destination)) {
        throw "Editor source intake did not create the model destination"
    }
    if (-not (Test-Path -LiteralPath $cache)) {
        throw "Editor model-cache writer did not create the .asset artifact"
    }
    $firstCache = Get-Item -LiteralPath $cache
    if ($firstCache.Length -le 0) {
        throw "Editor model-cache writer created an empty artifact"
    }
    if (-not (Test-Path -LiteralPath $embeddedTexture) -or
        (Get-Item -LiteralPath $embeddedTexture).Length -le 0) {
        throw "Editor embedded-texture writer did not create the PNG artifact"
    }
    if ((Test-Path -LiteralPath ($cache + ".tmp")) -or
        (Test-Path -LiteralPath ($embeddedTexture + ".tmp"))) {
        throw "Editor asset writer left a temporary publication artifact"
    }
    $firstHash = (Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash
    $firstWrite = $firstCache.LastWriteTimeUtc
    $firstTextureHash = (Get-FileHash -LiteralPath $embeddedTexture -Algorithm SHA256).Hash
    $firstTextureWrite = (Get-Item -LiteralPath $embeddedTexture).LastWriteTimeUtc

    # Reimport preserves the source timestamp. The existing cache must therefore
    # be read, not rewritten through the authoring port.
    $null = Invoke-Import "cached"
    $secondCache = Get-Item -LiteralPath $cache
    $secondHash = (Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash
    if ($secondHash -ne $firstHash -or $secondCache.LastWriteTimeUtc -ne $firstWrite) {
        throw "a usable model cache was unexpectedly rewritten"
    }
    if ((Get-FileHash -LiteralPath $embeddedTexture -Algorithm SHA256).Hash -ne
            $firstTextureHash -or
        (Get-Item -LiteralPath $embeddedTexture).LastWriteTimeUtc -ne
            $firstTextureWrite) {
        throw "a cached model unexpectedly rewrote its embedded texture"
    }

    # 같은 Editor 세션에서 같은 source를 다시 import하면 기존 cache hit로
    # 끝나면 안 된다. import transaction의 ContentReload가 이전 generation을
    # lookup에서 분리하고 두 번째 load가 새 Model을 설치해야 한다.
    [IO.File]::WriteAllLines($commandFile, @(
        "model.load $source"
        "model.load $source"
        "quit"
    ))
    $runtimeReloadOutput = Invoke-Import "runtime-reload"
    if ($runtimeReloadOutput -notmatch 'runtime-cache=reloaded') {
        throw "same-session model reimport did not replace the runtime cache generation"
    }

    # Terrain payload is assembled in a .tmp directory, renamed as one immutable
    # generation, and only then made visible by atomically replacing the descriptor.
    [IO.File]::WriteAllLines($commandFile, @(
        "terrain.authoring.probe $terrainName $terrainTexture"
        "quit"
    ))
    $terrainOutput = Invoke-Import "terrain-commit"
    if ($terrainOutput -notmatch '\[terrain\.authoring\.probe\] committed') {
        throw "Terrain authoring transaction did not commit"
    }
    if (-not (Test-Path -LiteralPath $terrainDescriptor) -or
        -not (Test-Path -LiteralPath ($terrainDescriptor + ".meta"))) {
        throw "Terrain descriptor/meta publication is incomplete"
    }

    $terrainJson = Get-Content -LiteralPath $terrainDescriptor -Raw | ConvertFrom-Json
    $heightPath = Join-Path $terrainRoot $terrainJson.heightmap
    $splatPath = Join-Path $terrainRoot $terrainJson.splatmaps[0]
    $diffusePath = Join-Path $terrainRoot $terrainJson.layers[0].diffuseTexturePath
    foreach ($artifact in @($heightPath, $splatPath, $diffusePath)) {
        if (-not (Test-Path -LiteralPath $artifact) -or
            (Get-Item -LiteralPath $artifact).Length -le 0) {
            throw "Terrain transaction published a missing/empty artifact: $artifact"
        }
    }
    foreach ($png in @($heightPath, $splatPath)) {
        $signature = [IO.File]::ReadAllBytes($png)
        if ($signature.Length -lt 8 -or $signature[0] -ne 0x89 -or
            $signature[1] -ne 0x50 -or $signature[2] -ne 0x4E -or
            $signature[3] -ne 0x47) {
            throw "Terrain transaction produced an invalid PNG: $png"
        }
    }
    $descriptorHash = (Get-FileHash -LiteralPath $terrainDescriptor -Algorithm SHA256).Hash
    $generationCount = @(Get-ChildItem -LiteralPath $terrainData -Directory).Count
    $temporaryArtifacts = @(Get-ChildItem -LiteralPath $terrainRoot -Recurse -Force |
        Where-Object { $_.Name -like "*$terrainName*" -and $_.FullName -match '\.tmp' })
    if ($temporaryArtifacts.Count -ne 0) {
        throw "Terrain transaction left temporary publication artifacts"
    }

    # A rejected update must not alter the prior commit marker or publish a new
    # generation. '-' deliberately supplies a missing diffuse source.
    [IO.File]::WriteAllLines($commandFile, @(
        "terrain.authoring.probe $terrainName -"
        "quit"
    ))
    $terrainRejectOutput = Invoke-Import "terrain-reject"
    if ($terrainRejectOutput -notmatch '\[terrain\.authoring\.probe\] rejected') {
        throw "invalid Terrain authoring request was not rejected"
    }
    if ((Get-FileHash -LiteralPath $terrainDescriptor -Algorithm SHA256).Hash -ne
        $descriptorHash -or
        @(Get-ChildItem -LiteralPath $terrainData -Directory).Count -ne $generationCount) {
        throw "rejected Terrain transaction changed the committed generation"
    }

    "asset authoring ownership: PASS (cache=$($firstCache.Length) bytes, runtime reload=PASS, terrain transaction=PASS)"
}
finally {
    $verifiedAssets = @()
    foreach ($target in $createdModelAssets) {
        $absoluteTarget = [IO.Path]::GetFullPath($target)
        $leaf = Split-Path $absoluteTarget -Leaf
        if (-not $absoluteTarget.StartsWith($modelRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not $leaf.StartsWith($probeName, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified probe asset: $absoluteTarget"
        }
        $verifiedAssets += $absoluteTarget
    }
    foreach ($target in $createdMaterialAssets) {
        $absoluteTarget = [IO.Path]::GetFullPath($target)
        $leaf = Split-Path $absoluteTarget -Leaf
        if (-not $absoluteTarget.StartsWith($materialRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not $leaf.StartsWith($embeddedName, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified embedded-texture probe: $absoluteTarget"
        }
        $verifiedAssets += $absoluteTarget
    }
    foreach ($target in $verifiedAssets) {
        Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
    }
    foreach ($target in @($terrainDescriptor, ($terrainDescriptor + ".meta"))) {
        $absoluteTarget = [IO.Path]::GetFullPath($target)
        if (-not $absoluteTarget.StartsWith($terrainRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $absoluteTarget -Leaf).StartsWith(
                $terrainName, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified Terrain probe asset: $absoluteTarget"
        }
        Remove-Item -LiteralPath $absoluteTarget -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $terrainData) {
        $absoluteTerrainData = [IO.Path]::GetFullPath($terrainData)
        if (-not $absoluteTerrainData.StartsWith($terrainRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $absoluteTerrainData -Leaf).StartsWith(
                $terrainName, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified Terrain probe directory: $absoluteTerrainData"
        }
        Remove-Item -LiteralPath $absoluteTerrainData -Recurse -Force
    }
    if (Test-Path -LiteralPath $tempRoot) {
        $verifiedTemp = [IO.Path]::GetFullPath($tempRoot)
        if (-not $verifiedTemp.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $verifiedTemp -Leaf).StartsWith(
                "CE_AssetWriterProbe_", [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified probe directory: $verifiedTemp"
        }
        Remove-Item -LiteralPath $verifiedTemp -Recurse -Force
    }
}

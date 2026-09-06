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

# PHASE 3.75 MBC9 — legacy ModelLoader(Assimp)와 그 cache/embedded-image writer 경로는
# 물리 삭제됐다. 모델 sidecar/generation 쓰기는 Assets/ModelAssetAuthoringTransaction만
# 소유한다(verify-mbc-cutover-freeze의 sidecar writer 허용목록). 여기서는 되살아나지
# 않았는지와 generation 로더가 파일을 쓰지 않는지만 잰다.
if (Test-Path -LiteralPath (Join-Path $repoRoot "Engine\RenderEngine\ModelLoader.cpp")) {
    throw "legacy ModelLoader.cpp가 되살아났다 — 모델 로드는 Assets/ModelAssetGeneration만 탄다"
}
Assert-DoesNotMatch "Engine\RenderEngine\Assets\ModelAssetGeneration.cpp" `
    'std::ofstream|SaveToWICFile|create_directories\s*\('
Assert-DoesNotMatch "Engine\RenderEngine\DataSystem.cpp" 'SaveToWICFile'

# Terrain produces only a value snapshot. PNG/texture/descriptor publication and
# transaction lifetime belong to the Editor adapter.
Assert-DoesNotMatch "Engine\SceneRuntime\Terrain.cpp" `
    'std::ofstream|stbi_write_png|create_directories\s*\(|copy_file\s*\(|WorkerPools'
Assert-Matches "Engine\SceneRuntime\Terrain.cpp" `
    'AssetAuthoringPort::WriteTerrain'
Assert-DoesNotMatch "Engine\SceneRuntime\Terrain.cpp" `
    'BuildOutTrrain|SaveEditorHeightMap|SaveEditorSplatMap'

# Foliage도 같은 계약이다. 컴포넌트는 YAML payload까지만 만들고 목적 경로 결정과
# 원자적 게시·meta 생성은 Editor adapter가 소유한다. CreateMeta 직접 호출이
# 남아 있으면 저작 트랜잭션이 두 곳으로 갈라진다.
Assert-DoesNotMatch "Engine\SceneRuntime\FoliageComponent.cpp" `
    'std::ofstream|create_directories\s*\(|copy_file\s*\(|AssetAuthoringPort::CreateMeta'
Assert-Matches "Engine\SceneRuntime\FoliageComponent.cpp" `
    'AssetAuthoringPort::WriteFoliage'
# 손대지 않은 writer 노드를 흘리면 0바이트를 낸다. 빈 시퀀스를 명시해야
# 타입/인스턴스가 0개인 자산도 저장되고 다시 열린다.
Assert-Matches "Engine\SceneRuntime\FoliageComponent.cpp" `
    'typesNode\.SetSequence\(\)'
Assert-Matches "Engine\SceneRuntime\FoliageComponent.cpp" `
    'instancesNode\.SetSequence\(\)'
# Foliage world matrix는 디스크 정본이 아니라 position/rotation/scale에서 다시 만드는
# 런타임 캐시다. Mathematics 전환 뒤에도 reflection/YAML에 들어가면 asset schema가
# 조용히 넓어지므로 타입과 비직렬화 규약을 함께 고정한다.
Assert-Matches "Engine\RenderEngine\Interfaces\FoliageInstance.h" `
    'math::matrix4x4\s+m_worldMatrix'
Assert-DoesNotMatch "Engine\RenderEngine\Interfaces\FoliageInstance.h" `
    'Mathf::xMatrix\s+m_worldMatrix|meta::field<&Self::m_worldMatrix>|meta::field<&Self::m_isCulled>'
Assert-Matches "Engine\RenderEngine\Interfaces\FoliageInstance.h" `
    'RebuildWorldMatrix\(\)'

# BlackBoard도 같은 계약이다. 읽기 경로(Deserialize)는 Core에 남으므로 이름→경로
# 규약이 두 벌이 되지 않도록 한 헬퍼에서만 만든다.
Assert-DoesNotMatch "Engine\SceneRuntime\BlackBoard.cpp" `
    'std::ofstream|create_directories\s*\(|AssetAuthoringPort::CreateMeta'
Assert-Matches "Engine\SceneRuntime\BlackBoard.cpp" `
    'AssetAuthoringPort::WriteBlackBoard'
Assert-Matches "Engine\SceneRuntime\BlackBoard.cpp" `
    'entriesNode\.SetSequence\(\)'
$blackBoardSource = Read-Source "Engine\SceneRuntime\BlackBoard.cpp"
$blackBoardPathPolicy = ([regex]::Matches(
    $blackBoardSource, 'BehaviorTree\\\\')).Count
if ($blackBoardPathPolicy -ne 1) {
    throw ("BlackBoard name-to-path policy must exist exactly once " +
        "(found $blackBoardPathPolicy) — write and read must not diverge")
}
# 두 handler 별칭은 같은 함수 포인터 타입이라 서로 바꿔 설치해도 컴파일된다.
# 타입이 못 막는 짝을 여기서 문자로 못 박는다.
# 프로젝트 설정 자산은 GUID로 참조되지 않고 ProjectSetting 폴더에 .meta가 하나도
# 없다. meta를 만드는 저작 자산 경로를 재사용하면 없던 사이드카가 생기기 시작한다.
Assert-DoesNotMatch "Engine\SceneRuntime\PhysicsManager.cpp" `
    'std::ofstream|create_directories\s*\(|AssetAuthoringPort::CreateMeta'
Assert-Matches "Engine\SceneRuntime\PhysicsManager.cpp" `
    'AssetAuthoringPort::WriteCollisionMatrix'
# D4 Animator는 씬 reflection YAML만 쓴다. JSON reader/writer 또는 Editor
# authoring handler가 다시 생기면 단일 정본 계약이 깨진다.
Assert-DoesNotMatch "Engine\SceneRuntime\Animator.cpp" `
	'std::ofstream|create_directories\s*\(|AssetAuthoringPort::CreateMeta|AssetAuthoringPort::WriteAnimatorController|SerializeControllers|DeserializeControllers|ImportLegacyControllerJson|nlohmann'
Assert-DoesNotMatch "Editor\EngineEntry\EditorAssetDatabase.cpp" `
	'AnimatorControllerWriter|WriteAnimatorControllerThroughEditor'

Assert-DoesNotMatch "Engine\SceneRuntime\InputActionManager.cpp" `
    'std::ofstream|create_directories\s*\(|AssetAuthoringPort::CreateMeta'
Assert-Matches "Engine\SceneRuntime\InputActionManager.cpp" `
    'AssetAuthoringPort::WriteInputActionMap'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallInputActionMapWriter\(\s*&WriteInputActionMapThroughEditor\)'

Assert-DoesNotMatch "Engine\SceneRuntime\TagManager.cpp" `
    'std::ofstream|create_directories\s*\(|AssetAuthoringPort::CreateMeta'
Assert-Matches "Engine\SceneRuntime\TagManager.cpp" `
    'AssetAuthoringPort::WriteTagManager'
Assert-Matches "Engine\SceneRuntime\TagManager.cpp" `
    'tagsNode\.SetSequence\(\)'
Assert-Matches "Engine\SceneRuntime\TagManager.cpp" `
    'layersNode\.SetSequence\(\)'
$tagManagerSource = Read-Source "Engine\SceneRuntime\TagManager.cpp"
$tagPathPolicy = ([regex]::Matches($tagManagerSource, 'TagManager\.asset')).Count
if ($tagPathPolicy -ne 1) {
    throw ("TagManager name-to-path policy must exist exactly once " +
        "(found $tagPathPolicy) — write and read must not diverge")
}

# ★ 이번 슬라이스가 실제로 밟은 함정의 회귀 방지. 태그 저장은 asset database가
#   authoring handler를 설치한 뒤부터 걷기 전까지만 성공한다. 호출 순서가 그
#   창 밖으로 나가면 빌드도 다른 게이트도 잡지 못한 채 저장이 조용히 사라진다.
$editorMainLines = Get-Content -LiteralPath (Join-Path $repoRoot "Editor\EngineEntry\EditorMain.cpp")
function Find-CallLine([string]$pattern) {
    for ($i = 0; $i -lt $editorMainLines.Count; $i++) {
        if ($editorMainLines[$i] -match $pattern) { return $i + 1 }
    }
    throw "EditorMain.cpp call site not found: $pattern"
}
$databaseUp = Find-CallLine 'EditorAssetDatabase::Get\(\)\.Initialize\(\)'
$tagUp = Find-CallLine 'TagManagers->Initialize\(\)'
$tagDown = Find-CallLine 'TagManagers->Finalize\(\)'
$databaseDown = Find-CallLine 'EditorAssetDatabase::Get\(\)\.Shutdown\(\)'
if ($tagUp -lt $databaseUp) {
    throw ("TagManagers->Initialize (line $tagUp) must run after " +
        "EditorAssetDatabase::Get().Initialize (line $databaseUp) — " +
        "first-run tag defaults are lost without an installed handler")
}
if ($tagDown -gt $databaseDown) {
    throw ("TagManagers->Finalize (line $tagDown) must run before " +
        "EditorAssetDatabase::Get().Shutdown (line $databaseDown) — " +
        "the shutdown save is lost after the handler is uninstalled")
}
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallTagManagerWriter\(&WriteTagManagerThroughEditor\)'

Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallCollisionMatrixWriter\(\s*&WriteCollisionMatrixThroughEditor\)'
$publishSettingBody = [regex]::Match((Read-Source "Editor\EngineEntry\EditorAssetDatabase.cpp"),
    'bool PublishUncatalogedLocked[\s\S]*?\n\t\}').Value
if ([string]::IsNullOrWhiteSpace($publishSettingBody)) {
    throw "PublishUncatalogedLocked body could not be located for inspection"
}
if ($publishSettingBody -match 'CreateMetaLocked') {
    throw "project setting publication must not create .meta sidecars"
}

Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallFoliageWriter\(&WriteFoliageThroughEditor\)'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallBlackBoardWriter\(&WriteBlackBoardThroughEditor\)'
# Editor 쪽 규약 철자도 한 벌만 있어야 Core의 ResolveBlackBoardPath와 갈라지지 않는다.
$databaseSource = Read-Source "Editor\EngineEntry\EditorAssetDatabase.cpp"
foreach ($literal in @('L"BehaviorTree"', 'L".blackboard"')) {
    $spelled = ([regex]::Matches($databaseSource, [regex]::Escape($literal))).Count
    if ($spelled -ne 1) {
        throw "Editor BlackBoard convention literal $literal must appear once (found $spelled)"
    }
}

Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallModelCacheWriter'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallEmbeddedTextureWriter'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallTerrainWriter'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'InstallFoliageWriter'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'SaveToWICFile'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'file::copy_file'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'stbi_write_png'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'file::rename\(stagingDirectory, finalGeneration'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'MoveFileExW\(descriptorTemporary\.c_str\(\), descriptorPath\.c_str\(\)'

$playerSources = Get-ChildItem -LiteralPath (Join-Path $repoRoot "Player") `
    -Recurse -File -Include *.cpp,*.h | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Raw
    }
$playerText = $playerSources -join "`n"
if ($playerText -match 'Install(?:ModelCacheWriter|EmbeddedTextureWriter|TerrainWriter|FoliageWriter|BlackBoardWriter|CollisionMatrixWriter|TagManagerWriter|InputActionMapWriter)') {
    throw "Player installs an Editor asset-authoring writer"
}

if ([string]::IsNullOrWhiteSpace($EditorExe)) {
$EditorExe = Join-Path $repoRoot "Bin\x64-Release\Editor\CreatorEditor.exe"
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
$foliageRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "Dynamic_CPP\Assets\Foliage"))
$foliageName = "CE_FoliageWriterProbe_" + $probeName.Substring($probeName.Length - 12)
$foliageAsset = Join-Path $foliageRoot ($foliageName + ".foliage")
$behaviorTreeRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "Dynamic_CPP\Assets\BehaviorTree"))
$blackBoardName = "CE_BlackBoardWriterProbe_" + $probeName.Substring($probeName.Length - 12)
$blackBoardAsset = Join-Path $behaviorTreeRoot ($blackBoardName + ".blackboard")
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

function Test-ByteSequence([byte[]]$bytes, [byte[]]$needle) {
    if ($needle.Length -eq 0 -or $bytes.Length -lt $needle.Length) { return $false }
    for ($offset = 0; $offset -le $bytes.Length - $needle.Length; $offset++) {
        $equal = $true
        for ($index = 0; $index -lt $needle.Length; $index++) {
            if ($bytes[$offset + $index] -ne $needle[$index]) {
                $equal = $false
                break
            }
        }
        if ($equal) { return $true }
    }
    return $false
}

function Invoke-Import([string]$suffix) {
    $stdout = Join-Path $tempRoot ("stdout-" + $suffix + ".txt")
    $stderr = Join-Path $tempRoot ("stderr-" + $suffix + ".txt")
    $process = Start-Process -FilePath $EditorExe `
        -ArgumentList "--commandlet-script", $commandFile `
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

    # I6-A(폐기 결정, 2026-09-01): legacy `.asset` 모델 캐시와 임베디드 텍스처
    # 디스크 추출은 **폐기됐다**. 제품 로드는 experiment importer를 타고
    # (LoadModelViaExperiment), 임베디드 텍스처의 신원은 모델 sidecar의
    # subAssets가 지며(I2-E), 바이트는 cooked artifact가 진다(D5-b1).
    # 그래서 이 검사는 "캐시를 만들었는가"가 아니라 **"만들지 않는가"**를 잰다.
    #
    # ★ 이 뒤집기 전까지 이 검사는 HEAD부터 붉었다 — 실패 사유가 결함이 아니라
    #   **결정되지 않은 정책**이었다. 결정이 났으므로 그 대우를 단정한다.
    if (Test-Path -LiteralPath $cache) {
        throw "legacy .asset 모델 캐시가 다시 생겼다(I6-A에서 폐기한 표면): $cache"
    }
    if (Test-Path -LiteralPath $embeddedTexture) {
        throw "임베디드 텍스처가 디스크로 추출됐다(I6-A에서 폐기한 표면): $embeddedTexture"
    }
    if ((Test-Path -LiteralPath ($cache + ".tmp")) -or
        (Test-Path -LiteralPath ($embeddedTexture + ".tmp"))) {
        throw "Editor asset writer left a temporary publication artifact"
    }

    # 재import는 여전히 원본 복사본을 그대로 두어야 한다(source intake는 살아
    # 있는 표면이다 — 폐기한 것은 캐시 게시뿐이다).
    $firstDestinationHash =
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    $null = Invoke-Import "cached"
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne
            $firstDestinationHash) {
        throw "재import가 원본 복사본을 다시 썼다"
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
    if ($terrainOutput -notmatch '\[terrain\.authoring\.probe\] committed .*roundtrip=PASS width=2 height=2 layers=1') {
        throw "Terrain authoring transaction did not commit"
    }
    if (-not (Test-Path -LiteralPath $terrainDescriptor) -or
        -not (Test-Path -LiteralPath ($terrainDescriptor + ".meta"))) {
        throw "Terrain descriptor/meta publication is incomplete"
    }

    $terrainYaml = Get-Content -LiteralPath $terrainDescriptor -Raw
    if ($terrainYaml -notmatch '(?m)^schemaVersion:\s*1\s*$' -or
        $terrainYaml.TrimStart().StartsWith('{')) {
        throw "Terrain descriptor is not canonical YAML"
    }
	function Read-TerrainScalar([string]$field) {
		$match = [regex]::Match($terrainYaml,
			"(?m)^[ \t]*$([regex]::Escape($field)):[ \t]*(.+?)[ \t]*$")
        if (-not $match.Success) { throw "Terrain field is missing: $field" }
        return $match.Groups[1].Value.Trim('"', "'")
    }
    $heightPath = Join-Path $terrainRoot (Read-TerrainScalar 'heightmap')
	$splatMatch = [regex]::Match($terrainYaml,
		'(?m)^splatmaps:[ \t]*\r?\n[ \t]*-[ \t]*(.+?)[ \t]*$')
    if (-not $splatMatch.Success) { throw "Terrain splatmap is missing" }
    $splatPath = Join-Path $terrainRoot $splatMatch.Groups[1].Value.Trim('"', "'")
    $diffusePath = Join-Path $terrainRoot (Read-TerrainScalar 'diffuseTexturePath')
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

    # Foliage payload는 Core가 만들고 게시는 Editor adapter가 한다. 커밋되면
    # .foliage와 .meta가 함께 나오고 .tmp 잔여가 없어야 한다.
    [IO.File]::WriteAllLines($commandFile, @(
        "foliage.authoring.probe $foliageName"
        "quit"
    ))
    $foliageOutput = Invoke-Import "foliage-commit"
    if ($foliageOutput -notmatch
        '\[foliage\.authoring\.probe\] committed .*fields=4-runtime-absent roundtrip=PASS derived=PASS') {
        throw "Foliage authoring transaction did not preserve schema and derived-world round trip"
    }
    if (-not (Test-Path -LiteralPath $foliageAsset) -or
        -not (Test-Path -LiteralPath ($foliageAsset + ".meta"))) {
        throw "Foliage asset/meta publication is incomplete"
    }
    if ((Get-Item -LiteralPath $foliageAsset).Length -le 0) {
        throw "Foliage transaction published an empty asset"
    }
    $foliagePayload = Get-Content -LiteralPath $foliageAsset -Raw
    foreach ($field in @('m_position:', 'm_rotation:', 'm_scale:', 'm_foliageTypeID:')) {
        if ($foliagePayload -notmatch [regex]::Escape($field)) {
            throw "Foliage payload is missing persisted field $field"
        }
    }
    if ($foliagePayload -match 'm_worldMatrix:|m_isCulled:') {
        throw "Foliage payload serialized a runtime-only field"
    }
    $foliageHash = (Get-FileHash -LiteralPath $foliageAsset -Algorithm SHA256).Hash
    $foliageTemporaries = @(Get-ChildItem -LiteralPath $foliageRoot -Recurse -Force |
        Where-Object { $_.Name -like "*$foliageName*" -and $_.FullName -match '\.tmp' })
    if ($foliageTemporaries.Count -ne 0) {
        throw "Foliage transaction left temporary publication artifacts"
    }

    # 목적지가 Foliage 루트를 벗어나면 거부되고 기존 커밋은 그대로여야 한다.
    [IO.File]::WriteAllLines($commandFile, @(
        "foliage.authoring.probe $foliageName escape"
        "quit"
    ))
    $foliageRejectOutput = Invoke-Import "foliage-reject"
    if ($foliageRejectOutput -notmatch '\[foliage\.authoring\.probe\] rejected') {
        throw "Foliage destination escape was not rejected"
    }
    if (Test-Path -LiteralPath (Join-Path $terrainRoot ($foliageName + ".foliage"))) {
        throw "rejected Foliage transaction wrote outside the Foliage root"
    }
    if ((Get-FileHash -LiteralPath $foliageAsset -Algorithm SHA256).Hash -ne $foliageHash) {
        throw "rejected Foliage transaction changed the committed asset"
    }

    # 콜론이 든 이름은 std::filesystem이 평범한 파일명으로 보지만 NTFS는 대체 데이터
    # 스트림으로 연다. 이름 검사가 막는지 보고, 회귀했다면 생겼을 기반 파일까지 센다.
    [IO.File]::WriteAllLines($commandFile, @(
        "foliage.authoring.probe ${foliageName}:ads"
        "quit"
    ))
    $foliageAdsOutput = Invoke-Import "foliage-ads"
    if ($foliageAdsOutput -notmatch '\[foliage\.authoring\.probe\] rejected') {
        throw "Foliage alternate-data-stream name was not rejected"
    }
    $foliageEntries = @(Get-ChildItem -LiteralPath $foliageRoot -Force |
        Where-Object { $_.Name -like "$foliageName*" })
    if ($foliageEntries.Count -ne 2) {
        throw ("Foliage root holds unexpected probe artifacts after the ADS rejection: " +
            ($foliageEntries.Name -join ', '))
    }

    # BlackBoard probe는 Foliage와 달리 runtime 타입의 실제 직렬화 경로를 태운다.
    # 저장한 값이 같은 이름으로 다시 읽혀 돌아오는지까지 확인해야 write/read 규약이
    # 갈라지지 않았음을 증명할 수 있다.
    [IO.File]::WriteAllLines($commandFile, @(
        "blackboard.authoring.probe $blackBoardName"
        "quit"
    ))
    $blackBoardOutput = Invoke-Import "blackboard-commit"
    if ($blackBoardOutput -notmatch '\[blackboard\.authoring\.probe\] committed keys=1 roundtrip=4177') {
        throw "BlackBoard authoring transaction did not round-trip its value"
    }
    if (-not (Test-Path -LiteralPath $blackBoardAsset) -or
        -not (Test-Path -LiteralPath ($blackBoardAsset + ".meta"))) {
        throw "BlackBoard asset/meta publication is incomplete"
    }
    $blackBoardTemporaries = @(Get-ChildItem -LiteralPath $behaviorTreeRoot -Recurse -Force |
        Where-Object { $_.Name -like "*$blackBoardName*" -and $_.FullName -match '\.tmp' })
    if ($blackBoardTemporaries.Count -ne 0) {
        throw "BlackBoard transaction left temporary publication artifacts"
    }

    # 값이 0개인 blackboard도 저장되고 다시 열려야 한다. 손대지 않은 YAML 노드를
    # 그대로 흘리면 0바이트가 나와 게시가 거부되고, 예전에는 그 파일이 조용히
    # 만들어진 뒤 아무 값도 복원하지 못했다.
    [IO.File]::WriteAllLines($commandFile, @(
        "blackboard.authoring.probe ${blackBoardName}_empty empty"
        "quit"
    ))
    $blackBoardEmptyOutput = Invoke-Import "blackboard-empty"
    if ($blackBoardEmptyOutput -notmatch '\[blackboard\.authoring\.probe\] committed keys=0') {
        throw "empty BlackBoard did not commit or did not reload"
    }

    # 이름이 비면 파일명이 ".blackboard"가 되고 stem()이 이름 전체를 돌려주는 바람에
    # 쓰기 경로가 ".blackboard.blackboard"로 어긋난다. Core가 먼저 막아야 한다.
    [IO.File]::WriteAllLines($commandFile, @(
        "blackboard.authoring.probe $blackBoardName noname"
        "quit"
    ))
    $blackBoardNoNameOutput = Invoke-Import "blackboard-noname"
    if ($blackBoardNoNameOutput -notmatch '\[blackboard\.authoring\.probe\] rejected') {
        throw "empty BlackBoard name was not rejected"
    }
    $strayBlackBoard = @(Get-ChildItem -LiteralPath $behaviorTreeRoot -Force |
        Where-Object { $_.Name -like ".blackboard*" })
    if ($strayBlackBoard.Count -ne 0) {
        throw ("empty BlackBoard name published a stray asset: " +
            ($strayBlackBoard.Name -join ', '))
    }

    # 충돌 행렬은 프로젝트 설정 자산이다. 값을 뒤집어 저장하고 파일에서 다시 읽어
    # 왕복을 확인한 뒤 원래 값으로 되돌린다 — 저장소 파일이 그대로여야 한다.
    $matrixAsset = Join-Path $repoRoot "Dynamic_CPP\ProjectSetting\CollisionMatrix.asset"
    $matrixBefore = (Get-FileHash -LiteralPath $matrixAsset -Algorithm SHA256).Hash
    [IO.File]::WriteAllLines($commandFile, @(
        "collisionmatrix.authoring.probe"
        "quit"
    ))
    $matrixOutput = Invoke-Import "collisionmatrix-commit"
    if ($matrixOutput -notmatch '\[collisionmatrix\.authoring\.probe\] committed roundtrip=ok restored=ok') {
        throw "CollisionMatrix authoring transaction did not round-trip through disk"
    }
    if ((Get-FileHash -LiteralPath $matrixAsset -Algorithm SHA256).Hash -ne $matrixBefore) {
        throw "CollisionMatrix probe did not restore the repository asset"
    }
    if (Test-Path -LiteralPath ($matrixAsset + ".meta")) {
        throw "project setting publication created a .meta sidecar"
    }

    # 설정 루트 밖 목적지는 거부되어야 한다.
    [IO.File]::WriteAllLines($commandFile, @(
        "collisionmatrix.authoring.probe escape"
        "quit"
    ))
    $matrixEscapeOutput = Invoke-Import "collisionmatrix-escape"
    if ($matrixEscapeOutput -notmatch '\[collisionmatrix\.authoring\.probe\] rejected') {
        throw "CollisionMatrix destination outside the project setting root was not rejected"
    }
    if (Test-Path -LiteralPath (Join-Path $foliageRoot "CollisionMatrix.asset")) {
        throw "rejected CollisionMatrix transaction wrote outside the setting root"
    }

    # 태그는 편집이 아니라 종료 시 Finalize가 저장한다. 한 프로세스 안에서 확인하면
    # 메모리 상태만 보게 되므로, 추가하고 정상 종료한 뒤 **다시 켜서** 확인해야
    # authoring handler 수명 창 안에서 저장됐음이 증명된다.
    $tagAsset = Join-Path $repoRoot "Dynamic_CPP\ProjectSetting\TagManager.asset"
    $tagBefore = (Get-FileHash -LiteralPath $tagAsset -Algorithm SHA256).Hash
    $tagProbeName = "CE_TagProbe_" + $probeName.Substring($probeName.Length - 12)

    [IO.File]::WriteAllLines($commandFile, @(
        "tag.add $tagProbeName"
        "quit"
    ))
    $tagAddOutput = Invoke-Import "tag-add"
    if ($tagAddOutput -notmatch '\[tag\.add\] has=true') {
        throw "TagManager did not accept the probe tag in memory"
    }
    if ((Get-FileHash -LiteralPath $tagAsset -Algorithm SHA256).Hash -eq $tagBefore) {
        throw "shutdown Finalize did not persist the tag — the authoring handler was already uninstalled"
    }

    [IO.File]::WriteAllLines($commandFile, @(
        "tag.has $tagProbeName"
        "tag.remove $tagProbeName"
        "quit"
    ))
    $tagReloadOutput = Invoke-Import "tag-reload"
    if ($tagReloadOutput -notmatch '\[tag\.has\] has=true') {
        throw "restarted Editor did not load the persisted probe tag"
    }
    if ($tagReloadOutput -notmatch '\[tag\.remove\] has=false') {
        throw "TagManager did not remove the probe tag"
    }
    if ((Get-FileHash -LiteralPath $tagAsset -Algorithm SHA256).Hash -ne $tagBefore) {
        throw "tag probe did not restore the repository asset"
    }
    if (Test-Path -LiteralPath ($tagAsset + ".meta")) {
        throw "TagManager publication created a .meta sidecar"
    }

    # 입력 액션맵은 맵마다 파일 하나이고, 읽기는 디렉터리 스캔이다. 이름에 '.'이 든
    # 맵이 잘리지 않는지(예전 replace_extension 결함), 그리고 저장한 것이 재기동 후
    # 스캔으로 다시 읽히는지를 함께 본다.
    $inputMapRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "Dynamic_CPP\Assets\InputMap"))
    $inputMapName = "CE_InputProbe_" + $probeName.Substring($probeName.Length - 12) + ".v2"
    $inputMapAsset = Join-Path $inputMapRoot ($inputMapName + ".inputmap")

    [IO.File]::WriteAllLines($commandFile, @(
        "inputmap.authoring.probe save $inputMapName"
        "quit"
    ))
    $inputSaveOutput = Invoke-Import "inputmap-save"
    if ($inputSaveOutput -notmatch '\[inputmap\.authoring\.probe\] save=ok') {
        throw "input action map did not save through the Editor authoring transaction"
    }
    if (-not (Test-Path -LiteralPath $inputMapAsset)) {
        throw "input action map name with a dot was truncated — expected $inputMapAsset"
    }
    if (Test-Path -LiteralPath ($inputMapAsset + ".meta")) {
        throw "input action map publication created a .meta sidecar"
    }

    [IO.File]::WriteAllLines($commandFile, @(
        "inputmap.authoring.probe verify $inputMapName"
        "quit"
    ))
    $inputVerifyOutput = Invoke-Import "inputmap-verify"
    if ($inputVerifyOutput -notmatch '\[inputmap\.authoring\.probe\] verify found=1 actions=1 stable=1') {
        throw "restarted Editor did not load the persisted input action map"
    }

	"asset authoring ownership: PASS (legacy .asset 캐시 미생성=PASS, 임베디드 추출 미생성=PASS, source intake=PASS, runtime reload=PASS, terrain transaction=PASS, foliage transaction=PASS, blackboard transaction=PASS, collision matrix=PASS, tag manager=PASS, input map=PASS, animator single-truth=PASS)"
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
    # escape 음성 경로가 실제로 회귀하면 산출물이 Terrain 루트에 떨어진다. 그때도
    # Assets에 잔여를 남기지 않도록 두 루트를 모두 정리한다.
    $foliageResidue = @(
        $foliageAsset
        ($foliageAsset + ".meta")
        (Join-Path $terrainRoot ($foliageName + ".foliage"))
        (Join-Path $terrainRoot ($foliageName + ".foliage.meta"))
        (Join-Path $foliageRoot $foliageName)
    )
    foreach ($target in $foliageResidue) {
        $absoluteTarget = [IO.Path]::GetFullPath($target)
        $insideFoliage = $absoluteTarget.StartsWith($foliageRoot, [StringComparison]::OrdinalIgnoreCase)
        $insideTerrain = $absoluteTarget.StartsWith($terrainRoot, [StringComparison]::OrdinalIgnoreCase)
        if ((-not ($insideFoliage -or $insideTerrain)) -or
            -not (Split-Path $absoluteTarget -Leaf).StartsWith(
                $foliageName, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified Foliage probe asset: $absoluteTarget"
        }
        Remove-Item -LiteralPath $absoluteTarget -Force -ErrorAction SilentlyContinue
    }
    $blackBoardResidue = @(
        $blackBoardAsset
        ($blackBoardAsset + ".meta")
        (Join-Path $behaviorTreeRoot ($blackBoardName + "_empty.blackboard"))
        (Join-Path $behaviorTreeRoot ($blackBoardName + "_empty.blackboard.meta"))
    )
    foreach ($target in $blackBoardResidue) {
        $absoluteTarget = [IO.Path]::GetFullPath($target)
        if (-not $absoluteTarget.StartsWith($behaviorTreeRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $absoluteTarget -Leaf).StartsWith(
                $blackBoardName, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified BlackBoard probe asset: $absoluteTarget"
        }
        Remove-Item -LiteralPath $absoluteTarget -Force -ErrorAction SilentlyContinue
    }
	if ($null -ne $inputMapAsset) {
        $absoluteInputMap = [IO.Path]::GetFullPath($inputMapAsset)
        if (-not $absoluteInputMap.StartsWith($inputMapRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $absoluteInputMap -Leaf).StartsWith(
                "CE_InputProbe_", [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove an unverified input map probe: $absoluteInputMap"
        }
        Remove-Item -LiteralPath $absoluteInputMap -Force -ErrorAction SilentlyContinue
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

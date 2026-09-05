# X1 Transform authored-write publication gate.
#
# 1) Every known writer retains its publication marker.
# 2) Transform TRS storage stays private (the Release build is the authoritative
#    compiler proof; this scan catches the old direct access spellings early).
# 3) Removing one publication marker from an in-memory specimen makes the
#    inventory checker fail. No repository file is modified by this mutation proof.
# 4) The runtime probe exercises all reason counters and verifies EntityHandle
#    resolution remains stable before/after publication.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 120
)

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

$inventory = @(
    [pscustomobject]@{ Writer = "C++ setters"; Path = "Engine\SceneRuntime\Transform.cpp"; Pattern = "PublishLocalWrite\(reason\);"; Count = 10 },
    [pscustomobject]@{ Writer = "Script"; Path = "Engine\SceneRuntime\ClrHost.cpp"; Pattern = "TransformWriteReason::Script"; Count = 8 },
    [pscustomobject]@{ Writer = "Inspector"; Path = "Editor\EngineGUIWindow\InspectorWindow.cpp"; Pattern = "TransformWriteReason::Inspector"; Count = 12 },
    [pscustomobject]@{ Writer = "Reflection hook"; Path = "Engine\Utility_Framework\ReflectionTypedYml.h"; Pattern = "obj\.OnPropertyChanged\(memberName, Meta::CurrentPropertyChangeSource\(\)\);"; Count = 1 },
    [pscustomobject]@{ Writer = "Prefab scope"; Path = "Engine\Utility_Framework\ReflectionYml.h"; Pattern = "ScopedPropertyChangeSource sourceScope\(PropertyChangeSource::Prefab\);"; Count = 1 },
    # X7 folds rigidbody+CCT readback into one batch and keeps pending CCT teleport
    # as the second batch, so Physics publication has two call sites.
    [pscustomobject]@{ Writer = "Physics"; Path = "Engine\SceneRuntime\PhysicsManager.cpp"; Pattern = "TransformWriteReason::Physics"; Count = 2 },
    [pscustomobject]@{ Writer = "CharacterController"; Path = "Engine\SceneRuntime\CharacterControllerComponent.cpp"; Pattern = "m_transform->SetRotation\(resultRot\);"; Count = 1 },
    [pscustomobject]@{ Writer = "Socket"; Path = "Engine\SceneRuntime\Socket.cpp"; Pattern = "TransformWriteReason::Socket"; Count = 2 },
    [pscustomobject]@{ Writer = "Gizmo"; Path = "Editor\EngineGUIWindow\SceneViewWindow.cpp"; Pattern = "TransformWriteReason::Gizmo"; Count = 7 },
    [pscustomobject]@{ Writer = "Animator socket cache"; Path = "Engine\SceneRuntime\AnimationJob.cpp"; Pattern = "TransformWriteReason::Animator"; Count = 1 },
    # MBC3-11(7732d380)이 ModelSceneBridge.cpp(595줄)를 지우고
    # ModelSceneInstantiation.cpp(254줄)로 통합했다. 이 게이트는 사라진 파일을
    # 계속 요구해 그날부터 빨갰다 — 인벤토리가 이주를 따라가지 못한 것이지
    # 발행이 깨진 것이 아니다(나머지 변이 5종은 그동안에도 정상 RED였다).
    # 9 → 3은 통합의 결과다. ConsoleCommandSystem.cpp에도 ModelImport 호출이
    # 하나 있지만 그것은 모든 reason을 한 번씩 태우는 CLI 자가 검증 프로브라
    # 제품 writer가 아니다 — 인벤토리에 넣지 않는다.
    [pscustomobject]@{ Writer = "Model import"; Path = "Engine\SceneRuntime\ModelSceneInstantiation.cpp"; Pattern = "TransformWriteReason::ModelImport"; Count = 3 },
    [pscustomobject]@{ Writer = "Hierarchy"; Path = "Engine\SceneRuntime\Transform.cpp"; Pattern = "PublishLocalWrite\(TransformWriteReason::Hierarchy\);"; Count = 1 },
    [pscustomobject]@{ Writer = "Reset"; Path = "Engine\SceneRuntime\Transform.cpp"; Pattern = "void Transform::TransformReset\(TransformWriteReason reason\)"; Count = 1 }
)

function Test-InventoryEntry {
    param(
        [pscustomobject]$Entry,
        [string]$Content
    )
    return ([regex]::Matches($Content, $Entry.Pattern)).Count -eq $Entry.Count
}

$failed = $false
foreach ($entry in $inventory) {
    $path = Join-Path $repo $entry.Path
    if (-not (Test-Path $path)) {
        "실패: writer 파일 없음 - $($entry.Writer): $($entry.Path)"
        $failed = $true
        continue
    }

    $content = [System.IO.File]::ReadAllText($path)
    if (-not (Test-InventoryEntry $entry $content)) {
        $actual = ([regex]::Matches($content, $entry.Pattern)).Count
        "실패: writer publication marker 불일치 - $($entry.Writer): actual=$actual expected=$($entry.Count)"
        $failed = $true
        continue
    }

    # Mutation proof: remove exactly one marker in memory. The same checker must
    # reject the specimen, otherwise this gate cannot detect a missing publish.
    $mutated = [regex]::Replace($content, $entry.Pattern, "", 1)
    if (Test-InventoryEntry $entry $mutated) {
        "실패: mutation이 RED가 되지 않음 - $($entry.Writer)"
        $failed = $true
    }
    else {
        "mutation RED: $($entry.Writer)"
    }
}

$sourceFiles = Get-ChildItem -Path (Join-Path $repo "Engine"),(Join-Path $repo "Editor") `
    -Recurse -File -Include *.h,*.hpp,*.cpp
$directPatterns = @(
    'Transform_\(\)\.(position|rotation|scale)\b',
    '(?<![A-Za-z0-9_])(transform|m_transform|pTransform|transformPtr)->(position|rotation|scale)\b'
)
$directWrites = @($sourceFiles | Select-String -Pattern $directPatterns | Where-Object {
    $_.Path -notlike '*\Transform.cpp' -and
    $_.Path -notlike '*\Transform.h' -and
    $_.Line -notmatch '^\s*//'
})
if ($directWrites.Count -ne 0) {
    "실패: private TRS 우회 $($directWrites.Count)건"
    $directWrites | ForEach-Object { "  $($_.Path):$($_.LineNumber):$($_.Line.Trim())" }
    $failed = $true
}

if ($failed) { exit 1 }

if (-not (Test-Path $Exe)) {
    "실패: 실행 파일이 없다: $Exe"
    exit 1
}

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "transform_write_publication_$runId.txt"
$outFile = Join-Path $Work "transform_write_publication_$runId.out"
$errFile = Join-Path $Work "transform_write_publication_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.transformwritestats probe",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "실패: runtime probe 시간 초과 ($TimeoutSeconds 초)"
    exit 1
}

$output = [System.IO.File]::ReadAllText($outFile)
if ($output -notmatch '\[scene\.transformwritestats\] probe=PASS') {
    "실패: runtime publication probe가 PASS하지 않았다"
    $output | Select-String -Pattern 'scene\.transformwritestats' | ForEach-Object { $_.Line }
    if (Test-Path $errFile) { Get-Content $errFile }
    exit 1
}

$reasonLines = [regex]::Matches(
    $output, '\[scene\.transformwritestats\] reason=([^ ]+) count=([0-9]+)')
if ($reasonLines.Count -ne 12) {
    "실패: reason 출력 수가 12가 아니다: $($reasonLines.Count)"
    exit 1
}

foreach ($match in $reasonLines) {
    if ([uint64]$match.Groups[2].Value -eq 0) {
        "실패: publication 0건 - $($match.Groups[1].Value)"
        exit 1
    }
}

"PASS: X1 writer inventory=$($inventory.Count), mutation RED=$($inventory.Count), runtime reasons=$($reasonLines.Count), resolver=stable"
exit 0

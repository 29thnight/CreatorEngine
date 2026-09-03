# PHASE 3.75 MBC11 — §8.4 성능 예산 게이트 (Release 전용).
#
# MBC0 기준선(docs/plans/ModelAssetBigBangCutoverBaseline.md §4)과 새 단일 경로를 **별도
# 실행**으로 비교한다. legacy 런타임과 A/B 스위치는 제품에 없다 — 기준값은 이 스크립트에
# 상수로 박혀 있고(기준선 문서의 표), 새 값은 지금 exe로 잰다.
#
#   B1 cold source import  — 임시 프로젝트 사본에서 in-process authoring transaction
#                            (assets.modelbench … author) min ms ≤ MBC0 legacy Assimp 소스 추정
#   B2 cooked load         — 같은 사본에서 방금 게시한 generation을 런타임 리더로 재로드
#                            (author 줄의 cookedMinMs) ≤ MBC0 legacy `.asset` 읽기(min) × 1.25.
#                            작업 트리 Library를 은퇴/재로드하는 cooked 모드는 참고 수치다 —
#                            포맷 버전이 바뀐 직후엔 Library 재임포트 전까지 실패할 수 있다.
#   B3 씬 로드             — FT_Primitives·Test1 SceneLoadTotal/iter ≤ 기준 × 1.10
#   B4 부팅 catalog        — ≤ 기준 × 1.10
#   B5 peak working set    — cooked bench 프로세스 ≤ 1,351 MB
#   B6 frame/GPU/VRAM      — MBC0에 archive가 없다. dx12.scene(FT_Primitives+Gunner)의
#                            메시 업로드 KB·커버리지와 VRAM을 `mbc11_perf_archive.json`에
#                            기록(-Archive)하고, archive가 있으면 그 대비 ×1.10 안을 요구한다.
#
# ★ Debug로 재면 오답이다(verify-serialization-baseline 주석). 기본 exe가 Release다.
# ★ 붉으면 cutover를 연기한다 — legacy fallback을 되살려 맞추지 않는다(§8.4).
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\Bin\x64-Release\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$Iterations = 5,
    [int]$TimeoutSeconds = 900,
    [switch]$Archive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $Work ('creator-mbc11-budget-' + [guid]::NewGuid().ToString('N'))
$archivePath = Join-Path $PSScriptRoot 'mbc11_perf_archive.json'
$failures = [System.Collections.Generic.List[string]]::new()
$report = [System.Collections.Generic.List[string]]::new()
function Add-Failure([string]$Message) { $failures.Add($Message) }

# ── MBC0 기준선 (Release, 2026-09-02, ms) ──
$legacyAssetReadMin = @{
    'Ani_Mon_3_die' = 89.7; 'Cha_Mon_5' = 20.7; 'Gunner_F_Mythic' = 9.5; 'Prim_Cone' = 1.2
    'Prim_Cube' = 1.1; 'Prim_Cylinder' = 1.2; 'Prim_IcoSphere' = 1.0; 'Prim_MatGrid' = 4.8
    'Prim_Plane' = 0.7; 'Prim_Sphere' = 1.2; 'Prim_Suzanne' = 0.9; 'Prim_Torus' = 0.9
    'scene' = 54.3; 'SU_Mythic' = 5.8
}
$legacyAssimpEstimate = @{
    'Ani_Mon_3_die' = 190; 'Cha_Mon_5' = 34; 'Gunner_F_Mythic' = 158; 'Prim_Cone' = 25
    'Prim_Cube' = 27; 'Prim_Cylinder' = 27; 'Prim_IcoSphere' = 19; 'Prim_MatGrid' = 39
    'Prim_Plane' = 21; 'Prim_Sphere' = 24; 'Prim_Suzanne' = 23; 'Prim_Torus' = 20
    'scene' = 859; 'SU_Mythic' = 76
}
$budgetSceneTest1Ms = 31.54 * 1.10
$budgetSceneFtMs = 48.18 * 1.10
$budgetBootMs = 43.25 * 1.10
$budgetPeakWorkingSetMB = 1351
$cookedFactor = 1.25

function Invoke-Editor([string]$Label, [string[]]$Commands) {
    $scenario = Join-Path $run ($Label + '.txt')
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe
    $start.Arguments = '--script "' + $scenario.Replace('"', '\"') + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "CreatorEditor 시작 실패: $Exe" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); throw "CreatorEditor timeout: $Label" }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stdout.txt')), $stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stderr.txt')), $stderr, [Text.UTF8Encoding]::new($false))
    if ($process.ExitCode -ne 0) { Add-Failure "$Label 종료 코드 $($process.ExitCode)" }
    return $stdout
}

function Read-BenchRows([string]$Text, [string]$Key) {
    $rows = @{}
    # author 줄은 authorMinMs… 뒤에 cookedMinMs…가 오므로 키 앞의 토큰을 건너뛴다.
    foreach ($m in [regex]::Matches($Text, '\[CLI\] assets\.modelbench model=(\S+) [^\r\n]*?' + $Key + 'MinMs=([\d.]+) ' + $Key + 'AvgMs=([\d.]+)')) {
        $rows[$m.Groups[1].Value] = [double]$m.Groups[2].Value
    }
    return $rows
}

try {
    if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "CreatorEditor 실행 파일이 없다: $Exe" }
    if ($Exe -notmatch 'x64-Release') { Add-Failure "Release exe가 아니다(성능 예산은 Release로만 잰다): $Exe" }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($run -match '\s') { throw "console 인자로 넘길 작업 경로에 공백이 있다: $run" }
    $modelsDir = Join-Path $root 'Dynamic_CPP\Assets\Models'
    $animationDir = Join-Path $root 'Dynamic_CPP\Assets\Animation'
    $gunner = Join-Path $modelsDir 'Gunner_F_Mythic.glb'
    $baseScene = Join-Path $root 'Dynamic_CPP\Assets\Scenes\FT_Primitives.creator'

    # ── B3/B4 ──
    $serialization = & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify-serialization-baseline.ps1') `
        -Exe $Exe -Work $run -Baseline 2>&1 | Out-String
    [IO.File]::WriteAllText((Join-Path $run 'serialization.txt'), $serialization, [Text.UTF8Encoding]::new($false))
    $sceneBlocks = [regex]::Matches($serialization,
        'mode=scene target=(\S+?)\.creator[^\r\n]*\r?\n\[serialize\.bench\] mode=scene stage=SceneLoadTotal totalUs=[\d.]+ perIterUs=([\d.]+)')
    $test1Ms = -1.0; $ftMs = -1.0
    foreach ($block in $sceneBlocks) {
        $target = $block.Groups[1].Value
        $ms = [double]$block.Groups[2].Value / 1000.0
        if ($target -like '*Test1') { $test1Ms = $ms }
        if ($target -like '*FT_Primitives') { $ftMs = $ms }
    }
    $boot = [regex]::Match($serialization, 'mode=boot stage=AssetCatalog totalMs=([\d.]+)')
    $bootMs = if ($boot.Success) { [double]$boot.Groups[1].Value } else { -1.0 }
    $report.Add(('B3 씬 로드   Test1 {0:N2} ms (≤ {1:N2}) · FT_Primitives {2:N2} ms (≤ {3:N2})' -f $test1Ms, $budgetSceneTest1Ms, $ftMs, $budgetSceneFtMs))
    $report.Add(('B4 부팅 catalog {0:N2} ms (≤ {1:N2})' -f $bootMs, $budgetBootMs))
    if ($test1Ms -lt 0 -or $test1Ms -gt $budgetSceneTest1Ms) { Add-Failure "B3 Test1 씬 로드 $test1Ms ms > $budgetSceneTest1Ms" }
    if ($ftMs -lt 0 -or $ftMs -gt $budgetSceneFtMs) { Add-Failure "B3 FT_Primitives 씬 로드 $ftMs ms > $budgetSceneFtMs" }
    if ($bootMs -lt 0 -or $bootMs -gt $budgetBootMs) { Add-Failure "B4 부팅 catalog $bootMs ms > $budgetBootMs" }

    # ── B2/B5 (cooked generation 로드, peak working set) ──
    $cooked = Invoke-Editor 'cooked' @(
        "assets.modelbench $($modelsDir.Replace('\', '/')) $Iterations cooked",
        "assets.modelbench $($animationDir.Replace('\', '/')) $Iterations cooked",
        'quit')
    $workspaceRows = Read-BenchRows $cooked 'cooked'
    $workspaceMeasured = [regex]::Match($cooked, 'assets\.modelbench done mode=cooked measured=(\d+) failed=(\d+)')
    if ($workspaceMeasured.Success) {
        $report.Add(('B2w 작업 트리 Library 재로드(참고) measured={0} failed={1}' -f $workspaceMeasured.Groups[1].Value, $workspaceMeasured.Groups[2].Value))
    }

    # ── B1 (cold source import — 임시 프로젝트 사본에서 in-process authoring) ──
    $tempProject = Join-Path $run 'project'
    foreach ($sub in @('Assets\Models', 'Assets\Animation', 'ProjectSetting', 'Library\ModelAssetGenerations')) {
        New-Item -ItemType Directory -Path (Join-Path $tempProject $sub) -Force | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $root 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset') `
        -Destination (Join-Path $tempProject 'ProjectSetting\AssetIdentity.asset')
    foreach ($dir in @(@{ Src = $modelsDir; Dst = 'Assets\Models' }, @{ Src = $animationDir; Dst = 'Assets\Animation' })) {
        foreach ($file in Get-ChildItem -LiteralPath $dir.Src -File | Where-Object { $_.Extension -in '.glb', '.gltf', '.fbx', '.meta' }) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $tempProject ($dir.Dst + '\' + $file.Name))
        }
    }
    # authoring transaction은 shader sidecar(GBuffer/Forward.shadermeta.meta)와 외부 텍스처 sidecar를
    # asset root에서 읽는다 — 사본에도 그대로 둔다(Shaders 0.5MB·Materials 25MB).
    foreach ($dir in @('Assets\Shaders', 'Assets\Materials')) {
        Copy-Item -LiteralPath (Join-Path $root ('Dynamic_CPP\' + $dir)) -Destination (Join-Path $tempProject $dir) -Recurse -Force
    }
    $tempModels = (Join-Path $tempProject 'Assets\Models').Replace('\', '/')
    $tempAnimation = (Join-Path $tempProject 'Assets\Animation').Replace('\', '/')
    $author = Invoke-Editor 'author' @(
        "assets.modelbench $tempModels $Iterations author",
        "assets.modelbench $tempAnimation $Iterations author",
        'quit')
    $authorRows = Read-BenchRows $author 'author'
    foreach ($name in ($legacyAssimpEstimate.Keys | Sort-Object)) {
        $budget = [double]$legacyAssimpEstimate[$name]
        if (-not $authorRows.ContainsKey($name)) { Add-Failure "B1 $name author 표본이 없다"; continue }
        $value = $authorRows[$name]
        $mark = if ($value -le $budget) { 'ok' } else { 'OVER' }
        $report.Add(('B1 import {0,-16} {1,8:N3} ms (≤ {2,8:N1}) {3}' -f $name, $value, $budget, $mark))
        if ($value -gt $budget) { Add-Failure "B1 $name cold import $value ms > $budget" }
    }
    # B2 — 같은 사본의 generation을 런타임 리더로 재로드한 min ms(author 줄의 cookedMinMs).
    $cookedRows = Read-BenchRows $author 'cooked'
    foreach ($name in ($legacyAssetReadMin.Keys | Sort-Object)) {
        $budget = [double]$legacyAssetReadMin[$name] * $cookedFactor
        if (-not $cookedRows.ContainsKey($name)) { Add-Failure "B2 $name cooked 표본이 없다"; continue }
        $value = $cookedRows[$name]
        $mark = if ($value -le $budget) { 'ok' } else { 'OVER' }
        $report.Add(('B2 cooked {0,-16} {1,8:N3} ms (≤ {2,8:N3}) {3}' -f $name, $value, $budget, $mark))
        if ($value -gt $budget) { Add-Failure "B2 $name cooked load $value ms > $budget" }
    }
    # 단계 분해(참고) — 어느 단계가 B1/B2 비용인지 판정 근거로 남긴다.
    foreach ($m in [regex]::Matches($author, '\[CLI\] assets\.modelbench model=(\S+) [^\r\n]*?authorPhases=(\S+) cookedPhases=(\S+)')) {
        $report.Add(('   phases {0,-16} author {1}' -f $m.Groups[1].Value, $m.Groups[2].Value))
        $report.Add(('   phases {0,-16} cooked {1}' -f $m.Groups[1].Value, $m.Groups[3].Value))
    }
    # B5 — 사본 저작+로드 프로세스의 peak working set(MBC0 bench도 소스 디코드+cook을 포함했다).
    $peak = [regex]::Matches($author, 'assets\.modelbench done .* peakWorkingSetMB=([\d.]+)')
    $peakMb = if ($peak.Count -gt 0) { [double]$peak[$peak.Count - 1].Groups[1].Value } else { -1.0 }
    $report.Add(('B5 peak working set {0:N1} MB (≤ {1})' -f $peakMb, $budgetPeakWorkingSetMB))
    if ($peakMb -lt 0 -or $peakMb -gt $budgetPeakWorkingSetMB) { Add-Failure "B5 peak working set $peakMb MB > $budgetPeakWorkingSetMB" }

    # ── B6 archive/비회귀 ──
    $frame = Invoke-Editor 'frame' @(
        "scene.switch $($baseScene.Replace('\', '/'))",
        'wait 60',
        "model.loadcached $($gunner.Replace('\', '/'))",
        'wait 60',
        'model.place Gunner_F_Mythic',
        'wait 60',
        'object.transform Gunner_F_Mythic 1.5 0 0 0 0 0 0.3 0.3 0.3',
        'object.property Gunner_F_Mythic Animator m_isEnabled false',
        'wait 30',
        'dx12.scene',
        'assets.modelbench - 1 cooked',
        'quit')
    $upload = [regex]::Match($frame, '메시 업로드\s+(\d+)\(generation\s+(\d+),\s+(\d+)KB\)')
    $coverage = [regex]::Match($frame, '커버리지\s+(\d+)/65536')
    $frameVram = [regex]::Matches($frame, 'assets\.modelbench done .* vramUsedMB=(\d+)')
    $current = [ordered]@{
        meshUploads = if ($upload.Success) { [int]$upload.Groups[1].Value } else { -1 }
        meshUploadKB = if ($upload.Success) { [int]$upload.Groups[3].Value } else { -1 }
        coverage = if ($coverage.Success) { [int]$coverage.Groups[1].Value } else { -1 }
        vramUsedMB = if ($frameVram.Count -gt 0) { [int]$frameVram[$frameVram.Count - 1].Groups[1].Value } else { -1 }
        sceneTest1Ms = $test1Ms; sceneFtMs = $ftMs; bootMs = $bootMs; peakWorkingSetMB = $peakMb
        measuredAt = (Get-Date).ToString('yyyy-MM-dd HH:mm')
    }
    if ($frame -notmatch '\[CLI\] dx12\.scene 통과') { Add-Failure 'B6 dx12.scene이 통과하지 않았다.' }
    if ($current.meshUploads -lt 1 -or $current.meshUploads -ne [int]$upload.Groups[2].Value) { Add-Failure 'B6 dx12.scene 메시 업로드가 전량 generation이 아니거나 없다.' }
    $report.Add(('B6 frame  메시 업로드 {0}개/{1} KB · 커버리지 {2} · VRAM {3} MB' -f $current.meshUploads, $current.meshUploadKB, $current.coverage, $current.vramUsedMB))
    $b6Failed = @($failures | Where-Object { $_ -like 'B6*' }).Count -gt 0
    if ($Archive -and $b6Failed) {
        # 0 업로드·미통과 실행을 기준선으로 굳히면 이후 비회귀 판정이 눈먼다.
        $report.Add('B6 archive 보류 — B6가 붉은 실행은 기준선으로 뜨지 않는다')
    }
    elseif ($Archive) {
        [IO.File]::WriteAllText($archivePath, ($current | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
        $report.Add("B6 archive 기록: $archivePath")
    }
    elseif (Test-Path -LiteralPath $archivePath) {
        $archived = Get-Content -LiteralPath $archivePath -Raw | ConvertFrom-Json
        if ($archived.meshUploadKB -gt 0 -and $current.meshUploadKB -gt $archived.meshUploadKB * 1.10) {
            Add-Failure "B6 메시 업로드 KB 회귀: $($current.meshUploadKB) > archive $($archived.meshUploadKB) × 1.10"
        }
        if ($archived.vramUsedMB -gt 0 -and $current.vramUsedMB -gt 0 -and $current.vramUsedMB -gt $archived.vramUsedMB * 1.10) {
            Add-Failure "B6 VRAM 회귀: $($current.vramUsedMB) > archive $($archived.vramUsedMB) × 1.10"
        }
        if ($archived.coverage -gt 0 -and $current.coverage -ne $archived.coverage) {
            Add-Failure "B6 커버리지가 archive와 다르다: $($current.coverage) vs $($archived.coverage) — 그림이 바뀌었다"
        }
        $report.Add("B6 archive 대조: $archivePath")
    }
    else {
        $report.Add('B6 archive 없음 — -Archive로 먼저 뜬다(비회귀 판정 보류, 기록만)')
    }
}
catch {
    Add-Failure "예외: $($_.Exception.Message)"
}

"model-cutover-budget failures=$($failures.Count) run=$run"
$report | ForEach-Object { "  $_" }
if ($failures.Count -gt 0) {
    '실패:'
    $failures | ForEach-Object { "  $_" }
    exit 1
}
'통과 — §8.4 예산 B1~B5를 만족하고 B6 계측을 남겼다(legacy fallback 없이)'
exit 0

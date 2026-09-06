# PHASE 3.75 MBC11 — §8.4 성능 예산 게이트 (Release 전용).
#
# MBC0 기준선(docs/plans/archive/ModelAssetBigBangCutoverBaseline.md §4)과 새 단일 경로를 **별도
# 실행**으로 비교한다. legacy 런타임과 A/B 스위치는 제품에 없다 — 기준값은 이 스크립트에
# 상수로 박혀 있고(기준선 문서의 표), 새 값은 지금 exe로 잰다.
#
#   B1 cold source import  — 임시 프로젝트 사본에서 in-process authoring transaction
#                            (assets.modelbench … author). **B1a 디코드**만 legacy 추정과
#                            비교하고, 그 위에 얹힌 신원·staging·검증·원자 게시(**B1b**)는
#                            legacy에 대응물이 없어 archive 대비 비회귀로 본다.
#   B2 cooked load         — 같은 사본에서 방금 게시한 generation을 런타임 리더로 재로드.
#                            **B2a**(CEMC 읽기·디코드·조립)만 legacy `.asset` 읽기 × 1.25와
#                            비교하고, **B2b**(신원·epoch·SHA 검증)는 비회귀, **B2c**(임베디드
#                            텍스처 디코드)는 PHASE 12 T1a/T2로 이관해 기록·비회귀만 한다.
#                            작업 트리 Library를 은퇴/재로드하는 cooked 모드는 참고 수치다 —
#                            포맷 버전이 바뀐 직후엔 Library 재임포트 전까지 실패할 수 있다.
#                            ★ 축을 왜 갈랐는지는 아래 상수 블록과 기준선 §4.4a에 있다.
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
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

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

# ── B1/B2 축 재유도 (2026-09-04, 계획서 §8.4 개정) ──
#
# 총합끼리 비교하는 것은 판별력이 없었다 — 기준값이 잰 일과 새 경로가 재는 일이
# 다르다. legacy 소스 임포트 추정치는 **디코드**였고(프로토타입 experiment.bench의
# min 역산), legacy `.asset` 읽기는 **텍스처도 신원 검증도 해시 검증도 하지 않았다**.
# 그래서 같은 일끼리만 비교 예산으로 판정하고, legacy에 대응물이 없는 칸은 archive
# 대비 비회귀로만 본다. 예산 정의를 조용히 넓히지 않기 위해 어느 칸이 어느 판정을
# 받는지 여기 적어 둔다(기준선 §4.4a).
#
#   B1a 비교   author `source-read+decode`                    ≤ legacy Assimp 소스 추정
#   B1b 비회귀 author 총합 − 디코드 (신원·staging·검증·원자 게시)  ≤ archive
#   B2a 비교   cooked `cemc-read`+`cemc-decode+validate`
#              +`materials+meshes+skeleton`+`assemble`         ≤ legacy `.asset` min × 1.25
#   B2b 비회귀 cooked `identity+sidecar`+`cemc-sha`             ≤ archive
#   B2c 이관   cooked `textures-read+sha+decode`               → PHASE 12 T1a/T2
#
# B2c는 cook artifact가 디코드 완료 형식이 되면 사라지는 칸이다(TexturePipelinePlan
# §5 T1a·§8 "런타임 텍스처 로드 < 1 ms/장"). 여기서는 기록하고 회귀만 막는다.
$phaseDecodeKey = 'source-read+decode'
$phaseB2aKeys = @('cemc-read', 'cemc-decode+validate', 'materials+meshes+skeleton', 'assemble')
$phaseB2bKeys = @('identity+sidecar', 'cemc-sha')
$phaseB2cKey = 'textures-read+sha+decode'
# 시간 축 archive는 KB·VRAM보다 흔들린다(같은 exe·같은 자산에서 stage-write가 8~11 ms).
# 그래서 여유를 ×1.25로 두고, 작은 값이 흔들림으로 붉어지지 않게 절대 하한을 함께 둔다.
$archiveTimeFactor = 1.25
$archiveTimeFloorMs = 2.0

function Invoke-Editor([string]$Label, [string[]]$Commands) {
    $scenario = Join-Path $run ($Label + '.txt')
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe
    $start.Arguments = '--commandlet-script "' + $scenario.Replace('"', '\"') + '"'
    $resultPath = Join-Path $run ($Label + '.results.jsonl')
    $start.Arguments += ' --result-file "' + $resultPath + '"'
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
    $results = @(Read-CommandResults $resultPath)
    if (@($results | Where-Object status -ne succeeded).Count -ne 0) { Add-Failure "$Label has failed command results" }
    return ,$results
}

function Read-BenchRows($Results, [string]$Key) {
    $rows = @{}
    foreach ($result in @($Results | Where-Object command -eq 'assets.modelbench')) {
        foreach ($model in $result.data.models) { $rows[$model.model] = [double]$model.($Key + 'MinMs') }
    }
    return $rows
}

function Sum-Phases([hashtable]$Phases, [string[]]$Keys) {
    $total = 0.0
    foreach ($key in $Keys) { if ($Phases.ContainsKey($key)) { $total += $Phases[$key] } }
    return $total
}

# archive JSON에서 모델별 시간을 꺼낸다. StrictMode에서 없는 속성 접근은 예외이므로
# 항목이 아직 없는 옛 archive(축 재유도 전에 뜬 것)에도 견디게 PSObject로 본다.
function Get-ArchivedTime($Archived, [string]$Group, [string]$Name) {
    if ($null -eq $Archived) { return $null }
    $groupProperty = $Archived.PSObject.Properties[$Group]
    if ($null -eq $groupProperty -or $null -eq $groupProperty.Value) { return $null }
    $entry = $groupProperty.Value.PSObject.Properties[$Name]
    if ($null -eq $entry) { return $null }
    return $entry.Value
}

# archive 대비 비회귀. 첫 기록이면 판정을 보류한다(0 값·빈 archive를 정답으로 굳히지 않는다).
function Test-ArchiveTime([string]$Label, [double]$Current, $Archived, [System.Collections.Generic.List[string]]$Report) {
    if ($null -eq $Archived) {
        $Report.Add(('   {0,-28} {1,8:N3} ms (archive 없음 — 기록만)' -f $Label, $Current))
        return $true
    }
    $limit = [Math]::Max([double]$Archived * $archiveTimeFactor, [double]$Archived + $archiveTimeFloorMs)
    $ok = $Current -le $limit
    $Report.Add(('   {0,-28} {1,8:N3} ms (archive {2,8:N3} → ≤ {3,8:N3}) {4}' -f $Label, $Current, [double]$Archived, $limit, $(if ($ok) { 'ok' } else { 'OVER' })))
    return $ok
}

try {
    if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "CreatorEditor 실행 파일이 없다: $Exe" }
    if ($Exe -notmatch 'x64-Release') { Add-Failure "Release exe가 아니다(성능 예산은 Release로만 잰다): $Exe" }
    # archive는 B1b·B2b·B2c 비회귀 판정에 쓰므로 B1/B2보다 먼저 읽는다.
    $archived = if (Test-Path -LiteralPath $archivePath) {
        Get-Content -LiteralPath $archivePath -Raw | ConvertFrom-Json
    } else { $null }
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
    foreach ($sample in @($cooked | Where-Object command -eq 'assets.modelbench')) {
        $report.Add("B2w measured=$($sample.data.measured) failed=$($sample.data.failed)")
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
    $cookedRows = Read-BenchRows $author 'cooked'
    $authorPhaseRows = @{}
    $cookedPhaseRows = @{}
    foreach ($sample in @($author | Where-Object command -eq 'assets.modelbench')) {
        foreach ($model in $sample.data.models) {
            $authorPhaseRows[$model.model] = @{}
            $cookedPhaseRows[$model.model] = @{}
            foreach ($phase in $model.authorPhases) { $authorPhaseRows[$model.model][$phase.phase] = [double]$phase.milliseconds }
            foreach ($phase in $model.cookedPhases) { $cookedPhaseRows[$model.model][$phase.phase] = [double]$phase.milliseconds }
        }
    }

    # ── B1a 비교 예산 / B1b 비회귀 기준선 ──
    $overheadNow = [ordered]@{}
    foreach ($name in ($legacyAssimpEstimate.Keys | Sort-Object)) {
        $budget = [double]$legacyAssimpEstimate[$name]
        if (-not $authorRows.ContainsKey($name) -or -not $authorPhaseRows.ContainsKey($name)) {
            Add-Failure "B1 $name author 표본이 없다"; continue
        }
        $total = $authorRows[$name]
        $phases = $authorPhaseRows[$name]
        if (-not $phases.ContainsKey($phaseDecodeKey)) {
            Add-Failure "B1a $name `'$phaseDecodeKey`' 단계가 없다 — 단계 이름이 바뀌었나"; continue
        }
        $decode = $phases[$phaseDecodeKey]
        $overhead = $total - $decode
        $overheadNow[$name] = [Math]::Round($overhead, 3)
        $mark = if ($decode -le $budget) { 'ok' } else { 'OVER' }
        $report.Add(('B1a decode {0,-16} {1,8:N3} ms (≤ {2,8:N1}) {3} · B1b 트랜잭션 {4,8:N3} ms (총합 {5,8:N3})' -f `
            $name, $decode, $budget, $mark, $overhead, $total))
        if ($decode -gt $budget) { Add-Failure "B1a $name source decode $decode ms > $budget" }
        $archivedOverhead = Get-ArchivedTime $archived 'authoringOverheadMs' $name
        if (-not (Test-ArchiveTime ("B1b $name") $overhead $archivedOverhead $report)) {
            Add-Failure "B1b $name 저작 트랜잭션 회귀: $([Math]::Round($overhead,3)) ms > archive $archivedOverhead × $archiveTimeFactor"
        }
    }

    # ── B2a 비교 예산 / B2b 비회귀 / B2c PHASE 12 이관 ──
    $identityNow = [ordered]@{}
    $textureNow = [ordered]@{}
    foreach ($name in ($legacyAssetReadMin.Keys | Sort-Object)) {
        $budget = [double]$legacyAssetReadMin[$name] * $cookedFactor
        if (-not $cookedRows.ContainsKey($name) -or -not $cookedPhaseRows.ContainsKey($name)) {
            Add-Failure "B2 $name cooked 표본이 없다"; continue
        }
        $total = $cookedRows[$name]
        $phases = $cookedPhaseRows[$name]
        if (-not $phases.ContainsKey('cemc-read') -or -not $phases.ContainsKey('cemc-sha')) {
            Add-Failure "B2a $name cemc 단계 분해가 없다 — 단계 이름이 바뀌었나"; continue
        }
        $comparable = Sum-Phases $phases $phaseB2aKeys
        $identity = Sum-Phases $phases $phaseB2bKeys
        $texture = if ($phases.ContainsKey($phaseB2cKey)) { $phases[$phaseB2cKey] } else { 0.0 }
        $identityNow[$name] = [Math]::Round($identity, 3)
        $textureNow[$name] = [Math]::Round($texture, 3)
        $mark = if ($comparable -le $budget) { 'ok' } else { 'OVER' }
        $report.Add(('B2a load   {0,-16} {1,8:N3} ms (≤ {2,8:N3}) {3} · B2b 검증 {4,7:N3} · B2c 텍스처 {5,8:N3} (총합 {6,8:N3})' -f `
            $name, $comparable, $budget, $mark, $identity, $texture, $total))
        if ($comparable -gt $budget) { Add-Failure "B2a $name cooked load $comparable ms > $budget" }
        $archivedIdentity = Get-ArchivedTime $archived 'identityVerifyMs' $name
        if (-not (Test-ArchiveTime ("B2b $name") $identity $archivedIdentity $report)) {
            Add-Failure "B2b $name 신원·해시 검증 회귀: $([Math]::Round($identity,3)) ms > archive $archivedIdentity × $archiveTimeFactor"
        }
        $archivedTexture = Get-ArchivedTime $archived 'textureDecodeMs' $name
        if (-not (Test-ArchiveTime ("B2c $name (PHASE 12)") $texture $archivedTexture $report)) {
            Add-Failure "B2c $name 임베디드 텍스처 디코드 회귀: $([Math]::Round($texture,3)) ms > archive $archivedTexture × $archiveTimeFactor (PHASE 12 T1a가 내릴 칸이지 올릴 칸이 아니다)"
        }
    }
    # 단계 분해(참고) — 어느 단계가 B1/B2 비용인지 판정 근거로 남긴다.
    foreach ($sample in @($author | Where-Object command -eq 'assets.modelbench')) {
        foreach ($model in $sample.data.models) {
            $report.Add("phases $($model.model) author=$($model.authorPhases | ConvertTo-Json -Compress) cooked=$($model.cookedPhases | ConvertTo-Json -Compress)")
        }
    }
    $peakMb = [double](@($author | Where-Object command -eq 'assets.modelbench' | ForEach-Object { $_.data.peakWorkingSetMB }) | Measure-Object -Maximum).Maximum
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
    $sceneData = Get-SucceededCommand $frame 'dx12.scene'
    $frameMemory = Get-SucceededCommand $frame 'assets.modelbench'
    $current = [ordered]@{
        meshUploads = [int]$sceneData.meshUploads; meshUploadKB = [int]$sceneData.uploadKB
        coverage = [int]$sceneData.coverage; vramUsedMB = [int]$frameMemory.vramUsedMB
        sceneTest1Ms = $test1Ms; sceneFtMs = $ftMs; bootMs = $bootMs; peakWorkingSetMB = $peakMb
        # legacy에 대응물이 없는 칸(§8.4 개정) — 비교가 아니라 비회귀로만 본다.
        authoringOverheadMs = $overheadNow
        identityVerifyMs = $identityNow
        textureDecodeMs = $textureNow
        measuredAt = (Get-Date).ToString('yyyy-MM-dd HH:mm')
    }
    if ($current.meshUploads -lt 1 -or $current.meshUploads -ne $sceneData.generationUploads) { Add-Failure 'B6 typed generation uploads are missing or incomplete' }
    $report.Add(('B6 frame  메시 업로드 {0}개/{1} KB · 커버리지 {2} · VRAM {3} MB' -f $current.meshUploads, $current.meshUploadKB, $current.coverage, $current.vramUsedMB))
    if ($Archive -and $failures.Count -gt 0) {
        # 0 업로드·미통과 실행을 기준선으로 굳히면 이후 비회귀 판정이 눈먼다.
        # B6뿐 아니라 **어느 칸이라도** 붉은 실행은 뜨지 않는다 — B1b/B2b 회귀를
        # 기준선으로 굳히면 회귀가 새 정답이 된다.
        $report.Add(('archive 보류 — 붉은 실행({0}건)은 기준선으로 뜨지 않는다' -f $failures.Count))
    }
    elseif ($Archive) {
        [IO.File]::WriteAllText($archivePath, ($current | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
        $report.Add("archive 기록(B1b·B2b·B2c·B6): $archivePath")
    }
    elseif ($null -ne $archived) {
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
'통과 — §8.4 비교 예산(B1a·B2a·B3·B4·B5) 충족, 비회귀 축(B1b·B2b·B6)과 PHASE 12 이관 축(B2c) 기록'
exit 0

# Verifies actual native script.reload, old instance teardown, new OnTickSimulate and old ALC collection.
# Dedicated simulation coroutine cancellation was retired 2026-10-22.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work)
. (Join-Path $PSScriptRoot "CommandResults.ps1")
New-Item -ItemType Directory -Path $Work -Force | Out-Null

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_reload_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\FSharpNative\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }
else { "GameScripts.dll을 찾지 못했다: $dll" }

# 로그를 고를 기준 시각. "가장 최신"만으로 고르면 이번 실행이 로그를 못 남겼을 때
# 옛 파일을 읽고 조용히 통과한다.
$runStart = Get-Date

$resultPath = Join-Path $Work 'lifecycle_reload.results.jsonl'
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
$proc = Start-Process -FilePath $Exe -ArgumentList @('--script', ('"'+$scenario+'"'), '--result-format', 'jsonl', '--result-file', ('"'+$resultPath+'"')) -WindowStyle Hidden `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "lifecycle_reload.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_reload.err") -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "타임아웃 ($TimeoutSeconds 초)."; exit 1 }

$logDir = Join-Path $exeDir "Saved\Log"
$editorLog = @(Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $runStart } |
    Sort-Object LastWriteTime -Descending)

if ($editorLog.Count -eq 0) {
    "이번 실행의 에디터 로그가 없다: $logDir\Editor_*.html ($runStart 이후)"
    exit 1
}

$logText = (Get-Content -LiteralPath $editorLog[0].FullName -Raw) -replace '<[^>]+>', ''

$rows = @([regex]::Matches($logText, '\[LC7\]\s+id=(\d+)\s+point=(\w+)\s+frame=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{
            Id    = [int]$_.Groups[1].Value
            Point = $_.Groups[2].Value
            Frame = [long]$_.Groups[3].Value
        }
    })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

# 어셈블리 세대와 문맥 회수는 엔진 로그가 남긴다.
$results = @(Read-CommandResults $resultPath)
$reloads = @($results | Where-Object { $_.command -eq 'script.reload' -and $_.status -eq 'succeeded' })
if ($proc.ExitCode -ne 0 -or @($results | Where-Object status -ne 'succeeded').Count) { throw 'Lifecycle reload command failed' }
$status = Get-SucceededCommand $results 'script.status'
$statusStale = [bool]$status.previousContextAlive
$statusClean = -not $statusStale
$dropped = [regex]::Match($logText, '리로드로 재개 (\d+) 건을 버렸다')

""
"─ 엔진 관측 ─────────────────────────────────────────────────────"
"  성공한 어셈블리 교체 명령: $($reloads.Count)"
"  script.status: $(if ($statusClean) { '이전 어셈블리 정리됨' } elseif ($statusStale) { '이전 어셈블리 잔존(참조 누수)' } else { '(표지 없음)' })"
"  배수 폐기    : $(if ($dropped.Success) { "재개 $($dropped.Groups[1].Value) 건" } else { '0 건(로그 없음)' })"

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) { "  id={0,-3} {1,-10} f={2}" -f $r.Id, $r.Point, $r.Frame }
""

$failed = New-Object System.Collections.Generic.List[string]

# ── 세대 자르기 ───────────────────────────────────────────────────────────────
#
# init 표지가 세대의 시작이다. 두 번째 init 부터가 리로드 뒤 세대다.

$initIdx = @()
for ($i = 0; $i -lt $rows.Count; ++$i) { if ($rows[$i].Point -eq 'init') { $initIdx += $i } }

"세대 경계: init 표지 $($initIdx.Count) 개 (기대 2 — 리로드 전 1 + 리로드 후 1)"

if ($initIdx.Count -lt 2) {
    "  → 리로드 뒤 인스턴스가 만들어지지 않았다. 리로드가 실패했거나 복원이 0건이다."
    "     아래 판정은 세대를 가를 수 없으므로 건너뛴다."
    $failed.Add('NN(세대없음)')
    ""
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

$gen1 = @($rows[$initIdx[0]..($initIdx[1] - 1)])
$gen2 = @($rows[$initIdx[1]..($rows.Count - 1)])

function Count-In($set, [string]$point) { return @($set | Where-Object { $_.Point -eq $point }).Count }

# ── 판정 NN: 어셈블리가 실제로 갈렸다 ─────────────────────────────────────────
#
# 이것이 없으면 아래 전부가 무의미하다 — 리로드가 안 일어났는데 초록이 나오면
# 그 초록은 리로드에 대해 아무것도 말하지 않는다.

"판정 NN 리로드 발생: 성공한 교체 명령 $($reloads.Count)회 · 정적 id 되돌아감 $(if ($gen2[0].Id -le $gen1[0].Id) { '있음' } else { '없음' })"
if ($reloads.Count -lt 2) {
    $failed.Add('NN(교체 명령 횟수)')
}
if ($gen2[0].Id -gt $gen1[0].Id) {
    "  → 정적 카운터가 이어졌다. 어셈블리가 갈리지 않고 인스턴스만 새로 만들어졌다는"
    "     뜻이다 — 이 시나리오는 재생 재시작과 구별되지 않는다."
    $failed.Add('NN(카운터)')
}

# ── 판정 OO: 리로드 전 인스턴스가 축소 삼단을 받는다 ──────────────────────────

$bad = @()
foreach ($p in @('end', 'removing', 'uninit')) {
    $c = Count-In $gen1 $p
    if ($c -ne 1) { $bad += "$p=$c" }
}
"판정 OO 이전 세대 축소: 어긋난 훅 $($bad.Count) 종 (기대 0)"
if ($bad.Count -gt 0) {
    "  $($bad -join ' · ') — 각각 1이어야 한다"
    "  → 리로드가 이전 세대를 접지 않고 버렸다. 구독·자원이 해제되지 않은 채"
    "     인스턴스만 사라진다."
    $failed.Add('OO')
}

# ── 판정 PP: 리로드 전 대기가 취소되고 다음 세대로 흐르지 않는다 ──────────────
#
# "누수가 없다"를 취소 표지와 함께 센다. 세대 게이트에서 측정으로 확인한 대로,
# 취소를 아예 안 해도 누수 표지는 0으로 나온다 — 컴포넌트가 목록에서 빠져 재개될
# 기회 자체가 없기 때문이다.

$oldUpdate = Count-In $gen1 'update'
if ($oldUpdate -ne 1) { $failed.Add('PP(old-fixed-update)') }
# The removed API has no pending simulation coroutine to cancel. New generation must update independently.

# ── 판정 QQ: 리로드 후 인스턴스가 전체 진입 단계를 받는다 ─────────────────────
#
# 시나리오가 **재생 중에** 리로드하므로, 새 인스턴스는 씬에도 들어가야 하고
# 시뮬레이션도 시작해야 한다. init 만 오면 스크립트는 살아 있는 척하면서 아무
# 코드도 돌리지 않는다.

$bad = @()
foreach ($p in @('init', 'added', 'enable', 'begin', 'update')) {
    $c = Count-In $gen2 $p
    if ($c -ne 1) { $bad += "$p=$c" }
}
"판정 QQ 새 세대 완결: 어긋난 훅 $($bad.Count) 종 (기대 0)"
if ($bad.Count -gt 0) {
    "  $($bad -join ' · ') — 각각 1이어야 한다"
    "  → 재생 중 리로드했는데 새 인스턴스가 씬 진입·시뮬레이션 시작을 못 받았다."
    "     스크립트가 죽은 채로 재생이 계속된다 — 저작자에게는 '리로드했더니 코드가"
    "     안 돈다'로 보인다."
    "     Cmd_script_reload 는 OnInitialized()만 부르고, 나머지 단계는"
    "     Scene::DrainPendingLifecycle 이 상태 머신을 보고 구동한다. 리로드가 그"
    "     상태를 되돌리지 않으므로 엔티티 쪽에는 '이미 다 돌았다'로 보인다."
    $failed.Add('QQ')
}

# ── 판정 RR: 이전 어셈블리가 정리된다 ─────────────────────────────────────────

"판정 RR 문맥 회수: $(if ($statusClean) { '정리됨' } elseif ($statusStale) { '잔존(참조 누수)' } else { '표지 없음' })"
if (-not $statusClean) {
    if ($statusStale) {
        "  → 이전 어셈블리가 회수되지 않았다. 리로드를 반복하면 로드 문맥이 그만큼 쌓인다."
    }
    else {
        "  → script.status 표지가 없다. 시나리오가 그 명령을 부르지 못했다."
    }
    $failed.Add('RR')
}

# ── 판정 SS: 편집 모드 리로드가 관리 훅을 흘리지 않는다 ──────────────────────
#
# 시나리오는 재생 **전에** 리로드를 한 번 부른다. 그 복원 구간에서 픽스처 표지가
# 하나라도 나오면 편집 모드에서 훅이 새어 나간 것이다.
#
# ★ 첫 판은 이 축을 "첫 init 표지 앞에 표지가 몇 개인가"로 잡았는데 그것은
#   원리적으로 언제나 0이다 — 누출된 init 자신이 첫 표지가 되기 때문이다. 변이
#   R7(가드 제거)이 실제로 init·added·enable 셋을 흘렸는데도 그 판정은 초록이었다.
#   축을 로그의 **복원 구간**으로 바꿔 다시 잡았다.
#
# Read the fresh assembly's hook counter immediately after edit-mode restoration.
# The instance factory can run in edit mode; lifecycle hooks must remain silent.
$observations = @($results | Where-Object command -eq 'script.invoke')
if ($observations.Count -ne 1 -or $observations[0].status -ne 'succeeded') {
    $failed.Add('SS(missing-observation)')
} else {
    $leakedHooks = [int]$observations[0].data.returnValue
    "SS edit-mode hooks: $leakedHooks (expected 0)"
    if ($leakedHooks -ne 0) { $failed.Add('SS') }
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 리로드가 이전 세대를 접고 새 세대를 온전히 세운다(편집 모드는 무음)"
exit 0

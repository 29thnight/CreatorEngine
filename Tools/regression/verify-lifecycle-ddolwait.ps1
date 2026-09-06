# 생명주기 DDOL 이송 중 대기 게이트 (LC7-c).
#
# ── 무엇을 메우는가 ──
#
# 설계 문서 §1.2-5는 이렇게 적었다: "DDOL 이동은 씬 Removing/Added 통지를 주며
# **인스턴스와 시뮬레이션을 유지한다.** detach를 파괴·취소로 취급하거나
# Initialized/Begin을 다시 전달하지 않는다."
#
# 기존 `verify-ddol-script` 는 그 문장의 **앞 절반**만 잰다 — 통지가 왔는가, 몇 번
# 왔는가. 뒤 절반(시뮬레이션이 유지되는가)은 어느 게이트도 보지 않았다. 이송은
# 씬 그래프에서 오브젝트를 떼어 다른 씬에 붙이는 일이라, 떼는 쪽을 파괴로 오독하면
# 스코프가 취소되고 OnSimulate 본문이 그 자리에서 풀린다 — 통지는 정확한데 루틴만
# 죽는 상태가 가능하다.
#
# ── 판정 ──
#
#   FF 대기 생존   이송을 건넌 대기가 정상 완료되고 핸들이 살아 있다
#   GG 루틴 단일   OnSimulate 가 다시 시작되지 않는다
#   HH 진입 단일   OnInitialized·OnBeginSimulation 이 다시 전달되지 않는다
#   II 이송 통지   Removing·Added 가 이송분만큼 온다(기존 게이트와 같은 축)
#
# FF 를 "cancelled 표지가 없다"로 재지 않는다. 취소로 풀린 것과 **영영 풀리지 않은
# 것**이 구별되지 않기 때문이다. 픽스처가 정상 완료와 취소를 둘 다 표지로 남기므로
# resumed 가 1건이어야 한다.
#
# II 를 여기서도 재는 이유: 통지 축과 대기 축을 같은 실행에서 보면 "통지는 왔는데
# 대기는 죽었다"를 한 자리에서 가를 수 있다. 다른 실행의 초록을 끌어와 붙이면
# 그 조합을 못 본다.
#
# ── 변이 기록 ──
#
#   R6  OnRemovingFromScene 전달 앞에 Scope.Cancel() 삽입(= detach 를 파괴로 취급)
#       → FF 만 붉음(취소 표지가 이송 프레임에 찍힌다) · GG·HH·II 초록
#
# **기존 `verify-ddol-script` 도 R6 을 잡는다.** 이 게이트를 세울 때 세운 예상은
# "순서 골든은 취소 위치를 안 보므로 눈멀 것"이었는데, 돌려 보니 골든에
# SimulateCancel 이 박혀 있어 위치 이동을 잡았다. 예상이 틀렸으므로 그대로 적는다.
#
# 그러면 이 게이트의 고유 축은 R6 이 아니라 **재개를 양의 표지로 요구하는 것**이다.
# 대기가 취소되지도 완료되지도 않고 이송 뒤 그대로 멎는 상태 — 스코프가 틱을 못
# 받는 상태 — 에서는 취소가 종료 시점에야 오고, 그때 훅 순서는 정상과 같은 모양이
# 된다. 이 게이트는 resumed 표지가 이송 프레임보다 **뒤에** 1건 있을 것을 요구하므로
# 그 상태를 붉힌다.
#
# 그 상태를 변이로 만들지는 못했다(이송된 인스턴스만 관리 틱 목록에서 빼는 변이가
# 필요하다). 다만 실제로 한 번 관측했다: 시나리오의 마지막 wait 가 대기 길이보다
# 짧았던 첫 실행에서 재개가 오지 않고 취소가 종료 프레임에 찍혔다. 그래서 FF 는
# "취소 0건"이 아니라 "재개 1건 + 이송보다 뒤"로 재고, 프레임 환산을 로그 맨 위에
# 남긴다 — 그 두 상태가 표지 부재로는 구별되지 않기 때문이다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-ddolwait.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [string]$DestScene = "L7DdolWaitDest",
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$template = Join-Path $PSScriptRoot "lifecycle_ddolwait_probe.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }
else { "GameScripts.dll을 찾지 못했다: $dll" }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dst = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\$DestScene.creator"

# 실행 전에 지운다(ddol_script 게이트와 같은 이유). 남겨 두면 이전 실행이 만든
# 파일 덕에 scene.save 가 실패해도 scene.switch 가 성공해 버려, 저장 경로가 죽은
# 채로 게이트가 통과한다.
if (Test-Path $dst) { Remove-Item -LiteralPath $dst -Force }

$scenario = Join-Path $Work "lifecycle_ddolwait_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{DEST_SCENE\}\}', ($dst -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

# 로그를 고를 기준 시각. "가장 최신"만으로 고르면 이번 실행이 로그를 못 남겼을 때
# 옛 파일을 읽고 조용히 통과한다.
$runStart = Get-Date

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "lifecycle_ddolwait.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_ddolwait.err") -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "타임아웃 ($TimeoutSeconds 초)."; exit 1 }

if (-not (Test-Path $dst)) {
    "판정 0 목적지 씬: 저장되지 않았다 — $dst"
    "  → scene.save 가 실패했다. 이송이 일어나지 않았으므로 아래 판정은 전부 무의미하다."
    exit 1
}

$logDir = Join-Path $exeDir "Saved\Log"
$editorLog = @(Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $runStart } |
    Sort-Object LastWriteTime -Descending)

if ($editorLog.Count -eq 0) {
    "이번 실행의 에디터 로그가 없다: $logDir\Editor_*.html ($runStart 이후)"
    exit 1
}

$logText = (Get-Content -LiteralPath $editorLog[0].FullName -Raw) -replace '<[^>]+>', ''

# point=X [name=Y] frame=N — name 은 resumed 에만 있다.
$rows = @([regex]::Matches($logText, '\[LC7c\]\s+point=(\w+)(?:\s+name=(\S+))?\s+frame=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{
            Point = $_.Groups[1].Value
            Name  = $_.Groups[2].Value
            Frame = [long]$_.Groups[3].Value
        }
    })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

# 프레임당 dt. 재개 표지가 없을 때 그것이 결함인지 시나리오의 wait 부족인지 가르는
# 값이라 트레이스 위에 먼저 보여준다.
$dt = [regex]::Match($logText, '\[LC7c\]\s+point=dt\s+(value=\S+\s+cross=\S+\s+frames.\S+)')
if ($dt.Success) { "프레임 환산: $($dt.Groups[1].Value)" }
else { "프레임 환산: 표지 없음 — 재생에서 PostPhysics 가 한 번도 오지 않았다" }

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) { "  {0,-10} {1,-14} f={2}" -f $r.Point, $(if ($r.Name) { "name=$($r.Name)" } else { '' }), $r.Frame }
""

$failed = New-Object System.Collections.Generic.List[string]

function Count-Of([string]$point) { return @($rows | Where-Object { $_.Point -eq $point }).Count }
function Frame-Of([string]$point) {
    $r = @($rows | Where-Object { $_.Point -eq $point })
    if ($r.Count -eq 0) { return $null }
    return $r[0].Frame
}

# 이송 통지만 골라내기 위한 구간. 앞은 sim(=생성분이 끝난 프레임), 뒤는 end(=종료
# 축소가 시작된 프레임)다. 이 둘을 쓰지 않으면 생성 시 added 와 종료 시 removing 이
# 이송분에 섞여, "이송이 통지를 한 번 줬는가"를 잴 수 없다.
$simFrame = Frame-Of 'sim'
$endFrame = Frame-Of 'end'
if ($null -eq $simFrame) { $simFrame = -1 }
if ($null -eq $endFrame) { $endFrame = [long]::MaxValue }

function Count-InTransfer([string]$point) {
    return @($rows | Where-Object { $_.Point -eq $point -and $_.Frame -gt $simFrame -and $_.Frame -lt $endFrame }).Count
}

# ── 판정 FF: 이송을 건넌 대기가 정상 완료된다 ─────────────────────────────────

$resumed = Count-Of 'resumed'
$cancelled = Count-Of 'cancelled'

"판정 FF 대기 생존: 재개 $resumed 건 · 취소 $cancelled 건 (기대 1 / 0)"
if ($cancelled -gt 0) {
    "  → 이송이 스코프를 취소했다. detach 를 파괴로 취급하고 있다 — 씬을 옮겼을 뿐인데"
    "     진행 중인 루틴이 전부 풀린다(설계 문서 §1.2-5 위반)."
    $failed.Add('FF(취소)')
}
if ($resumed -ne 1) {
    if ($resumed -eq 0 -and $cancelled -eq 0) {
        "  → 대기가 취소되지도, 완료되지도 않았다. 이송 뒤 스코프가 틱을 못 받고 있다 —"
        "     인스턴스는 살아 있는데 시간이 흐르지 않는 상태다. 표지 부재로만 드러나므로"
        "     취소 표지를 함께 세지 않았다면 이 상태는 '취소 0건'으로 읽혀 초록이었다."
    }
    elseif ($resumed -gt 1) {
        "  → 재개가 여러 번 왔다. OnSimulate 가 다시 시작됐다는 뜻이다(판정 GG 참고)."
    }
    $failed.Add('FF')
}
else {
    $row = @($rows | Where-Object { $_.Point -eq 'resumed' })[0]
    "         재개 시점의 오브젝트 이름: $($row.Name) (f=$($row.Frame))"
    if (-not $row.Name) {
        "  → 대기는 풀렸는데 이름을 읽지 못했다. 핸들이 이송을 건너지 못했다."
        $failed.Add('FF(핸들)')
    }

    # 재개가 이송보다 **뒤**여야 이 게이트가 무언가를 잰 것이다. 시나리오의 타이밍이
    # 밀려 이송 전에 대기가 끝나면 나머지 판정이 전부 초록이면서 아무것도 증명하지
    # 않는다 — 이송을 건너지 않은 대기는 이송에 대해 말해 주는 바가 없다.
    $transferFrame = @($rows | Where-Object { $_.Point -eq 'removing' -and $_.Frame -gt $simFrame -and $_.Frame -lt $endFrame } | ForEach-Object { $_.Frame })
    if ($transferFrame.Count -eq 0) {
        "  → 이송 구간의 removing 이 없어 재개가 이송을 건넜는지 확인할 수 없다(판정 II 참고)."
        $failed.Add('FF(구간불명)')
    }
    elseif ($row.Frame -le $transferFrame[0]) {
        "  → 재개(f=$($row.Frame))가 이송(f=$($transferFrame[0]))보다 앞이다. 대기가 이송을 건너지"
        "     않았으므로 이 실행은 이송에 대해 아무것도 재지 못했다 — 시나리오의 대기 길이를"
        "     늘리거나 이송 시점을 앞당겨야 한다."
        $failed.Add('FF(건너지않음)')
    }
}

# ── 판정 GG: OnSimulate가 다시 시작되지 않는다 ────────────────────────────────

$sim = Count-Of 'sim'
"판정 GG 루틴 단일: OnSimulate 시작 $sim 회 (기대 1)"
if ($sim -ne 1) {
    "  → 이송이 새 진입으로 오독됐다. 같은 컴포넌트가 루틴 둘을 동시에 돌린다."
    $failed.Add('GG')
}

# ── 판정 HH: 진입 단계가 다시 전달되지 않는다 ─────────────────────────────────

$bad = @()
foreach ($p in @('init', 'begin')) {
    $c = Count-Of $p
    if ($c -ne 1) { $bad += "$p=$c" }
}
"판정 HH 진입 단일: 어긋난 훅 $($bad.Count) 종 (기대 0)"
if ($bad.Count -gt 0) {
    "  $($bad -join ' · ') — 각각 1이어야 한다"
    "  → 이송이 초기화·시작을 다시 전달했다. 씬을 옮기는 것과 새로 태어나는 것이"
    "     구분되지 않는다."
    $failed.Add('HH')
}

# ── 판정 II: 이송 통지가 온다 ─────────────────────────────────────────────────
#
# 전체 횟수가 아니라 **이송 구간**만 센다. 생성 시 added 와 종료 축소의 removing 이
# 섞이면 "이송이 통지를 한 번 줬는가"를 잴 수 없다 — 실제로 첫 판이 그렇게 짜여
# removing 2회를 이중 발화로 오독했다(둘째는 종료분이었다).
#
# 기존 verify-ddol-script 와 같은 축인데, 대기 축과 같은 실행에서 봐야 "통지는
# 왔는데 대기는 죽었다"를 한 자리에서 가를 수 있다.

$added = Count-InTransfer 'added'
$removing = Count-InTransfer 'removing'
"판정 II 이송 통지: 이송 구간(f>$simFrame, f<$endFrame)의 added $added 회 · removing $removing 회 (기대 1 / 1)"
if ($added -ne 1 -or $removing -ne 1) {
    if ($added -lt 1 -or $removing -lt 1) {
        "  → 이송 통지가 닿지 않았다. 이송 자체가 일어나지 않았을 수 있다 — 그러면"
        "     위 FF·GG·HH 의 초록은 아무것도 증명하지 않는다."
    }
    else {
        "  → 통지가 예상보다 많다. 이송이 두 번 일어났거나 통지가 이중 발화한다."
    }
    $failed.Add('II')
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 이송이 통지를 주면서 시뮬레이션을 그대로 유지한다"
exit 0

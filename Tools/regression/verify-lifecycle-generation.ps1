# 생명주기 재생 세대 게이트 (LC7).
#
# ── 무엇을 메우는가 ──
#
# 기존 생명주기 게이트는 전부 재생을 **한 번**만 태운다. 두 번째 재생에서 무엇이
# 되는지는 어느 게이트도 보지 않았다. 그런데 에디터에서 재생·정지 반복은 저작 중
# 가장 자주 하는 일이고, 정지는 씬을 백업에서 되살리므로 인스턴스가 통째로 새로
# 만들어진다(설계 문서 §4 트랙 L1).
#
# 그 경계에서 샐 수 있는 것들:
#   · 1세대의 대기가 2세대에서 재개된다(스코프 토큰이 새로 열리지 않았다)
#   · 축소를 두 번 받거나 한 번도 못 받는다
#   · 2세대가 1세대의 상태를 물려받는다
#
# ── 판정 ──
#
#   X 세대 분리   재생마다 새 인스턴스가 하나씩 서고, 세대 구간이 겹치지 않는다
#   Y 세대 완결   각 세대가 전체 생명주기를 **정확히 한 번씩** 받는다
#   Z 누수 없음   대기가 정지에서 취소되고, 다음 세대로 흘러가지 않는다
#
# Z 는 "표지가 없다"로 재지 않는다. 취소로 풀린 것과 애초에 도달하지 못한 것을
# 가를 수 없기 때문이다. 픽스처가 취소를 catch 해 `cancelled` 표지를 남기므로,
# **cancelled 가 세대 수만큼 있고 leaked 가 0** 이어야 한다. 앞의 절반이 없으면
# 뒤의 절반은 아무것도 증명하지 않는다.
#
# 이건 추측이 아니라 측정이다. 변이 R1(정지 시 `Scope.Cancel` 제거)에서
# **leaked 는 여전히 0** 이었다 — 컴포넌트가 목록에서 빠져 틱을 못 받으니 대기가
# 재개될 기회 자체가 없다. cancelled 축이 없었다면 Z 는 그 변이에 초록이었다.
#
# ── 변이 기록 ──
#
#   R1  정지 축소의 `b.Scope.Cancel()` 제거   → Z(취소)만 붉음 · X·Y 초록
#   R2  정지 축소의 OnEndSimulation 전달 제거 → Y 세 세대 붉음 · X·Z 초록
#
# X 는 제품 변이로 붉히지 못했다. X 가 붉으려면 "재생마다 인스턴스가 새로 서지
# 않는다"여야 하는데 그건 네이티브 씬 백업·복원 자체의 결함이고, 관리 측 변이로는
# 도달하지 않는다(관리 측을 어떻게 망가뜨려도 인스턴스는 씬 복원이 만든다).
# 그래서 X 는 "지금 값이 옳다"까지만 고정하고, 이빨은 증명되지 않은 채 둔다.
#
# ── 세 번 도는 이유 ──
#
# 두 번으로는 "두 번째가 특별한가"와 "매번 같은가"를 가를 수 없다. 세 번이면
# 세대마다 무언가가 쌓이는 결함(축소가 세대 수만큼 누적되는 등)이 드러난다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-generation.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_generation_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }
else { "GameScripts.dll을 찾지 못했다: $dll" }

# 로그를 고를 기준 시각. "가장 최신"만으로 고르면 이번 실행이 로그를 못 남겼을 때
# 옛 파일을 읽고 조용히 통과한다.
$runStart = Get-Date

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "lifecycle_generation.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_generation.err") -PassThru

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

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) { "  id={0,-3} {1,-10} f={2}" -f $r.Id, $r.Point, $r.Frame }
""

$failed = New-Object System.Collections.Generic.List[string]

$ids = @($rows | ForEach-Object { $_.Id } | Sort-Object -Unique)

function Points-Of([int]$id) { return @($rows | Where-Object { $_.Id -eq $id } | ForEach-Object { $_.Point }) }
function Count-Of([int]$id, [string]$point) { return @((Points-Of $id) | Where-Object { $_ -eq $point }).Count }

# ── 판정 X: 재생마다 새 인스턴스가 하나씩 서고, 세대가 겹치지 않는다 ──────────
#
# 개수만 세면 "한 재생에서 셋이 만들어지고 나머지 둘은 비었다"와 구분되지 않는다.
# 각 세대가 차지한 프레임 구간이 서로 겹치지 않아야 진짜로 세대가 갈린 것이다.

"판정 X 세대 분리: 서로 다른 인스턴스 $($ids.Count) 개 — id $($ids -join ', ') (기대 3)"
if ($ids.Count -ne 3) {
    if ($ids.Count -lt 3) {
        "  → 재생 세 번에 인스턴스가 $($ids.Count) 개뿐이다. 정지가 인스턴스를 접지 않았거나"
        "     다음 재생이 옛 인스턴스를 그대로 쓴다 — 세대가 섞인다."
    }
    else {
        "  → 재생 수보다 인스턴스가 많다. 한 재생에서 인스턴스가 여러 번 만들어진다."
    }
    $failed.Add('X')
}

$spans = @(foreach ($id in $ids) {
    $f = @($rows | Where-Object { $_.Id -eq $id } | ForEach-Object { $_.Frame })
    [pscustomobject]@{ Id = $id; First = ($f | Measure-Object -Minimum).Minimum; Last = ($f | Measure-Object -Maximum).Maximum }
})
$spans = @($spans | Sort-Object First)

$overlaps = @()
for ($i = 1; $i -lt $spans.Count; ++$i) {
    if ($spans[$i].First -le $spans[$i - 1].Last) {
        $overlaps += "id=$($spans[$i - 1].Id)[$($spans[$i - 1].First)..$($spans[$i - 1].Last)] ∩ id=$($spans[$i].Id)[$($spans[$i].First)..$($spans[$i].Last)]"
    }
}

"판정 X 구간 비겹침: 겹친 세대 쌍 $($overlaps.Count) 쌍 (기대 0) — $(($spans | ForEach-Object { "id=$($_.Id) $($_.First)..$($_.Last)" }) -join ' · ')"
if ($overlaps.Count -gt 0) {
    foreach ($o in $overlaps) { "  $o" }
    "  → 두 세대가 같은 프레임 구간에 함께 살아 있었다. 정지가 이전 인스턴스를 접기"
    "     전에 다음 재생이 새 인스턴스를 세웠거나, 접힌 인스턴스가 목록에 남았다."
    $failed.Add('X(겹침)')
}

# ── 판정 Y: 각 세대가 전체 생명주기를 정확히 한 번씩 ──────────────────────────

$once = @('init', 'added', 'enable', 'begin', 'sim', 'end', 'removing', 'uninit')
foreach ($id in $ids) {
    $bad = @()
    foreach ($p in $once) {
        $c = Count-Of $id $p
        if ($c -ne 1) { $bad += "$p=$c" }
    }
    "판정 Y 세대 완결 [id=$id]: 어긋난 훅 $($bad.Count) 종 (기대 0)"
    if ($bad.Count -gt 0) {
        "  $($bad -join ' · ') — 각각 1이어야 한다"
        "  → 0은 그 세대가 단계를 건너뛴 것이고, 2 이상은 네이티브 구동과 관리"
        "     폴백이 둘 다 부른 것이다."
        $failed.Add("Y(id=$id)")
    }
}

# ── 판정 Z: 대기가 정지에서 취소되고 다음 세대로 흐르지 않는다 ────────────────

$cancelled = @($rows | Where-Object { $_.Point -eq 'cancelled' }).Count
$leaked = @($rows | Where-Object { $_.Point -eq 'leaked' }).Count

"판정 Z 누수 없음: 취소 $cancelled 건 · 누수 $leaked 건 (기대 3 / 0)"
if ($leaked -gt 0) {
    "  → 정지를 지나 대기가 살아남아 재개됐다. 스코프 토큰이 새로 열리지 않았거나"
    "     이전 세대의 대기가 다음 세대의 틱을 타고 있다."
    $failed.Add('Z(누수)')
}
if ($cancelled -ne $ids.Count) {
    "  → 취소 표지가 세대 수와 다르다. 누수 0건만으로는 아무것도 증명하지 못한다 —"
    "     취소로 풀린 것과 애초에 대기에 도달하지 못한 것을 가를 수 없기 때문이다."
    $failed.Add('Z(취소)')
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 재생마다 새 세대가 서고, 이전 세대의 대기가 넘어오지 않는다"
exit 0

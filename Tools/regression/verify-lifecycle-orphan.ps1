# 생명주기 고아 청소 게이트 (LC7-f).
#
# ── 무엇을 메우는가 ──
#
# `ScriptRegistry.SweepOrphans` 는 씬 언로드에서 "정상 파괴 경로를 타지 못한 관리
# 인스턴스"를 거두고, 거둔 게 있으면 경고를 남긴다 — 그 경고가 곧 **수명 배선에
# 구멍이 생겼다**는 신호다. 0건이 계약이고, 그래서 0건일 때는 일부러 아무 로그도
# 남기지 않는다(매 씬 전환마다 '0건'을 찍으면 정작 0이 아닌 날에 눈에 안 띈다).
#
# 그 경고를 보는 게이트가 없었다. 씬을 전환하는 시나리오는 여럿 있지만
# (`ddol_script_probe`, `lifecycle_ddolwait_probe`) 전부 **이송된** 쪽을 보고,
# 이송되지 않고 파괴되는 쪽이 정상 경로로 접히는지는 아무도 재지 않았다.
#
# ── 판정 ──
#
#   XX 정상 축소   씬 언로드에서 인스턴스가 축소 삼단을 받는다
#   YY 고아 0건    고아 청소가 아무것도 거두지 않는다
#
# 둘을 함께 봐야 한다. YY 는 "경고가 없다"로 재는 판정이라 홀로 두면 **씬 언로드가
# 아예 안 일어난 경우와 구별되지 않는다** — 아무 일도 안 하면 언제나 초록이다.
# XX 가 그 양의 증거를 준다: 축소 삼단이 왔다는 것은 언로드가 일어났고 정상 경로가
# 인스턴스를 잡았다는 뜻이다.
#
# ── 변이 기록 ──
#
#   R9  ScriptComponent::OnUninitializing 의 ClrHost::DestroyComponent 호출 제거
#       (= 네이티브는 인스턴스를 잊는데 관리 목록에는 남는다)
#       → **YY 만 붉음** · XX 는 초록
#
# XX 가 초록으로 남는 것이 이 변이의 요점이다. 축소 훅은 `NotifyManagedLifecycle` 이
# 먼저 전달하므로 표지는 그대로 나온다 — 훅만 세는 게이트는 이 누수에 눈이 먼다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-orphan.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [string]$DestScene = "L7OrphanDest",
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$template = Join-Path $PSScriptRoot "lifecycle_orphan_probe.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }
else { "GameScripts.dll을 찾지 못했다: $dll" }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dst = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\$DestScene.creator"

# 실행 전에 지운다(ddol 게이트와 같은 이유). 남겨 두면 이전 실행이 만든 파일 덕에
# scene.save 가 실패해도 scene.switch 가 성공해 버려, 저장 경로가 죽은 채로
# 게이트가 통과한다.
if (Test-Path $dst) { Remove-Item -LiteralPath $dst -Force }

$scenario = Join-Path $Work "lifecycle_orphan_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{DEST_SCENE\}\}', ($dst -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

# 로그를 고를 기준 시각. "가장 최신"만으로 고르면 이번 실행이 로그를 못 남겼을 때
# 옛 파일을 읽고 조용히 통과한다.
$runStart = Get-Date

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "lifecycle_orphan.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_orphan.err") -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "타임아웃 ($TimeoutSeconds 초)."; exit 1 }

if (-not (Test-Path $dst)) {
    "판정 0 목적지 씬: 저장되지 않았다 — $dst"
    "  → scene.save 가 실패했다. 씬 전환이 일어나지 않았으므로 아래 판정은 무의미하다."
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

$rows = @([regex]::Matches($logText, '\[LC7\]\s+id=(\d+)\s+point=(\w+)\s+frame=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{ Point = $_.Groups[2].Value; Frame = [long]$_.Groups[3].Value }
    })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

# 고아 청소 경고. 스크립트와 트리 건수를 함께 남긴다.
$sweep = [regex]::Match($logText, '\[씬 언로드\][^\r\n]*스크립트 (\d+)개 · 트리 (\d+)개')

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) { "  {0,-10} f={1}" -f $r.Point, $r.Frame }
""

$failed = New-Object System.Collections.Generic.List[string]

function Count-Of([string]$point) { return @($rows | Where-Object { $_.Point -eq $point }).Count }

# ── 판정 XX: 씬 언로드에서 인스턴스가 축소 삼단을 받는다 ──────────────────────
#
# 이것이 아래 YY 의 전제다. 언로드가 아예 없으면 고아 청소도 안 돌고, 그러면
# "경고 0건"은 아무것도 증명하지 않는다.

$bad = @()
foreach ($p in @('end', 'removing', 'uninit')) {
    $c = Count-Of $p
    if ($c -ne 1) { $bad += "$p=$c" }
}
"판정 XX 정상 축소: 어긋난 훅 $($bad.Count) 종 (기대 0)"
if ($bad.Count -gt 0) {
    "  $($bad -join ' · ') — 각각 1이어야 한다"
    "  → 씬 전환에서 인스턴스가 정상 파괴 경로를 지나지 않았다. 아래 판정 YY 의"
    "     초록은 '언로드가 없었다'는 뜻일 수 있으므로 함께 의심해야 한다."
    $failed.Add('XX')
}

# ── 판정 YY: 고아 청소가 아무것도 거두지 않는다 ───────────────────────────────

if (-not $sweep.Success) {
    "판정 YY 고아 0건: 거둔 것 없음 (경고 없음 — 계약대로다)"
}
else {
    $scripts = [int]$sweep.Groups[1].Value
    $trees = [int]$sweep.Groups[2].Value
    "판정 YY 고아 0건: 스크립트 $scripts 개 · 트리 $trees 개를 거뒀다 (기대 0 / 0)"
    "  → 정상 파괴 경로를 타지 못한 인스턴스가 있다. 컴포넌트 축소가 관리 목록의"
    "     제거까지 가지 못하는 파괴 경로가 생겼다는 뜻이다 — 고아 청소가 뒤에서"
    "     주워 담고 있으므로 겉으로는 아무 증상도 없다."
    $failed.Add('YY')
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 씬 언로드가 인스턴스를 정상 경로로 접고 고아를 남기지 않는다"
exit 0

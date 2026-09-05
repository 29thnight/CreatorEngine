# 생명주기 즉시 제거 게이트 (LC7-e).
#
# ── 무엇을 메우는가 ──
#
# 컴포넌트를 **프레임 중간에 그 자리에서** 소멸시키는 경로는 하나뿐이다 —
# PrefabUtility::ApplyComponentDiff 의 차집합 적용. 씬의 정상 파괴는 전부
# Scene::FlushPendingDestroy 를 지나 프레임 경계에서 일어난다.
#
# 그 경로에 축소 삼단 배선이 들어 있다(예전에는 OnRemovingFromScene 을 빠뜨렸고
# 그것이 고쳐졌다). 그런데 그 배선이 **관리 스크립트에 실제로 닿는지**는 어느
# 게이트도 재지 않았다. 네이티브 컴포넌트에는 닿고 ScriptComponent 에서 끊기는
# 상태가 가능하고, 그러면 스크립트의 구독·대기가 해제되지 않은 채 인스턴스만
# 사라진다.
#
# ── 판정 ──
#
#   TT 제거 발생   차집합이 실제로 인스턴스에 적용됐다
#   UU 축소 전달   축소 삼단이 관리 스크립트에 각 1회 닿는다
#   VV 대기 취소   진행 중이던 대기가 취소된다
#   WW 시점 확인   그 축소가 **정지가 아니라 즉시 제거 시점**에 왔다
#
# WW 가 없으면 나머지 셋이 아무것도 증명하지 않는다. 즉시 제거가 축소를 전혀 안
# 주더라도 뒤이은 정지가 같은 삼단을 주므로 UU·VV 는 그대로 초록이다.
#
# 경계는 프레임 산술이 아니라 로그 순서로 잡는다. `[CLI] 프리팹 갱신 적용` 은
# ApplyComponentDiff 가 **끝난 뒤** 찍히므로, 즉시 제거가 낸 축소는 그 줄보다
# 앞에 있고 정지가 낸 축소는 뒤에 있다. 시나리오의 wait 값이 바뀌어도 이 경계는
# 흔들리지 않는다.
#
# 그 순서에 기대는 것이 이 판정의 전제다. 구현이 바뀌어 갱신 로그가 차집합보다
# 앞서 찍히면 WW 가 붉어진다 — 조용히 틀리지 않고 드러난다.
#
# ── 변이 기록 ──
#
#   R8  ApplyComponentDiff 의 축소 삼단 호출 제거
#       → **WW 만 붉음** · TT·UU·VV 는 초록
#
# 이 결과가 WW 의 존재 이유를 그대로 보여 준다. 축소를 통째로 없앴는데도 UU(삼단
# 각 1회)와 VV(취소 1건)는 초록이었다 — 뒤이은 정지가 폴백으로 같은 삼단을 주기
# 때문이다(f=284 → f=469 로 옮겨 갔을 뿐이다). 시점을 가르지 않는 게이트는 이
# 결함에 완전히 눈이 먼다.
#
# ── 픽스처를 새로 만들지 않는 이유 ──
#
# GenerationProbe 가 이미 필요한 것을 전부 남긴다: 훅 여덟 종을 세고, 긴 대기의
# 정상 완료와 취소를 갈라 표지한다. 즉시 제거에서 재는 것도 정확히 그 둘이다.
#
# 사용법: pwsh Tools\regression\verify-lifecycle-immediate.ps1

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

$scenario = Join-Path $PSScriptRoot "lifecycle_immediate_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

"실행 파일: $Exe ($((Get-Item $Exe).LastWriteTime))"
$dll = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $dll) { "GameScripts: $((Get-Item $dll).LastWriteTime)" }
else { "GameScripts.dll을 찾지 못했다: $dll" }

# 로그를 고를 기준 시각. "가장 최신"만으로 고르면 이번 실행이 로그를 못 남겼을 때
# 옛 파일을 읽고 조용히 통과한다.
$runStart = Get-Date

$outPath = Join-Path $Work "lifecycle_immediate.out"
$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError  (Join-Path $Work "lifecycle_immediate.err") -PassThru

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
$stdout = if (Test-Path $outPath) { Get-Content -LiteralPath $outPath -Raw } else { '' }

$rows = @([regex]::Matches($logText, '\[LC7\]\s+id=(\d+)\s+point=(\w+)\s+frame=(\d+)') |
    ForEach-Object {
        [pscustomobject]@{
            Point = $_.Groups[2].Value
            Frame = [long]$_.Groups[3].Value
            Index = $_.Index
        }
    })

if ($rows.Count -eq 0) {
    "픽스처 기록이 0건이다. 스크립트가 붙지 않았거나 재생에 들어가지 못했다."
    "로그: $($editorLog[0].FullName)"
    exit 1
}

# 차집합 적용 결과는 stdout 에만 있다(printf). 엔진 로그에는 완료 표지만 남는다.
$applied = [regex]::Match($stdout, '\[prefab\.update\][^\r\n]*등록 인스턴스 (\d+)개 중 (\d+)개에 적용')

# 즉시 제거의 완료 경계.
$updateMark = [regex]::Match($logText, '\[CLI\] 프리팹 갱신 적용')

""
"─ 엔진 관측 ─────────────────────────────────────────────────────"
"  차집합 적용: $(if ($applied.Success) { "등록 $($applied.Groups[1].Value)개 중 $($applied.Groups[2].Value)개" } else { '(표지 없음)' })"
"  갱신 완료 표지: $(if ($updateMark.Success) { '있음' } else { '없음' })"

""
"─ 픽스처 트레이스 ───────────────────────────────────────────────"
foreach ($r in $rows) {
    $side = if ($updateMark.Success) { if ($r.Index -lt $updateMark.Index) { '즉시제거 이전' } else { '갱신 이후' } } else { '' }
    "  {0,-10} f={1,-6} {2}" -f $r.Point, $r.Frame, $side
}
""

$failed = New-Object System.Collections.Generic.List[string]

function Count-Of([string]$point) { return @($rows | Where-Object { $_.Point -eq $point }).Count }

# ── 판정 TT: 차집합이 실제로 적용됐다 ─────────────────────────────────────────
#
# 이것이 없으면 아래 전부가 무의미하다. prefab.update 는 대상 인스턴스를 못 찾아도
# 조용히 0건 적용하고 끝나므로(명령 구현이 그 함정을 주석으로 적어 두었다), 적용
# 건수를 세지 않으면 "아무것도 안 지웠는데 정지가 축소를 줘서 초록"이 된다.

if (-not $applied.Success) {
    "판정 TT 제거 발생: prefab.update 적용 표지가 없다"
    "  → 명령이 실행되지 못했다. 아래 판정은 즉시 제거에 대해 아무것도 말하지 않는다."
    $failed.Add('TT(표지없음)')
}
else {
    $count = [int]$applied.Groups[2].Value
    "판정 TT 제거 발생: 인스턴스 $count 개에 적용 (기대 1 이상)"
    if ($count -lt 1) {
        "  → 0건 적용이다. 프리팹 identity 가 어긋나 대상 인스턴스를 찾지 못했다 —"
        "     이 실행은 즉시 제거 경로를 밟지 않았다."
        $failed.Add('TT')
    }
}

# ── 판정 UU: 축소 삼단이 관리 스크립트에 닿는다 ───────────────────────────────

$bad = @()
foreach ($p in @('end', 'removing', 'uninit')) {
    $c = Count-Of $p
    if ($c -ne 1) { $bad += "$p=$c" }
}
"판정 UU 축소 전달: 어긋난 훅 $($bad.Count) 종 (기대 0)"
if ($bad.Count -gt 0) {
    "  $($bad -join ' · ') — 각각 1이어야 한다"
    "  → 즉시 제거가 축소를 관리 스크립트에 전달하지 못했다. 네이티브 배선은 있는데"
    "     ScriptComponent 에서 끊기면 이 모양이 된다 — 구독·대기가 해제되지 않은 채"
    "     인스턴스만 사라진다."
    $failed.Add('UU')
}

# ── 판정 VV: 진행 중이던 대기가 취소된다 ──────────────────────────────────────

$cancelled = Count-Of 'cancelled'
$leaked = Count-Of 'leaked'
"판정 VV 대기 취소: 취소 $cancelled 건 · 누수 $leaked 건 (기대 1 / 0)"
if ($cancelled -ne 1 -or $leaked -gt 0) {
    if ($leaked -gt 0) {
        "  → 제거를 지나 대기가 살아남아 재개됐다."
    }
    else {
        "  → 취소 표지가 1건이 아니다. 스코프가 즉시 제거 경로에서 취소되지 않는다."
    }
    $failed.Add('VV')
}

# ── 판정 WW: 축소가 정지가 아니라 즉시 제거 시점에 왔다 ───────────────────────

if (-not $updateMark.Success) {
    "판정 WW 시점 확인: 갱신 완료 표지가 없어 시점을 가를 수 없다"
    $failed.Add('WW(경계없음)')
}
else {
    $teardown = @($rows | Where-Object { $_.Point -in @('end', 'removing', 'uninit') })
    $afterUpdate = @($teardown | Where-Object { $_.Index -gt $updateMark.Index })

    "판정 WW 시점 확인: 갱신 완료 뒤에 온 축소 훅 $($afterUpdate.Count) 건 (기대 0)"
    if ($afterUpdate.Count -gt 0) {
        "  늦게 온 훅: $(($afterUpdate | ForEach-Object { "$($_.Point)(f=$($_.Frame))" }) -join ' · ')"
        "  → 축소가 즉시 제거가 아니라 그 뒤(정지)에서 왔다. 즉시 제거는 컴포넌트를"
        "     지우기만 하고 축소를 주지 않았다는 뜻이다 — 판정 UU·VV 의 초록은 정지가"
        "     만든 것이라 즉시 제거에 대해 아무것도 말하지 않는다."
        $failed.Add('WW')
    }
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 즉시 제거가 그 자리에서 축소 삼단을 관리 스크립트에 전달한다"
exit 0

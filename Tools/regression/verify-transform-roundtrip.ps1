# 트랜스폼 값 왕복 대조 (SceneGraphRedesignPlan §4 트랙 S — S1-b 선행 게이트)
#
# ── 이 검사가 메우는 구멍 ──
#
# 역직렬화기는 스키마 쪽 이름으로만 값을 당겨 오고 YAML의 키를 열거하지 않는다.
# 그래서 필드를 다른 곳으로 옮기면(S1-b: GameObject::m_transform → 컴포넌트 블록)
# 승격 경로가 빠진 자리에서 위치·회전·크기가 **에러 없이 사라진다**.
#
# 기존 세트는 이걸 못 잡는다:
#   - prefab_roundtrip : 인스턴스·등록 **개수**만 본다. 값은 안 본다.
#   - authored_rects   : 값을 대조하지만 대상이 UI(RectTransform)뿐이다.
#   - reflect_golden   : 기본 생성 타입의 덤프라 저작 데이터의 왕복은 안 본다.
#
# ── 판정 항목 ──
#
#   1  before/after 다이제스트가 둘 다 있다   — 시나리오가 도중에 멈추지 않았다
#   2  오브젝트 수가 최소치 이상               — 빈 씬을 재고 통과하지 않는다(README 원칙)
#   3  줄 단위 diff 0                          — ★ 값이 왕복을 건넜다
#   4  해시 일치                               — 3의 독립 확인
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-transform-roundtrip.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [int]$MinObjects = 30,  # Test1.creator의 실제 규모(68 노드)에 대한 하한 —
                            # 0이면 검사가 눈을 감은 것이고, 너무 낮으면 "기본 씬을
                            # 재고 통과"를 못 잡는다(실측: 경로가 틀렸을 때 3개였다)
    [string]$SourceScene = ""
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "transform_roundtrip.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

# 원본 씬 — 저작 자산 중 가장 큰 것(계층 깊이·오브젝트 수 모두). 저장소 루트는
# 이 스크립트 위치(Tools/regression)에서 두 단계 위다.
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($SourceScene)) {
    $SourceScene = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\Test1.creator"
}
if (-not (Test-Path $SourceScene)) { "원본 씬이 없다: $SourceScene"; exit 1 }

# 경로는 실행 직전에 채운다 — 절대 경로를 시나리오에 박으면 다른 작업 폴더에서
# 조용히 실패하고, 그 실패가 '오브젝트 3개'라는 엉뚱한 모습으로 나타난다.
$srcPath  = ($SourceScene) -replace '\\', '/'
$tempScene = Join-Path $Work "TransformRoundTrip.creator"
$tmpPath  = ($tempScene) -replace '\\', '/'
if (Test-Path $tempScene) { Remove-Item $tempScene -Force }

$scenario = Join-Path $Work "transform_roundtrip_resolved.txt"
((Get-Content $template -Raw) -replace '\{\{SRC_SCENE\}\}', $srcPath) -replace '\{\{TMP_SCENE\}\}', $tmpPath |
    Set-Content $scenario -Encoding UTF8

$outFile = Join-Path $Work "transform_roundtrip.out"
$errFile = Join-Path $Work "transform_roundtrip.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "실패: 시간 초과 ($TimeoutSeconds 초)"
    exit 1
}

$lines = Get-Content $outFile

$before = @($lines | Where-Object { $_ -match '^\[tfdigest:before\] ' } | ForEach-Object { $_ -replace '^\[tfdigest:before\] ', '' })
$after  = @($lines | Where-Object { $_ -match '^\[tfdigest:after\] '  } | ForEach-Object { $_ -replace '^\[tfdigest:after\] ',  '' })

# 마지막 줄은 "합계 …" 요약이다 — 본문과 분리한다.
$beforeSummary = $before | Where-Object { $_ -match '^합계 ' } | Select-Object -Last 1
$afterSummary  = $after  | Where-Object { $_ -match '^합계 ' } | Select-Object -Last 1
$beforeRows = @($before | Where-Object { $_ -notmatch '^합계 ' })
$afterRows  = @($after  | Where-Object { $_ -notmatch '^합계 ' })

if (-not $beforeSummary -or -not $afterSummary) {
    "실패: 다이제스트가 둘 다 나오지 않았다 (before=$($beforeRows.Count)줄, after=$($afterRows.Count)줄)"
    "  시나리오가 도중에 멈췄을 수 있다. 출력: $outFile"
    exit 1
}

if ($beforeRows.Count -lt $MinObjects) {
    "실패: 오브젝트가 너무 적다 ($($beforeRows.Count) < $MinObjects) — 검사가 빈 씬을 재고 통과할 뻔했다"
    exit 1
}

$failed = $false

if ($beforeRows.Count -ne $afterRows.Count) {
    "실패: 오브젝트 수가 다르다 (저장 전 $($beforeRows.Count) vs 재로드 후 $($afterRows.Count))"
    $failed = $true
}

$limit = [Math]::Min($beforeRows.Count, $afterRows.Count)
$diffCount = 0
for ($i = 0; $i -lt $limit; $i++) {
    if ($beforeRows[$i] -ne $afterRows[$i]) {
        $diffCount++
        if ($diffCount -le 10) {
            "  줄 $($i + 1): 저장 전 '$($beforeRows[$i])'"
            "           재로드 후 '$($afterRows[$i])'"
        }
    }
}

if ($diffCount -gt 0) {
    "실패: 트랜스폼 값이 왕복을 못 건넜다 ($diffCount 줄 다름 / $limit 줄)"
    if ($diffCount -gt 10) { "  (앞 10건만 표시)" }
    $failed = $true
}

$beforeHash = if ($beforeSummary -match '해시 ([0-9a-f]+)') { $Matches[1] } else { "?" }
$afterHash  = if ($afterSummary  -match '해시 ([0-9a-f]+)') { $Matches[1] } else { "?" }
if ($beforeHash -ne $afterHash) {
    "실패: 다이제스트 해시가 다르다 (저장 전 $beforeHash vs 재로드 후 $afterHash)"
    $failed = $true
}

if (Test-Path $tempScene) { Remove-Item $tempScene -Force }

if ($failed) { exit 1 }

"전체 통과 — 트랜스폼 값 $($beforeRows.Count)개 오브젝트가 저장·재로드를 건넜다 (해시 $beforeHash)"
exit 0

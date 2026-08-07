# 생명주기 기준선 회귀 (PHASE 9-0).
#
# 두 가지 모드로 쓴다.
#
#   1) 기준선 뜨기 — PHASE 9 교체 **전에** 한 번:
#        .\verify-lifecycle-baseline.ps1 -Baseline
#      결과가 lifecycle_baseline.tsv 로 이 폴더에 남는다. 이 파일을 커밋한다.
#
#   2) 대조 — 9-1~9-3 각 단계 후:
#        .\verify-lifecycle-baseline.ps1
#      새로 뜬 기록을 기준선과 비교해 다르면 실패로 끝난다.
#
# ── 무엇을 비교하고 무엇을 무시하는가 ──
#
# 인스턴스 ID는 실행마다 다르다(make_guid). 그대로 비교하면 항상 불일치가 나서
# 이 검증이 곧 무시된다. 그래서 ID 열은 지우고 비교한다.
#
# 프레임 번호도 지운다. 로딩에 걸리는 프레임 수는 디스크 상태에 따라 흔들리고,
# 알고 싶은 것은 "몇 번째 프레임에 불렸나"가 아니라 "어떤 순서로 불렸나"다.
#
# 남는 것은 (단계, 타입, 오브젝트 이름) 세 열의 순서다. 그것이 생명주기의 계약이다.

param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [switch]$Baseline,
    [int]$TimeoutSeconds = 300,
    # 왕복에 쓸 두 씬. 구조가 다른 둘을 고르는 것이 요점이다 — 같은 성격의 씬끼리
    # 오가면 컴포넌트 구성이 비슷해 파괴·초기화 순서의 차이가 드러나지 않는다.
    [string]$SceneA = "FT_Material",
    [string]$SceneB = "UITestScene"
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "..\..\scripts\lifecycle_baseline.txt"
$template = [System.IO.Path]::GetFullPath($template)
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

# 씬 경로를 실행 직전에 채운다.
#
# scene.switch는 절대 경로를 받는데, 절대 경로를 시나리오 파일에 박아 두면
# 다른 사람의 작업 폴더에서 조용히 실패한다(로드 실패는 종료 코드로 드러나지
# 않고 '기록 0건'으로만 나타나 원인을 짚기 어렵다). 그래서 여기서 만든다.
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"

function Resolve-Scene([string]$name) {
    # 이름만 줬으면 표준 씬 폴더에서 .creator를 찾고, 경로를 줬으면 그대로 쓴다.
    $path = if ([System.IO.Path]::IsPathRooted($name)) { $name }
            else { Join-Path $sceneDir "$name.creator" }
    if (-not (Test-Path $path)) {
        "씬이 없다: $path"
        "사용 가능한 씬:"
        Get-ChildItem $sceneDir -Filter *.creator -ErrorAction SilentlyContinue |
            ForEach-Object { "  " + $_.BaseName }
        exit 1
    }
    return ($path -replace '\\', '/')
}

$pathA = Resolve-Scene $SceneA
$pathB = Resolve-Scene $SceneB

$scenario = Join-Path $Work "lifecycle_baseline_resolved.txt"
(Get-Content $template -Raw) `
    -replace '\{\{SCENE_A\}\}', $pathA `
    -replace '\{\{SCENE_B\}\}', $pathB |
    Set-Content $scenario -Encoding UTF8

$baselineFile = Join-Path $PSScriptRoot "lifecycle_baseline.tsv"
# lifecycle.dump 는 상대 경로를 프로세스 작업 디렉터리 기준으로 푼다.
$producedFile = Join-Path $exeDir "lifecycle_trace.tsv"

if (Test-Path $producedFile) { Remove-Item $producedFile -Force }

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "lifecycle_baseline.out") `
    -RedirectStandardError  (Join-Path $Work "lifecycle_baseline.err") -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null

if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

if ($proc.ExitCode -ne 0) {
    "비정상 종료: 0x{0:X8}" -f $proc.ExitCode
    "덤프가 남았다면 $exeDir\Dump\ 의 최신 .txt에 스택이 있다."
    exit 1
}

if (-not (Test-Path $producedFile)) {
    "기록 파일이 나오지 않았다: $producedFile"
    "lifecycle.dump 가 '기록 0건'으로 끝났을 수 있다. 표준 출력을 볼 것:"
    "  " + (Join-Path $Work "lifecycle_baseline.out")
    exit 1
}

# 비교용으로 정규화한다: 주석 제거 → 프레임(1열)·인스턴스ID(5열) 제거.
function Get-Normalized([string]$path) {
    Get-Content $path |
        Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne '' } |
        ForEach-Object {
            $f = $_ -split "`t"
            if ($f.Count -ge 4) { "$($f[1])`t$($f[2])`t$($f[3])" } else { $_ }
        }
}

$produced = @(Get-Normalized $producedFile)

if ($produced.Count -eq 0) {
    "기록이 비어 있다. 씬은 열렸는데 생명주기가 한 건도 안 불렸다는 뜻이다."
    "표준 출력에서 scene.switch 결과를 확인할 것: " + (Join-Path $Work "lifecycle_baseline.out")
    exit 1
}

if ($Baseline) {
    Copy-Item $producedFile $baselineFile -Force
    "기준선 저장: $baselineFile ($($produced.Count) 사건)"
    "이 파일을 커밋하면 이후 실행이 여기에 대조된다."
    exit 0
}

if (-not (Test-Path $baselineFile)) {
    "기준선이 없다: $baselineFile"
    "먼저 -Baseline 으로 한 번 떠 둘 것 (PHASE 9 교체 전에 떠야 의미가 있다)."
    exit 1
}

$expected = @(Get-Normalized $baselineFile)

$diffs = Compare-Object -ReferenceObject $expected -DifferenceObject $produced -SyncWindow 0

if ($null -eq $diffs -or $diffs.Count -eq 0) {
    "전체 통과 ($($produced.Count) 사건 · 순서 동일)"
    exit 0
}

"생명주기 순서가 달라졌다 — 차이 $($diffs.Count) 건 (앞 40건만 표시)"
$diffs | Select-Object -First 40 | ForEach-Object {
    $mark = if ($_.SideIndicator -eq '<=') { "기준선에만" } else { "현재에만  " }
    "  $mark  $($_.InputObject)"
}
""
"기준선: $baselineFile"
"현재:   $producedFile"
"의도한 변경이라면 -Baseline 으로 갱신하되, 무엇이 왜 바뀌었는지 커밋 메시지에 남길 것."
exit 1

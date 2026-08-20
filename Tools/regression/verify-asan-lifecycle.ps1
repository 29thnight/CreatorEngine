# ASan 실행 검증 (PHASE 9-9)
#
# EngineAsan=true로 빌드한 실행 파일에 수명 스트레스 시나리오를 물려 돌린다.
# ASan이 침묵하고 종료 코드가 0이면 R1(순회 중 UAF)·R2(즉시 파괴)가 실제로
# 닫혀 있다는 뜻이다 — 지금까지의 설계 논증·회귀 통과와 달리 기계가 하는 말이다.
#
# 선행:
#   msbuild CreatorEngine.sln /p:Configuration=Debug /p:Platform=x64 /p:EngineAsan=true
#
# 사용법:
#   pwsh Tools\regression\verify-asan-lifecycle.ps1
#   pwsh Tools\regression\verify-asan-lifecycle.ps1 -SceneA Test1 -SceneB Test2
param(
    [string]$Exe = "",
    # ★ 이 둘은 **시나리오가 직접 만든다**(2026-08-21). 예전 기본값은 저작 자산
    # Test1/Test2였고, 둘 다 .gitignore가 무시하는 파일이라 이 게이트가 이 기계
    # 밖에서는 돌지 않았다.
    [string]$SceneA = "AsanLife3D",
    [string]$SceneB = "AsanLifeUI",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSec = 600
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $repoRoot "x64\Debug\Academy_4Q.exe"
}
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)

# ── ASan 런타임 DLL ──
# /MD 계열은 clang_rt.asan_dynamic-x86_64.dll을 실행 시점에 찾는다. 없으면
# 프로세스가 아예 기동하지 못하고, 그 실패는 "검증 통과"와 구분이 안 된다.
# 그래서 여기서 미리 확인하고 없으면 도구 폴더에서 가져다 둔다.
$asanDll = Join-Path $exeDir "clang_rt.asan_dynamic-x86_64.dll"
if (-not (Test-Path $asanDll)) {
    $vcTools = $env:VCToolsInstallDir
    if ([string]::IsNullOrEmpty($vcTools)) {
        # 계측 코드와 런타임은 같은 툴셋이어야 한다. 버전이 어긋나면 붙긴 붙어도
        # 보고가 어긋날 수 있고, 그러면 "조용하다"를 믿을 수 없게 된다 —
        # 이 검증의 결론이 통째로 흔들리는 자리다.
        # 경로 문자열 정렬은 못 쓴다("18" < "2022"라 옛 VS를 집는다). MSVC 툴셋
        # 버전을 숫자로 읽어 가장 높은 것을 고른다.
        $candidate = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" `
            -ErrorAction SilentlyContinue |
            Sort-Object { [version]($_.FullName -replace '.*\\MSVC\\([0-9.]+)\\.*', '$1') } |
            Select-Object -Last 1
        if ($null -eq $candidate) {
            "ASan 런타임 DLL을 찾지 못했다. 개발자 명령 프롬프트에서 실행하거나"
            "clang_rt.asan_dynamic-x86_64.dll을 exe 옆에 복사할 것: $exeDir"
            exit 1
        }
        $source = $candidate.FullName
    } else {
        $source = Join-Path $vcTools "bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll"
    }
    Copy-Item $source $asanDll -Force
    "ASan 런타임 복사: $source"
}

# ── 씬 경로 치환 ──
# scene.switch는 절대 경로를 받는다. 시나리오에 박아 두면 다른 작업 폴더에서
# 조용히 실패하고, 로드 실패는 종료 코드가 아니라 '아무 일도 안 일어남'으로만
# 나타나 원인을 짚기 어렵다(lifecycle_baseline과 같은 이유).
$sceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"

function Resolve-Scene([string]$name) {
    $path = if ([System.IO.Path]::IsPathRooted($name)) { $name }
            else { Join-Path $sceneDir "$name.creator" }

    # ★ 존재를 확인하지 않고 **지운다** — 시나리오가 실행 중에 만들기 때문이다.
    # 남겨 두면 이전 실행이 만든 파일 덕에 scene.save가 실패해도 왕복이 성립해
    # ASan이 "조용하다"고 보고한다. 그 침묵은 아무것도 증명하지 않는다.
    if (Test-Path $path) { Remove-Item -LiteralPath $path -Force }
    return ($path -replace '\\', '/')
}

$template = Join-Path $repoRoot "scripts\asan_lifecycle_stress.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$scenario = Join-Path $Work "asan_lifecycle_resolved.txt"
(Get-Content $template -Raw) `
    -replace '\{\{SCENE_A\}\}', (Resolve-Scene $SceneA) `
    -replace '\{\{SCENE_B\}\}', (Resolve-Scene $SceneB) |
    Set-Content $scenario -Encoding UTF8

# 누수 검출은 Windows에서 지원되지 않으므로 끄고, 첫 오류에서 즉시 멈춘다 —
# 계속 돌리면 첫 오류가 만든 오염이 뒤의 보고서를 전부 못 믿게 만든다.
$env:ASAN_OPTIONS = "detect_leaks=0:halt_on_error=1:abort_on_error=1:print_stats=1"

$outLog = Join-Path $Work "asan_lifecycle.out"
$errLog = Join-Path $Work "asan_lifecycle.err"

"=== ASan 수명 스트레스 ($SceneA <-> $SceneB) ==="
$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir -RedirectStandardOutput $outLog -RedirectStandardError $errLog `
    -PassThru

if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    $proc.Kill()
    "시간 초과 ($TimeoutSec 초) — 멈춘 지점을 로그에서 확인할 것: $outLog"
    exit 1
}

$exit = $proc.ExitCode

# ASan 보고서는 stderr로 나온다. 종료 코드만 보면 놓칠 수 있어 양쪽을 다 훑는다.
$report = @()
foreach ($log in @($outLog, $errLog)) {
    if (Test-Path $log) {
        $report += Select-String -Path $log -Pattern "ERROR: AddressSanitizer|SUMMARY: AddressSanitizer" -ErrorAction SilentlyContinue
    }
}

if ($report.Count -gt 0) {
    "ASan 보고 발생:"
    $report | ForEach-Object { "  " + $_.Line }
    "전체 로그: $errLog"
    exit 1
}

if ($exit -ne 0) {
    "종료 코드 $exit — ASan 보고는 없으나 정상 종료가 아니다."
    "로그: $outLog"
    if (Test-Path $errLog) { Get-Content $errLog -Tail 20 }
    exit 1
}

# 무장이 실제로 발화했는지 확인한다. 시나리오가 다 돌아도 장치가 안 터졌으면
# "재현하지 않았다"이지 "안전하다"가 아니다 — 이 검증의 전제가 무너진다.
#
# ★ 2026-08-19 정정 — 이 검사는 그동안 발화가 아니라 **무장**을 세고 있었다.
#   옛 패턴 "순회 한복판에서"는 lifecycle.stress 명령이 무장 직후 찍는 콘솔
#   확인 printf의 문구다. 정작 발화 로그는 Debug->LogWarning으로만 나가는데
#   그 싱크는 인메모리·HTML 파일뿐이고 **stdout에는 한 글자도 안 쓴다** —
#   즉 여기서 아무리 세도 stdout에서는 발화를 볼 수 없었다. 주석에는
#   "발화했는지 확인한다"고 적혀 있었으니, 자가 재는 것과 이름이 어긋난
#   경우다. FireReentrancyStress가 이제 같은 줄을 stdout에도 내므로
#   (Scene.cpp) 진짜 발화를 센다.
$fireLines = @(Select-String -Path $outLog -Pattern "재진입 시험 발화" -ErrorAction SilentlyContinue)
$fired = $fireLines.Count
if ($fired -lt 5) {
    "재진입 발화 $fired 회 — 5회를 기대했다. 장치가 서지 않았을 수 있다."
    "  (무장만 되고 소비되지 않았을 수 있다 — Scene::FireReentrancyStress의 stdout 줄을 확인할 것)"
    exit 1
}

# 그중 최소 1회는 **순회 한복판**이어야 한다(트랙 C · C2-0).
#
# C3가 옛 RegistryTick을 걷어내면서 midTraversal=true 호출부가 코드에서 통째로
# 사라졌고, 그 뒤로 이 시험은 순회 밖에서만 터지고 있었다 — 겨냥한 것을 재지
# 않는 상태였다. CameraSystem::Update 루프 안에 발화점을 복원했으므로 여기서
# 그것이 실제로 소비됐는지 본다. "전부 순회 중"은 기대하지 않는다: 앞선 파괴가
# 카메라를 마킹하면 다음 프레임부터 m_cameras가 비어 폴백으로 떨어지는 것이
# 정상 동작이다.
$midTraversal = @($fireLines | Where-Object { $_.Line -match "순회 중" }).Count
if ($midTraversal -lt 1) {
    "발화 $fired 회 전부 순회 밖이다 — 순회 중 발화가 0회다."
    "  C2-0의 발화점(CameraSystem::Update 루프)이 죽었거나, 그 프레임에 카메라가 없었다."
    exit 1
}

"재진입 발화 $fired 회(그중 순회 중 $midTraversal 회) · ASan 보고 0건 · 종료 코드 0"
"전체 통과"
exit 0

# U7/E7-c 전용 회귀.
#
# ui.navprobe가 메모리에서 UI 프리팹을 저작하고 다음을 한 번에 판정한다.
#   1. 직렬화 형식에 navObject/m_gameObjectType이 없고 로컬 경로가 있다.
#   2. 같은 프리팹을 두 번 소환해도 각 Navigation이 자기 인스턴스 안을 가리킨다.
#   3. UI도 다른 Entity처럼 새 instanceID를 받는다.
#   4. Canvas=Transform+Rect, UI=Rect only 공간 조합이 왕복된다.
#   5. 구 navObject 데이터가 인메모리 승격으로 계속 열린다.
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\CreatorEditor.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$scenario = Join-Path $PSScriptRoot "ui_navigation_local.txt"
$outPath = Join-Path $Work "ui_navigation_local.out"
$errPath = Join-Path $Work "ui_navigation_local.err"

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath -RedirectStandardError $errPath -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }
if ($proc.ExitCode -ne 0) {
    ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode)
    if (Test-Path $errPath) { Get-Content -LiteralPath $errPath }
    exit 1
}
if (-not (Test-Path $outPath)) { "출력이 없다: $outPath"; exit 1 }

$text = Get-Content -LiteralPath $outPath -Raw
$match = [regex]::Match($text,
    '\[ui\.navprobe\] (PASS|FAIL) schema=(PASS|FAIL) isolated=(PASS|FAIL) freshIds=(PASS|FAIL) spatial=(PASS|FAIL) legacy=(PASS|FAIL)')
if (-not $match.Success) {
    "ui.navprobe 결과가 없다 — 시나리오가 도중에 멈췄다"
    "출력: $outPath"
    exit 1
}

$labels = @('전체', '스키마', '인스턴스 격리', 'ID 재발급', '공간 조합', '구파일 승격')
for ($i = 1; $i -le 6; ++$i) { "$($labels[$i - 1]) $($match.Groups[$i].Value)" }
if ($match.Groups[1].Value -ne 'PASS') { exit 1 }

"전체 통과 — Navigation이 전역 ID 없이 프리팹 인스턴스 안에서 복원된다"
exit 0

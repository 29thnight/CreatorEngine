# 종료 순서 회귀 (층 2).
#
# 첫 프레임이 만들어지기 전에 종료를 걸어, Dx11Main::Finalize가 렌더 스레드를
# 세운 뒤에야 렌더 씬을 해체하는지 확인한다.
#
# 순서가 잘못되면(isGameToRender만 내려놓고 곧바로 Decommissioning) CB 스레드가
# 아직 커맨드를 만드는 중에 발밑에서 자료구조가 사라져 죽는다. 실측:
#   수정 전  6회 중 6회 · 5회 중 5회 · 6회 중 5회 크래시(0xC0000005)
#   수정 후  8회 중 0회
#
# 확률적이라 한 번만 돌려서는 판정이 안 된다. 기본 6회.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$Runs = 6
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

# 상대 경로로 넘기면 작업 디렉터리 기준으로 풀려 엉뚱한 곳을 본다.
$script = Join-Path $PSScriptRoot "immediate_quit.txt"
if (-not (Test-Path $script)) { "시나리오가 없다: $script"; exit 1 }

$failures = @()

for ($i = 1; $i -le $Runs; $i++) {
    $proc = Start-Process -FilePath $Exe -ArgumentList "--commandlet-script", $script `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "shutdown_order_$i.out") `
        -RedirectStandardError (Join-Path $Work "shutdown_order_$i.err") -PassThru
    $proc.WaitForExit(120000) | Out-Null

    if (-not $proc.HasExited) {
        $proc.Kill()
        $failures += "{0}회차: 타임아웃" -f $i
        continue
    }

    if ($proc.ExitCode -ne 0) {
        $failures += "{0}회차: 종료 코드 0x{1:X8}" -f $i, $proc.ExitCode
    }
}

if ($failures.Count -gt 0) {
    "실패 $($failures.Count) / $Runs"
    $failures | ForEach-Object { "  $_" }
    ""
    "종료 순서가 깨졌다. Dx11Main::Finalize에서 렌더 스레드 대기가"
    "SceneManagers->Decommissioning()보다 앞에 있는지 확인할 것."
    "덤프가 남았다면 $exeDir\Saved\Dump\ 의 최신 .txt에 스택이 있다."
    exit 1
}

"전체 통과 ($Runs 회 · 첫 프레임 전 종료)"
exit 0

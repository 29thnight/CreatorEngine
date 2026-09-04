# 기능별 테스트 씬을 하나씩 불러와 화면을 PNG로 남긴다.
#
#   pwsh Tools/featuretest/run-featuretests.ps1
#
# 저작(build-scenes.ps1)과 나눈 이유: 씬이 저장되는 것과 그 씬이 제대로 보이는
# 것은 다른 문제다. 저작은 CLI 로그로 판정되지만 렌더 결과는 눈으로 봐야 하고,
# 그래서 이쪽은 그림을 남기는 것이 목적이다.
#
# 캡처는 밖에서 창을 찍는다(capture-window.ps1). 엔진 내부 캡처는 게임 스레드에서
# 죽는 문제로 보류돼 있다 — Tools/regression/README.md에 남아 있는 그 건이다.
param(
    [string]$Exe = "",
    [string]$OutDir = "",
    [int]$TimeoutSec = 180,
    [int]$Width = 1600,
    [int]$Height = 900,
    [string[]]$Only = @(),
    [switch]$KeepFog,
    [switch]$ResizeTo
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $PSScriptRoot "screenshots"
}

if (-not (Test-Path $Exe)) {
    Write-Host "실행 파일이 없다: $Exe" -ForegroundColor Red
    exit 1
}

$sceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"
$capture = Join-Path $repoRoot "Tools\regression\capture-window.ps1"
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) "CreatorEngine_FeatureTest"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$scenes = @("FT_Primitives", "FT_Shadow", "FT_Material", "FT_Lights")
if ($Only.Count -gt 0) { $scenes = $scenes | Where-Object { $Only -contains $_ } }

$captured = @()
$failed = @()

foreach ($name in $scenes) {
    $scenePath = Join-Path $sceneDir "$name.creator"
    if (-not (Test-Path $scenePath)) {
        Write-Host "[$name] 씬이 없다 — build-scenes.ps1을 먼저 돌릴 것" -ForegroundColor Red
        $failed += $name
        continue
    }

    $png = Join-Path $OutDir "$name.png"
    if (Test-Path $png) { Remove-Item $png -Force }

    # 씬을 열고 오래 머문다. 캡처는 밖에서 하므로 엔진은 그동안 그리기만 하면
    # 되고, 캡처가 끝난 뒤 quit이 돌아 프로세스가 닫힌다.
    $cmdFile = Join-Path $workDir "run_$name.txt"
    # 기본은 창 크기를 건드리지 않는다.
    #
    # 한때 리사이즈 뒤 화면이 검게 변해(붉은 테두리까지) 캡처가 기능이 아니라
    # 그 버그를 찍고 있었다. 원인은 SSGI·블룸·비트마스크의 1/N 해상도 버퍼가
    # 화면 추종 선언에서 빠져 있던 것이었고 지금은 고쳤다.
    #
    # 그래도 기본을 '건드리지 않음'으로 두는 이유는 재현성이다 — 캡처 절차가
    # 변수를 하나 더 넣으면 결과가 그 변수 탓인지 구분하기 어려워진다.
    # 실제로 그 함정에 한 번 빠졌다. 리사이즈까지 포함해 찍으려면 -ResizeTo를 준다.
    $commands = @()
    if ($ResizeTo) {
        $commands += @("window.resize $Width $Height", "wait 20")
    }
    $commands += @(
        "scene.switch $($scenePath -replace '\\', '/')",
        "wait 180"
    )

    # 볼류메트릭 포그를 끈다.
    #
    # 프로젝트 설정에 mStrength 2 · blending 0.851로 저장돼 있어 최종 색의
    # 85%가 안개 색이다. 게임의 룩으로는 의도된 값이지만, 기능 하나를
    # 확인하려고 찍는 그림에서는 보려는 것을 덮어 버린다.
    # 설정 파일을 고치지 않고 런타임에만 끈다 — 저작물을 건드리면 다음 사람이
    # 왜 룩이 달라졌는지 모른다.
    if (-not $KeepFog) {
        # ★ 여기서 포그를 **끄지 못한다.**
        #
        #   예전에는 `render.post fog off` 를 걸었다. 그 명령은 DX11 SceneRenderer
        #   가 소비하던 구 전역 설정만 바꿨고 EnhancedRenderer 에는 전달되지
        #   않아서, DX12 로 넘어온 뒤로는 **성공처럼 출력되는 무효 명령**이었다.
        #   그 사실을 모른 채 이 줄이 남아 있었고, README 는 "캡처 전에 포그를
        #   끈다"고 적어 두었다 — 그 동안 기능 확인용 그림은 전부 포그가 최종
        #   색의 85% 를 덮은 채로 찍혔다.
        #
        #   LC6 이 그 죽은 명령을 표에서 지우면서 이 호출부도 사실에 맞춘다.
        #   런타임 포그 토글은 Enhanced PostChain 튜닝 API 가 생겨야 가능하다.
        #   없는 기능을 부르는 줄을 남겨 두면 다음 사람도 같은 것을 믿는다.
        Write-Warning ("포그를 런타임으로 끌 수 없다(Enhanced PostChain 튜닝 API 부재). " +
                       "그림에 포그가 남는다 — 이전 캡처도 마찬가지였다.")
    }

    # 캡처가 끝날 때까지 살아 있어야 한다. wait는 프레임 수라 프레임률이
    # 높으면 금방 지나간다 — 초 단위 대기(Start-Sleep)보다 넉넉히 잡는다.
    $commands += @(
        "scene.dump $name",
        "wait 20000",
        "quit"
    )
    Set-Content -Path $cmdFile -Value ($commands -join "`n") -NoNewline -Encoding UTF8

    Write-Host ""
    Write-Host "[$name] 실행 중..."

    $proc = Start-Process -FilePath $Exe -ArgumentList "--script `"$cmdFile`"" `
        -WorkingDirectory (Split-Path -Parent $Exe) -PassThru -NoNewWindow

    # 씬 로드와 첫 프레임이 안정될 때까지 기다린 뒤 찍는다. 너무 일찍 찍으면
    # 로딩 창이 그대로 남고, 그건 '렌더가 됐다'의 증거가 아니다 — 실제로
    # 한 번 로딩 화면이 찍혔다. 창 제목으로 거르려 했으나 이 창은 프로세스
    # API로 제목이 읽히지 않아, 대기 시간으로 막는다.
    Start-Sleep -Seconds 22

    $captureOutput = & pwsh $capture -OutFile $png -TimeoutSec 30 2>&1
    $captureOk = ($LASTEXITCODE -eq 0)

    try { if (-not $proc.HasExited) { $proc | Stop-Process -Force } } catch {}

    if ($captureOk -and (Test-Path $png)) {
        $bytes = (Get-Item $png).Length
        Write-Host ("  캡처: {0} ({1:N0} 바이트)" -f (Split-Path -Leaf $png), $bytes) -ForegroundColor Green
        $captured += $name
    } else {
        Write-Host "  캡처 실패: $captureOutput" -ForegroundColor Red
        $failed += $name
    }
}

Write-Host ""
Write-Host "캡처 $($captured.Count)개 · 실패 $($failed.Count)개 · 출력 $OutDir"
if ($failed.Count -gt 0) {
    Write-Host "실패: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
exit 0

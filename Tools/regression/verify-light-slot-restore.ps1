# 광원 슬롯 복원 대조 (트랙 S — Scene 부기 자료구조)
#
# ── 이 검사가 메우는 구멍 ──
#
# `LightComponent::m_lightIndex`가 직렬화되므로, 로드된 컴포넌트는 `AddLight()`가
# 아니라 `EnsureLightSlot(저장된 인덱스)`로 슬롯을 집는다. 슬롯 배열을 자라게 하는
# 책임이 전부 `Scene::EnsureLightSlot` 안에 있는데, 그 조건이 off-by-one이었다:
#
#   if (index > m_lightSlots.size() || 0 == m_lightSlots.size()) resize(index + 1);
#       ~~~~~~~~~~~~~~~~~~~~~~~ >= 여야 한다
#
# 라이트가 **하나면** 뒤쪽 `0 == size()`가 우연히 구해 준다. **둘째부터** resize가
# 돌지 않아 `m_lightSlots[1]`이 범위 밖이 된다 — `vector subscript out of range`
# (0xC0000409). 뒤쪽 조건이 결함을 가리는 역할을 했다.
#
# ── 음성 시험 (2026-08-21 실측, 고침 전 코드로 세 번) ──
#
#   라이트 1 · 저장→로드→play      정상 종료           -> 통과(결함이 안 보인다)
#   라이트 2 · 저장→로드→play      0xC0000409          -> 판정 3 실패
#   라이트 2 + 메시 12 (생명주기 A) 0xC0000409          -> 판정 3 실패
#
# 첫 줄이 이 결함이 오래 숨어 있던 이유다. 그래서 이 게이트는 라이트를 **셋** 둔다.
#
# 사용법:
#   pwsh Tools\regression\verify-light-slot-restore.ps1
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # 시나리오가 직접 만드는 씬이다. 저작 자산이 아니다(§0.05).
    [string]$SceneName = ("LightSlotProbe_" + [guid]::NewGuid().ToString("N"))
)

. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "light_slot_restore.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$scenePath = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\$SceneName.creator"

# This gate owns only a fresh generated scene. Preserve any existing project assets.
$sceneFiles = @($scenePath, ($scenePath + '.meta'))
foreach ($file in $sceneFiles) { if (Test-Path -LiteralPath $file) { throw "Gate asset already exists: $file" } }
try {
$scenario = Join-Path $Work "light_slot_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{SCENE\}\}', ($scenePath -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "light_slot.out"
$errPath = Join-Path $Work "light_slot.err"

$resultPath = Join-Path $Work "light_slot.results.jsonl"
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
$proc = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"'+$scenario+'"'), '--result-file', ('"'+$resultPath+'"')) -WindowStyle Hidden `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

if (-not (Test-Path $outPath)) { "표준 출력이 없다: $outPath"; exit 1 }
$out = Get-Content -LiteralPath $outPath

$results = @(Read-CommandResults $resultPath)
$failed = @()
try {
    Get-SucceededCommand $results 'scene.save' | Out-Null
    Get-SucceededCommand $results 'scene.switch' | Out-Null
    $hierarchy = Get-SucceededCommand $results 'scene.hierarchycheck'
    if ($hierarchy.objects -lt 5) { $failed += "Restored object coverage missing: $($hierarchy.objects)" }
    foreach ($axis in @('pairMismatch','orphan','unreachable','storeMismatch')) {
        if ($hierarchy.$axis -ne 0) { $failed += "Hierarchy $axis=$($hierarchy.$axis)" }
    }
} catch { $failed += $_.Exception.Message }

# 판정 3 — ★ 본 판정.
"종료 코드  : 0x{0:X8}  ← 본 판정" -f $proc.ExitCode
""
if ($proc.ExitCode -ne 0) {
    $failed += ("종료 코드 비정상: 0x{0:X8} — 광원 슬롯이 복원되지 않았다(Scene::EnsureLightSlot의 범위 조건 확인). 0xC0000409는 vector 첨자 초과의 __fastfail이다" -f $proc.ExitCode)
}

if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  $_" }
    "출력: $outPath"
    exit 1
}

"전체 통과 — 라이트 3개짜리 씬이 저장·재로드·재생을 건넜다"
exit 0

} finally {
    foreach ($file in $sceneFiles) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file } }
}

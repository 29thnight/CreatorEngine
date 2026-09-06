param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$scenePath = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\E5AiRegistryDest.creator"
if (Test-Path $scenePath) { Remove-Item -LiteralPath $scenePath -Force }

$scenario = Join-Path $Work "ai_registry_resolved.txt"
(Get-Content (Join-Path $PSScriptRoot "ai_registry_probe.txt") -Raw) `
    -replace '\{\{SCENE_DEST\}\}', ($scenePath -replace '\\', '/') |
    Set-Content $scenario -Encoding UTF8

$outPath = Join-Path $Work "ai_registry.out"
$errPath = Join-Path $Work "ai_registry.err"
$proc = Start-Process -FilePath $Exe -ArgumentList "--commandlet-script", $scenario `
    -WorkingDirectory $exeDir -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

$lines = @(Get-Content $outPath | Where-Object { $_ -match '\[AI 레지스트리\]' })
$failed = @()
if ($lines.Count -ne 3) { $failed += "status 줄 $($lines.Count)개 (기대 3)" }
if ($lines.Count -ge 1 -and $lines[0] -notmatch 'object=E5Ai registered=1 total=1') {
    $failed += "최초 등록 실패: $($lines[0])"
}
if ($lines.Count -ge 2 -and $lines[1] -notmatch 'object=E5Ai registered=1 total=1') {
    $failed += "DDOL 재등록 실패: $($lines[1])"
}
if ($lines.Count -ge 3 -and $lines[2] -notmatch 'total=0') {
    $failed += "파괴 후 해지 실패: $($lines[2])"
}
if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

$lines
if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"; $failed | ForEach-Object { "  $_" }; exit 1
}
"전체 통과 — AI registry가 최초 등록, DDOL Scene handle 재등록, 파괴 해지를 지킨다"
exit 0

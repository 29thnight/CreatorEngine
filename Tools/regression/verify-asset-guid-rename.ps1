param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

if (-not (Test-Path -LiteralPath $Exe)) {
    "실행 파일이 없다: $Exe"
    exit 1
}

$run = Join-Path $Work ("CE_D2GuidRename_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run "commands.txt"
$stdout = Join-Path $run "stdout.txt"
$stderr = Join-Path $run "stderr.txt"
@(
    "asset.guid.rename.probe"
    "wait 10"
    "quit"
) | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory ([System.IO.Path]::GetDirectoryName($Exe)) `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout -Raw
} else { "" }
$errorBytes = if (Test-Path -LiteralPath $stderr) {
    (Get-Item -LiteralPath $stderr).Length
} else { 0 }
$match = [regex]::Match($text,
    '\[asset\.guid\.rename\] pass guid=([0-9a-f-]{36}) save=yes move=yes identity=yes material-roundtrip=yes cleanup=yes')

"asset-guid-rename exit=$($process.ExitCode) stderr=$errorBytes output=$run"
if ($match.Success) { "guid=$($match.Groups[1].Value)" }
if ($process.ExitCode -ne 0 -or $errorBytes -ne 0 -or -not $match.Success) {
    if (-not $match.Success) { "통과 표식이 없다" }
    exit 1
}

"전체 통과 — UUIDv4 material authoring과 target/meta rename이 같은 정체성을 유지했다"
exit 0

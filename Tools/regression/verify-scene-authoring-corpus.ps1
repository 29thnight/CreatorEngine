param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sceneRoot = Join-Path $root 'Dynamic_CPP\Assets\Scenes'
$scenes = @(Get-ChildItem -LiteralPath $sceneRoot -File -Filter '*.creator' |
    Sort-Object Name)

# D2-d의 전수 범위가 조용히 줄어드는 것을 막는 ratchet이다. 씬을 의도적으로
# 추가하거나 제거했다면 숫자와 계획의 자산 목록을 함께 갱신해야 한다.
$expectedSceneCount = 14
if ($scenes.Count -ne $expectedSceneCount) {
    "씬 코퍼스가 예상과 다르다: expected=$expectedSceneCount actual=$($scenes.Count)"
    $scenes.Name
    exit 1
}

$sourceHashes = @{}
foreach ($scene in $scenes) {
    $sourceHashes[$scene.FullName] = (Get-FileHash -LiteralPath $scene.FullName -Algorithm SHA256).Hash
}

# 저장 대상은 저장소 밖의 고유 임시 트리다. 원본은 입력으로만 열고 pass1/pass2의
# 동명 파일끼리 비교하므로, 저장 경로에서 유래하는 씬 루트 이름 차이도 배제한다.
$run = Join-Path $Work ("CE_D2SceneCorpus_" + [guid]::NewGuid().ToString('N'))
$pass1 = Join-Path $run 'pass1'
$pass2 = Join-Path $run 'pass2'
New-Item -ItemType Directory -Path $pass1 -Force | Out-Null
New-Item -ItemType Directory -Path $pass2 -Force | Out-Null

$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$commands = [System.Collections.Generic.List[string]]::new()
foreach ($scene in $scenes) {
    $first = Join-Path $pass1 $scene.Name
    $second = Join-Path $pass2 $scene.Name
    $commands.Add("scene.switch $($scene.FullName.Replace('\', '/'))")
    $commands.Add('wait 4')
    $commands.Add("scene.save $($first.Replace('\', '/'))")
    $commands.Add("scene.switch $($first.Replace('\', '/'))")
    $commands.Add('wait 4')
    $commands.Add("scene.save $($second.Replace('\', '/'))")
}
$commands.Add('quit')
$commands | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout -Raw
} else { '' }
$errorLines = if (Test-Path -LiteralPath $stderr) {
    @(Get-Content -LiteralPath $stderr)
} else { @() }
$knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
$unexpectedErrors = @($errorLines | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning
})
$lodWarnings = @($errorLines | Where-Object { $_ -eq $knownLodWarning }).Count

$expectedOperations = $expectedSceneCount * 2
$loadSuccesses = ([regex]::Matches($text, '\[CLI\] LoadScene 반환: 성공')).Count
$saveSuccesses = ([regex]::Matches($text, '\[CLI\] 씬 저장:')).Count
$reportedFailures = @([regex]::Matches($text,
    '\[CLI\] (?:씬 로드 실패|씬 저장 실패|LoadScene 반환: 널)'))

$missing = [System.Collections.Generic.List[string]]::new()
$roundTripMismatches = [System.Collections.Generic.List[string]]::new()
foreach ($scene in $scenes) {
    $first = Join-Path $pass1 $scene.Name
    $second = Join-Path $pass2 $scene.Name
    if (-not (Test-Path -LiteralPath $first -PathType Leaf)) {
        $missing.Add("pass1/$($scene.Name)")
        continue
    }
    if (-not (Test-Path -LiteralPath $second -PathType Leaf)) {
        $missing.Add("pass2/$($scene.Name)")
        continue
    }

    $firstHash = (Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash
    $secondHash = (Get-FileHash -LiteralPath $second -Algorithm SHA256).Hash
    if ($firstHash -ne $secondHash) {
        $roundTripMismatches.Add($scene.Name)
    }
}

$sourceMutations = [System.Collections.Generic.List[string]]::new()
foreach ($scene in $scenes) {
    if (-not (Test-Path -LiteralPath $scene.FullName -PathType Leaf)) {
        $sourceMutations.Add("삭제됨: $($scene.Name)")
        continue
    }
    $afterHash = (Get-FileHash -LiteralPath $scene.FullName -Algorithm SHA256).Hash
    if ($afterHash -ne $sourceHashes[$scene.FullName]) {
        $sourceMutations.Add("변경됨: $($scene.Name)")
    }
}

"scene-authoring-corpus exit=$($process.ExitCode) output=$run"
"scenes=$($scenes.Count) loads=$loadSuccesses/$expectedOperations saves=$saveSuccesses/$expectedOperations missing=$($missing.Count) unstable=$($roundTripMismatches.Count) sourceMutations=$($sourceMutations.Count) lodWarnings=$lodWarnings unexpectedStderr=$($unexpectedErrors.Count)"

$passed = 0 -eq $process.ExitCode -and
    $loadSuccesses -eq $expectedOperations -and
    $saveSuccesses -eq $expectedOperations -and
    $reportedFailures.Count -eq 0 -and
    $missing.Count -eq 0 -and
    $roundTripMismatches.Count -eq 0 -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedErrors.Count -eq 0
if (-not $passed) {
    if ($reportedFailures.Count -gt 0) { "CLI 보고 실패=$($reportedFailures.Count)" }
    if ($missing.Count -gt 0) { '누락 파일:'; $missing }
    if ($roundTripMismatches.Count -gt 0) { '1차/2차 저장 불일치:'; $roundTripMismatches }
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    if ($unexpectedErrors.Count -gt 0) { '예상하지 않은 stderr:'; $unexpectedErrors }
    exit 1
}

'전체 통과 — 14개 저작 씬이 원본 불변 상태로 load→save→reload→save 바이트 안정성을 만족했다'
exit 0

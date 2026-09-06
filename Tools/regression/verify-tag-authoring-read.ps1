param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

# SerializationPlan D3-b-L — TagManager **읽기 경로**.
#
# ★ 왜 새로 만들었나. `TagManager::Load`를 ryml로 옮긴 뒤 변이를 넣어 봤더니
#   **어떤 게이트도 잡지 못했다** — scene 코퍼스·prefab·golden·play 왕복·
#   asset-authoring-ownership이 전부 초록이었다. 기존 `tag.authoring.probe`는
#   Add/Has/Remove 즉 **메모리 조작만** 재고 디스크에서 읽은 결과는 보지 않았다.
#
#   초록인 게이트가 여럿 있다는 것이 그 경로가 지켜진다는 뜻은 아니다. 옮긴 코드가
#   실제로 검사되는지는 **변이로 확인해야** 알 수 있고, 확인해 보니 아니었다.
#
# ★ 판정은 자산 파일과의 대조다. CLI가 읽어 낸 태그·레이어 **이름 집합**이
#   `ProjectSetting/TagManager.asset`의 내용과 일치해야 한다 — 개수만 세면
#   "다른 것을 같은 수만큼 읽은" 경우를 통과시킨다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assetPath = Join-Path $root 'Dynamic_CPP\ProjectSetting\TagManager.asset'
if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
    "TagManager.asset이 없다: $assetPath"
    exit 1
}

# 자산에서 기대값을 읽는다. 두 개의 단순 시퀀스라 줄 단위 스캔으로 충분하다.
$lines = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($assetPath)) -split "`r?`n"
$expectedTags = New-Object System.Collections.Generic.List[string]
$expectedLayers = New-Object System.Collections.Generic.List[string]
$section = ''
foreach ($line in $lines) {
    if ($line -match '^\s*tags\s*:\s*$')   { $section = 'tags';   continue }
    if ($line -match '^\s*layers\s*:\s*$') { $section = 'layers'; continue }
    if ($line -match '^\s*-\s*(.+?)\s*$') {
        $value = $Matches[1].Trim().Trim('"').Trim("'")
        if ($section -eq 'tags')   { $expectedTags.Add($value) }
        if ($section -eq 'layers') { $expectedLayers.Add($value) }
        continue
    }
    # 시퀀스가 아닌 줄을 만나면 섹션이 끝난 것이다.
    if ($line -match '^\S') { $section = '' }
}

# ★ **이 게이트는 저작 자산을 오염시킬 수 있다.** 에디터는 종료 시 TagManager를
#   저장한다 — 읽기가 깨진 빌드로 돌리면 빈 상태가 디스크를 덮는다. 실제로 변이
#   실험 중에 이 경로로 태그 목록을 통째로 잃었다(그리고 그 사실을 몇 번의 실행이
#   지난 뒤에야 알아챘다 — 게이트가 자기가 먹은 것을 보고하지 않았기 때문이다).
#
#   그래서 원본을 미리 떠 두고, 끝난 뒤 **바이트 단위로 되돌리고 대조**한다.
$assetBackup = [IO.File]::ReadAllBytes($assetPath)

$run = Join-Path $Work ("CE_D3bLTag_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$resultPath = Join-Path $run 'results.jsonl'
@('tag.list', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario, '--result-format', 'jsonl', '--result-file', $resultPath) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) { $process.Kill(); "TIMEOUT output=$run"; exit 1 }

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
$failures = New-Object System.Collections.Generic.List[string]

$record = if (Test-Path -LiteralPath $resultPath) {
    Get-Content -LiteralPath $resultPath | ConvertFrom-Json | Where-Object command -eq 'tag.list' | Select-Object -First 1
} else { $null }
$actualTags = @(); $actualLayers = @()
if ($null -eq $record -or $record.status -ne 'succeeded' -or $process.ExitCode -ne 0) {
    $failures.Add('tag.list did not return a successful JSON result')
} else {
    $actualTags = @($record.data.tags)
    $actualLayers = @($record.data.layers)
}
# 0개를 읽고 "일치"로 읽지 않는다.
#
# ★ 이 저장소의 자산은 실제로 `tags: []`다(레이어만 16개). 그래서 "태그가 0이면
#   실패"로 쓰면 정상 상태에서 빨개진다 — 대신 **둘의 합**이 0이면 실패로 본다.
#   빈 집합을 성공으로 읽지 않으면서 실제 데이터에도 맞는 조건이다.
if (($expectedTags.Count + $expectedLayers.Count) -eq 0) {
    $failures.Add('자산에서 기대값을 하나도 못 읽었다 — 게이트가 잴 것이 없다')
}
if (($actualTags.Count + $actualLayers.Count) -eq 0) {
    $failures.Add('CLI가 태그·레이어를 하나도 읽지 못했다')
}

$missingTags = @($expectedTags | Where-Object { $actualTags -notcontains $_ })
$extraTags = @($actualTags | Where-Object { $expectedTags -notcontains $_ })
$missingLayers = @($expectedLayers | Where-Object { $actualLayers -notcontains $_ })
$extraLayers = @($actualLayers | Where-Object { $expectedLayers -notcontains $_ })
if ($missingTags.Count -gt 0) { $failures.Add("읽지 못한 태그: $($missingTags -join ', ')") }
if ($extraTags.Count -gt 0) { $failures.Add("자산에 없는 태그를 읽었다: $($extraTags -join ', ')") }
if ($missingLayers.Count -gt 0) { $failures.Add("읽지 못한 레이어: $($missingLayers -join ', ')") }
if ($extraLayers.Count -gt 0) { $failures.Add("자산에 없는 레이어를 읽었다: $($extraLayers -join ', ')") }

# 자산 복원 + 오염 판정. 실패 여부와 무관하게 항상 되돌린다.
$assetAfter = [IO.File]::ReadAllBytes($assetPath)
$mutated = -not [Linq.Enumerable]::SequenceEqual([byte[]]$assetBackup, [byte[]]$assetAfter)
if ($mutated) {
    [IO.File]::WriteAllBytes($assetPath, $assetBackup)
    $failures.Add('실행이 TagManager.asset을 변경했다 — 복원했지만 읽기 경로가 깨져 있다는 뜻이다')
}

"expectedTags=$($expectedTags.Count) actualTags=$($actualTags.Count) expectedLayers=$($expectedLayers.Count) actualLayers=$($actualLayers.Count)"

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    "출력: $run"
    exit 1
}

Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
'전체 통과 — TagManager가 자산의 태그·레이어를 그대로 읽고, 자산은 변하지 않았다'
exit 0

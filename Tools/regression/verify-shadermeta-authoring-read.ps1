param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

# SerializationPlan D3-b-L — ShaderMeta **읽기 경로**.
#
# ★ 왜 새로 만들었나. 이 파서의 계약은 `dx12.selftest`(EnhancedSceneRendererSelfTest의
#   `ValidateShaderMeta`) 안에만 있다. 그런데 그것은 회귀 세트(run-all)에 **없고**,
#   자기 하네스인 `Tools/dx12-validation/Invoke-DX12Validation.ps1`은 vcpkg baseline
#   preflight에 막혀 지금 이 기계에서 돌지 않는다.
#
#   변이로 확인했다 — `ValidateMap`의 unknown-field 거부를 `if (false && !known)`로
#   무력화하고 다시 빌드했더니:
#     · `dx12.selftest`                       → 실패(잡는다)
#     · `verify-experiment-asset-cooker`      → 통과(눈멀다)
#   즉 **정기적으로 도는 게이트 중 이 경로를 지키는 것이 없다.** ryml 이식 전에
#   자를 먼저 세운다 — TagManager에서 이 순서를 놓쳐 저작 자산을 통째로 잃었다.
#
# ★ 두 방향을 잰다.
#     수용: 실자산 6개가 파싱되고, 읽어 낸 property·axis·pass **이름 집합**이
#           자산 텍스트에서 유도한 집합과 일치한다.
#     거절: 잘못된 문서 6종이 **각자의 사유로** 거부된다.
#   저작 코퍼스는 전부 유효하므로 수용만 재면 "무엇이든 통과시키는 파서"가 만점을
#   받는다 — backend 교체에서 가장 흔한 실패가 그 방향이다.
#
# ★ 개수가 아니라 집합을 비교한다. 개수만 세면 "다른 것을 같은 수만큼 읽은" 경우가
#   통과한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = (Resolve-Path (Join-Path $root 'Dynamic_CPP\Assets')).Path

$metaFiles = @(Get-ChildItem -LiteralPath $assets -Recurse -Filter '*.shadermeta' -File |
    Sort-Object FullName)
if ($metaFiles.Count -eq 0) {
    '.shadermeta 자산을 하나도 찾지 못했다 — 게이트가 잴 것이 없다'
    exit 1
}

# ── 자산에서 기대값을 유도한다 ────────────────────────────────────────────────
#
# 두 번째 파서를 만드는 것이 아니다. 여기서 뽑는 것은 **이름뿐**이고, 타입·기본값·
# 상태 해석은 CLI 쪽 출력에 맡긴다. 이름은 한 줄 안에 있어 줄 스캔으로 충분하다.
$expected = @{}
foreach ($file in $metaFiles) {
    $relative = ([IO.Path]::GetRelativePath($assets, $file.FullName)) -replace '\\', '/'
    $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName))
    $lines = $text -split "`r?`n"

    $name = ''
    $source = ''
    $props = New-Object System.Collections.Generic.List[string]
    $axes = New-Object System.Collections.Generic.List[string]
    $passes = New-Object System.Collections.Generic.List[string]
    $section = ''
    foreach ($line in $lines) {
        if ($line -match '^name\s*:\s*(\S+)\s*$')   { $name = $Matches[1]; $section = ''; continue }
        if ($line -match '^source\s*:\s*(\S+)\s*$') { $source = $Matches[1]; $section = ''; continue }
        if ($line -match '^properties\s*:\s*$')     { $section = 'properties'; continue }
        if ($line -match '^keywords\s*:\s*$')       { $section = 'keywords'; continue }
        if ($line -match '^passes\s*:\s*$')         { $section = 'passes'; continue }
        if ($line -match '^\S') { $section = ''; continue }

        switch ($section) {
            'properties' { if ($line -match '^\s*-\s*\{\s*name\s*:\s*([A-Za-z0-9_]+)') { $props.Add($Matches[1]) } }
            'keywords'   { if ($line -match '^\s*-\s*\{\s*axis\s*:\s*([A-Za-z0-9_]+)') { $axes.Add($Matches[1]) } }
            'passes'     { if ($line -match '^\s*-\s*(?:\{\s*)?name\s*:\s*([A-Za-z0-9_]+)') { $passes.Add($Matches[1]) } }
        }
    }
    $expected[$relative] = [pscustomobject]@{
        Name = $name; Source = $source
        Props = $props; Axes = $axes; Passes = $passes
    }
}

# ★ 저작 자산은 건드리지 않아야 한다. 에디터는 종료 시 여러 저작 상태를 저장하므로
#   읽기가 깨진 빌드가 코퍼스를 덮을 수 있다 — TagManager에서 실제로 그렇게 잃었다.
$snapshot = @{}
foreach ($file in $metaFiles) { $snapshot[$file.FullName] = [IO.File]::ReadAllBytes($file.FullName) }

$run = Join-Path $Work ("CE_D3bLShaderMeta_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('shadermeta.probe', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) { $process.Kill(); "TIMEOUT output=$run"; exit 1 }

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
$failures = New-Object System.Collections.Generic.List[string]

# ── 요약 ──────────────────────────────────────────────────────────────────────
$summary = [regex]::Match($text, 'files=(\d+) parsed=(\d+) rejectCases=(\d+) rejected=(\d+)')
if (-not $summary.Success) {
    $failures.Add('요약 라인이 없다 — 명령 미등록/낡은 exe이거나 프로세스가 죽었다')
    $files = 0; $parsed = 0; $rejectCases = 0; $rejected = 0
} else {
    $files = [int]$summary.Groups[1].Value
    $parsed = [int]$summary.Groups[2].Value
    $rejectCases = [int]$summary.Groups[3].Value
    $rejected = [int]$summary.Groups[4].Value
}

if ($files -ne $metaFiles.Count) {
    $failures.Add("CLI가 찾은 파일 수가 다르다: cli=$files disk=$($metaFiles.Count)")
}
if ($parsed -ne $metaFiles.Count) {
    $failures.Add("파싱에 성공한 파일이 전부가 아니다: parsed=$parsed / $($metaFiles.Count)")
}
# 0건을 "차이 없음"으로 읽지 않는다.
if ($rejectCases -eq 0) { $failures.Add('거절 사례가 0건이다 — 느슨해지는 이식을 못 잡는다') }
if ($rejected -ne $rejectCases) {
    $failures.Add("거절되지 않았거나 사유가 다른 사례가 있다: rejected=$rejected / $rejectCases")
}
foreach ($match in [regex]::Matches($text, '(?m)^\[shadermeta\.probe\] reject=(\S+) accepted=(\d) reasonMatched=(\d)')) {
    if ($match.Groups[2].Value -ne '0' -or $match.Groups[3].Value -ne '1') {
        $failures.Add("거절 계약 위반: $($match.Groups[1].Value) accepted=$($match.Groups[2].Value) reasonMatched=$($match.Groups[3].Value)")
    }
}

# ── 파일별 수용 대조 ──────────────────────────────────────────────────────────
function Compare-Set([string]$label, [string]$file, $expectedItems, $actualItems, $sink) {
    $missing = @($expectedItems | Where-Object { $actualItems -notcontains $_ })
    $extra = @($actualItems | Where-Object { $expectedItems -notcontains $_ })
    if ($missing.Count -gt 0) { $sink.Add("$file : 읽지 못한 $label -> $($missing -join ', ')") }
    if ($extra.Count -gt 0) { $sink.Add("$file : 자산에 없는 $label 을 읽었다 -> $($extra -join ', ')") }
}

$seen = New-Object System.Collections.Generic.List[string]
foreach ($match in [regex]::Matches($text,
    '(?m)^\[shadermeta\.probe\] file=(\S+) ok=(\d) guidFromCatalog=(\d) name=(\S+) source=(\S+) props=(\d+) keywords=(\d+) passes=(\d+)')) {
    $relative = $match.Groups[1].Value
    $seen.Add($relative)
    if (-not $expected.ContainsKey($relative)) {
        $failures.Add("자산 목록에 없는 파일을 읽었다: $relative")
        continue
    }
    $want = $expected[$relative]
    if ($match.Groups[2].Value -ne '1') {
        $failures.Add("$relative : 파싱 실패")
        continue
    }
    # sidecar 없이 통과한 것을 감추지 않는다 — 실자산은 카탈로그가 GUID를 알아야 한다.
    if ($match.Groups[3].Value -ne '1') {
        $failures.Add("$relative : 카탈로그 GUID가 없다(sentinel로 파싱됨)")
    }
    if ($match.Groups[4].Value -ne $want.Name) {
        $failures.Add("$relative : name 불일치 cli=$($match.Groups[4].Value) asset=$($want.Name)")
    }
    if ($match.Groups[5].Value -ne $want.Source) {
        $failures.Add("$relative : source 불일치 cli=$($match.Groups[5].Value) asset=$($want.Source)")
    }

    $actualProps = @([regex]::Matches($text, "(?m)^\[shadermeta\.probe\] prop=$([regex]::Escape($relative))\|([^|]+)\|") |
        ForEach-Object { $_.Groups[1].Value })
    $actualAxes = @([regex]::Matches($text, "(?m)^\[shadermeta\.probe\] axis=$([regex]::Escape($relative))\|([^|]+)\|") |
        ForEach-Object { $_.Groups[1].Value })
    $actualPasses = @([regex]::Matches($text, "(?m)^\[shadermeta\.probe\] pass=$([regex]::Escape($relative))\|([^|]+)\|") |
        ForEach-Object { $_.Groups[1].Value })

    Compare-Set 'property' $relative $want.Props $actualProps $failures
    Compare-Set 'keyword axis' $relative $want.Axes $actualAxes $failures
    Compare-Set 'pass' $relative $want.Passes $actualPasses $failures

    # 유도한 이름이 하나도 없으면 대조가 공집합끼리 맞은 것이다.
    if ($want.Props.Count -eq 0 -and $want.Passes.Count -eq 0) {
        $failures.Add("$relative : 자산에서 유도한 이름이 0개다 — 대조가 아무것도 검증하지 않았다")
    }
}
foreach ($key in $expected.Keys) {
    if ($seen -notcontains $key) { $failures.Add("CLI가 읽지 않은 자산: $key") }
}

# ── 자산 무변경 판정 ──────────────────────────────────────────────────────────
foreach ($file in $metaFiles) {
    $after = [IO.File]::ReadAllBytes($file.FullName)
    if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$snapshot[$file.FullName], [byte[]]$after)) {
        [IO.File]::WriteAllBytes($file.FullName, $snapshot[$file.FullName])
        $failures.Add("실행이 $($file.Name)을 변경했다 — 복원했지만 경로가 깨져 있다는 뜻이다")
    }
}

"files=$files parsed=$parsed rejectCases=$rejectCases rejected=$rejected diskFiles=$($metaFiles.Count)"

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    "출력: $run"
    exit 1
}

Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
'전체 통과 — ShaderMeta가 실자산을 그대로 읽고 잘못된 문서를 각자의 사유로 거부했다'
exit 0

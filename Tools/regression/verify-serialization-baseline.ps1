param(
    # ★ 기본값이 Release다. 이 게이트만 다른 게이트와 다른 이유:
    #   Debug는 같은 조건에서 단계별로 4~16배 느리고 **단계 간 비중까지 뒤집는다**
    #   (D0 실측: SceneParse는 Debug/Release 15.9배, ComponentLoad는 5.5배 — Debug로
    #   보면 파싱 비중을 67%로, Release로 보면 58%로 읽는다). 성능 기준선을 Debug exe로
    #   재는 것은 오독이 아니라 오답이다.
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [int]$SceneIterations = 5,
    [int]$PrefabIterations = 10,
    # 계약 검사만 하고 수치는 참고로만 찍는다(기본). -Baseline을 주면 기준선 표 형식으로
    # 출력해 계획서에 옮겨 적기 좋게 한다.
    [switch]$Baseline
)

# ★ 이 게이트는 **수치 회귀를 판정하지 않는다.** 같은 exe·같은 코퍼스로 세 번 돌린
#   결과가 SceneLoadTotal ±7%, 부팅 catalog ±18%까지 흔들린다(D0 실측). 그 폭 안에서
#   문턱을 걸면 게이트가 무작위로 빨개지고, 그러면 세트 전체가 무시되기 시작한다.
#   여기서 지키는 것은 **계측 계약**이다 — Release로 쟀는가, 모든 단계가 실제로
#   불렸는가, 분해 합이 루트를 넘지 않는가, 부팅이 `.meta`를 0개가 아니라 읽었는가.
#   수치 비교는 `-Baseline` 출력을 사람이 계획서 표와 대조해서 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    "  (Release 미빌드일 수 있다. Debug로 대체하지 않는다 — 성능 기준선은 Release로만 판정한다.)"
    exit 1
}

# Start-Process는 정규화되지 않은 경로(`Tools\regression\..\..\Bin\...`)를 받으면
# ERROR_FILE_CORRUPT로 실패한다 — 존재 검사만 통과하고 실행에서 무너진다.
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# 계측 대상은 저작 코퍼스의 최대 씬과 실제 draw가 걸린 씬 둘이다. 하나만 재면
# "큰 숫자가 좁은 커버리지를 가리는" 형태가 된다.
$scenes = @(
    (Join-Path $root 'Dynamic_CPP\Assets\Scenes\Test1.creator'),
    (Join-Path $root 'Dynamic_CPP\Assets\Scenes\FT_Primitives.creator')
)
foreach ($s in $scenes) {
    if (-not (Test-Path -LiteralPath $s -PathType Leaf)) { "씬이 없다: $s"; exit 1 }
}

# 현존 최대 프리팹. §1.7 ⑥ — 계획서가 상정한 462KB 프리팹은 이 저장소에 없다.
$prefab = 'NestedProbeParent'

$run = Join-Path $Work ("CE_D0Serialization_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'

$commands = New-Object System.Collections.Generic.List[string]
$commands.Add('serialize.bench boot')
foreach ($s in $scenes) {
    $commands.Add("serialize.bench scene $($s.Replace('\', '/')) $SceneIterations")
}
$commands.Add("serialize.bench prefab $prefab $PrefabIterations")
$commands.Add('quit')
$commands | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }

# 다른 게이트와 같은 알려진 경고만 허용한다.
$knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
$errorLines = if (Test-Path -LiteralPath $stderr) { @(Get-Content -LiteralPath $stderr) } else { @() }
$unexpectedErrors = @($errorLines | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning
})

$failures = New-Object System.Collections.Generic.List[string]

# ── 계약 1: Release exe로 쟀는가 ──────────────────────────────────────────────
$configs = @([regex]::Matches($text, '\[serialize\.bench\] config=(\w+)') | ForEach-Object { $_.Groups[1].Value })
if ($configs.Count -eq 0) {
    $failures.Add('config 라인이 없다 — serialize.bench가 등록되지 않았거나 명령이 실행되지 않았다')
} elseif (@($configs | Where-Object { $_ -ne 'Release' }).Count -gt 0) {
    $failures.Add("Release가 아닌 구성으로 측정됐다: $([string]::Join(',', ($configs | Sort-Object -Unique)))")
}

# ── 계약 2: selfcheck 전건 통과 ───────────────────────────────────────────────
$selfchecks = @([regex]::Matches($text, '\[serialize\.bench\] mode=(\w+) selfcheck=(\w+)(?: reason=([\w-]+))?') |
    ForEach-Object {
        [pscustomobject]@{
            Mode   = $_.Groups[1].Value
            Result = $_.Groups[2].Value
            Reason = $_.Groups[3].Value
        }
    })
$expectedSelfcheckCount = 1 + $scenes.Count + 1   # boot + scene들 + prefab
if ($selfchecks.Count -ne $expectedSelfcheckCount) {
    # 검사가 덜 돌았는데 "실패 0"으로 읽히는 형태를 막는다.
    $failures.Add("selfcheck 라인이 $($selfchecks.Count)개다 — $expectedSelfcheckCount 개를 기대했다")
}
foreach ($sc in $selfchecks) {
    if ($sc.Result -ne 'pass') {
        $failures.Add("selfcheck 실패: mode=$($sc.Mode) reason=$($sc.Reason)")
    }
}

# ── 계약 3: 부팅 catalog가 실제로 `.meta`를 파싱했는가 ────────────────────────
$bootMatch = [regex]::Match($text,
    '\[serialize\.bench\] mode=boot stage=AssetCatalog totalMs=([\d.]+) parsedMeta=(\d+)')
if (-not $bootMatch.Success) {
    $failures.Add('boot catalog 라인을 찾지 못했다')
} else {
    $parsedMeta = [int]$bootMatch.Groups[2].Value
    if ($parsedMeta -le 0) { $failures.Add('부팅 catalog가 .meta를 0개 파싱했다 — 잴 것이 없다') }
}

# ── 계약 4: 씬 단계 분해가 성립하는가 ─────────────────────────────────────────
$sceneBlocks = @([regex]::Matches($text,
    '\[serialize\.bench\] mode=scene target=(?<target>\S+) iterations=(?<iters>\d+) loaded=(?<loaded>\d+) warmup=\d+'))
if ($sceneBlocks.Count -ne $scenes.Count) {
    $failures.Add("씬 측정 블록이 $($sceneBlocks.Count)개다 — $($scenes.Count)개를 기대했다")
}

$ratioMatches = @([regex]::Matches($text,
    '\[serialize\.bench\] mode=scene rootUs=(?<root>[\d.]+) childSumUs=(?<child>[\d.]+) unattributedUs=(?<un>-?[\d.]+) childRatio=(?<ratio>[\d.]+)'))
foreach ($m in $ratioMatches) {
    $rootUs = [double]$m.Groups['root'].Value
    $childUs = [double]$m.Groups['child'].Value
    if ($rootUs -le 0) { $failures.Add('rootUs가 0 이하다 — 계측이 끊겼다') }
    if ($childUs -gt $rootUs) { $failures.Add('분해 합이 루트를 넘는다 — 계측 자체가 틀렸다') }
}

# ── 계약 5: 예상 밖 stderr 0 ──────────────────────────────────────────────────
if ($unexpectedErrors.Count -gt 0) {
    $failures.Add("예상 밖 stderr $($unexpectedErrors.Count)줄: $($unexpectedErrors[0])")
}

# ── 보고 ──────────────────────────────────────────────────────────────────────
"exe=$Exe"
"exeModified=$((Get-Item -LiteralPath $Exe).LastWriteTime.ToString('s'))"
"output=$run"
"selfchecks=$($selfchecks.Count) pass=$(@($selfchecks | Where-Object { $_.Result -eq 'pass' }).Count)"
"knownLodWarnings=$(@($errorLines | Where-Object { $_ -eq $knownLodWarning }).Count) unexpectedStderr=$($unexpectedErrors.Count)"

if ($Baseline) {
    ''
    '--- D0 기준선 (이 출력을 SerializationPlan에 옮긴다) ---'
    @([regex]::Matches($text, '\[serialize\.bench\][^\r\n]*')) | ForEach-Object { $_.Value }
}

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'PASS'
exit 0

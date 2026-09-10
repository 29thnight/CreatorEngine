[CmdletBinding()]
param(
    # 비우면 `Build\Staging\Dynamic_CPP.current.json`이 가리키는 Release stage를 쓴다.
    [string]$Stage = '',

    # ★ 한 번만 재지 않는다. D0 기준선(SerializationPlan D0 절)은 같은 exe·같은
    #   코퍼스로 프로세스 3회를 돌려 SceneLoadTotal ±7%, 부팅 catalog ±18%가
    #   흔들리는 것을 실측했다. 그 폭 안에서 한 번 잰 값을 표에 옮기면 후속 판정이
    #   노이즈와 구분되지 않는다. 기본 3회, 평균과 범위를 함께 낸다.
    [ValidateRange(1, 20)]
    [int]$Runs = 3,

    [ValidateRange(1, 1000)]
    [int]$SmokeFrames = 60,

    [ValidateRange(10, 600)]
    [int]$TimeoutSeconds = 180,

    # §5 완료 기준 2의 문턱. 0이면 판정하지 않고 수치만 낸다(기본) — 세트에 넣을 때는
    # 문턱을 걸지 않는다. D0 게이트가 수치 회귀를 판정하지 않는 것과 같은 이유다:
    # 변동폭 안에서 문턱을 걸면 게이트가 무작위로 빨개지고 세트 전체가 무시된다.
    # 수용 판정은 사람이 `-MaxSceneLoadMs`/`-MaxBootMs`를 D0 표에서 유도해 한 번 건다.
    [double]$MaxSceneLoadMs = 0,
    [double]$MaxBootMs = 0
)

# SerializationPlan D5-d · §5 완료 기준 2 — 쿠킹 경로를 도는 Release Player의 씬 로드를
# D0과 **같은 단계 이름**으로 잰다.
#
# ★ D0 기준선은 Editor `serialize.bench`가 **저작 경로**(텍스트 → ryml → 리플렉션)에서
#   잰 값이고, §5 기준 2는 **Player 쿠킹 경로**(CEDO → 리플렉션)의 값을 그 표와 맞대라고
#   한다. 두 경로는 exe도 입력 포맷도 다르므로 여기서 재는 것은 "같은 씬을 여는 데 드는
#   Player 실측"이지 Editor 수치의 재현이 아니다. 축이 다른 것을 같은 표에 놓을 때는
#   그 사실을 같이 적는다(budget-axis-mismatch 전례).
#
# ★ Player는 시작 씬을 **한 번** 연다(워밍업 없는 cold load 1회). D0의 "워밍업 1회 후
#   5회 평균"보다 불리한 조건이므로, 이 값이 D0 평균 아래면 조건 차이가 판정을
#   흔들지 않는다.
#
# 계약(수치와 무관하게 항상 단정):
#   1. `[runtime.serialization] enabled=yes` — 계측이 켜진 채 잰 값이다.
#   2. SceneParse calls=1 이고 `[scene.document] source=cooked` 마커가 있다 — §5 기준 2의
#      "SceneParse 호출 0"은 텍스트 파싱 0을 뜻하는데, D0 계측의 SceneParse 스코프는
#      `ParseSceneDocument`(텍스트든 CEDO든 문서 획득) 전체를 감싸므로 쿠킹 경로에서도
#      1회 찍힌다(2026-09-10 실측 1.4 ms). 텍스트 0은 아래 7번이 잰다.
#   3. SceneLoadTotal calls=1 · totalUs>0 — 시작 씬을 실제로 열었다.
#   4. EntityDeserialize·ComponentLoad calls>0 — 리플렉션 경로가 실제로 돌았다
#      ("빨라서 0"과 "안 돌아서 0"을 가른다).
#   5. 분해 합(SceneParse+EntityDeserialize+ComponentLoad) ≤ SceneLoadTotal.
#   6. AssetCatalog calls>0 — CEMF identity를 실제로 읽었고, `[asset.catalog] source=cemf
#      … metaParsed=0` 마커가 있다(부팅이 `.meta`를 파싱하지 않았다).
#   7. `[runtime.text-parser] calls=0` — D6 계약을 같은 실행에서 다시 본다.
#   8. `Scene loaded:` 마커가 manifest의 startupScene을 가리킨다 — 이 줄은 stdout이 아니라
#      Player 런타임 로그(`%TEMP%\CreatorEngine\Player\<pid>\RuntimeData\Log\*.html`)에
#      찍히므로 그 로그를 함께 읽는다.
#   9. 런타임 로그에 `[model.generation] 게시 전 검증 실패`·`MeshRenderer 모델 generation 해석
#      실패`가 없다 — 모델을 하나도 못 그려도 프레임은 돌아 smoke 마커만으로는 초록이다
#      (2026-09-10 첫 MBC11 패키지: 시작 씬 모델 8/8 거부, text-parser 8회의 실체).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stageRoot = Join-Path $repoRoot 'Build\Staging'
if ([string]::IsNullOrWhiteSpace($Stage)) {
    $pointerPath = Join-Path $stageRoot 'Dynamic_CPP.current.json'
    if (-not (Test-Path -LiteralPath $pointerPath -PathType Leaf)) {
        "Release stage 포인터가 없다: $pointerPath (Tools/build.ps1 -Config Release 로 먼저 패키징한다)"
        exit 1
    }
    $pointer = Get-Content -LiteralPath $pointerPath -Raw | ConvertFrom-Json
    $Stage = Join-Path $stageRoot ([string]$pointer.releaseDirectory)
}
$stagePath = [IO.Path]::GetFullPath($Stage)
$playerPath = Join-Path $stagePath 'Player.exe'
$manifestPath = Join-Path $stagePath 'package-manifest.json'
foreach ($required in @($playerPath, $manifestPath, (Join-Path $stagePath 'GameAssets.pak'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        "stage 입력이 없다: $required"
        exit 1
    }
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$startupScene = [string]$manifest.startupScene
if ([string]::IsNullOrWhiteSpace($startupScene)) {
    "package-manifest.json에 startupScene이 없다: $manifestPath"
    exit 1
}
if ([string]$manifest.config -ne 'Release') {
    # Debug는 단계별로 4~16배 느리고 단계 간 비중까지 뒤집는다(D0 실측). 성능 수치는
    # Release로만 판정한다 — D0 게이트와 같은 규칙이다.
    "Release stage가 아니다: config=$($manifest.config)"
    exit 1
}

$failures = [Collections.Generic.List[string]]::new()
$samples = [Collections.Generic.List[object]]::new()

function Invoke-PlayerRun {
    param([int]$Index)

    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    # 짧게 둔다: 이 뒤에 `CreatorEngine\Player\<pid>\RuntimeContent\`(45자)와 MBC11
    # generation 항목(`Assets\Derived\Models\xx\<id>\<gen>\textures\<id>.png`, 111자)이
    # 붙어 MAX_PATH를 넘기면 Player가 추출 실패로 exit 2 한다(build.ps1 verify temp 전례).
    $testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase `
        ('CE-D5d-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))))
    New-Item -ItemType Directory -Path $testRoot | Out-Null

    # ★ stdout을 파이프로 받지 않는다. D0 게이트가 Start-Process 파이프 위에서 제품의
    #   printf 한 줄당 ~10 ms를 계측 구간 안에 얹어 읽던 것을 2026-09-10에 실측했다.
    #   Player는 stdout이 버퍼링되지만 시간을 재는 게이트는 같은 규칙을 따른다 —
    #   cmd 리다이렉트로 자식에게 파일 핸들을 직접 넘긴다.
    $stdoutPath = Join-Path $testRoot 'stdout.txt'
    $stderrPath = Join-Path $testRoot 'stderr.txt'
    #   `cmd /c "<명령줄>"`은 GUI exe를 기다리지 않을 수 있어 배치 파일을 거친다.
    $launcher = Join-Path $testRoot 'run.cmd'
    @(
        '@echo off',
        ('"{0}" --smoke {1} > "{2}" 2> "{3}"' -f $playerPath, $SmokeFrames, $stdoutPath, $stderrPath),
        'exit /b %ERRORLEVEL%'
    ) | Set-Content -LiteralPath $launcher -Encoding ascii
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $env:ComSpec
    $startInfo.WorkingDirectory = $stagePath
    $startInfo.UseShellExecute = $false
    # ArgumentList는 따옴표를 다시 이스케이프하므로 cmd가 읽을 원문은 Arguments로 넘긴다.
    $startInfo.Arguments = '/d /c "' + $launcher + '"'
    $startInfo.Environment['TEMP'] = $testRoot
    $startInfo.Environment['TMP'] = $testRoot
    $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $startInfo.Environment['PATH'] = @(
        $stagePath,
        (Join-Path $windowsRoot 'System32'),
        $windowsRoot
    ) -join [IO.Path]::PathSeparator

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw "run $Index — Player를 띄우지 못했다" }
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            try { $process.Kill($true) } catch {}
            $process.WaitForExit()
            throw "run $Index — ${TimeoutSeconds}s 안에 끝나지 않았다"
        }
        $process.WaitForExit()
        $readAll = { param($path) if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Content -LiteralPath $path -Raw } else { '' } }
        # Player 런타임 로그(HTML·txt) — TEMP를 격리했으므로 이 실행의 PID 것만 있다.
        $runtimeLog = ''
        $playerRoot = Join-Path $testRoot 'CreatorEngine\Player'
        if (Test-Path -LiteralPath $playerRoot -PathType Container) {
            $runtimeLog = @(Get-ChildItem -LiteralPath $playerRoot -Recurse -File |
                Where-Object { $_.Extension -in @('.html', '.log', '.txt') } |
                ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
        }
        $stdoutText = & $readAll $stdoutPath
        $stderrText = & $readAll $stderrPath
        # 격리 TEMP에는 풀린 pak(~460 MB)이 남는다 — 읽을 것을 다 읽었으면 지운다.
        try { Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction Stop } catch {
            Write-Warning "run $Index — 격리 TEMP를 지우지 못했다: $testRoot ($($_.Exception.Message))"
        }
        return [pscustomobject]@{
            ExitCode   = $process.ExitCode
            Stdout     = $stdoutText
            Stderr     = $stderrText
            RuntimeLog = $runtimeLog
            TempRoot   = $testRoot
        }
    } finally {
        $process.Dispose()
    }
}

function Read-Stage {
    param([string]$Text, [string]$Name)
    $m = [regex]::Match($Text,
        '\[runtime\.serialization\]\s*stage=' + [regex]::Escape($Name) + '\s+totalUs=([\d.]+)\s+calls=(\d+)')
    if (-not $m.Success) { return $null }
    return [pscustomobject]@{
        Name  = $Name
        Ms    = [double]$m.Groups[1].Value / 1000.0
        Calls = [int]$m.Groups[2].Value
    }
}

for ($run = 1; $run -le $Runs; ++$run) {
    $result = Invoke-PlayerRun -Index $run
    $text = $result.Stdout
    if ($result.ExitCode -ne 0) {
        $failures.Add("run $run — Player 종료 코드 $($result.ExitCode): $($result.Stderr)")
        continue
    }

    $enabled = [regex]::Match($text, '\[runtime\.serialization\]\s*enabled=(yes|no)')
    if (-not $enabled.Success) {
        $failures.Add("run $run — [runtime.serialization] enabled= 마커가 없다 (Player가 이 계측을 찍지 않는 빌드다)")
        continue
    }
    if ($enabled.Groups[1].Value -ne 'yes') {
        $failures.Add("run $run — 직렬화 계측이 꺼진 채 실행됐다 (enabled=no)")
    }

    $stages = @{}
    foreach ($name in @('SceneLoadTotal', 'SceneParse', 'EntityDeserialize', 'ComponentLoad', 'AssetCatalog')) {
        # `$stage`라 쓰면 안 된다 — 스크립트 파라미터 `[string]$Stage`와 같은 변수라
        # (대소문자 무시) pscustomobject가 문자열로 변환돼 `.Calls`가 사라진다(2026-09-10 실측).
        $stageRecord = Read-Stage -Text $text -Name $name
        if ($null -eq $stageRecord) { $failures.Add("run $run — stage=$name 줄이 없다"); continue }
        $stages[$name] = $stageRecord
    }
    if ($stages.Count -ne 5) { continue }

    if ($stages.SceneParse.Calls -ne 1) {
        $failures.Add("run $run — SceneParse calls=$($stages.SceneParse.Calls) — 시작 씬 문서를 정확히 한 번 획득해야 한다")
    }
    if ($text -notmatch '\[scene\.document\]\s*source=cooked\b') {
        $failures.Add("run $run — [scene.document] source=cooked 마커가 없다 (시작 씬이 CEDO가 아니다)")
    }
    if ($stages.SceneLoadTotal.Calls -ne 1 -or $stages.SceneLoadTotal.Ms -le 0) {
        $failures.Add("run $run — SceneLoadTotal calls=$($stages.SceneLoadTotal.Calls) ms=$($stages.SceneLoadTotal.Ms) — 시작 씬을 한 번 열지 않았다")
    }
    if ($stages.EntityDeserialize.Calls -le 0 -or $stages.ComponentLoad.Calls -le 0) {
        $failures.Add("run $run — EntityDeserialize/ComponentLoad calls가 0이다 — 리플렉션 경로가 돌지 않았다")
    }
    $childMs = $stages.SceneParse.Ms + $stages.EntityDeserialize.Ms + $stages.ComponentLoad.Ms
    if ($childMs -gt $stages.SceneLoadTotal.Ms) {
        $failures.Add("run $run — 분해 합 $childMs ms가 루트 $($stages.SceneLoadTotal.Ms) ms를 넘는다 — 계측이 틀렸다")
    }
    if ($stages.AssetCatalog.Calls -le 0) {
        $failures.Add("run $run — AssetCatalog calls=0 — CEMF identity를 읽지 않았다")
    }
    $catalog = [regex]::Match($text, '\[asset\.catalog\]\s*source=cemf\s+identities=(\d+)\s+metaParsed=(\d+)')
    if (-not $catalog.Success) {
        $failures.Add("run $run — [asset.catalog] source=cemf 마커가 없다 (부팅이 CEMF를 쓰지 않았다)")
    } elseif ([int]$catalog.Groups[2].Value -ne 0) {
        $failures.Add("run $run — 부팅이 .meta를 $($catalog.Groups[2].Value)개 파싱했다")
    }
    $textParser = [regex]::Match($text, '\[runtime\.text-parser\]\s*calls=(\d+)')
    if (-not $textParser.Success) {
        $failures.Add("run $run — [runtime.text-parser] calls= 마커가 없다")
    } elseif ([int]$textParser.Groups[1].Value -ne 0) {
        # 어디서 텍스트를 읽었는지까지 적는다 — `<memory>`뿐이면 파일이 아니라 메모리
        # 문자열 파싱이다(2026-09-10: MBC11 generation 로더가 CEDO로 쿠킹된
        # AssetIdentity.asset을 YAML로 파싱해 모델마다 1회씩 세던 것이 이렇게 드러났다).
        $sources = @([regex]::Matches($text, '\[runtime\.text-parser\.call\]\s*source=([^\r\n]+)') |
            ForEach-Object { $_.Groups[1].Value.Trim() } | Group-Object |
            ForEach-Object { '{0}x{1}' -f $_.Name, $_.Count })
        $failures.Add("run $run — runtime text-parser 계수가 $($textParser.Groups[1].Value)이다 (source: $($sources -join ', '))")
    }
    $sceneMarker = 'Scene loaded:[^\r\n<]*' + [regex]::Escape($startupScene)
    if (($text + "`n" + $result.RuntimeLog) -notmatch $sceneMarker) {
        $failures.Add("run $run — 'Scene loaded: …$startupScene' 마커가 stdout에도 런타임 로그에도 없다")
    }
    foreach ($pattern in @('\[model\.generation\]\s*게시 전 검증 실패', 'MeshRenderer 모델 generation 해석 실패')) {
        $hits = [regex]::Matches($result.RuntimeLog, $pattern).Count
        if ($hits -gt 0) {
            $failures.Add("run $run — 런타임 로그에 '$pattern' ${hits}건 — 모델을 그리지 못한 씬 로드는 §5 수치가 아니다")
        }
    }

    $samples.Add([pscustomobject]@{
        Run               = $run
        SceneLoadTotalMs  = $stages.SceneLoadTotal.Ms
        SceneParseMs      = $stages.SceneParse.Ms
        EntityDeserializeMs = $stages.EntityDeserialize.Ms
        ComponentLoadMs   = $stages.ComponentLoad.Ms
        UnattributedMs    = $stages.SceneLoadTotal.Ms - $childMs
        EntityCalls       = $stages.EntityDeserialize.Calls
        ComponentCalls    = $stages.ComponentLoad.Calls
        AssetCatalogMs    = $stages.AssetCatalog.Ms
        CatalogIdentities = $stages.AssetCatalog.Calls
    })
}

function Format-Stat {
    param([double[]]$Values)
    if ($Values.Count -eq 0) { return 'n/a' }
    $avg = ($Values | Measure-Object -Average).Average
    $min = ($Values | Measure-Object -Minimum).Minimum
    $max = ($Values | Measure-Object -Maximum).Maximum
    return ('{0:F3} ms (범위 {1:F3}–{2:F3})' -f $avg, $min, $max)
}

"stage=$($stagePath.Substring($repoRoot.Length).TrimStart('\'))"
"startupScene=$startupScene config=$($manifest.config) buildId=$($manifest.workspaceHead) dirty=$($manifest.workspaceDirty)"
"runs=$Runs collected=$($samples.Count)"
if ($samples.Count -gt 0) {
    "SceneLoadTotal    = $(Format-Stat @($samples | ForEach-Object { $_.SceneLoadTotalMs }))"
    "  SceneParse      = $(Format-Stat @($samples | ForEach-Object { $_.SceneParseMs })) calls=1 (CEDO 문서 획득)"
    "  EntityDeserialize = $(Format-Stat @($samples | ForEach-Object { $_.EntityDeserializeMs })) calls=$($samples[0].EntityCalls)"
    "  ComponentLoad   = $(Format-Stat @($samples | ForEach-Object { $_.ComponentLoadMs })) calls=$($samples[0].ComponentCalls)"
    "  미귀속          = $(Format-Stat @($samples | ForEach-Object { $_.UnattributedMs }))"
    "AssetCatalog(CEMF) = $(Format-Stat @($samples | ForEach-Object { $_.AssetCatalogMs })) identities=$($samples[0].CatalogIdentities)"

    if ($MaxSceneLoadMs -gt 0) {
        $avgScene = (@($samples | ForEach-Object { $_.SceneLoadTotalMs }) | Measure-Object -Average).Average
        if ($avgScene -gt $MaxSceneLoadMs) {
            $failures.Add(('SceneLoadTotal 평균 {0:F3} ms가 문턱 {1:F3} ms를 넘는다' -f $avgScene, $MaxSceneLoadMs))
        }
    }
    if ($MaxBootMs -gt 0) {
        $avgBoot = (@($samples | ForEach-Object { $_.AssetCatalogMs }) | Measure-Object -Average).Average
        if ($avgBoot -gt $MaxBootMs) {
            $failures.Add(('AssetCatalog 평균 {0:F3} ms가 문턱 {1:F3} ms를 넘는다' -f $avgBoot, $MaxBootMs))
        }
    }
}

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'PASS — Release Player가 쿠킹 경로로 시작 씬을 열었다 (scene.document cooked · CEMF catalog · text-parser 0)'
exit 0

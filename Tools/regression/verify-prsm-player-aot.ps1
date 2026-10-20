# PrSM Player AOT 게이트 — 패키징이 만든 스크립트 DLL 을 Player 가 LLVM 없이 적재해 돌린다(D-048).
#
# ── 무엇을 잡는가 ──
#
# 에디터는 PrSM 을 prsm_jit(LLVM ORC)로 돌리지만 Player 에는 LLVM 이 없다. 패키징
# (CreatorBuildTool package-game → PrsmModuleBuilder)이 엔진 배포본의 prsmc 로 프로젝트를
# 오브젝트로 만들고, lld-link 로 PrSM/GameScripts.prsm.dll 하나에 링크한다. 그 DLL 은 C 런타임
# 없이 Player.runtime.dll 의 export(ce_*·prsm_host_*·바인딩 래퍼)만 import 한다. Player 는
# 기동 때 그것을 LoadLibrary → prsm_module_get → 표 검증 → 등록한다.
#
#   임시 프로젝트   ProjectSetting·Shaders·Assets/Scripts/PrSM(프로브) + C# 앵커 하나
#   씬 저작         에디터 --development-project --script(숨김): PrsmProbeTarget = Empty + LightComponent
#                   + PrsmScriptComponent(PrsmTransformProbe)
#   패키징          publish-engine → select-engine → package-game(offscreen smoke)
#   단정            package 산출물에 PrSM/GameScripts.prsm.dll, 그 import 는 Player.runtime.dll 하나,
#                   smoke stdout 에 [PRSM_AOT] loaded types=2,
#                   패키지 Player(숨김 offscreen + --command-service)의 player.object PrsmProbeTarget:
#                     position (p.x+1, p.y*2, p.z-4)  scale (2,2,2)  rotation (0,0.6,0,0.8)
#
# 기준선 p 는 에디터가 저작한 원점(0,0,0)이다 — 기대 position 은 (1, 0, -4) 이다.
# 기준선 scale 1·rotation 항등은 저작 씬에서 읽어 단정한다(이미 기대값이면 증명이 없다).
#
# ── 이 게이트가 못 보는 것 ──
#
# 핫 리로드(Player 는 AOT 고정), Shipping 구성(명령 서비스가 없다 — 적재 마커만 같은 경로다),
# 광원 Color·Type 쓰기(player.object 는 Transform 만 낸다 — 에디터 게이트 verify-prsm-bindings 가 덮는다).
#
# 사용법:
#   pwsh Tools\regression\verify-prsm-player-aot.ps1 -Configuration Debug
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [int]$TimeoutSeconds = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$bin = Join-Path $repo "Bin/x64-$Configuration"
$editor = Join-Path $bin 'Editor/CreatorEditor.exe'
$tool = Join-Path $bin 'Tools/CreatorBuildTool/CreatorBuildTool.exe'
foreach ($required in $editor, $tool, (Join-Path $bin 'Player/Player.exe'), (Join-Path $bin 'PrSM/prsmc.exe'), (Join-Path $bin 'PrSM/lld-link.exe')) {
    if (-not (Test-Path -LiteralPath $required)) { "필요한 파일이 없다: $required"; exit 1 }
}

$caseRoot = Join-Path $repo ('Build/Validation/PrsmPlayerAot/' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $caseRoot 'Project'
$assets = Join-Path $project 'Assets'
New-Item -ItemType Directory -Path "$assets/Scenes", "$assets/Script", "$assets/Scripts/PrSM" -Force | Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$assets/Shaders" -Recurse
foreach ($name in 'project.json', 'PrsmLightProbe.prsm', 'PrsmTransformProbe.prsm') {
    Copy-Item "$repo/Dynamic_CPP/Assets/Scripts/PrSM/$name" "$assets/Scripts/PrSM/$name"
}
# 패키지 smoke 는 관리 런타임 준비(registeredScriptTypes > 0)도 요구한다 — PrSM 과 무관한 앵커.
[IO.File]::WriteAllText("$assets/Script/PrsmAotAnchor.cs", "using CreatorEngine;`n`npublic sealed class PrsmAotAnchor : Component`n{`n}`n")

# ── 씬 저작(에디터, 숨김) ──
$sceneName = 'PrsmPlayerAot.creator'
$scenePath = Join-Path $assets "Scenes/$sceneName"
$scenario = Join-Path $caseRoot 'author.txt'
@"
wait 60
scene.new PrsmPlayerAot
wait 40
object.create MainCamera Camera
object.create PrsmProbeTarget Empty
component.add PrsmProbeTarget LightComponent
component.add PrsmProbeTarget PrsmScriptComponent
object.property PrsmProbeTarget PrsmScriptComponent m_typeName PrsmTransformProbe
wait 5
object.properties PrsmProbeTarget Transform
scene.save $scenePath
wait 20
log.flush
quit
"@ | Set-Content -LiteralPath $scenario -Encoding utf8
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$authorResults = Join-Path $caseRoot 'author.results.jsonl'
$proc = Start-Process -FilePath $editor -WindowStyle Hidden -WorkingDirectory (Split-Path $editor) -PassThru `
    -ArgumentList @('--development-project', ('"' + $project + '"'), '--script', ('"' + $scenario + '"'), '--result-format', 'jsonl', '--result-file', ('"' + $authorResults + '"')) `
    -RedirectStandardOutput (Join-Path $caseRoot 'author.out') -RedirectStandardError (Join-Path $caseRoot 'author.err')
if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) { $proc.Kill(); throw "씬 저작 타임아웃. $caseRoot" }
$results = @(Read-CommandResults $authorResults)
if ($proc.ExitCode -ne 0 -or @($results | Where-Object status -ne 'succeeded').Count -or -not (Test-Path -LiteralPath $scenePath)) {
    throw "씬 저작 실패(exit=$($proc.ExitCode)). $caseRoot/author.*"
}
$baseline = @($results | Where-Object command -eq 'object.properties')[0].data.values
if ([double]$baseline.scale.x -ne 1 -or [double]$baseline.rotation.y -ne 0 -or [double]$baseline.rotation.w -ne 1) {
    throw '저작 기준선이 항등 Transform 이 아니다.'
}

# ── 패키징 ──
function Invoke-Tool([string]$log, [string[]]$arguments) {
    $output = @(& $tool @arguments 2>&1)
    $exit = $LASTEXITCODE
    $output | Set-Content (Join-Path $caseRoot $log) -Encoding utf8
    if ($exit -ne 0) { throw "CreatorBuildTool $($arguments[0]) 실패. $caseRoot/$log" }
    return $output
}
Invoke-Tool 'engine.log' @('publish-engine', '--repository', $repo, '--config', $Configuration, '--output-root', (Join-Path $caseRoot 'Engine'), '--no-pointer') | Out-Null
$distribution = (Get-ChildItem (Join-Path $caseRoot 'Engine') -Directory | Where-Object Name -NotLike '.candidate-*').FullName
Invoke-Tool 'select.log' @('select-engine', '--engine-distribution', $distribution, '--project', $project) | Out-Null
Invoke-Tool 'package.log' @('package-game', '--repository', $repo, '--engine-distribution', $distribution, '--project', $project,
    '--config', $Configuration, '--stage-root', (Join-Path $caseRoot 'Stage'), '--input-mode', 'Project', '--startup-scene', $sceneName,
    '--render-backend', 'dx12', '--smoke-offscreen', '--smoke-frames', '120', '--smoke-timeout-sec', '300') | Out-Null
$pointer = Get-Content (Join-Path $caseRoot 'Stage/Project.current.json') -Raw | ConvertFrom-Json
$packageRoot = Join-Path $caseRoot "Stage/$($pointer.releaseDirectory)"
$module = Join-Path $packageRoot 'PrSM/GameScripts.prsm.dll'
if (-not (Test-Path -LiteralPath $module)) { throw "패키지에 스크립트 모듈이 없다: $module" }
# package-game 은 smoke Player 의 stdout 을 자기 출력에 싣는다(package.log).
$smoke = Get-Content (Join-Path $caseRoot 'package.log') -Raw
if ($smoke -notmatch '\[PRSM_AOT\] loaded types=2\b') { throw "smoke 가 PrSM AOT 적재 마커를 내지 않았다. $caseRoot/package.log" }
foreach ($forbidden in 'prsm_jit.dll', 'LLVM-C.dll', 'prsmc.exe', 'lld-link.exe') {
    if (@(Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter $forbidden).Count) { throw "패키지에 런타임이 쓰지 않는 도구가 들어갔다: $forbidden" }
}
$llvm = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/llvm-readobj.exe'
if (Test-Path -LiteralPath $llvm) {
    $imports = @(& $llvm --coff-imports $module | Select-String '^\s*Name: (.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() })
    if ($imports.Count -ne 1 -or $imports[0] -ne 'Player.runtime.dll') { throw "스크립트 모듈 import 가 Player.runtime.dll 하나가 아니다: $($imports -join ',')" }
}

# ── 패키지 Player 실행(숨김 offscreen) + 명령 서비스 ──
$runTemp = Join-Path $caseRoot 'PlayerTemp'
New-Item -ItemType Directory -Path $runTemp -Force | Out-Null
$previousTemp = $env:TEMP
$env:TEMP = $runTemp; $env:TMP = $runTemp
try {
    $player = Start-Process -FilePath (Join-Path $packageRoot 'Player.exe') -WorkingDirectory $packageRoot -WindowStyle Hidden -PassThru `
        -ArgumentList @('--smoke', '1000000', '--smoke-offscreen', '--command-service') `
        -RedirectStandardOutput (Join-Path $caseRoot 'player.out') -RedirectStandardError (Join-Path $caseRoot 'player.err')
}
finally { $env:TEMP = $previousTemp; $env:TMP = $previousTemp }
try {
    $deadline = (Get-Date).AddSeconds(180); $info = $null
    while (-not $info -and (Get-Date) -lt $deadline) {
        if ($player.HasExited) { throw "Player 가 먼저 종료했다(exit=$($player.ExitCode)). $caseRoot/player.out" }
        foreach ($candidate in @(Get-ChildItem -Path $runTemp -Recurse -Filter 'endpoint.json' -ErrorAction SilentlyContinue)) {
            try { $parsed = Get-Content -LiteralPath $candidate.FullName -Raw | ConvertFrom-Json; if ($parsed.pid -eq $player.Id -and $parsed.port -gt 0) { $info = $parsed } } catch { }
        }
        if (-not $info) { Start-Sleep -Milliseconds 400 }
    }
    if (-not $info) { throw '명령 서비스가 뜨지 않았다.' }
    $base = "http://127.0.0.1:$($info.port)"; $auth = @{ Authorization = "Bearer $($info.token)" }
    function Send([string]$body) {
        $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth -ContentType 'application/json' -Body $body -UseBasicParsing -SkipHttpErrorCheck -TimeoutSec 60
        return ($r.Content | ConvertFrom-Json)
    }
    # 생명주기는 첫 프레임들에서 돈다 — 적용될 때까지 짧게 되읽는다.
    $object = $null
    for ($i = 0; $i -lt 50; ++$i) {
        $object = Send '{"command":"player.object","args":["PrsmProbeTarget"],"mode":"sync"}'
        if ($object.status -eq 'succeeded' -and [double]$object.data.scale.x -eq 2) { break }
        Start-Sleep -Milliseconds 200
    }
    $null = Send '{"command":"quit","mode":"sync"}'
    if (-not $player.WaitForExit(30000)) { throw 'quit 뒤 30초 안에 종료하지 않았다.' }
}
finally { if (-not $player.HasExited) { $player.Kill() } }
if ($object.status -ne 'succeeded') { throw "player.object 실패: $($object | ConvertTo-Json -Depth 5 -Compress)" }

$failed = 0; $passed = 0
function Expect([string]$label, $actual, [double]$expected) {
    if ([Math]::Abs([double]$actual - $expected) -gt 1e-4) { "  실패: $label = $actual (기대 $expected)"; $script:failed++ } else { $script:passed++ }
}
$data = $object.data
Expect 'position.x' $data.position.x ([double]$baseline.position.x + 1)
Expect 'position.y' $data.position.y ([double]$baseline.position.y * 2)
Expect 'position.z' $data.position.z ([double]$baseline.position.z - 4)
foreach ($axis in 'x', 'y', 'z') { Expect "scale.$axis" $data.scale.$axis 2 }
Expect 'rotation.x' $data.rotation.x 0
Expect 'rotation.y' $data.rotation.y 0.6
Expect 'rotation.z' $data.rotation.z 0
Expect 'rotation.w' $data.rotation.w 0.8
# 건수를 정확히 못 박는다 — position 3 · scale 3 · rotation 4.
if ($failed) { "단정 $failed 건 실패 (통과 $passed 건). $caseRoot"; exit 1 }
if ($passed -ne 10) { "단정 $passed 건 — 10 건이어야 한다."; exit 1 }
$caseRoot | Set-Content (Join-Path $repo "Build/Validation/PrsmPlayerAot/latest-$Configuration.txt")
"PRSM_PLAYER_AOT_OK configuration=$Configuration assertions=$passed module=PrSM/GameScripts.prsm.dll output=$caseRoot"
exit 0

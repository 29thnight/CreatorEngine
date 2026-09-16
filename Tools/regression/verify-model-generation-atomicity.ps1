# PBR-W8 — 모델 generation 캐시의 원자성과 **재임포트 실패 뒤 current 불변**.
#
# ── 이 게이트가 왜 새로 생겼나 ───────────────────────────────────────────
#
# 계약은 `PBRWiringStabilizationPlan.md` §1 의 한 줄이다:
#   "reload/재임포트 실패 시 마지막 정상 generation을 유지하고 부분 게시하지 않는다."
#
# 이 계약을 재는 검사는 **이미 저장소에 있었다** — `RunModelAssetGenerationSelfTest`
# 의 `verifyTamperDoesNotPublish` 가 정확히 그것을 4 종으로 잰다. 그런데 저장소
# 전체에서 그 함수의 **호출자가 0** 이었다(형제 셋도 같다). 컴파일되고 링크까지
# 되는데 아무도 부르지 않아, 계약이 12 일 넘게 측정되지 않고 있었다.
#
# ★ 죽어 있던 이유는 게으름이 아니라 **fixture 전제**였다. 그 검사는
#   ① ModelId 디렉터리가 정확히 1 개이고 ② generation 이 `1`·`2` 인 프로젝트를
#   요구했는데, 살아 있는 프로젝트엔 103 개가 있고 번호는 watcher 가 한 번 더
#   임포트해서 제어할 수 없다. 둘 다 **재는 것과 무관한 전제**라 걷어냈다.
#
# ── 왜 fixture 를 커밋하지 않고 매 회차 만드나 ──────────────────────────
#
# 처음에는 generation 두 벌을 스냅샷해 `Tools/regression/fixtures/` 에 추적하려
# 했다(ignore 규칙 `/Dynamic_CPP/Library/` 는 앵커돼 있어 가능하다). 실측이
# 막았다 — 9-02·9-03 산출물은 **포맷 4·5** 인데 지금 리더는 **9** 를 요구한다.
# cooked 산출물을 fixture 로 박으면 포맷이 오를 때마다 같이 썩는다. 그래서
# 게이트가 저장소 모델을 복사해 두 번 임포트하고, 끝나면 치운다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$project = Join-Path $root 'Dynamic_CPP'
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-generation-' + [guid]::NewGuid().ToString('N'))
$utf8 = [Text.UTF8Encoding]::new($false)
$process = $null
$keepArtifacts = $false
$lastExit = 0

# 원본은 저장소의 corpus 자산이다. 복사본을 **다른 이름**으로 임포트하므로
# modelId 가 갈리고 원본의 추적 사이드카는 건드리지 않는다(실측으로 확인했다).
$sourceModel = Join-Path $project 'Assets\Models\Prim_Cube.glb'
$probeName = 'GenerationAtomicityProbe'
$probeAsset = Join-Path $project "Assets\Models\$probeName.glb"
$probeMeta = "$probeAsset.meta"

function Get-ProbeModelId {
    if (-not (Test-Path -LiteralPath $probeMeta -PathType Leaf)) { return $null }
    $line = Get-Content -LiteralPath $probeMeta | Where-Object { $_ -like 'assetId: *' } | Select-Object -First 1
    if (-not $line) { return $null }
    return ($line -split ' ', 2)[1].Trim()
}

# 치우기는 **앞뒤로** 한다. 앞선 회차가 죽어 잔해가 남았으면 그것이 다음 회차의
# 전제를 바꾼다(generation 번호가 이어져 버린다).
function Remove-Probe {
    $id = Get-ProbeModelId
    foreach ($path in @($probeAsset, $probeMeta)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
    }
    if ($id) {
        $generationDir = Join-Path $project "Library\ModelAssetGenerations\$id"
        if (Test-Path -LiteralPath $generationDir) {
            Remove-Item -LiteralPath $generationDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# $ExpectedExit 가 -1 이면 종료 코드로 판정하지 않는다.
#
# ★ 이것이 필요한 이유를 변이가 가르쳐 줬다. `assets.generation` 은 실패하면
#   exit 4 를 내는데, 종료 코드에서 먼저 죽으면 **왜** 실패했는지를 담은 report 를
#   읽지 못한다. 그러면 "무엇이 깨졌나"를 게이트가 말하지 못하고 사람이 로그를
#   뒤져야 한다. 판정은 수로 하고, 종료 코드는 그 수와 어긋나는지만 본다.
function Invoke-Editor([string]$Name, [string[]]$Commands, [int]$ExpectedExit = 0) {
    $scenario = Join-Path $run "$Name.txt"
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", $utf8)
    $resultPath = Join-Path $run "$Name.results.jsonl"
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Resolve-Path -LiteralPath $Editor).Path
    $start.ArgumentList.Add('--commandlet-script'); $start.ArgumentList.Add($scenario)
    $start.ArgumentList.Add('--result-file');       $start.ArgumentList.Add($resultPath)
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $script:process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $finished) { $process.Kill(); $process.WaitForExit() }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run "$Name.stdout.txt"), $stdout, $utf8)
    [IO.File]::WriteAllText((Join-Path $run "$Name.stderr.txt"), $stderr, $utf8)
    $exitCode = $process.ExitCode
    $process.Dispose()
    $script:process = $null
    if (-not $finished) { throw "$Name timed out; artifacts: $run" }
    if ($ExpectedExit -ge 0 -and $exitCode -ne $ExpectedExit) {
        throw "$Name exit=$exitCode expected=$ExpectedExit; artifacts: $run"
    }
    $script:lastExit = $exitCode
    return @(Read-CommandResults $resultPath)
}

try {
    if (-not (Test-Path -LiteralPath $sourceModel -PathType Leaf)) {
        throw "fixture 원본이 없다: $sourceModel"
    }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    Remove-Probe
    Copy-Item -LiteralPath $sourceModel -Destination $probeAsset -Force

    # ── ① generation 두 벌을 만든다 ──
    #
    # ★ 번호는 예측하지 않는다. watcher 가 복사본을 보고 한 번 더 임포트해서
    #   `model.load` 두 번이 generation 두 개가 아니다. 검사가 실재하는 두 벌을
    #   스스로 고르므로 게이트는 "두 벌 이상 생겼다" 만 확인하면 된다.
    $imports = Invoke-Editor 'import' @(
        "model.load `"$probeAsset`"",
        "model.load `"$probeAsset`"",
        'quit')
    $loads = @($imports | Where-Object command -eq 'model.load')
    if ($loads.Count -ne 2) { throw "model.load 결과가 2 건이 아니다: $($loads.Count)" }
    foreach ($row in $loads) {
        if ($row.status -ne 'succeeded') {
            throw "model.load status=$($row.status) code=$($row.code): $($row.message)"
        }
    }
    # 둘째가 진짜 **재임포트**여야 한다. 둘 다 'loaded' 면 캐시가 갈리지 않은 것이고,
    # 그러면 generation 두 벌이 없어 이 게이트는 아무것도 재지 못한다.
    if ($loads[1].data.cache -ne 'reloaded') {
        throw "둘째 model.load 가 재임포트가 아니다(cache=$($loads[1].data.cache)) — generation 두 벌이 안 생긴다"
    }

    $modelId = Get-ProbeModelId
    if (-not $modelId) { throw "probe sidecar 에서 modelId 를 못 읽었다: $probeMeta" }
    $generationDir = Join-Path $project "Library\ModelAssetGenerations\$modelId"
    $generations = @(Get-ChildItem -LiteralPath $generationDir -Directory | Select-Object -ExpandProperty Name)
    if ($generations.Count -lt 2) {
        throw "generation 이 두 벌 미만이다($($generations -join ',')) — 원자 교체를 잴 수 없다"
    }

    # ── ② 되살린 자가 검증 ──
    # ★ 종료 코드로 죽지 않는다(-1). 실패했을 때의 **수**가 진단이기 때문이다.
    #   변이 회차가 이것을 가르쳐 줬다 — exit 4 에서 먼저 죽으니 게이트가
    #   "무엇이 깨졌나" 를 말하지 못하고 사람이 로그를 뒤져야 했다.
    $verify = Invoke-Editor 'generation' @(
        "assets.generation `"$project`" $modelId",
        'quit') -ExpectedExit -1
    $data = (Get-CommandResult $verify 'assets.generation').data

    # ★ fixture 가 안 섰는데 "실패 0" 으로 읽히는 길을 막는다. 이른 반환에서는
    #   단정이 한 건도 안 돌고, 그 회차의 failed 도 0 이다.
    if (-not $data.fixtureResolved) {
        throw "fixture 가 서지 않았다 — 단정이 돌지 않았다: $($data.log)"
    }
    if ($data.failed -ne 0) { throw "generation 단정 실패 $($data.failed) 건: $($data.log)" }
    if ($data.passed -lt 40) { throw "단정이 $($data.passed) 건뿐이다 — 검사가 통째로 건너뛰었나: $($data.log)" }

    # ★ 여기가 W8 계약이다. 셋을 **따로** 묻는다.
    #   - tamperCases  : 주입을 실제로 했나 (0 이면 자극조차 못 한 것이다)
    #   - tamperRejected : 게시 전에 거부했나
    #   - tamperCurrentHeld : 거부 **뒤에도** current generation 이 그대로인가
    #   합쳐 세면 "거부는 되는데 current 가 날아가는" 회귀를 못 본다.
    if ($data.tamperCases -lt 4) {
        throw "tamper 주입이 $($data.tamperCases) 건뿐이다 — 실패 경로를 자극하지 못했다"
    }
    if ($data.tamperRejected -ne $data.tamperCases) {
        throw "tamper 거부 $($data.tamperRejected)/$($data.tamperCases) — 망가진 generation 이 게시됐다"
    }
    if ($data.tamperCurrentHeld -ne $data.tamperCases) {
        throw ("재임포트 실패 뒤 current generation 이 $($data.tamperCurrentHeld)/$($data.tamperCases) 만 유지됐다" +
            " — §1 '마지막 정상 generation 유지' 위반")
    }

    if ($lastExit -ne 0) {
        throw "단정 실패 0 인데 exit=$lastExit — 결과 계약 불일치: $($data.log)"
    }

    "model generation atomicity PASS (model=$modelId generations=$($generations.Count)" +
        " assertions=$($data.passed) tamper=$($data.tamperCases)" +
        " rejected=$($data.tamperRejected) currentHeld=$($data.tamperCurrentHeld))"
    "── 축 회계 ──"
    "  원자 교체·retire           ran"
    "  tamper 게시 전 거부        ran ($($data.tamperCases) 종)"
    "  실패 뒤 current 유지       ran ($($data.tamperCurrentHeld)/$($data.tamperCases))"
    # ★ 자극하지 못한 것을 **평상시 출력에도** 적는다. 잊힌 사각지대는 초록으로 읽힌다.
    "  Animator 의 last-good 유지 미자극 — Animator::BindModelGeneration 은 검증보다 먼저"
    "                             m_modelGeneration.reset() 을 한다(뼈 있는 fixture 필요)"
    "  런타임 캐시 공백(retire→적재 실패) 미자극 — 은퇴가 먼저 도는 경로는 별도 축"
    exit 0
}
catch {
    # ★ 에러 문구가 artifacts 경로를 안내하는데 finally 가 지워 버리면 그
    #   안내는 거짓말이다. 붉은 회차의 산출물은 남긴다.
    $script:keepArtifacts = $true
    Write-Error $_
    exit 1
}
finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    Remove-Probe
    if (-not $keepArtifacts -and (Test-Path -LiteralPath $run)) {
        Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
    }
    elseif ($keepArtifacts) { Write-Host "artifacts: $run" }
}

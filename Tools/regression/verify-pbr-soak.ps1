# PHASE 4 W9 — 장시간 회전·이동·재임포트 중 세대 밀봉이 깨지지 않는지.
#
# 연속 표본에서는 W8의 상태 수치를, 마지막 제품 frame에서는 픽셀을 검사한다.
# 모든 frame의 검정/변색 부재를 증명하지 않으며, 수치가 다루지 않는 원인은 남는다.
#     · seal 위반(세대 혼합·지난 프레임 밀봉·기록 단계 누락)
#     · 인코더가 조용히 버린 명령(놓인 PSO 핸들·만료 descriptor)
#     · 업로드 실패를 흰색으로 덮은 횟수
#   이 셋 중 하나라도 0이 아니면 해당 회차를 실패시킨다.
#
# ★ drawCount 도 함께 본다. 위 셋이 0이어도 그릴 것이 0이면 화면은 검다 —
#   "위반 없음"과 "그렸음"은 다른 질문이다.
#
# ★★ 재임포트 축 (2026-09-16 정정). 처음 판은 `model.loadcached` 로 "같은 모델을 다시
#   열면 generation 이 갈린다" 고 적었지만 Release 740 s 회차의 1886 회가 모두 캐시
#   적중(executedMs ~0.5)이었다 — 재임포트는 한 번도 일어나지 않았다. 게다가 배치된
#   인스턴스는 자기 generation 을 붙들고 재바인딩되지 않으므로, 재임포트만으로는 그려지는
#   generation 이 바뀌지 않는다.
#
#   그래서 세대 혼합이 **실제로 일어나는 모양**을 만든다: 작은 probe 모델을 복사해
#   `model.load` 로 진짜 재임포트하고, 옛 generation 인스턴스(Anchor)를 남긴 채 새
#   generation 인스턴스(Fresh)를 배치해 **한 프레임에 같은 modelId 의 두 generation** 을
#   그린다. 자극했는지는 렌더러가 센다(`mixedGenerationModels` · `mixedNewestGeneration`).
#   그 수가 전진하지 않으면 재임포트 축은 자극되지 않은 것이고 게이트는 붉다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [ValidateSet('dx12', 'vulkan')][string]$Backend = 'dx12',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    # 기본값은 게이트용으로 짧다. W9 acceptance 는 -Minutes 10 으로 돌린다.
    [double]$Minutes = 1.0,
    # 표본 하나에 드는 벽시계 초. 표본 수 = Minutes*60 / 이 값. 구성·기계마다 다르므로
    # 실제 시간을 요구하는 회차는 짧은 회차로 재서 넘긴다.
    [double]$SecondsPerSample = 0.6,
    # 0 이 아니면 프로세스 실행과 예열 후 첫/마지막 표본 간격이 모두 이 초 이상이어야 한다.
    # W9 acceptance 는 -MinimumWallSeconds 600 을 함께 준다.
    [double]$MinimumWallSeconds = 0,
    # 몇 표본마다 probe 를 재임포트하나. 재임포트 하나가 generation 디렉터리 하나다.
    [int]$ReimportEvery = 20,
    [int]$TimeoutSeconds = 1800
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $PSBoundParameters.ContainsKey('Editor')) {
    $Editor = Join-Path $root "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
}
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-pbr-soak-' + [guid]::NewGuid().ToString('N'))
$settings = Join-Path $root 'Dynamic_CPP\ProjectSetting\EngineSettings.asset'
$utf8 = [Text.UTF8Encoding]::new($false)
$original = $null

# 한 표본이 도는 데 드는 프레임 수. Debug 라이브 렌더러의 실측 프레임 시간에
# 맞춰 잡았고, 정확한 벽시계가 아니라 "충분히 많은 표본"이 목적이다.
$framesPerSample = 30

# 재임포트 probe — 저장소 corpus 의 작은 모델을 **다른 이름**으로 복사한다. modelId 가
# 갈리므로 원본의 추적 사이드카를 건드리지 않고, 끝나면 앞뒤로 치운다.
$project = Join-Path $root 'Dynamic_CPP'
$probeSource = Join-Path $project 'Assets\Models\Prim_Cube.glb'
$probeName = 'SoakReimportProbe'
$probeAsset = Join-Path $project "Assets\Models\$probeName.glb"
$probeMeta = "$probeAsset.meta"

function Remove-Probe {
    $id = $null
    if (Test-Path -LiteralPath $probeMeta -PathType Leaf) {
        $line = Get-Content -LiteralPath $probeMeta | Where-Object { $_ -like 'assetId: *' } | Select-Object -First 1
        if ($line) { $id = ($line -split ' ', 2)[1].Trim() }
    }
    foreach ($path in @($probeAsset, $probeMeta)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
    }
    if ($id) {
        $generationDir = Join-Path $project "Library\ModelAssetGenerations\$id"
        $generationRoot = [IO.Path]::GetFullPath((Join-Path $project 'Library/ModelAssetGenerations')) + [IO.Path]::DirectorySeparatorChar
        $generationDir = [IO.Path]::GetFullPath($generationDir)
        if ($id -notmatch '^[0-9a-fA-F-]{36}$' -or -not $generationDir.StartsWith($generationRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Unsafe generated probe directory'
        }
        if (Test-Path -LiteralPath $generationDir) {
            Remove-Item -LiteralPath $generationDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function New-SoakCommands([int]$Samples) {
    $commands = [Collections.Generic.List[string]]::new()
    $commands.Add("scene.switch `"$root/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator`"")
    # 첫 대기가 렌더 예열을 겸한다(캡처 게이트와 같은 이유).
    $commands.Add('wait 2000')
    $commands.Add('editor.viewport game')
    $commands.Add("model.loadcached `"$root/Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb`"")
    $commands.Add('model.place Gunner_F_Mythic')
    # probe 의 첫 generation 인스턴스(Anchor). 끝까지 지우지 않는다 — 옛 generation 을 그리는 쪽이다.
    #
    # ★ 이름을 바꾸지 않는다. `object.rename` 은 옛 이름을 이름 집합에서 놓지 않아(2026-09-16
    #   실측: rename 뒤 같은 모델을 배치하면 원래 이름이 아니라 `(1)` 이 붙고, 그 뒤로도 원래
    #   이름은 돌아오지 않는다) 스크립트가 이름을 예측할 수 없게 된다. 대신 Anchor 가 원래
    #   이름을, Fresh 가 `(1)` 을 갖는다 — `object.delete` 는 이름을 놓으므로 매 회차 `(1)` 이다.
    $commands.Add("model.load `"$probeAsset`"")
    $commands.Add("model.place $probeName")
    $commands.Add("object.transform $probeName -2 0 2 0 0 0 0.5 0.5 0.5")
    $commands.Add('wait 30')
    $freshPlaced = $false

    for ($i = 0; $i -lt $Samples; $i++) {
        # 회전과 이동을 함께 준다 — 회전만 주면 배치 키가 거의 그대로라
        # 재밀봉 경로가 충분히 돌지 않는다.
        $angle = ($i * 23) % 360
        $z = 2.0 + [math]::Sin($i * 0.4)
        $commands.Add(
            ('object.transform Gunner_F_Mythic 0 0 {0:0.###} 0 {1} 0 0.025 0.025 0.025' -f $z, $angle))
        $commands.Add("wait $framesPerSample")
        $commands.Add('render.pbr.sealstatus')

        # 재임포트: probe 를 진짜로 다시 임포트하고(새 generation), 직전 Fresh 를 지운 뒤
        # 새 generation 으로 다시 배치한다. Anchor 는 옛 generation 을 계속 그린다.
        if (0 -eq ($i % $ReimportEvery) -and $i -gt 0) {
            $commands.Add("model.load `"$probeAsset`"")
            if ($freshPlaced) { $commands.Add("object.delete `"$probeName (1)`"") }
            $commands.Add("model.place $probeName")
            $commands.Add(("object.transform `"$probeName (1)`" 2 0 2 0 {0} 0 0.5 0.5 0.5" -f $angle))
            $freshPlaced = $true
            $commands.Add('wait 10')
            $commands.Add('render.pbr.sealstatus')
        }
    }
    $commands.Add("render.pbr.capture `"$run/final-frame`" game")
    $commands.Add('quit')
    return $commands
}

try {
    if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
        throw 'Close the running Editor before this test; it temporarily selects the startup backend.'
    }
    New-Item -ItemType Directory -Path $run | Out-Null
    if (-not (Test-Path -LiteralPath $probeSource -PathType Leaf)) { throw "probe 원본이 없다: $probeSource" }
    Remove-Probe
    Copy-Item -LiteralPath $probeSource -Destination $probeAsset -Force
    $original = [IO.File]::ReadAllBytes($settings)
    $text = $utf8.GetString($original)
    $backendPattern = '(?m)(^render:\r?\n\s{2}backend: )\w+'
    if ([regex]::Matches($text, $backendPattern).Count -ne 1) { throw 'Runtime backend setting is ambiguous.' }
    [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, "`${1}$Backend"), $utf8)

    $samples = [int][math]::Max(10, [math]::Ceiling($Minutes * 60 / $secondsPerSample))
    $scenario = Join-Path $run 'soak.txt'
    $commands = @(New-SoakCommands $samples)
    [IO.File]::WriteAllText($scenario, ($commands -join "`n") + "`n", $utf8)
    $resultPath = Join-Path $run 'soak.results.jsonl'

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Resolve-Path -LiteralPath $Editor).Path
    $start.ArgumentList.Add('--commandlet-script')
    $start.ArgumentList.Add($scenario)
    $start.ArgumentList.Add('--result-file')
    $start.ArgumentList.Add($resultPath)
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    # 에디터는 GUI 앱이라 CreateNoWindow 만으로는 창이 뜬다. baseline 게이트와 맞춘다.
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    # ★ -Minutes 는 표본 수를 정할 뿐 벽시계가 아니다. 2026-09-16 Debug dx12 에서
    #   -Minutes 10 이 200~259 s 에 끝났다. PASS 줄에 실제 경과를 같이 낸다.
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw "Soak timed out after $TimeoutSeconds s; artifacts: $run"
    }
    # Parsing thousands of JSON rows is host analysis, not additional renderer soak time.
    $clock.Stop()
    [IO.File]::WriteAllText((Join-Path $run 'soak.out'), $stdoutTask.Result, $utf8)
    [IO.File]::WriteAllText((Join-Path $run 'soak.err'), $stderrTask.Result, $utf8)
    if ($process.ExitCode -ne 0) {
        throw "Soak exited $($process.ExitCode); artifacts: $run"
    }

    $results = @(Read-CommandResults $resultPath)
    if ($results.Count -ne $commands.Count) { throw 'Soak terminal command count mismatch' }
    # 자극 명령이 하나라도 실패했으면 아래 수는 그 자극을 재지 않은 것이다.
    $failedRows = @($results | Where-Object status -ne 'succeeded')
    if ($failedRows.Count -gt 0) {
        $first = $failedRows[0]
        throw "soak 명령 $($failedRows.Count) 건 실패 (첫째: $($first.command) $($first.code) $($first.message)); artifacts: $run"
    }
    $status = @($results | Where-Object command -eq 'render.pbr.sealstatus')
    if ($status.Count -lt $samples) {
        throw "Soak collected $($status.Count) samples, expected at least $samples; artifacts: $run"
    }

    $previousFrame = -1
    $movedFrames = 0
    # ★ 첫 표본은 라이브 렌더러가 첫 프레임을 내기 **전**일 수 있다(2026-09-16 10분 회차:
    #   표본 0 이 framesRendered 0 · drawCount 0, 나머지 1098 은 전부 10). 그것은 soak 중
    #   검어진 것이 아니라 아직 시작하지 않은 것이다. framesRendered 로 가르고, 예열이
    #   끝난 뒤의 drawCount 0 은 그대로 실패다. 예열이 끝나지 않으면 그것도 실패다.
    $warmupSamples = 0
    $maxWarmupSamples = 10
    $rendering = $false
    foreach ($sample in $status) {
        $data = $sample.data
        if ($sample.status -ne 'succeeded') { throw "sealstatus sample failed; artifacts: $run" }
        if (-not $rendering) {
            if ($data.framesRendered -le 0) {
                $warmupSamples++
                if ($warmupSamples -gt $maxWarmupSamples) {
                    throw "Live renderer never produced a frame ($warmupSamples samples); artifacts: $run"
                }
                continue
            }
            $rendering = $true
        }
        if (-not $data.enabled) { throw "Live renderer went dark during the soak; artifacts: $run" }
        if ($data.gbufferViolations -ne 0 -or $data.forwardViolations -ne 0) {
            throw "Seal violations during soak (gbuffer $($data.gbufferViolations), forward $($data.forwardViolations)); artifacts: $run"
        }
        if ($data.gbufferUnstamped -ne 0 -or $data.forwardUnstamped -ne 0) {
            throw "Unstamped product draws during soak; artifacts: $run"
        }
        if ($data.encoderDrops -ne 0) {
            throw "Encoder dropped commands during soak ($($data.encoderDrops)); artifacts: $run"
        }
        if ($data.textureUploadFailures -ne 0) {
            throw "Texture upload failures during soak; artifacts: $run"
        }
        if ($data.drawCount -le 0) {
            throw "Frame drew nothing during soak (검은 화면); artifacts: $run"
        }
        # 프레임이 실제로 진행했는지. 같은 프레임을 여러 번 재면 표본 수만
        # 늘고 잰 것은 하나다.
        if ($data.frameId -gt $previousFrame) { $movedFrames++ }
        $previousFrame = $data.frameId
    }
    $measured = $status.Count - $warmupSamples
    $final = Get-Content -LiteralPath (Join-Path $run 'final-frame/manifest.json') -Raw | ConvertFrom-Json
    if (-not $final.finite -or $final.draws.Count -eq 0 -or $final.attachments.Count -ne 7 -or
        $final.validationCount -ne 0 -or $final.frameId -le $previousFrame) {
        throw 'Soak final product capture is invalid'
    }
    $finalDepth = $final.attachments | Where-Object name -eq depth
    $finalHdr = $final.attachments | Where-Object name -eq preToneHdr
    if ($finalDepth.min -eq $finalDepth.max -or $finalHdr.rgbMax -le 0) { throw 'Soak final frame is empty' }

    # ── 재임포트 축: 자극했음을 수로 단정한다 ──
    $placedNames = @($results | Where-Object command -eq 'model.place' | ForEach-Object { $_.data.name })
    $unexpectedNames = @($placedNames | Where-Object { $_ -notin @('Gunner_F_Mythic', $probeName, "$probeName (1)") })
    if ($unexpectedNames.Count -gt 0) {
        throw "배치 이름이 예측과 다르다($($unexpectedNames[0])) — Fresh 를 지우지 못해 쌓이고 있다; artifacts: $run"
    }
    $reimports = @($results | Where-Object command -eq 'model.load' | Select-Object -Skip 1)
    $expectedReimports = [int][math]::Floor(($samples - 1) / $ReimportEvery)
    $reloaded = @($reimports | Where-Object { $_.data.cache -eq 'reloaded' }).Count
    if ($expectedReimports -lt 1) {
        throw "표본 $samples 개로는 재임포트가 0 회다 — -ReimportEvery 를 줄여라; artifacts: $run"
    }
    if ($reloaded -ne $expectedReimports) {
        throw "재임포트 $reloaded/$expectedReimports 회만 새 generation 을 냈다(cache=reloaded); artifacts: $run"
    }
    $mixedSamples = @($status | Where-Object { $_.data.mixedGenerationModels -ge 1 }).Count
    $newest = @($status | ForEach-Object { [uint64]$_.data.mixedNewestGeneration } |
        Where-Object { $_ -gt 0 } | Sort-Object -Unique)
    if ($mixedSamples -eq 0) {
        throw "한 프레임에 같은 모델의 두 generation 이 그려진 표본이 0 이다 — 세대 혼합을 자극하지 못했다; artifacts: $run"
    }
    # 재임포트마다 새 번호가 그려져야 한다. watcher 가 한 번 더 임포트하면 번호는 더 뛸
    # 수 있지만(그래서 '이상'), 모자라면 새 generation 이 화면에 닿지 않은 것이다.
    if ($newest.Count -lt $expectedReimports) {
        throw "그려진 새 generation 이 $($newest.Count) 종뿐이다(재임포트 $expectedReimports 회); artifacts: $run"
    }
    if ($movedFrames -lt [int]($measured * 0.9)) {
        throw "Soak samples did not advance frames ($movedFrames/$measured); artifacts: $run"
    }

    $wallSeconds = $clock.Elapsed.TotalSeconds
    $activeSampleSeconds = ([double]$status[-1].timing.queuedMs -
        [double]$status[$warmupSamples].timing.queuedMs) / 1000.0
    if ($MinimumWallSeconds -gt 0 -and $wallSeconds -lt $MinimumWallSeconds) {
        $measuredPerSample = $wallSeconds / [math]::Max(1, $status.Count)
        throw ("Soak ran {0:0} s of wall time, required {1:0} s — 표본 수가 모자라다. " +
               "-SecondsPerSample {2:0.###} 로 다시 돌려라; artifacts: $run") -f `
            $wallSeconds, $MinimumWallSeconds, $measuredPerSample
    }
    if ($MinimumWallSeconds -gt 0 -and $activeSampleSeconds -lt $MinimumWallSeconds) {
        throw "Active sample interval was $activeSampleSeconds s, required $MinimumWallSeconds s (startup/final capture excluded)"
    }

    [ordered]@{ passed=$true; backend=$Backend; configuration=$Configuration;
        processWallSeconds=$wallSeconds; activeSampleSeconds=$activeSampleSeconds;
        measuredSamples=$measured; warmupSamples=$warmupSamples;
        advancingFrames=$movedFrames; reimports=$reloaded; mixedGenerationSamples=$mixedSamples;
        distinctNewestGenerations=$newest.Count; lastGeneration=$newest[-1];
        sealViolations=0; unstamped=0; encoderDrops=0; textureUploadFailures=0;
        finalFrameId=$final.frameId; finalFinite=$final.finite; finalValidationCount=$final.validationCount;
        pixelCoverage='final frame only; continuous samples use seal and draw counters' } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
    Write-Output ("PBR soak PASS: {0} samples ({1} warmup excluded), {2} advancing frames, backend {3}, requested {4} min, wall {5:0} s" -f `
        $measured, $warmupSamples, $movedFrames, $Backend, $Minutes, $clock.Elapsed.TotalSeconds)
    Write-Output ("  reimport: {0} reloaded, {1} mixed-generation samples, {2} distinct newest generations (last {3})" -f `
        $reloaded, $mixedSamples, $newest.Count, $newest[-1])
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
finally {
    if ($null -ne $original) { [IO.File]::WriteAllBytes($settings, $original) }
    Remove-Probe
}

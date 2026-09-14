# PHASE 4 W0/W2~W8: product-frame capture, shared PBR, backend defaults and seal identity.
# Captures are observations, not W9 visual acceptance or cross-backend goldens.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [ValidateSet('dx12', 'vulkan')][string[]]$Backend = @('dx12', 'vulkan'),
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-pbr-' + [guid]::NewGuid().ToString('N'))
$settings = Join-Path $root 'Dynamic_CPP\ProjectSetting\EngineSettings.asset'
$utf8 = [Text.UTF8Encoding]::new($false)
$original = $null
$process = $null
# W1 fixture 가 model.load 로 복사되어 앉는 자리. finally 가 반드시 치운다 —
# 게이트가 자산 트리에 잔해를 남기면 다음 실행의 전제가 달라진다.
$importedNormalPair = Join-Path $root 'Dynamic_CPP\Assets\Models\NormalPair'

function Invoke-Editor([string]$Name, [string[]]$Commands, [int]$ExpectedExit = 0) {
    $scenario = Join-Path $run "$Name.txt"
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", $utf8)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Resolve-Path -LiteralPath $Editor).Path
    $start.ArgumentList.Add('--commandlet-script')
    $start.ArgumentList.Add($scenario)
    $resultPath = Join-Path $run "$Name.results.jsonl"
    $start.ArgumentList.Add('--result-file')
    $start.ArgumentList.Add($resultPath)
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
    if ($exitCode -ne $ExpectedExit) { throw "$Name exit=$exitCode expected=$ExpectedExit; artifacts: $run" }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) { throw "$Name wrote stderr; artifacts: $run" }
    $results = @(Read-CommandResults $resultPath)
    if ($results.Count -ne $Commands.Count) { throw "$Name terminal result count mismatch; artifacts: $run" }
    for ($i = 0; $i -lt $Commands.Count; $i++) {
        if ($results[$i].command -ne ($Commands[$i] -split ' ', 2)[0]) {
            throw "$Name terminal result order mismatch at $i"
        }
    }
    if ($ExpectedExit -eq 0 -and @($results | Where-Object status -ne 'succeeded').Count) {
        throw "$Name has unsuccessful command results; artifacts: $run"
    }
    return $results
}

function Assert-Capture([string]$Directory, [string]$ExpectedBackend, [string[]]$Models) {
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.source -ne 'product-live' -or $manifest.backend -ne $ExpectedBackend -or
        -not $manifest.finite -or $manifest.validationCount -ne 0 -or
        $manifest.frameId -le $manifest.requestedAfterFrameId -or
        $manifest.draws.Count -eq 0 -or $manifest.attachments.Count -ne 7) {
        throw "Invalid product capture: $Directory"
    }
    foreach ($model in $Models) {
        $meta = [IO.File]::ReadAllText((Join-Path $root "Dynamic_CPP\Assets\Models\$model.glb.meta"))
        $id = [regex]::Match($meta, '(?m)^assetId:\s*(\S+)').Groups[1].Value
        if (-not $id -or $id -notin @($manifest.draws.modelId)) {
            throw "Capture omits visible model $model ($id): $Directory"
        }
    }
    foreach ($draw in $manifest.draws) {
        if ($draw.modelGeneration -le 0 -or $draw.shaderMetaGeneration -le 0 -or
            $draw.propertyBytes.Count -eq 0) { throw "Unsealed material/model: $Directory" }
        if (@($draw.textures | Where-Object property -EQ 'aoMap').Count -ne 1 -or
            @($draw.textures | Where-Object { $_.space -ne 0 -or $_.register -lt 16 -or $_.register -ge 128 }).Count -ne 0) {
            throw "Missing AO/reflected material texture range: $Directory"
        }
    }
    foreach ($attachment in $manifest.attachments) {
        $size = (Get-Item -LiteralPath (Join-Path $Directory $attachment.file)).Length
        $expected = [long]$attachment.width * $attachment.height * $attachment.channels * 4
        if ($size -ne $expected -or $attachment.nonfinite -ne 0) {
            throw "Invalid $($attachment.name) readback: $Directory"
        }
    }
    $depth = $manifest.attachments | Where-Object name -EQ 'depth'
    $hdr = $manifest.attachments | Where-Object name -EQ 'preToneHdr'
    if ($depth.min -ge 1 -or $depth.min -eq $depth.max -or $hdr.rgbMax -le 0) {
        throw "Empty depth/HDR capture: $Directory"
    }

    # W8 — 세대 원자 밀봉. 이 세 단정이 전부 실장면 프레임에서만 성립한다.
    #   ① 제품 draw는 전부 도장을 받는다(도장 없는 draw = 밀봉 경로를 안 탄 것)
    #   ② 한 프레임 안에서 세대가 섞이지 않는다(위반 0)
    #   ③ 인코더가 조용히 버린 명령이 없다(검은 화면·플리커의 직접 경로)
    if (-not $manifest.PSObject.Properties['sealLedger'] -or -not $manifest.sealLedger.recorded) {
        throw "Capture has no seal ledger (W8 배선이 끊겼다): $Directory"
    }
    foreach ($draw in $manifest.draws) {
        if (-not $draw.PSObject.Properties['seal'] -or -not $draw.seal.stamped -or
            $draw.seal.hash -le 0 -or $draw.seal.frameId -ne $manifest.frameId -or
            $draw.seal.sceneEpoch -ne $manifest.sceneEpoch) {
            throw "Draw seal missing or from another frame: $Directory"
        }
    }
    foreach ($pass in @('gbuffer', 'forward')) {
        $ledger = $manifest.sealLedger.$pass
        if ($ledger.violations -ne 0) {
            throw "Seal violations in $pass ($($ledger.lastReason)): $Directory"
        }
        if ($ledger.unstamped -ne 0) {
            throw "Unstamped product draws in $pass : $Directory"
        }
        if ($ledger.frameId -ne $manifest.frameId) {
            throw "Seal ledger frame mismatch in $pass : $Directory"
        }
        if ($ledger.samplerIdentity -le 0 -and $ledger.stamped -gt 0) {
            throw "Sampler identity was never recorded in $pass : $Directory"
        }
    }
    if ($manifest.sealLedger.encoderDrops -ne 0) {
        throw "Encoder dropped commands ($($manifest.sealLedger.lastEncoderDrop)): $Directory"
    }
    if ($manifest.sealLedger.textureUploadFailures -ne 0) {
        throw "Silent neutral substitution after upload failure: $Directory"
    }
}

try {
    if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
        throw 'Close the running Editor before this test; it temporarily selects the startup backend.'
    }
    New-Item -ItemType Directory -Path $run | Out-Null
    $original = [IO.File]::ReadAllBytes($settings)
    $text = $utf8.GetString($original)
    $backendPattern = '(?m)(^render:\r?\n\s{2}backend: )\w+'
    if ([regex]::Matches($text, $backendPattern).Count -ne 1) { throw 'Runtime backend setting is ambiguous.' }
    $captureDirs = @{}
    foreach ($api in $Backend) {
        [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, "`${1}$api"), $utf8)
        $primitive = Join-Path $run "$api-primitives"
        $gunner = Join-Path $run "$api-gunner"
        $normalPair = Join-Path $run "$api-normalpair"
        # W1 fixture 는 자산 트리 밖에 있어 model.load 가 트리 안으로 복사한다.
        # backend 둘이 같은 조건에서 돌도록 매 회차 앞에서 지운다 — 남겨 두면
        # 두 번째 회차만 "제자리 열기" 경로를 타서 두 실행이 같은 것을 재지 않는다.
        if (Test-Path -LiteralPath $importedNormalPair) {
            Remove-Item -Recurse -Force -LiteralPath $importedNormalPair
        }
        $commands = @(
            "scene.switch `"$root/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator`"",
            # ★ 첫 대기가 렌더 예열을 겸한다 (2026-09-14). 라이브 렌더러의 첫
            #   프레임은 파이프라인 구축과 ShaderMeta 적용으로 Debug 에서
            #   13~28초가 걸린다. 예전에는 30 프레임으로도 통했는데, 그것은
            #   BuildPipeline 이 표시 락을 구축 내내 쥐어 게임 스레드까지
            #   멈춰 세운 덕이었다 — 프레임이 느려서 수가 곧 시간이었다.
            #   뒤의 `wait 30` 들은 그대로 둔다: 예열은 프로세스당 한 번이다.
            'wait 2000',
            # ★ 게임 타깃은 **수요가 있어야** 그려진다 (PHASE 21 W4). 중앙
            #   ViewportHost 가 Scene 모드이면 게임 뷰를 아예 만들지 않으므로
            #   `game` 캡처가 영원히 완료되지 않는다 — 모드를 먼저 세운다.
            'editor.viewport game',
            'wait 30',
            "render.pbr.capture `"$primitive`" game",
            "model.loadcached `"$root/Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb`"",
            'model.place Gunner_F_Mythic',
            'object.transform Gunner_F_Mythic 0 0 2 0 180 0 0.025 0.025 0.025',
            'wait 30',
            "render.pbr.capture `"$gunner`" game",
            # ★ W1 — normal-map 저작 유무의 정본이 하나인지는 **실장면 프레임**에서만
            #   물을 수 있다. 격리 fixture 에는 스냅샷이 없어 draw.useNormalMap 폴백이
            #   쓰이기 때문이다(EnhancedGBufferPass.cpp:439). 노멀맵 있는 재질과 없는
            #   재질을 **한 자산의 두 primitive** 로 두어 같은 노드·같은 프레임 아래
            #   draw 둘이 뜨게 한다 — 그래야 변인이 재질 하나로 좁혀진다.
            "model.load `"$root/Tools/regression/fixtures/pbr-normal-pair/NormalPair.gltf`"",
            'model.place NormalPair',
            'object.transform NormalPair 0 0 2 0 180 0 1 1 1',
            'wait 30',
            "render.pbr.capture `"$normalPair`" game",
            "render.pbr.normalpair `"$normalPair`"",
            'quit')
        $results = @(Invoke-Editor $api $commands)
        $captures = @($results | Where-Object command -eq 'render.pbr.capture')
        if ($captures.Count -ne 3 -or @($captures | Where-Object { $_.data.frameId -le 0 }).Count) {
            throw "$api capture did not return three completed frames; artifacts: $run"
        }
        Assert-Capture $primitive $api @('Prim_Cube', 'Prim_Sphere', 'Prim_Cylinder')
        Assert-Capture $gunner $api @('Gunner_F_Mythic')
        # 이 캡처의 모델 확인은 Models 목록이 아니라 render.pbr.normalpair 가 한다.
        # Assert-Capture 의 모델 검사는 `<이름>.glb.meta` 를 전제하는데 이 fixture 는
        # 하위 폴더의 `.gltf` 다. 그리고 여기서 물어야 할 것은 "모델이 떴나" 가
        # 아니라 "대조쌍이 섰고 두 유도가 같은 답을 내나" 다.
        Assert-Capture $normalPair $api @()
        $normalVerdict = Get-SucceededCommand $results 'render.pbr.normalpair'
        if (-not $normalVerdict.passed) {
            throw "$api normal-map pair verification failed; artifacts: $run"
        }
        $captureDirs[$api] = @{ primitives = $primitive; gunner = $gunner }
        Write-Output "$api product capture PASS: $primitive; $gunner; $normalPair (normal pair PASS)"
    }
    # W9 — 두 backend 캡처의 float32 readback을 실제로 맞댄다.
    #
    # ★ W0부터 이 .f32 들을 읽는 코드가 저장소에 하나도 없었다. 게이트는 파일
    #   크기만 재고 있었고 계획서의 RMSE 는 손으로 한 번 잰 값이었다. 여기서
    #   비로소 "같은 그림인가"가 게이트의 질문이 된다. 한 backend 만 돌린
    #   실행에서는 맞댈 짝이 없으므로 건너뛴다(그 사실을 출력한다).
    if ($captureDirs.Count -eq 2) {
        $left = $captureDirs['dx12']
        $right = $captureDirs['vulkan']
        foreach ($fixture in @('primitives', 'gunner')) {
            $outputJson = Join-Path $run "compare-$fixture.json"
            $results = @(Invoke-Editor "compare-$fixture" @(
                "render.pbr.compare `"$($left[$fixture])`" `"$($right[$fixture])`" `"$outputJson`"",
                'quit'))
            $data = Get-SucceededCommand $results 'render.pbr.compare'
            if (-not $data.passed) { throw "Cross-backend pixel comparison failed: $fixture" }
            if (-not (Test-Path -LiteralPath $outputJson)) {
                throw "Comparison result was not written: $outputJson"
            }
        }
        Write-Output "cross-backend capture compare PASS"
    } else {
        Write-Output "cross-backend capture compare SKIPPED (backend 하나만 실행됨)"
    }

    # The paired harness owns both DX12 and Vulkan test devices; keep its Editor
    # host on DX12 independently of the final product-capture backend above.
    [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, '${1}dx12'), $utf8)
    $null = Invoke-Editor 'defaults' @('vk.shadow', 'vk.gbuffer', 'vk.forward', 'vk.deferred', 'quit')
    $null = Invoke-Editor 'forward-shade' @('dx12.forwardshade', 'quit')
    # Material codec/seal contracts moved out of the Editor registry in PHASE 14.5.
    $contractLog = Join-Path $run 'experiment-contract.log'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify-experiment-contract.ps1') -Configuration $Configuration *> $contractLog
    if ($LASTEXITCODE -ne 0) { throw "Standalone material contracts failed; log: $contractLog" }
    $contractCommands = @('experiment.matresolve', 'experiment.matmigrate', 'experiment.cooked')
    $null = Invoke-Editor 'material-contracts' ($contractCommands + 'quit')
    foreach ($name in @('seal', 'parity', 'coverage', 'emission', 'transform', 'uv', 'mip', 'occlusion')) {
        $results = @(Invoke-Editor $name @("render.pbr.$name", 'quit'))
        $data = Get-SucceededCommand $results "render.pbr.$name"
        if (-not $data.passed) { throw "PBR $name did not execute its verification" }
    }
    # A later successful test must not erase an earlier failure exit code.
    $results = @(Invoke-Editor 'negative-exit' @('render.livecheck 1 1', 'dx12.gbuffer', 'quit') 4)
    if ((Get-CommandResult $results 'render.livecheck').status -ne 'failed') {
        throw 'Negative exit probe did not exercise failure.'
    }
    $null = Get-SucceededCommand $results 'dx12.gbuffer'
    $results = @(Invoke-Editor 'negative-capture' @("render.pbr.capture `"$run`" game", 'quit') 4)
    $capture = Get-CommandResult $results 'render.pbr.capture'
    if ($capture.status -ne 'failed' -or $capture.code -ne 'render.pbr.capture.rejected') {
        throw 'Capture did not reject an existing output directory.'
    }
    Write-Output "PBR W0/W1/W2/W3/W4/W5/W6/W7-normal/UV/mip baseline PASS (W9 acceptance pending): $run"
}
finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    if ($null -ne $original) { [IO.File]::WriteAllBytes($settings, $original) }
    if (Test-Path -LiteralPath $importedNormalPair) {
        Remove-Item -Recurse -Force -LiteralPath $importedNormalPair -ErrorAction SilentlyContinue
    }
}

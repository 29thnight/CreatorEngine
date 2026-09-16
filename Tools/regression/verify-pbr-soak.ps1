# PHASE 4 W9 — 장시간 회전·이동·재임포트 중 세대 밀봉이 깨지지 않는지.
#
# ★ 이 검사가 재는 것은 "10분 동안 검은 프레임이 없었다"가 아니라 **검은
#   프레임을 만드는 기계장치가 한 번도 돌지 않았다**이다. 픽셀을 매 프레임
#   읽는 것은 감당할 수 없고(1920x1080x4 float 7장), 눈으로 보는 것은 게이트가
#   아니다. W8이 세운 수 셋이 그 자리를 대신한다.
#     · seal 위반(세대 혼합·지난 프레임 밀봉·기록 단계 누락)
#     · 인코더가 조용히 버린 명령(놓인 PSO 핸들·만료 descriptor)
#     · 업로드 실패를 흰색으로 덮은 횟수
#   이 셋이 0이 아닌 채로 그려진 프레임이 곧 검정/변색 프레임의 원인이다.
#
# ★ drawCount 도 함께 본다. 위 셋이 0이어도 그릴 것이 0이면 화면은 검다 —
#   "위반 없음"과 "그렸음"은 다른 질문이다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [ValidateSet('dx12', 'vulkan')][string]$Backend = 'dx12',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    # 기본값은 게이트용으로 짧다. W9 acceptance 는 -Minutes 10 으로 돌린다.
    [double]$Minutes = 1.0,
    [int]$TimeoutSeconds = 1800
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-pbr-soak-' + [guid]::NewGuid().ToString('N'))
$settings = Join-Path $root 'Dynamic_CPP\ProjectSetting\EngineSettings.asset'
$utf8 = [Text.UTF8Encoding]::new($false)
$original = $null

# 한 표본이 도는 데 드는 프레임 수. Debug 라이브 렌더러의 실측 프레임 시간에
# 맞춰 잡았고, 정확한 벽시계가 아니라 "충분히 많은 표본"이 목적이다.
$framesPerSample = 30
$secondsPerSample = 0.6

function New-SoakCommands([int]$Samples) {
    $commands = [Collections.Generic.List[string]]::new()
    $commands.Add("scene.switch `"$root/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator`"")
    # 첫 대기가 렌더 예열을 겸한다(캡처 게이트와 같은 이유).
    $commands.Add('wait 2000')
    $commands.Add('editor.viewport game')
    $commands.Add("model.loadcached `"$root/Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb`"")
    $commands.Add('model.place Gunner_F_Mythic')
    $commands.Add('wait 30')

    for ($i = 0; $i -lt $Samples; $i++) {
        # 회전과 이동을 함께 준다 — 회전만 주면 배치 키가 거의 그대로라
        # 재밀봉 경로가 충분히 돌지 않는다.
        $angle = ($i * 23) % 360
        $z = 2.0 + [math]::Sin($i * 0.4)
        $commands.Add(
            ('object.transform Gunner_F_Mythic 0 0 {0:0.###} 0 {1} 0 0.025 0.025 0.025' -f $z, $angle))
        $commands.Add("wait $framesPerSample")
        $commands.Add('render.pbr.sealstatus')

        # 재임포트를 주기적으로 섞는다. 같은 모델을 다시 열면 generation 이
        # 갈리고, 그 순간이 세대 혼합이 일어날 수 있는 자리다 — 계획이 이
        # 검사에 재임포트를 함께 요구한 이유다.
        if (0 -eq ($i % 10) -and $i -gt 0) {
            $commands.Add("model.loadcached `"$root/Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb`"")
            $commands.Add('wait 10')
            $commands.Add('render.pbr.sealstatus')
        }
    }
    $commands.Add('quit')
    return $commands
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
    [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, "`${1}$Backend"), $utf8)

    $samples = [int][math]::Max(10, [math]::Ceiling($Minutes * 60 / $secondsPerSample))
    $scenario = Join-Path $run 'soak.txt'
    [IO.File]::WriteAllText($scenario, ((New-SoakCommands $samples) -join "`n") + "`n", $utf8)
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
    $process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "Soak timed out after $TimeoutSeconds s; artifacts: $run"
    }
    [IO.File]::WriteAllText((Join-Path $run 'soak.out'), $stdoutTask.Result, $utf8)
    [IO.File]::WriteAllText((Join-Path $run 'soak.err'), $stderrTask.Result, $utf8)
    if ($process.ExitCode -ne 0) {
        throw "Soak exited $($process.ExitCode); artifacts: $run"
    }

    $results = @(Read-CommandResults $resultPath)
    $status = @($results | Where-Object command -eq 'render.pbr.sealstatus')
    if ($status.Count -lt $samples) {
        throw "Soak collected $($status.Count) samples, expected at least $samples; artifacts: $run"
    }

    $previousFrame = -1
    $movedFrames = 0
    foreach ($sample in $status) {
        $data = $sample.data
        if ($sample.status -ne 'succeeded') { throw "sealstatus sample failed; artifacts: $run" }
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
    if ($movedFrames -lt [int]($status.Count * 0.9)) {
        throw "Soak samples did not advance frames ($movedFrames/$($status.Count)); artifacts: $run"
    }

    Write-Output ("PBR soak PASS: {0} samples, {1} advancing frames, backend {2}, {3} min" -f `
        $status.Count, $movedFrames, $Backend, $Minutes)
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
finally {
    if ($null -ne $original) { [IO.File]::WriteAllBytes($settings, $original) }
}

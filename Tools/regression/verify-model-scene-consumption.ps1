# Scene/MeshRenderer/material 직접 소비와 CreatorRobot cold-load closure.
# A: FT_Primitives 위에 Scale 1 로봇 배치 -> 4 renderer / 16 texture property 검증 -> 저장.
# B: 별도 프로세스로 로드 -> 같은 검증 -> 중복 reload -> 실 GPU 업로드/커버리지.
# 고유 texture owner는 12개이며 ORM은 MetallicRoughness/Occlusion이 공유한다.
# 중복 reload는 같은 generation과 owner 12개를 유지해야 한다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [switch]$PreserveArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $Work ('creator-mbc7-scene-' + [guid]::NewGuid().ToString('N'))
$failures = [System.Collections.Generic.List[string]]::new()
function Add-Failure([string]$Message) { $failures.Add($Message) }

function Invoke-Editor([string]$Label, [string[]]$Commands) {
    $scenario = Join-Path $run ($Label + '.commands.txt')
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--commandlet-script "' + $scenario.Replace('"', '\"') + '"'
    $resultPath = Join-Path $run ($Label + '.results.jsonl')
    $start.Arguments += ' --result-file "' + $resultPath + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "CreatorEditor 시작 실패: $Editor" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "CreatorEditor timeout($Label): $run"
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stdout.txt')), $stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stderr.txt')), $stderr,
        [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ Results = @(Read-CommandResults $resultPath); Stdout = $stdout; Stderr = $stderr; ExitCode = $process.ExitCode }
}

function Assert-SceneModelClosure([string]$Label, $Results) {
    $data = Get-SucceededCommand @($Results | Where-Object { $_.command -ne 'assets.scenemodel' -or -not $_.data.reload }) 'assets.scenemodel'
    if ($data.generationBound -lt 4 -or $data.unbound -ne 0 -or $data.handleInvalid -ne 0 -or
        $data.rhiView -ne $data.generationBound -or $data.meshIdPersisted -ne $data.generationBound -or
        $data.embeddedProps -ne 16 -or $data.generationTextures -ne 16 -or $data.otherTextures -ne 0 -or
        $data.missingTextures -ne 0 -or $data.robotEmbedded -ne 16) {
        Add-Failure "$Label typed scene model closure failed: $($data | ConvertTo-Json -Compress)"
    }
}

function Assert-RenderWaits([string]$Label, $Results, [int]$Expected) {
    # 예열 대기가 실제로 서지 않았으면 뒤의 판정은 준비되지 않은 렌더러를 잰 것이다.
    $waits = @($Results | Where-Object command -eq 'render.live.wait')
    if ($waits.Count -ne $Expected -or @($waits | Where-Object status -ne 'succeeded').Count -ne 0) {
        Add-Failure "$Label render.live.wait $($waits.Count)/$Expected 성공하지 않았다: $($waits | ConvertTo-Json -Compress -Depth 4)"
    }
}

try {
    if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) {
        throw "CreatorEditor 실행 파일이 없다: $Editor"
    }
    $robot = Join-Path $root 'Dynamic_CPP\Assets\Models\CreatorRobot.glb'
    if (-not (Test-Path -LiteralPath $robot -PathType Leaf)) { throw "스킨 모델이 없다: $robot" }
    $baseScene = Join-Path $root 'Dynamic_CPP\Assets\Scenes\FT_Primitives.creator'
    if (-not (Test-Path -LiteralPath $baseScene -PathType Leaf)) { throw "기준 씬이 없다: $baseScene" }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($run -match '\s') { throw "console 인자로 넘길 작업 경로에 공백이 있다: $run" }
    $savedScene = (Join-Path $run 'mbc7_robot.creator').Replace('\', '/')

    # ── A: 저작 프로세스 — 배치·폐포·저장 ──
    $author = Invoke-Editor 'author' @(
        "scene.switch $($baseScene.Replace('\', '/'))",
        # 렌더 예열은 프레임 수가 아니라 렌더 스레드의 완료 프레임으로 기다린다.
        # `wait 2000` 은 Release 에서 첫 프레임(ShaderMeta 반영 ~24초)을 덮지 못했다 —
        # B 의 주석과 render.live.wait 핸들러 주석 참고.
        'render.live.wait',
        "model.loadcached $($robot.Replace('\', '/'))",
        'wait 60',
        'model.place CreatorRobot',
        'wait 60',
        # 원점의 캐릭터는 씬 카메라를 삼켜 커버리지가 전면이 된다 — 저작값으로 옮겨
        # 저장·재로드에 반영한다(vertex-live 게이트와 같은 처방).
        'object.transform CreatorRobot 1.5 0 0 0 0 0 1 1 1',
        'wait 10',
        # 애니메이션 틱은 실시간이라 렌더가 비결정적이다 — 저장 전에 꺼서 B가
        # 결정적 바인드 포즈로 그리게 한다(vertex-live 게이트와 같은 처방).
        'object.property CreatorRobot Animator m_isEnabled false',
        'wait 10',
        'assets.modeldiag',
        'assets.scenemodel',
        "scene.save $savedScene",
        'wait 30',
        'quit')
    if ($author.ExitCode -ne 0) { Add-Failure "A 종료 코드 $($author.ExitCode)" }
    Assert-RenderWaits 'A' $author.Results 1
    if (-not [string]::IsNullOrWhiteSpace($author.Stderr)) { Add-Failure 'A stderr가 비어 있지 않다.' }
    # MBC10 — 배치 관측은 읽기 전용 스냅샷이다(제품 stdout 토큰 없음).
    $authorDiag = Get-SucceededCommand $author.Results 'assets.modeldiag'
    if ($authorDiag.instantiateGeneration -ne 1 -or $authorDiag.lastInstantiated -ne 'CreatorRobot' -or $authorDiag.instantiateRejected -ne 0) { Add-Failure 'A typed model placement coverage failed' }
    if ($author.Stdout -match '\[(mesh\.resolve|model\.instantiate|anim\.tick|material\.finalize)\]') {
        Add-Failure '1c 제품 경로가 무조건 진단 토큰을 다시 찍는다(MBC10).'
    }
    Assert-SceneModelClosure '2(A)' $author.Results

    if (-not (Test-Path -LiteralPath $savedScene)) { throw "저장 씬이 없다: $savedScene" }
    $sceneText = [IO.File]::ReadAllText($savedScene)
    $meshIds = [regex]::Matches($sceneText,
        'm_meshAssetId: [0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')
    if ($meshIds.Count -lt 4) { Add-Failure "3 저장 씬의 UUIDv8 m_meshAssetId가 4 미만이다: $($meshIds.Count)" }
    if ($sceneText -match 'm_meshAssetId: 00000000-0000-0000-0000-000000000000') {
        Add-Failure '3 nil m_meshAssetId가 저장됐다(해석 실패 renderer).'
    }

    # ── B: 콜드 프로세스 — 로드·폐포·중복 reload 알림·렌더 ──
    $cold = Invoke-Editor 'cold' @(
        "scene.switch $savedScene",
        # B 도 별도 프로세스라 렌더 예열을 처음부터 다시 치른다.
        # ★ 예전 `wait 2000` 은 프레임 수로 시간을 대신했다. 라이브 렌더러의 첫 프레임은
        #   GBuffer ShaderMeta 반영(slang reflect)에 Release 에서 약 24초를 쓰는데,
        #   Release 의 2000 프레임은 몇 초 만에 지나가 dx12.scene 의 10초 RenderThread
        #   drain 이 첫 프레임 도중에 걸렸다(`pending 2 · active 1`, 2026-09-17 cdb 스택).
        #   render.live.wait 는 이 명령 뒤 발행된 프레임을 렌더 스레드가 끝낼 때까지
        #   게임 스레드를 세우지 않고 다음 명령을 미룬다.
        'render.live.wait',
        'assets.modeldiag',
        'assets.scenemodel',
        'assets.scenemodel reload CreatorRobot',
        # 중복 알림 뒤에도 실제 scene 렌더가 이어져야 한다.
        'render.live.wait',
        'dx12.scene',
        'quit')
    if ($cold.ExitCode -ne 0) { Add-Failure "B 종료 코드 $($cold.ExitCode)" }
    Assert-RenderWaits 'B' $cold.Results 2
    if (-not [string]::IsNullOrWhiteSpace($cold.Stderr)) { Add-Failure 'B stderr가 비어 있지 않다.' }
    # FT 프리미티브 8 + Robot 4 — UUIDv8 corpus라 전량 typed여야 한다(해석 실패 0).
    $coldDiag = Get-SucceededCommand $cold.Results 'assets.modeldiag'
    if ($coldDiag.meshResolveGeneration -lt 12 -or $coldDiag.meshResolveFailed -ne 0) { Add-Failure 'Cold typed model resolution coverage failed' }
    Assert-SceneModelClosure '5(B)' $cold.Results
    $reload = Get-SucceededCommand @($cold.Results | Where-Object { $_.command -eq 'assets.scenemodel' -and $_.data.reload }) 'assets.scenemodel'
    if ($reload.textures -ne 12 -or $reload.reused -ne 12 -or $reload.created -ne 0 -or
        $reload.retired -ne 0 -or -not $reload.sameAggregate) { Add-Failure 'Duplicate reload failed to preserve generation and texture owners' }
    $sceneData = Get-SucceededCommand $cold.Results 'dx12.scene'
    if ($sceneData.meshUploads -lt 1 -or $sceneData.generationUploads -ne $sceneData.meshUploads -or $sceneData.coverage -le 0) { Add-Failure 'Typed GPU upload and coverage failed' }

    # ── 8: 정적 ──
    $meshRenderer = [IO.File]::ReadAllText((Join-Path $root 'Engine\SceneRuntime\MeshRenderer.cpp'))
    if ($meshRenderer -match 'modelGuidHint') {
        Add-Failure '8 MeshRenderer에 재질 노드 선독 순서 해킹(modelGuidHint)이 있다.'
    }
    if ($meshRenderer -notmatch 'BindModelGeneration' -or $meshRenderer -notmatch 'LoadModelAssetGeneration') {
        Add-Failure '8 MeshRenderer가 typed generation을 붙들지 않는다.'
    }
    $bridge = [IO.File]::ReadAllText((Join-Path $root 'Engine\SceneRuntime\PrimitiveProxyBridge.cpp'))
    if ($bridge -notmatch 'm_modelGeneration = component->m_modelGeneration') {
        Add-Failure '8 프록시가 typed generation을 복사하지 않는다.'
    }
    $renderer = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\Render\Scene\EnhancedSceneRenderer.cpp'))
    if ($renderer -notmatch 'BuildRHIModelMeshView\(\*proxy->m_modelGeneration' -or
        $renderer -notmatch 'generationSource') {
        Add-Failure '8 drawPool이 typed 뷰를 채우지 않거나 generation 수명을 붙들지 않는다.'
    }
    $dataSystem = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\DataSystem.cpp'))
    if ($dataSystem -notmatch 'ResolveModelGenerationTexture' -or
        $dataSystem -notmatch 'RetireModelGenerationTextures\(retired->Handle\(\)\)') {
        Add-Failure '8 generation texture 캐시 또는 generation 단위 retire가 빠졌다.'
    }
}
catch {
    Add-Failure "예외: $($_.Exception.Message)"
}

$summary = "model-scene-consumption failures=$($failures.Count) run=$run"
if ($failures.Count -gt 0) {
    $summary
    $failures | ForEach-Object { "  $_" }
    exit 1
}
$summary
'통과 — Scene/MeshRenderer typed generation·CreatorRobot cold closure 16/16, unique textures 12, 중복 reload의 model/texture owner 보존, 실GPU 업로드 전량 typed'
if (-not $PreserveArtifacts) {
    $workRoot = [IO.Path]::GetFullPath($Work).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $runPath = [IO.Path]::GetFullPath($run)
    if (-not $runPath.StartsWith($workRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Run directory escaped work root' }
    Remove-Item -LiteralPath $runPath -Recurse -Force -ErrorAction SilentlyContinue
}
exit 0

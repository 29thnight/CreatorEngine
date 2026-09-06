# TransformUpdatePlan X8 frame-persistent render proxy dirty queue gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false
$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\RenderProxyDirty.h"; Pattern = "Transform  = 1u << 0"; Label = "Transform bit" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\RenderProxyDirty.h"; Pattern = "Material   = 1u << 1"; Label = "Material bit" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\RenderProxyDirty.h"; Pattern = "Visibility = 1u << 2"; Label = "Visibility bit" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\RenderProxyDirty.h"; Pattern = "LOD        = 1u << 3"; Label = "LOD bit" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\RenderProxyDirty.h"; Pattern = "Payload    = 1u << 4"; Label = "Payload bit" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "registration.pending |= dirty"; Label = "mask OR publication" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "found->second.generation != ticket.generation"; Label = "registration generation validation" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.cpp"; Pattern = "PublishRenderProxyDirty(handle, ProxyDirty::Transform)"; Label = "sparse resolver publication" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Component.cpp"; Pattern = "ProxyDirty::Visibility"; Label = "enabled writer publication" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\MeshRenderer.h"; Pattern = "ProxyDirty::Material"; Label = "material setter publication" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\MeshRenderer.h"; Pattern = "ProxyDirty::LOD"; Label = "LOD setter publication" }
)
foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path -LiteralPath $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

$sceneSource = [System.IO.File]::ReadAllText(
    (Join-Path $repo "Engine\SceneRuntime\Scene.cpp"))
function Test-DirtyQueueContract([string]$source) {
    return $source.Contains("registration.pending |= dirty") -and
        $source.Contains("found->second.generation != ticket.generation") -and
        $source.Contains("registry.dirtyQueue.push_back") -and
        $source.Contains("registry.drainQueue.swap(registry.dirtyQueue)") -and
        $source.Contains("case Kind::Light:") -and
        (-not $source.Contains("meshSnapshot")) -and
        (-not $source.Contains("UIManagers->Images"))
}
if (-not (Test-DirtyQueueContract $sceneSource)) {
    "FAIL: X8 dirty queue/static commit contract"
    $failed = $true
}

$mutations = @(
    "registration.pending |= dirty",
    "found->second.generation != ticket.generation",
    "registry.drainQueue.swap(registry.dirtyQueue)",
    "case Kind::Light:"
)
foreach ($mutation in $mutations) {
    $mutated = $sceneSource.Replace($mutation, "")
    if (Test-DirtyQueueContract $mutated) {
        "FAIL: mutation stayed GREEN: $mutation"
        $failed = $true
    } else {
        "mutation RED: $mutation"
    }
}

$lightSystem = [System.IO.File]::ReadAllText(
    (Join-Path $repo "Engine\SceneRuntime\LightSystem.cpp"))
if ($lightSystem.Contains("UpdateCommand(light)")) {
    "FAIL: LightSystem still commits before the final transform resolve"
    $failed = $true
}

$proxyUpdateCount = ([regex]::Matches($sceneSource,
    'PublishRenderProxyDirty\([^;]+ProxyDirty::Transform\)')).Count
if ($proxyUpdateCount -lt 6) {
    "FAIL: expected transform publication in legacy/sparse/pull/batch/UI paths, actual=$proxyUpdateCount"
    $failed = $true
}

if ($failed) { exit 1 }
if (-not (Test-Path -LiteralPath $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "render_proxy_dirty_$runId.txt"
$outFile = Join-Path $Work "render_proxy_dirty_$runId.out"
$errFile = Join-Path $Work "render_proxy_dirty_$runId.err"
$commands = @(
    "scene.proxybench 128 1",
    "scene.proxybench 128 256",
    "scene.proxydirty probe",
    "quit"
)
[System.IO.File]::WriteAllLines($scenario, $commands)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$resultPath = $outFile + ".jsonl"
$proc = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"'+$scenario+'"'), '--result-file', ('"'+$resultPath+'"')) `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: X8 runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$commandResults = @(Read-CommandResults $resultPath)
if (@($commandResults | Where-Object status -ne 'succeeded').Count -ne 0) { throw 'Runtime command failed or was unavailable' }
$output = [System.IO.File]::ReadAllText($outFile)
$dedupe = 'dedupe=PASS publish=5 folded=4 drained=1 mask=0x1f'
$stationary = 'stationary=PASS passes=3 committed=0 phase=PASS pending=1 writers=PASS'
$generation = 'generation=PASS drained=2 stale=1 committed=1 probe=PASS'
$benchMatches = [regex]::Matches($output,
    '\[scene\.proxybench\] mode=stationary registered=(\d+) frames=128 avg=([0-9.]+)us .* committed=0 selfcheck=PASS')
$benchPass = $benchMatches.Count -eq 2 -and
    [int]$benchMatches[0].Groups[1].Value -eq 1 -and
    [int]$benchMatches[1].Groups[1].Value -eq 256
if (($proc.ExitCode -ne 0) -or
    ($output -notmatch [regex]::Escape($dedupe)) -or
    ($output -notmatch [regex]::Escape($stationary)) -or
    ($output -notmatch [regex]::Escape($generation)) -or
    (-not $benchPass)) {
    "FAIL: X8 render proxy dirty runtime gate"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.(proxydirty|proxybench)' }
    if (Test-Path -LiteralPath $errFile) { Get-Content -LiteralPath $errFile }
    exit 1
}

$output -split "`r?`n" | Where-Object { $_ -match 'scene\.(proxydirty|proxybench)' }
if ($benchPass) {
    "stationary scaling: registered=1 avg=$($benchMatches[0].Groups[2].Value)us; registered=256 avg=$($benchMatches[1].Groups[2].Value)us"
}
"PASS: X8 final single commit, dirty-mask dedupe, all resolve phases, writer bits, generation lifetime"

param(
    [string]$Editor = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-corpus-placement-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
$models = @(Get-ChildItem (Join-Path $root 'Dynamic_CPP/Assets') -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in '.fbx','.glb','.gltf' } | Sort-Object FullName)
if ($models.Count -eq 0) { throw 'No authoring models found' }
$before = @{}
$commands = [Collections.Generic.List[string]]::new()
foreach ($model in $models) {
    $meta = [IO.File]::ReadAllText($model.FullName + '.meta')
    $id = [regex]::Match($meta, '(?m)^assetId: (.+)\r?$').Groups[1].Value.Trim()
    if (!$id) { throw ('Missing model identity: ' + $model.FullName) }
    $before[$model.FullName] = @{id=$id;sha=(Get-FileHash -LiteralPath $model.FullName).Hash}
    $commands.Add('scene.new CorpusPlacement')
    $commands.Add('object.create RenderProbeCamera')
    $commands.Add('component.add RenderProbeCamera CameraComponent')
    $commands.Add('object.transform RenderProbeCamera 0 1 -5 0 0 0')
    $commands.Add('object.create RenderProbeLight Light')
    $commands.Add('object.transform RenderProbeLight 0 2 0 45 -25 0')
    $commands.Add('model.async "' + $model.FullName + '"')
    $commands.Add('model.async wait')
    $commands.Add('model.async status')
    if ($model.BaseName -eq 'Ani_Mon_3_die') {
        # This FBX has a centimetre-scaled animated extent, unlike the metre GLBs.
        # Frame the temporary probe scene; do not alter the source or import settings.
        $commands.Add('object.transform Ani_Mon_3_die 0 0 0 0 0 0 0.01 0.01 0.01')
        $commands.Add('object.transform RenderProbeCamera 0 0 -200 0 0 0')
    }
    $commands.Add('camera.editor match')
    $commands.Add('wait 120')
    $commands.Add('render.pbr.capture "' + (Join-Path $run ($model.BaseName + '-capture')) + '" editor')
    $commands.Add('scene.save "' + (Join-Path $run ($model.BaseName + '.scene')) + '"')
}
$commands.Add('editor.selftest')
$commands.Add('quit')
$utf8 = [Text.UTF8Encoding]::new($false)
$scriptPath = Join-Path $run 'commands.txt'
$resultPath = Join-Path $run 'results.jsonl'
[IO.File]::WriteAllLines($scriptPath, $commands, $utf8)
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = [IO.Path]::GetFullPath($Editor)
$start.ArgumentList.Add('--smoke-offscreen')
$start.ArgumentList.Add('--commandlet-script'); $start.ArgumentList.Add($scriptPath)
$start.ArgumentList.Add('--result-file'); $start.ArgumentList.Add($resultPath)
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($start)
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill(); $process.WaitForExit(); throw "Corpus placement timed out: $run"
}
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
[IO.File]::WriteAllText((Join-Path $run 'stdout.txt'), $stdout, $utf8)
[IO.File]::WriteAllText((Join-Path $run 'stderr.txt'), $stderr, $utf8)
$rows = @(Read-CommandResults $resultPath)
if ($process.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows | Where-Object status -ne succeeded).Count) {
    throw "Corpus command failed: $run"
}
$ready = [regex]::Matches($stdout, '\[model\.async\] ready path=(.*?) steps=')
if ($ready.Count -ne $models.Count) { throw "Only $($ready.Count)/$($models.Count) models were placed: $run" }
$records = @()
foreach ($model in $models) {
    if (@($ready | Where-Object { $_.Groups[1].Value -eq $model.FullName }).Count -ne 1) {
        throw ('Placement missing or repeated: ' + $model.FullName)
    }
    $meta = [IO.File]::ReadAllText($model.FullName + '.meta')
    $id = [regex]::Match($meta, '(?m)^assetId: (.+)\r?$').Groups[1].Value.Trim()
    if ($id -ne $before[$model.FullName].id -or (Get-FileHash -LiteralPath $model.FullName).Hash -ne $before[$model.FullName].sha) {
        throw ('Model source or identity changed: ' + $model.FullName)
    }
    $scene = [IO.File]::ReadAllText((Join-Path $run ($model.BaseName + '.scene')))
    $meshCount = [regex]::Matches($scene, 'm_meshAssetId:').Count
    if ([regex]::IsMatch($meta, '(?m)^\s+- kind: mesh\s*$') -and $meshCount -eq 0) {
        throw ('Model has no persisted scene meshes: ' + $model.FullName)
    }
    $capture = Join-Path $run ($model.BaseName + '-capture')
    $manifest = [IO.File]::ReadAllText((Join-Path $capture 'manifest.json')) | ConvertFrom-Json
    $draws = @($manifest.draws)
    if (!$manifest.finite -or $manifest.viewId -ne 1 -or ($meshCount -gt 0 -and $draws.Count -eq 0)) {
        throw ('Model has no actual Scene render output: ' + $model.FullName)
    }
    if ($model.BaseName -eq 'Ani_Mon_3_die' -and
        ($draws.Count -ne 6 -or @($draws | ForEach-Object { $_.world -join ',' } | Select-Object -Unique).Count -ne 1)) {
        throw 'FBX material-split meshes must retain one shared source-node transform'
    }
    $depth = [IO.File]::ReadAllBytes((Join-Path $capture 'depth.f32'))
    $covered = 0
    for ($offset = 0; $offset -lt $depth.Length; $offset += 4) {
        $value = [BitConverter]::ToSingle($depth, $offset)
        if ([single]::IsNaN($value) -or [single]::IsInfinity($value)) { throw 'Nonfinite rendered depth' }
        if ($value -lt 0.9999) { ++$covered }
    }
    if ($meshCount -gt 0 -and !$covered) { throw ('Model produces no mesh depth pixels: ' + $model.FullName) }
    $records += @{path=$model.FullName;modelId=$id;sceneMeshes=$meshCount;sourcePreserved=$true;
        renderedDraws=$draws.Count;coveredDepthPixels=$covered;capture=$capture}
}
$statuses = [regex]::Matches($stdout, '\[model\.async\] pending=(\d+) completed=(\d+) failed=(\d+) cancelled=(\d+)')
if (!$statuses.Count -or [int]$statuses[-1].Groups[2].Value -ne $models.Count -or [int]$statuses[-1].Groups[3].Value -ne 0) {
    throw "Placement queue did not complete without failures: $run"
}
@{passed=$true;models=$records;placements=$ready.Count;menuSelfTest=$true;renderingVerified=$true} | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
"Model corpus placement and rendering PASS ($($models.Count) models): $run"

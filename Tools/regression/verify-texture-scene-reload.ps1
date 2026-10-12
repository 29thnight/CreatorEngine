[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'An Editor is already running; use an isolated session.' }
$configs = if ($Configuration -eq 'All') { @('Debug','Release') } else { @($Configuration) }
$sourcePaths = @('Engine/SceneRuntime/SceneManager.cpp', 'Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp',
    'Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp', 'Editor/EngineEntry/Commands/SceneObjectCommands.cpp',
    'Tools/regression/verify-texture-scene-reload.ps1')
$sources = @($sourcePaths | ForEach-Object { $p=Join-Path $repo $_; [pscustomobject]@{path=$p; sha256=(Get-FileHash -LiteralPath $p).Hash} })
$receipts = @()
foreach ($config in $configs) {
    $root = Join-Path $repo ('Build/TSR-' + $config + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    $project = Join-Path $root 'Project'
    foreach ($directory in @('Assets/Scenes','Assets/Textures','Assets/Script','Assets/Shaders/DefaultPassShader','ProjectSetting')) {
        New-Item -ItemType Directory -Path (Join-Path $project $directory) -Force | Out-Null
    }
    Copy-Item -Path (Join-Path $repo 'Dynamic_CPP/ProjectSetting/*') -Destination (Join-Path $project 'ProjectSetting') -Recurse
    Copy-Item -Path (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader/*') -Destination (Join-Path $project 'Assets/Shaders/DefaultPassShader') -Recurse
    $texture = Join-Path $project 'Assets/Textures/fixture.png'
    Copy-Item -LiteralPath (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png') -Destination $texture
    [IO.File]::WriteAllText($texture + '.meta', "guid: 33333333-3333-4333-8333-333333333333`n")
    $exe = Join-Path $repo "Bin/x64-$config/Editor/CreatorEditor.exe"
    $fixture = Join-Path $project 'Assets/Scenes/SavedFixture.creator'
    function Invoke-Editor([string]$Name, [string[]]$Lines) {
        $script = Join-Path $root "$Name.txt"
        $results = Join-Path $root "$Name.jsonl"
        $Lines | Set-Content -LiteralPath $script -Encoding utf8
        $proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--development-project', ('"'+$project+'"'), '--commandlet-script', ('"'+$script+'"'),
                '--result-file', ('"'+$results+'"')) `
            -RedirectStandardOutput (Join-Path $root "$Name.out") -RedirectStandardError (Join-Path $root "$Name.err")
        $handle = $proc.Handle
        if (!$proc.WaitForExit(180000)) { $proc.Kill(); $proc.WaitForExit(); throw "Editor timeout: $root/$Name" }
        $proc.WaitForExit()
        if (!(Test-Path -LiteralPath $results)) { throw "No Editor command results: $root/$Name" }
        [pscustomobject]@{exitCode=$proc.ExitCode; rows=@(Get-Content -LiteralPath $results | ConvertFrom-Json)}
    }
    $author = Invoke-Editor 'author' @('scene.new SavedFixture', 'wait 20', 'object.create SavedOnly Empty',
        ('scene.save "'+$fixture+'"'), 'quit')
    if ($author.exitCode -ne 0 -or @($author.rows | Where-Object status -ne succeeded).Count -ne 0) { throw "Author failed: $root" }
    $text = [IO.File]::ReadAllText($fixture)
    $bundle = "m_requiredLoadAssetsBundle:`n  name: ReloadTexture`n  path: ''`n  assets:`n    - assetTypeID: 2`n      assetName: '" + $texture.Replace('\','/') + "'`n"
    $text = [regex]::Replace($text, '(?ms)^m_requiredLoadAssetsBundle:.*?(?=^\S|\z)', $bundle)
    [IO.File]::WriteAllText($fixture, $text)
    $corrupt = Join-Path $root 'Corrupt.creator'
    [IO.File]::WriteAllText($corrupt, "m_Entities: [`n")
    $missingTexture = Join-Path $root 'MissingTexture.creator'
    [IO.File]::WriteAllText($missingTexture, $text.Replace($texture.Replace('\','/'), (Join-Path $project 'Assets/Textures/absent.png').Replace('\','/')))
    $missing = Join-Path $root 'Absent.creator'
    $observations = @('success','missing','corrupt','dependency','recovery')
    $lines = @('scene.new CurrentFixture', 'wait 20', 'object.create InitialUnsaved Empty',
        ('scene.open_async "'+$fixture+'"'), 'wait 240', 'scene.load.status 1',
        ('scene.save "'+(Join-Path $root 'success.creator')+'"'),
        ('assets.texture load texture "'+$texture+'"'), 'object.describe SavedOnly', 'object.create RetainedUnsaved Empty')
    $paths = @($missing,$corrupt,$missingTexture,$fixture)
    for ($i=0; $i -lt $paths.Count; ++$i) {
        $lines += @(('scene.open_async "'+$paths[$i]+'"'), 'wait 240', ('scene.load.status '+($i+2)),
            ('scene.save "'+(Join-Path $root ($observations[$i+1]+'.creator'))+'"'))
        $lines += if ($i -lt 3) { 'object.describe RetainedUnsaved' } else { 'object.describe SavedOnly' }
    }
    $lines += 'quit'
    $run = Invoke-Editor 'reload' $lines
    $status = @($run.rows | Where-Object command -eq 'scene.load.status')
    if ($status.Count -ne 5 -or $status[0].data.state -ne 'Ready' -or $status[4].data.state -ne 'Ready') { throw "Ready states missing: $root" }
    foreach ($i in 1..3) {
        if ($status[$i].data.state -ne 'Failed' -or $status[$i].status -ne 'failed') { throw "Negative reload was not rejected: $root/$i" }
    }
    $failures = @($run.rows | Where-Object status -ne succeeded)
    if ($failures.Count -ne 3 -or @($failures | Where-Object command -ne 'scene.load.status').Count -ne 0 -or $run.exitCode -ne 4) {
        throw "Unexpected command/process failure: $root exit=$($run.exitCode)"
    }
    foreach ($name in $observations) {
        $saved = [IO.File]::ReadAllText((Join-Path $root "$name.creator"))
        if ($saved -notmatch 'SavedOnly' -or $saved -match 'InitialUnsaved') { throw "Incorrect activated scene: $root/$name" }
        $retain = $name -in @('missing','corrupt','dependency')
        if (($saved -match 'RetainedUnsaved') -ne $retain) { throw "Unsaved scene preservation/replacement failed: $root/$name" }
    }
    $retained = @($run.rows | Where-Object { $_.command -eq 'object.create' -and $_.data.name -eq 'RetainedUnsaved' })
    $described = @($run.rows | Where-Object command -eq 'object.describe')
    if ($retained.Count -ne 1 -or $described.Count -ne 5) { throw "Identity observations missing: $root" }
    foreach ($i in 1..3) {
        if ($described[$i].data.id -ne $retained[0].data.id -or $described[$i].data.sceneId -ne $retained[0].data.sceneId) {
            throw "Failed reload replaced the active scene/entity identity: $root/$i"
        }
    }
    if ($described[0].data.sceneId -eq $described[4].data.sceneId) { throw "Recovery did not replace the scene: $root" }
    $textureRows = @($run.rows | Where-Object command -eq 'assets.texture')
    if ($textureRows.Count -ne 1 -or $textureRows[0].data.loaded.width -ne 4 -or $textureRows[0].data.loaded.height -ne 4) {
        throw "Scene texture dependency not prepared: $root"
    }
    $receipts += [pscustomobject]@{configuration=$config; root=$root; exitCode=$run.exitCode;
        ready=2; rejected=3; preservedUnsaved=3; observations=5; preservedIdentity=3;
        binarySha256=(Get-FileHash -LiteralPath $exe).Hash;
        runtimeSha256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash}
    "TEXTURE_SCENE_RELOAD_OK configuration=$config ready=2 rejected=3 preservedUnsaved=3"
}
foreach ($s in $sources) { if ((Get-FileHash -LiteralPath $s.path).Hash -ne $s.sha256) { throw "Source drift: $($s.path)" } }
New-Item -ItemType Directory -Path (Join-Path $repo 'Build/TextureSceneReload') -Force | Out-Null
[ordered]@{receipts=$receipts; sources=$sources; sourceDrift=0; scope='Actual Editor command/frame path; no Inspector click or GPU pixel oracle'} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $repo 'Build/TextureSceneReload/result.json') -Encoding utf8

[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'An Editor is already running.' }
$configs = if ($Configuration -eq 'All') { @('Debug','Release') } else { @($Configuration) }
$paths = @('Editor/EngineEntry/EditorAssetDatabase.cpp','Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp',
    'Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp','Engine/SceneRuntime/SceneManager.cpp',
    'Engine/SceneRuntime/ImageComponent.cpp','Engine/SceneRuntime/SpriteRenderer.cpp','Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp',
    'Tools/regression/texture_reimport_scene_probe.inl','Tools/regression/verify-texture-reimport-scene.ps1')
$sources = @($paths | ForEach-Object { $p=Join-Path $repo $_; [pscustomobject]@{path=$p; sha256=(Get-FileHash -LiteralPath $p).Hash} })
$receipts = @()
foreach ($config in $configs) {
    $root = Join-Path $repo ('Build/TRS-' + $config + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
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
    [IO.File]::WriteAllText((Join-Path $project '.texture-reimport-probe'), 'isolated regression fixture')
    $before = (Get-FileHash -LiteralPath $texture).Hash
    $scenario = Join-Path $root 'scenario.txt'
    $results = Join-Path $root 'results.jsonl'
    @('scene.new TextureReimportGate','wait 20',
        ('assets.texture.reimportprobe "'+$texture+'" "'+$fixture+'"'), 'quit') |
        Set-Content -LiteralPath $scenario -Encoding utf8
    $runtime = Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
    $hostHash = (Get-FileHash -LiteralPath $runtime).Hash
    $proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--development-project', ('"'+$project+'"'), '--commandlet-script', ('"'+$scenario+'"'),
            '--result-file', ('"'+$results+'"')) `
        -RedirectStandardOutput (Join-Path $root 'editor.out') -RedirectStandardError (Join-Path $root 'editor.err')
    $handle = $proc.Handle
    if (!$proc.WaitForExit(180000)) { $proc.Kill(); $proc.WaitForExit(); throw "Probe timeout: $root" }
    $proc.WaitForExit()
    $rows = @(Get-Content -LiteralPath $results | ConvertFrom-Json)
    $probe = @($rows | Where-Object command -eq 'assets.texture.reimportprobe')
    if ($proc.ExitCode -ne 0 -or $probe.Count -ne 1 -or @($rows | Where-Object status -ne succeeded).Count -ne 0 -or
        !$probe[0].data.passed -or $probe[0].message -notmatch '^TEXTURE_REIMPORT_SCENE_OK .*automaticComponent=2 .*undo=retained prefab=autoApplyUndoRedo consumers=ImageComponent,SpriteRenderer$') { throw "Product probe failed: $root" }
    if ((Get-FileHash -LiteralPath $texture).Hash -ne $before -or (Get-FileHash -LiteralPath $runtime).Hash -ne $hostHash) { throw 'Fixture or binary drift' }
    $receipts += [pscustomobject]@{configuration=$config; root=$root; checks=$probe[0].data.checks;
        verdict=$probe[0].message; runtimeSha256=$hostHash; binarySha256=(Get-FileHash -LiteralPath $exe).Hash; sourceRestored=$true}
    "$config $($probe[0].message)"
}
foreach ($s in $sources) { if ((Get-FileHash -LiteralPath $s.path).Hash -ne $s.sha256) { throw "Source drift: $($s.path)" } }
New-Item -ItemType Directory -Path (Join-Path $repo 'Build/TextureReimportScene') -Force | Out-Null
[ordered]@{receipts=$receipts; sources=$sources; sourceDrift=0; scope='Single Editor process, actual ImageComponent/SpriteRenderer, real sidecar watcher, automatic scene refresh, Undo/Redo and prefab automatic Apply'} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $repo 'Build/TextureReimportScene/result.json') -Encoding utf8

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('manual','close','shutdown','conflict','conflictshutdown','delete','corrupt','deleteshutdown','corruptshutdown','soak','nativeclose','conflictnativeclose')][string]$Mode='manual',
    [Parameter(Mandatory)][string]$FixtureProject,
    [ValidateRange(0.8,1.5)][Nullable[double]]$UiScale = $null
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture=[IO.Path]::GetFullPath($FixtureProject)
if(!(Test-Path -LiteralPath (Join-Path $fixture 'Assets/Scenes/SavedFixture.creator'))){throw 'Provide a completed isolated texture reimport fixture project'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'An Editor is already running'}
$root=Join-Path $repo ('Build/TAUI-'+$Configuration+'-'+$Mode+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$project=Join-Path $root 'Project'
New-Item -ItemType Directory -Path $project -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $fixture 'Assets'),(Join-Path $fixture 'ProjectSetting') -Destination $project -Recurse
if ($null -ne $UiScale) {
    $settings = Join-Path $project 'ProjectSetting/EngineSettings.asset'
    $text = [IO.File]::ReadAllText($settings)
    $value = $UiScale.ToString([Globalization.CultureInfo]::InvariantCulture)
    $text = if ($text -match '(?m)^imguiScale:') {
        [regex]::Replace($text,'(?m)^imguiScale:[^\r\n]*','imguiScale: '+$value)
    } else { $text.TrimEnd() + "`nimguiScale: $value`n" }
    [IO.File]::WriteAllText($settings,$text,[Text.UTF8Encoding]::new($false))
}
$scene=Join-Path $project 'Assets/Scenes/SavedFixture.creator'
[IO.File]::WriteAllText($scene,[IO.File]::ReadAllText($scene).Replace($fixture,$project),[Text.UTF8Encoding]::new($false))
$source=Join-Path $project 'Assets/Textures/fixture.png'
Copy-Item -LiteralPath (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png') -Destination $source -Force
$meta=[IO.File]::ReadAllText($source+'.meta')
$meta=[regex]::Replace($meta,'maxDimension:\s*\d+','maxDimension: 0')
[IO.File]::WriteAllText($source+'.meta',$meta,[Text.UTF8Encoding]::new($false))
$other=Join-Path $project 'Assets/Textures/other.png'
Copy-Item -LiteralPath $source -Destination $other
$otherMeta=[regex]::Replace($meta.Replace('33333333-3333-4333-8333-333333333333','44444444-4444-4444-8444-444444444444'),'maxDimension:\s*\d+','maxDimension: 2')
[IO.File]::WriteAllText($other+'.meta',$otherMeta,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $project '.texture-auto-ui-race'),'isolated actual Inspector regression')
$setup=Join-Path $root 'setup.txt'
$lines=@('window.resize 1600 1000','wait 60',('scene.open_async "'+$scene+'"'),'wait 180','object.create UnsavedT0Texture','wait 30',('editor.browser go "'+(Join-Path $project 'Assets/Textures')+'"'),'wait 180','editor.window ###Editor.Inspector focus')
if ($null -ne $UiScale) { $lines += @('editor.theme','wait 2','editor.theme') }
if($Mode -ne 'manual'){$lines+=('assets.texture.pendingprobe "'+$source+'" '+$Mode)}
if($Mode -eq 'soak'){$lines+='object.describe UnsavedT0Texture'}
if($Mode -in @('close','conflict','delete','corrupt','soak')){$lines+='wait 36000'}
Write-CommandScript $setup $lines
$exe=Join-Path $repo ('Bin/x64-'+$Configuration+'/Editor/CreatorEditor.exe')
$previousWorkspace=$env:CREATOR_EDITOR_WORKSPACE_DIR
$previousLegacyIni=$env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR=Join-Path $root 'Workspace'
$env:CREATOR_EDITOR_LEGACY_INI=Join-Path $root 'none.ini'
try{
    $flag=if($Mode -eq 'manual'){'--script'}else{'--commandlet-script'}
    # This launcher waits for actual Inspector input; its window must stay visible.
    $process = Start-ResultProcess -Exe $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Normal `
        -Arguments @('--development-project', ('"'+$project+'"'), $flag, ('"'+$setup+'"'),
            '--result-format', 'jsonl', '--result-file', ('"'+(Join-Path $root 'commands.jsonl')+'"')) `
        -Stdout (Join-Path $root 'editor.out') -Stderr (Join-Path $root 'editor.err') `
        -Results (Join-Path $root 'runtime-results.jsonl')
}finally{
    $env:CREATOR_EDITOR_WORKSPACE_DIR=$previousWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI=$previousLegacyIni
}
[ordered]@{root=$root;configuration=$Configuration;mode=$Mode;requestedUiScale=$UiScale;processId=$process.Id;runtimeSha256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;head=(git -C $repo rev-parse HEAD)} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'launch.json') -Encoding utf8
$root

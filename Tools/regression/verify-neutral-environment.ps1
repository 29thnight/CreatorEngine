param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [Parameter(Mandatory=$true)][string]$FixtureProject,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$case=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $case){throw 'Use a new output directory'}
$project=Join-Path $case 'Project'
New-Item -ItemType Directory -Path $project -Force|Out-Null
foreach($folder in @('Assets','ProjectSetting','Saved')){
    $source=Join-Path $FixtureProject $folder
    if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $project -Recurse}
}
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Destination "$project/Assets/Shaders" -Recurse -Force
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$proc=$null
$checks=0
function Assert([bool]$Condition,[string]$Message){
    $script:checks++
    if(!$Condition){throw $Message}
}
try{
    $proc=Start-Process $exe -ArgumentList @('--development-project',$project,'--command-service','--smoke-offscreen') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$case/editor.stdout.log" -RedirectStandardError "$case/editor.stderr.log"
    $endpoint=$null
    $deadline=[DateTime]::UtcNow.AddSeconds(180)
    do{
        Start-Sleep -Milliseconds 100
        if($proc.HasExited){throw "Editor exited $($proc.ExitCode)"}
        if(Test-Path "$project/Library/CommandService/endpoint.json"){$endpoint=Get-Content "$project/Library/CommandService/endpoint.json" -Raw|ConvertFrom-Json}
        if([DateTime]::UtcNow -gt $deadline){throw 'Editor startup timeout'}
    }until($endpoint -and $endpoint.pid -eq $proc.Id)
    $base="http://127.0.0.1:$($endpoint.port)"
    $headers=@{Authorization="Bearer $($endpoint.token)"}
    function Cmd([string]$Name,[string[]]$Arguments=@(),[switch]$AllowFailure){
        $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body (@{command=$Name;args=@($Arguments);mode='async'}|ConvertTo-Json -Compress)
        if($Name -eq 'quit'){return}
        $deadline=[DateTime]::UtcNow.AddSeconds(120)
        if($r.operationId){
            $poll=$r.poll
            do{Start-Sleep -Milliseconds 60;$r=Invoke-RestMethod "$base$poll" -Headers $headers
                if([DateTime]::UtcNow -gt $deadline){throw "$Name timeout"}
            }until($r.state -eq 'completed')
        }
        $r|ConvertTo-Json -Depth 40 -Compress|Add-Content "$case/responses.jsonl"
        if($AllowFailure){return $r}
        if($r.status -ne 'succeeded'){throw "$Name failed: $($r.code) $($r.message)"}
        $r.data
    }
    function Fence{Cmd 'render.live.fence' @('120')|Out-Null}
    function Capture([string]$Name,[string]$View='editor'){
        Fence
        Cmd 'render.live.capture' @("$case/$Name",$View,'controlled')|Out-Null
    }
    function Overlay([string]$Value){
        Cmd 'editor.sceneview' @('skybox',$Value)|Out-Null
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        do{Start-Sleep -Milliseconds 100;$view=Cmd 'editor.sceneview'
            if([DateTime]::UtcNow -gt $deadline){throw 'Scene SkyBox toggle did not apply'}
        }until($view.skybox -eq ($Value -eq 'on'))
        Fence
    }
    Cmd 'play.foreground_override' @('off')|Out-Null
    Cmd 'scene.switch' @("$project/Assets/Scenes/LX_CookFixture.creator")|Out-Null
    Fence
    $initial=Cmd 'render.environment' @('status')
    Assert (!$initial.background -and $initial.path -eq 'forest.ceibl' -and $initial.iblGenerations -gt 0) 'Default forest must start hidden with IBL ready'
    $initial|ConvertTo-Json|Set-Content "$case/default.json"
    Cmd 'editor.window' @('###Editor.GamePreview','open')|Out-Null
    Capture 'scene-default'
    Capture 'game-default' 'game'
    Cmd 'render.environment' @('background','on')|Out-Null
    Capture 'scene-on'
    Capture 'game-on' 'game'
    Overlay 'off'
    Capture 'scene-hidden'
    Capture 'game-scene-hidden' 'game'
    Overlay 'on'
    Cmd 'render.environment' @('background','off')|Out-Null
    Capture 'scene-off'
    Cmd 'render.environment' @('background','on')|Out-Null
    Capture 'scene-restored'
    $after=Cmd 'render.environment' @('status')
    Assert ($after.iblGenerations -eq $initial.iblGenerations) 'Background toggles regenerated IBL'

    Cmd 'render.environment' @('background','off')|Out-Null
    Overlay 'off'
    $bad=Cmd 'render.environment' @("$case/missing.hdr") -AllowFailure
    Assert ($bad.status -ne 'succeeded') 'Missing environment accepted'
    $unchanged=Cmd 'render.environment' @('status')
    $view=Cmd 'editor.sceneview'
    Assert (!$unchanged.background -and $unchanged.path -eq $initial.path -and !$view.skybox) 'Failed selection changed visibility or source'

    $hdr=Join-Path $repo 'Dynamic_CPP/Assets/HDR/autumn_field_puresky_1k.hdr'
    Cmd 'render.environment' @($hdr)|Out-Null
    Fence
    $custom=Cmd 'render.environment' @('status')
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do{$view=Cmd 'editor.sceneview';Start-Sleep -Milliseconds 100
        if([DateTime]::UtcNow -gt $deadline){throw 'Custom selection did not show Scene environment'}
    }until($view.skybox)
    Assert ($custom.background -and $custom.path -eq $hdr) 'Custom HDR selection must enable background'
    Assert $view.skybox 'Custom selection must reset the Scene hide override'
    Capture 'scene-custom'
    $custom|ConvertTo-Json|Set-Content "$case/custom.json"
    Cmd 'render.environment' @('background','off')|Out-Null
    Fence
    $customOff=Cmd 'render.environment' @('status')
    Assert (!$customOff.background -and $customOff.iblGenerations -eq $custom.iblGenerations) 'Custom background off must retain IBL'
    Capture 'scene-custom-off'
    $corrupt=[IO.File]::ReadAllBytes((Join-Path $repo 'Resources/Environment/forest.ceibl'))
    $corrupt[$corrupt.Length-1]=$corrupt[$corrupt.Length-1] -bxor 1
    [IO.File]::WriteAllBytes("$case/corrupt.ceibl",$corrupt)
    $bad=Cmd 'render.environment' @("$case/corrupt.ceibl") -AllowFailure
    Assert ($bad.status -ne 'succeeded') 'Corrupt cooked environment accepted'
    $unchanged=Cmd 'render.environment' @('status')
    Assert (!$unchanged.background -and $unchanged.path -eq $hdr -and $unchanged.iblGenerations -eq $custom.iblGenerations) 'Rejected cook changed environment'
    Cmd 'render.environment' @((Join-Path $repo 'Resources/Environment/forest.ceibl'))|Out-Null
    Fence
    $explicit=Cmd 'render.environment' @('status')
    Assert $explicit.background 'Explicit environment selection must enable background, including forest'
    Cmd 'scene.new' @('NeutralGrid')|Out-Null
    Cmd 'render.environment' @('background','off')|Out-Null
    Capture 'scene-grid'
    Cmd 'log.flush'|Out-Null
    Cmd 'quit'|Out-Null
    if(!$proc.WaitForExit(30000)){throw 'Editor shutdown timeout'}
    Assert ($proc.ExitCode -eq 0) 'Editor shutdown failed'
    @{configuration=$Configuration;backend='dx12';checks=$checks;pid=$proc.Id;iblToggleBefore=$initial.iblGenerations;iblToggleAfter=$after.iblGenerations}|ConvertTo-Json|Set-Content "$case/result.json"
    Write-Output "NEUTRAL_ENVIRONMENT_OK $Configuration checks=$checks $case"
}finally{
    if($proc -and !$proc.HasExited){Stop-Process -Id $proc.Id}
}

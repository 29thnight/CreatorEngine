#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing Editor before this isolated gate.' }
$root = Join-Path $repo ('Build/AutomaticScripts-' + $Configuration + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$project = Join-Path $root 'Project'
foreach ($dir in @('Assets/Script','Assets/Scenes','Assets/Shaders/DefaultPassShader','ProjectSetting')) {
    New-Item -ItemType Directory -Path (Join-Path $project $dir) -Force | Out-Null
}
Copy-Item (Join-Path $repo 'Dynamic_CPP/ProjectSetting/*') (Join-Path $project 'ProjectSetting') -Recurse
Copy-Item (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader/*') (Join-Path $project 'Assets/Shaders/DefaultPassShader') -Recurse
& (Join-Path $PSScriptRoot 'sync-material-editor-scale.ps1') -Project $project
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$managed = Join-Path $repo "Bin/x64-$Configuration/Managed/Scripts"
$backup = Join-Path $root 'ScriptsBackup'
Copy-Item -LiteralPath $managed -Destination $backup -Recurse
$originalManaged = @((Get-ChildItem $managed -File).Name)
$source = Join-Path $project 'Assets/Script/AutoScriptGate.cs'
$text = @'
namespace CreatorEngine.Scripts
{
    public sealed partial class AutoScriptGate : Component
    {
        [SerializeField] public int Kept = 5;
        [EngineCallable]
        public static int Version()
        {
            return 1;
        }
    }
}
'@
$script:checks = 0
$process = $null
function Assert([bool]$condition, [string]$message) {
    if (!$condition) { throw $message }
    $script:checks++
}
function Call([string]$command, [string[]]$arguments = @(), [switch]$Maybe) {
    $body = @{command=$command;args=@($arguments);mode='async'} | ConvertTo-Json -Compress
    $response = Invoke-WebRequest "$script:base/command" -Method Post -Headers $script:headers -ContentType 'application/json' -Body $body -SkipHttpErrorCheck -TimeoutSec 30
    $result = $response.Content | ConvertFrom-Json
    if ($command -eq 'quit') { return $result }
    if ($result.PSObject.Properties['operationId']) {
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            Start-Sleep -Milliseconds 100
            $response = Invoke-WebRequest "$script:base$($result.poll)" -Headers $script:headers -SkipHttpErrorCheck -TimeoutSec 30
            $terminal = $response.Content | ConvertFrom-Json
            if ($terminal.state -eq 'completed') { break }
            if ([DateTime]::UtcNow -ge $deadline) { throw "Operation timed out: $command" }
        } while ($true)
        $result = $terminal
    }
    $response.Content | Add-Content (Join-Path $root 'responses.jsonl') -Encoding utf8
    if (!$Maybe) { Assert ($result.status -eq 'succeeded') "$command failed: $($response.Content)" }
    return $result
}
function Wait-Version([int]$version, [string]$type = 'AutoScriptGate') {
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $state = Call 'script.invoke' @($type,'Version') -Maybe
        if ($state.status -eq 'succeeded' -and $state.data.returnValue -eq [string]$version) { return }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Automatic script version $version timed out" }
        Start-Sleep -Milliseconds 200
    } while ($true)
}
function Components { @((Call 'script.status').data.components | Where-Object type -EQ 'AutoScriptGate') }
function Check-Fields {
    $components = Components
    Assert ($components.Count -eq 2) 'Both original script components must remain connected'
    foreach ($component in $components) {
        $fields = (Call 'script.fields' @([string]$component.instanceId)).data.fields
        Assert (@($fields | Where-Object name -EQ 'Kept')[0].value -eq 37) 'Serialized field value must survive automatic reload'
    }
}
try {
    $process = Start-Process $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -ArgumentList @('--development-project', ('"'+$project+'"'), '--command-service', '--allow-user-code', '--smoke-offscreen') -RedirectStandardOutput (Join-Path $root 'editor.out') -RedirectStandardError (Join-Path $root 'editor.err')
    $endpointPath = Join-Path $project 'Library/CommandService/endpoint.json'
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        if ($process.HasExited) { throw "Editor startup failed: $($process.ExitCode)" }
        if (Test-Path $endpointPath) {
            $endpoint = Get-Content $endpointPath -Raw | ConvertFrom-Json
            if ($endpoint.pid -eq $process.Id) { break }
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Endpoint timeout' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $script:base = "http://127.0.0.1:$($endpoint.port)"
    $script:headers = @{Authorization="Bearer $($endpoint.token)"}
    Call 'play.foreground_override' @('off') | Out-Null
    Call 'play.cursor' @('show') | Out-Null
    Call 'scene.new' @('AutomaticScriptGate') | Out-Null
    # File creation must compile and load without script.create or script.reload.
    [IO.File]::WriteAllText($source, $text)
    Wait-Version 1
    foreach ($name in @('ScriptA','ScriptB')) {
        Call 'object.create' @($name) | Out-Null
        Call 'script.add' @($name,'AutoScriptGate') | Out-Null
    }
    foreach ($component in (Components)) {
        $field = @((Call 'script.fields' @([string]$component.instanceId)).data.fields | Where-Object name -EQ 'Kept')[0]
        Call 'script.set' @([string]$component.instanceId,[string]$field.index,'37') | Out-Null
    }
    $v2 = $text.Replace('return 1;', 'return 2;')
    [IO.File]::WriteAllText($source,$v2)
    Wait-Version 2
    Check-Fields
    $ids = @((Components).instanceId) -join ','
    [IO.File]::WriteAllText($source,$v2 + "`nthis is invalid C#;")
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $status = Call 'script.creation'
        if (!$status.data.busy -and !$status.data.succeeded -and $status.message -like '*compilation failed*') { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Expected compiler failure was not observed' }
        Start-Sleep -Milliseconds 200
    } while ($true)
    Assert ((Call 'script.invoke' @('AutoScriptGate','Version')).data.returnValue -eq '2') 'Compiler failure must retain previous code'
    Assert ((@((Components).instanceId) -join ',') -eq $ids) 'Compiler failure must retain instance identity'
    Check-Fields
    [IO.File]::WriteAllText($source,$text.Replace('return 1;', 'return 3;'))
    Wait-Version 3
    Check-Fields
    Call 'play' | Out-Null
    [IO.File]::WriteAllText($source,$text.Replace('return 1;', 'return 4;'))
    Wait-Version 4
    Check-Fields
    Assert ((Call 'script.status').data.activeScripts -eq 2) 'Play reload must not duplicate script instances'
    Call 'stop' | Out-Null
    # Renaming and atomic replacement use the same watcher route.
    $renamed = Join-Path (Split-Path $source) 'AutoScriptRenamed.cs'
    Move-Item -LiteralPath $source -Destination $renamed
    [IO.File]::WriteAllText($renamed,$text.Replace('return 1;', 'return 5;'))
    Wait-Version 5
    Check-Fields
    $aux = Join-Path (Split-Path $renamed) 'AutoScriptAux.cs'
    [IO.File]::WriteAllText($aux,$text.Replace('AutoScriptGate','AutoScriptAux').Replace('return 1;', 'return 9;'))
    Wait-Version 9 'AutoScriptAux'
    Remove-Item -LiteralPath $aux
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $removed = Call 'script.invoke' @('AutoScriptAux','Version') -Maybe
        if ($removed.status -ne 'succeeded') { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Deleted C# source remained in the loaded assembly' }
        Start-Sleep -Milliseconds 200
    } while ($true)
    Check-Fields
    $staging = $renamed + '.tmp'
    [IO.File]::WriteAllText($staging,$text.Replace('return 1;', 'return 6;'))
    Move-Item -LiteralPath $staging -Destination $renamed -Force
    Wait-Version 6
    Check-Fields
    Call 'quit' | Out-Null
    Assert ($process.WaitForExit(60000)) 'Editor shutdown timed out'
    Assert ($process.ExitCode -eq 0) 'Editor exit failed'
    [ordered]@{passed=$true;configuration=$Configuration;checks=$script:checks;root=$root;scope='Real C# watcher, creation/edit/rename/delete/atomic save, compiler failure and recovery, field preservation, two component reconnects, Play single instance';runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $root 'result.json') -Encoding utf8
    "AUTOMATIC_SCRIPT_EDITOR_OK checks=$script:checks root=$root"
}
finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($file in (Get-ChildItem $managed -File)) {
        if ($file.Name -notin $originalManaged) { Remove-Item -LiteralPath $file.FullName }
    }
    Copy-Item (Join-Path $backup '*') $managed -Force
}

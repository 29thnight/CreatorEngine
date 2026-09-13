[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $PSScriptRoot '../../Artifacts/phase21-add-component/authoring-gate')
)
# Uses a disposable scene; restores project settings and docking bytes on every exit.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the editor before this isolated gate.' }
$settings = Join-Path $repo 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
$ini = Join-Path (Split-Path $Exe) 'Saved/Config/imgui.ini'
$endpointPath = Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json'
$settingsBytes = [IO.File]::ReadAllBytes($settings)
$hadIni = Test-Path -LiteralPath $ini
$iniBytes = if ($hadIni) { [IO.File]::ReadAllBytes($ini) } else { [byte[]]@() }
$script:checks = 0
$script:sequence = 0
$script:endpoint = $null
$owned = $null
$passed = $false
New-Item -ItemType Directory -Force -Path $Work | Out-Null
function Check([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    $script:checks++
}
function Call([string]$command, [string[]]$arguments = @(), [string]$expectedCode = 'ok') {
    $script:sequence++
    $reply = Invoke-WebRequest -Uri ('http://127.0.0.1:'+$script:endpoint.port+'/command') -Method Post `
        -Headers @{Authorization='Bearer '+$script:endpoint.token} -ContentType 'application/json' `
        -Body (@{command=$command;args=$arguments}|ConvertTo-Json -Compress) -SkipHttpErrorCheck -TimeoutSec 30
    $reply.Content | Set-Content -LiteralPath (Join-Path $Work ('{0:d3}-{1}.json' -f $script:sequence,$command)) -Encoding utf8
    $result = $reply.Content | ConvertFrom-Json
    if ($result.status -eq 'accepted') {
        $poll = $result.poll
        Check ($poll -match '^/operations/op-[0-9]+$') 'Unexpected operation polling path'
        $operationWatch = [Diagnostics.Stopwatch]::StartNew()
        do {
            Start-Sleep -Milliseconds 100
            $result = Invoke-RestMethod -Uri ('http://127.0.0.1:'+$script:endpoint.port+$poll) `
                -Headers @{Authorization='Bearer '+$script:endpoint.token} -TimeoutSec 5
        } while ($result.state -ne 'completed' -and $operationWatch.Elapsed.TotalSeconds -lt 90)
        Check ($result.state -eq 'completed') "$command timed out"
        $result | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $Work ('{0:d3}-{1}-completed.json' -f $script:sequence,$command)) -Encoding utf8
    }
    Check ($result.code -eq $expectedCode) "$command expected $expectedCode, got $($result.code): $($result.message)"
    Check (($result.status -eq 'succeeded') -eq ($expectedCode -eq 'ok')) "$command unexpected status $($result.status)"
    return $result.data
}
$fixturePrefix = 'ComponentMenuProbe' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$sourceRoot = Join-Path $repo 'Dynamic_CPP/Assets/Script'
$managedRoot = Join-Path (Split-Path (Split-Path $Exe)) 'Managed/Scripts'
$managedBefore = Join-Path $Work 'managed-before'
$fixtureArchive = Join-Path $Work 'fixture-files'
New-Item -ItemType Directory -Force -Path $managedBefore,$fixtureArchive,$sourceRoot | Out-Null
$managedFiles = @(Get-ChildItem -LiteralPath $managedRoot -Filter 'GameScripts.*' -File)
foreach ($file in $managedFiles) { Copy-Item -LiteralPath $file.FullName -Destination $managedBefore -Force }
function WaitCreation([bool]$success) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do { Start-Sleep -Milliseconds 300; $state = Call 'script.creation' } while ($state.busy -and $timer.Elapsed.TotalSeconds -lt 150)
    Check (-not $state.busy) 'Script compilation timed out'
    Check ($state.succeeded -eq $success) "Script creation success expected $success; inspect the compilation log"
    return $state
}
function Scripts([string]$owner) {
    $state = Call 'script.status'
    return @($state.components | Where-Object owner -eq $owner)
}
try {
    $text = [Text.Encoding]::UTF8.GetString($settingsBytes)
    $pattern = '(?m)(^render:\r?\n\s{2}backend: )\w+'
    Check ([regex]::Matches($text,$pattern).Count -eq 1) 'render.backend setting unavailable'
    [IO.File]::WriteAllText($settings, [regex]::Replace($text,$pattern,'${1}dx12'), [Text.UTF8Encoding]::new($false))
    $owned = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--command-service') -RedirectStandardOutput (Join-Path $Work 'editor.out') -RedirectStandardError (Join-Path $Work 'editor.err')
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $firstFrame = $null
    while ($watch.Elapsed.TotalSeconds -lt 90) {
        if ($owned.HasExited) { throw "Editor exited during startup: $($owned.ExitCode)" }
        try {
            $candidate = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
            if ($candidate.pid -eq $owned.Id) {
                $health = Invoke-RestMethod -Uri ('http://127.0.0.1:'+$candidate.port+'/health') `
                    -Headers @{Authorization='Bearer '+$candidate.token} -TimeoutSec 2
                if ($null -eq $firstFrame) { $firstFrame = $health.frame }
                if ($health.state -eq 'idle' -and $health.frame -gt $firstFrame) { $script:endpoint=$candidate; break }
            }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    Check ($null -ne $script:endpoint) 'Editor did not become idle with advancing frames'
    $selftest = Call 'commands.selftest'
    Check ($selftest.problems -eq 0) 'Command metadata mismatch'
    $selftest = Call 'editor.selftest'
    Check ($selftest.theme -and $selftest.windows -and $selftest.menus) 'Editor selftest failed'
    $null = Call 'scene.new' @('ComponentMenuFixture')
    $null = Call 'object.create' @('ScriptOwner')
    $null = Call 'object.create' @('OtherOwner')
    foreach ($name in @('class','CON','../Escape','9Bad','Has Space')) {
        $null = Call 'script.create' @('ScriptOwner',$name) 'args.invalid'
    }
    $first = Call 'script.add' @('ScriptOwner','Bobber')
    $second = Call 'script.add' @('ScriptOwner','Bobber')
    Check ($first.instanceId -ne $second.instanceId) 'Duplicate script classes share an instance'
    $null = Call 'script.set' @([string]$first.instanceId,'0','1.25')
    $null = Call 'script.set' @([string]$second.instanceId,'0','2.5')
    $before = Call 'undo.state'
    $null = Call 'script.add' @('ScriptOwner','UnknownMenuProbe') 'script.type_not_found'
    $after = Call 'undo.state'
    Check ($before.editUndo -eq $after.editUndo) 'Invalid script changed undo history'
    Check (@(Scripts 'ScriptOwner').Count -eq 2) 'Invalid script left an empty component'
    $null = Call 'object.lock' @('ScriptOwner','true')
    $null = Call 'script.add' @('ScriptOwner','Bobber') 'object.locked'
    $null = Call 'script.create' @('ScriptOwner',($fixturePrefix+'LockedEarly')) 'object.locked'
    $null = Call 'object.lock' @('ScriptOwner','false')
    $createdName = $fixturePrefix+'Created'
    $null = Call 'script.create' @('ScriptOwner',$createdName)
    $null = Call 'scene.select' @('OtherOwner')
    $state = WaitCreation $true
    Check (Test-Path -LiteralPath $state.source) 'Source file was not created'
    Check (Test-Path -LiteralPath ($state.source+'.meta')) 'Source metadata was not created'
    $scripts = @(Scripts 'ScriptOwner')
    Check ($scripts.Count -eq 3) 'Creation did not attach exactly one script'
    Check (@(Scripts 'OtherOwner').Count -eq 0) 'Changing selection redirected attachment'
    $bobbers = @($scripts | Where-Object type -eq 'Bobber')
    $fieldValues = foreach ($script in $bobbers) { $fields = Call 'script.fields' @([string]$script.instanceId); [double]$fields.fields[0].value }
    Check (($fieldValues | Sort-Object) -join ',' -eq '1.25,2.5') 'Reload lost fields from repeated script instances'
    $null = Call 'undo'
    Check (@(Scripts 'ScriptOwner').Count -eq 2) 'Undo script attachment'
    $null = Call 'redo'
    Check (@(Scripts 'ScriptOwner').Count -eq 3) 'Redo script attachment'
    $sourceHash = (Get-FileHash -LiteralPath $state.source).Hash
    $null = Call 'script.create' @('ScriptOwner',$createdName) 'args.invalid'
    Check ((Get-FileHash -LiteralPath $state.source).Hash -eq $sourceHash) 'Duplicate class overwrote its source'

    $invalidSource = Join-Path $sourceRoot ($fixturePrefix+'CompileError.cs')
    [IO.File]::WriteAllText($invalidSource, 'this deliberately does not compile;', [Text.UTF8Encoding]::new($false))
    $null = Call 'script.create' @('ScriptOwner',($fixturePrefix+'Retry'))
    $failed = WaitCreation $false
    Check (@(Scripts 'ScriptOwner').Count -eq 3) 'Compilation failure left a component'
    Check (Test-Path -LiteralPath $failed.source) 'Compilation failure deleted the source'
    Copy-Item -LiteralPath $failed.log -Destination (Join-Path $Work 'expected-compile-error.log') -Force
    Move-Item -LiteralPath $invalidSource -Destination $fixtureArchive
    $null = Call 'script.create' @('--retry')
    $null = WaitCreation $true
    Check (@(Scripts 'ScriptOwner').Count -eq 4) 'Retry failed to attach exactly one script'

    $null = Call 'script.create' @('ScriptOwner',($fixturePrefix+'LockedLate'))
    $null = Call 'object.lock' @('ScriptOwner','true')
    $null = WaitCreation $false
    Check (@(Scripts 'ScriptOwner').Count -eq 4) 'Late lock did not prevent attachment'
    $null = Call 'object.lock' @('ScriptOwner','false')
    $null = Call 'script.create' @('ScriptOwner',($fixturePrefix+'Cancelled'))
    $null = Call 'script.create' @('--cancel')
    $null = WaitCreation $false
    Check (@(Scripts 'ScriptOwner').Count -eq 4) 'Cancelled compilation attached a script'
    $null = Call 'object.create' @('DeletedOwner')
    $null = Call 'script.create' @('DeletedOwner',($fixturePrefix+'Deleted'))
    $null = Call 'object.delete' @('DeletedOwner')
    $null = WaitCreation $false
    Check (@(Scripts 'OtherOwner').Count -eq 0) 'Deleted target caused attachment to another entity'
    $passed = $true
} finally {
    if ($null -ne $owned -and -not $owned.HasExited) {
        if ($null -ne $script:endpoint) { try { $null = Call 'quit' } catch { } }
        if (-not $owned.WaitForExit(30000)) { $owned.Kill(); $owned.WaitForExit() }
    }
    foreach ($file in $managedFiles) {
        Copy-Item -LiteralPath (Join-Path $managedBefore $file.Name) -Destination $file.FullName -Force
        Check ((Get-FileHash -LiteralPath (Join-Path $managedBefore $file.Name)).Hash -eq (Get-FileHash -LiteralPath $file.FullName).Hash) 'Managed binary restore failed'
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $sourceRoot -File | Where-Object Name -like ($fixturePrefix+'*'))) {
        Move-Item -LiteralPath $file.FullName -Destination $fixtureArchive -Force
    }
    [IO.File]::WriteAllBytes($settings,$settingsBytes)
    if ($hadIni) { [IO.File]::WriteAllBytes($ini,$iniBytes) }
    elseif (Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    $restored = [Convert]::ToBase64String([IO.File]::ReadAllBytes($settings)) -ceq [Convert]::ToBase64String($settingsBytes)
    if ($hadIni) { $restored = $restored -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($ini)) -ceq [Convert]::ToBase64String($iniBytes)) }
    Check $restored 'Settings or docking restore failed'
    @{passed=$passed;checks=$script:checks;commands=$script:sequence;settingsRestored=$restored} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Work 'summary.json') -Encoding utf8
}
Write-Output "COMPONENT_BROWSER_OK checks=$script:checks commands=$script:sequence settingsRestored=$restored"

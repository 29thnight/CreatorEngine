[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $PSScriptRoot '../../Artifacts/phase21-inspector-navigation/authoring-gate')
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
    $null = Call 'scene.new' @('EntityAuthoringFixture')
    $a = Call 'object.create' @('NavA','Camera')
    Check ($a.editorIcon -eq '' -and -not $a.editorLocked) 'New entity default authoring state'
    $null = Call 'object.create' @('NavB')
    $null = Call 'object.create' @('NavC')
    $null = Call 'object.create' @('NavD')
    foreach ($name in @('NavA','NavB','NavC')) { $null = Call 'scene.select' @($name) }
    $before = Call 'undo.state'
    $nav = Call 'scene.navigate' @('back')
    Check ($nav.name -eq 'NavB') 'Back should select B'
    $nav = Call 'scene.navigate' @('forward')
    Check ($nav.name -eq 'NavC') 'Forward should select C'
    $after = Call 'undo.state'
    Check ($before.editUndo -eq $after.editUndo -and $before.editRedo -eq $after.editRedo) 'Navigation changed Undo history'
    $null = Call 'object.delete' @('NavB')
    $nav = Call 'scene.navigate' @('back')
    Check ($nav.name -eq 'NavA') 'History should skip destroyed B'
    $null = Call 'scene.select' @('NavD')
    $null = Call 'scene.navigate' @('forward') 'selection.history.end'
    $null = Call 'object.icon' @('NavA','camera')
    $null = Call 'undo'
    $a = Call 'object.describe' @('NavA')
    Check ($a.editorIcon -eq '') 'Undo icon'
    $null = Call 'redo'
    $a = Call 'object.describe' @('NavA')
    Check ($a.editorIcon -eq 'camera') 'Redo icon'
    $null = Call 'object.lock' @('NavA','true')
    $null = Call 'undo'
    $a = Call 'object.describe' @('NavA')
    Check (-not $a.editorLocked) 'Undo lock'
    $null = Call 'redo'
    $a = Call 'object.describe' @('NavA')
    Check ($a.editorLocked -and $a.editBlocked) 'Redo lock'
    $before = Call 'undo.state'
    $blocked = @(
        @('object.rename','NavA','Changed'), @('object.delete','NavA'),
        @('object.icon','NavA','light'), @('object.transform','NavA','1','2','3'),
        @('component.add','NavA','LightComponent'), @('component.remove','NavA','CameraComponent'),
        @('object.property','NavA','Transform','position','[1,2,3]'),
        @('object.parent','NavA','NavD'), @('object.parent','NavD','NavA')
    )
    foreach ($row in $blocked) { $null = Call $row[0] $row[1..($row.Count-1)] 'object.locked' }
    $after = Call 'undo.state'
    Check ($before.editUndo -eq $after.editUndo -and $before.editRedo -eq $after.editRedo) 'Rejected edit changed Undo history'
    $a = Call 'object.describe' @('NavA')
    Check ($a.name -eq 'NavA' -and $a.position[0] -eq 0 -and $a.editorIcon -eq 'camera') 'Rejected edits changed object'
    $copy = Call 'object.duplicate' @('NavA','NavCopy')
    $copy = Call 'object.describe' @('NavCopy')
    Check ($copy.editorIcon -eq 'camera' -and $copy.editorLocked) 'Duplicate lost authoring metadata'
    $null = Call 'object.parent' @('NavC','NavD')
    $null = Call 'object.lock' @('NavC','true')
    $d = Call 'object.describe' @('NavD')
    Check (-not $d.editorLocked -and $d.editBlocked) 'Locked descendant did not protect parent operations'
    $null = Call 'object.delete' @('NavD') 'object.locked'
    $null = Call 'object.transform' @('NavD','1','2','3') 'object.locked'
    $null = Call 'object.lock' @('NavC','false')
    $null = Call 'object.lock' @('NavD','true')
    $null = Call 'object.rename' @('NavC','Changed') 'object.locked'
    $null = Call 'object.lock' @('NavC','false') 'object.locked'
    $null = Call 'object.lock' @('NavD','false')
    $null = Call 'object.transform' @('NavC','1','2','3')
    $c = Call 'object.describe' @('NavC')
    Check (-not $c.editBlocked -and $c.position[0] -eq 1) 'Unlock did not restore editing'
    $fixture = Join-Path $Work 'EntityAuthoringFixture.creator'
    $null = Call 'scene.save' @($fixture)
    Check (Test-Path -LiteralPath $fixture) 'Fixture was not saved'
    $null = Call 'scene.new' @('HistoryResetFixture')
    $null = Call 'scene.navigate' @('back') 'selection.history.end'
    $null = Call 'scene.switch' @($fixture)
    $null = Call 'scene.navigate' @('back') 'selection.history.end'
    foreach ($name in @('NavA','NavCopy')) {
        $loaded = Call 'object.describe' @($name)
        Check ($loaded.editorIcon -eq 'camera' -and $loaded.editorLocked) "$name metadata did not survive scene roundtrip"
    }
    $null = Call 'scene.select' @('NavA')
    $selection = Call 'scene.selection'
    Check ($selection.multi -eq 1) 'Locked object cannot be selected'
    $null = Call 'object.lock' @('NavA','false')
    $null = Call 'object.rename' @('NavA','Unlocked')
    $null = Call 'component.add' @('Unlocked','LightComponent')
    $null = Call 'component.remove' @('Unlocked','LightComponent')
    $null = Call 'object.delete' @('Unlocked')
    $passed = $true
} finally {
    if ($null -ne $owned -and -not $owned.HasExited) {
        if ($null -ne $script:endpoint) { try { $null = Call 'quit' } catch { } }
        if (-not $owned.WaitForExit(30000)) { $owned.Kill(); $owned.WaitForExit() }
    }
    [IO.File]::WriteAllBytes($settings,$settingsBytes)
    if ($hadIni) { [IO.File]::WriteAllBytes($ini,$iniBytes) }
    elseif (Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    $restored = [Convert]::ToBase64String([IO.File]::ReadAllBytes($settings)) -ceq [Convert]::ToBase64String($settingsBytes)
    if ($hadIni) { $restored = $restored -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($ini)) -ceq [Convert]::ToBase64String($iniBytes)) }
    Check $restored 'Settings or docking restore failed'
    @{passed=$passed;checks=$script:checks;commands=$script:sequence;settingsRestored=$restored} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Work 'summary.json') -Encoding utf8
}
Write-Output "ENTITY_AUTHORING_OK checks=$script:checks commands=$script:sequence settingsRestored=$restored"

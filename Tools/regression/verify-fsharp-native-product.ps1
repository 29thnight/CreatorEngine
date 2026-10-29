[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineDistribution,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Work = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NativeReflectionProductResults.ps1')
if (![OperatingSystem]::IsWindows()) { throw 'This gate requires the actual Windows Editor and Player.' }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close CreatorEditor before the gate swaps its managed fixture.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$distribution = [IO.Path]::GetFullPath($EngineDistribution)
$workRoot = if ($Work) { [IO.Path]::GetFullPath($Work) } else {
    Join-Path $repo ('Build/Verification/NativeReflectionProduct-' + [guid]::NewGuid().ToString('N'))
}
if (Test-Path -LiteralPath $workRoot) { throw 'Use a new work directory so old results cannot satisfy this gate.' }
New-Item -ItemType Directory -Path $workRoot | Out-Null
$tool = Join-Path $distribution "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
& $tool verify-engine --engine-distribution $distribution *> "$workRoot/verify-engine.log"
if ($LASTEXITCODE) { throw "Distribution verification failed: $workRoot/verify-engine.log" }
$manifest = Get-Content "$distribution/engine.manifest.json" -Raw | ConvertFrom-Json
$apiMatch = [regex]::Match((Get-Content "$repo/Engine/Utility_Framework/Runtime/ScriptApiVersion.h" -Raw),
    'CreatorScriptApiVersion\s*=\s*(\d+)')
if (!$apiMatch.Success) { throw 'Native ScriptApi version is missing.' }
$apiVersion = [int]$apiMatch.Groups[1].Value
$revision = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $apiVersion -lt 48 -or $manifest.configuration -cne $Configuration -or
    $manifest.source.revision -cne $revision -or $manifest.hosts.Player.scriptApi -ne $apiVersion -or
    $manifest.hosts.CreatorEditor.scriptApi -ne $apiVersion) {
    throw 'Build a distribution from this checkout and configuration before running this gate.'
}
$inventory = Get-Content "$repo/Engine/SceneRuntime/ComponentTypeUUID.h" -Raw
$expectedComponents = @([regex]::Matches($inventory, '\{\s*"([A-Za-z_][A-Za-z_0-9]*)"\s*,\s*"[0-9a-f-]{36}"') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object)
if ($expectedComponents.Count -lt 27) { throw 'Native component source inventory is incomplete.' }
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$editorNativeClosure = Assert-NativeReflectionEditorClosure (Join-Path $repo "Bin/x64-$Configuration") `
    (Join-Path $distribution "Bin/x64-$Configuration") $Configuration $apiVersion
$editorNativeClosure | ConvertTo-Json | Set-Content "$workRoot/editor.native-closure.json" -Encoding utf8

$project = Join-Path $workRoot 'Project'
$scriptRoot = Join-Path $project 'Assets/Script'
New-Item -ItemType Directory -Force $scriptRoot, "$project/Assets/Scenes", "$project/Assets/Sounds" | Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
Copy-Item "$PSScriptRoot/fixtures/PhysicsCharacterPlayer.creator*" "$project/Assets/Scenes"
$scene = Join-Path $project 'Assets/Scenes/PhysicsCharacterPlayer.creator'
$sceneText = (Get-Content $scene -Raw).Replace('m_scriptType: CharacterPlayerProbe',
    'm_scriptType: FSharpNativeReflectionProductProbe')
if (!$sceneText.Contains('m_scriptType: FSharpNativeReflectionProductProbe')) { throw 'Fixture script replacement failed.' }
[IO.File]::WriteAllText($scene, $sceneText, [Text.UTF8Encoding]::new($false))
Copy-Item "$PSScriptRoot/NativeReflectionProduct/FSharpNativeReflectionProductProbe.fs", `
    "$PSScriptRoot/NativeReflectionProduct/compile.order" $scriptRoot

# Generate a two-second PCM fixture; no external assets or private tooling are copied.
$clip = Join-Path $project 'Assets/Sounds/ReflectionProbe.wav'
$sampleRate = 48000
$sampleCount = $sampleRate * 2
$writer = [IO.BinaryWriter]::new([IO.File]::Create($clip))
try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32](36 + $sampleCount * 2))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
    $writer.Write([uint32]16); $writer.Write([uint16]1); $writer.Write([uint16]1)
    $writer.Write([uint32]$sampleRate); $writer.Write([uint32]($sampleRate * 2))
    $writer.Write([uint16]2); $writer.Write([uint16]16)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([uint32]($sampleCount * 2))
    for ($index = 0; $index -lt $sampleCount; $index++) {
        $writer.Write([int16](1000 * [Math]::Sin(2 * [Math]::PI * 440 * $index / $sampleRate)))
    }
} finally { $writer.Dispose() }
$clipHash = (Get-FileHash $clip -Algorithm SHA256).Hash.ToLowerInvariant()
$clipSize = (Get-Item $clip).Length
@"
guid: ee03635c-f351-4d1e-b120-0a9f0848d4d4
audioClip:
  schemaVersion: 1
  loadMode: Resident
  spatialKind: NonSpatial
  codec: Wav
  payloadSize: $clipSize
  sourceContentHash: $clipHash
"@ | Set-Content ($clip + '.meta') -Encoding utf8
& $tool select-engine --engine-distribution $distribution --project $project *> "$workRoot/select.log"
if ($LASTEXITCODE) { throw 'Fixture engine selection failed.' }

function Wait-OwnedEndpoint($Process, [string]$Root) {
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        if ($Process.HasExited) { throw "Host exited before creating its endpoint: $($Process.ExitCode)" }
        foreach ($file in Get-ChildItem $Root -Recurse -Filter endpoint.json -ErrorAction SilentlyContinue) {
            try { $candidate = Get-Content $file.FullName -Raw | ConvertFrom-Json } catch { continue }
            if ($candidate.pid -eq $Process.Id) { return $candidate }
        }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The launched process did not create its own command endpoint.'
}

function Invoke-OwnedCommand($Endpoint, [string]$Name, [string[]]$Arguments = @()) {
    $base = 'http://127.0.0.1:' + $Endpoint.port
    $headers = @{ Authorization = 'Bearer ' + $Endpoint.token }
    $result = Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json `
        -Body (@{ command = $Name; args = @($Arguments); mode = 'async' } | ConvertTo-Json -Compress) -TimeoutSec 30
    if ($Name -eq 'quit') { return }
    if ($result.PSObject.Properties['operationId']) {
        $poll = $result.poll
        $deadline = [DateTime]::UtcNow.AddSeconds(150)
        do {
            Start-Sleep -Milliseconds 100
            $result = Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 30
        } until ($result.state -eq 'completed' -or [DateTime]::UtcNow -ge $deadline)
        if ($result.state -ne 'completed') { throw "Command timed out: $Name" }
    }
    if ($result.PSObject.Properties['result']) { $result = $result.result }
    @{ processId = $Endpoint.pid; command = $Name; args = $Arguments; result = $result } |
        ConvertTo-Json -Depth 25 -Compress | Add-Content "$workRoot/commands.jsonl" -Encoding utf8
    if ($result.status -cne 'succeeded') { throw "$Name failed: $($result.message)" }
    return $result.data
}

function Wait-PlayState($Endpoint, [bool]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $state = Invoke-OwnedCommand $Endpoint 'play.state'
        if (!$state.pending -and $state.committed -eq $Expected) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Requested Play/Stop was not committed.'
}

function Wait-ProbeResult($Process, [string]$Stdout) {
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $stream = [IO.File]::Open($Stdout, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $reader = [IO.StreamReader]::new($stream)
            try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
        if ($text.Contains('[native.reflection.failure]')) { throw "Managed product failure: $Stdout" }
        if ($text.Contains('[native.reflection.acceptance] ')) {
            return Read-NativeReflectionAcceptance $text $Process.Id $expectedComponents $apiVersion
        }
        if ($Process.HasExited) { throw "Host exited before its product result: $($Process.ExitCode)" }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Native reflection product result timed out: $Stdout"
}

$managed = Join-Path $workRoot 'Managed'
& dotnet build "$repo/Engine/Scripting/GameScripts/ScriptNative.GameScripts.fsproj" -c $Configuration `
    "-p:CreatorScriptSourceRoot=$scriptRoot" "-p:CreatorNativeGameOutDir=$managed/" `
    "-p:CreatorScriptBuildKey=NativeReflection-$([guid]::NewGuid().ToString('N'))" --nologo -v:q *> "$workRoot/build.log"
if ($LASTEXITCODE) { throw "Product probe compilation failed: $workRoot/build.log" }
$target = Join-Path $repo "Bin/x64-$Configuration/Managed/FSharpNative/Scripts"
$backup = Join-Path $workRoot 'ManagedBefore'
New-Item -ItemType Directory $backup | Out-Null
$files = @('GameScripts.dll', 'GameScripts.deps.json', 'GameScripts.pdb', 'GameScripts.scripts.metadata.json')
foreach ($name in $files) {
    if (!(Test-Path "$target/$name") -or !(Test-Path "$managed/$name")) { throw "Managed fixture file missing: $name" }
    Copy-Item "$target/$name" "$backup/$name"
}
$editor = $null
try {
    foreach ($name in $files) { Copy-Item "$managed/$name" "$target/$name" -Force }
    $editor = Start-ResultProcess -Exe $exe -WorkingDirectory (Split-Path $exe) `
        -Arguments @('--development-project', ('"' + $project + '"'), '--command-service', '--allow-user-code', '--smoke-offscreen') `
        -Stdout "$workRoot/editor.out" -Stderr "$workRoot/editor.err" -Results "$workRoot/editor.runtime.jsonl"
    $endpoint = Wait-OwnedEndpoint $editor $project
    $load = Invoke-OwnedCommand $endpoint 'scene.switch' @($scene)
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $state = Invoke-OwnedCommand $endpoint 'scene.load.status' @([string]$load.requestId)
        if ($state.complete) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (!$state.complete -or $state.state -cne 'Ready') { throw 'Editor fixture scene did not become ready.' }
    foreach ($component in @('LightComponent', 'SoundComponent', 'SceneRenderProfileComponent', 'DecalComponent', 'StateMachineComponent')) {
        $null = Invoke-OwnedCommand $endpoint 'component.add' @('CharacterGateActor', $component)
    }
    $null = Invoke-OwnedCommand $endpoint 'scene.save' @($scene)
    $null = Invoke-OwnedCommand $endpoint 'play.foreground_override' @('on')
    $null = Invoke-OwnedCommand $endpoint 'play'
    Wait-PlayState $endpoint $true
    $editorResult = Wait-ProbeResult $editor "$workRoot/editor.out"
    $null = Invoke-OwnedCommand $endpoint 'component.remove' @('CharacterGateActor', 'DecalComponent')
    $null = Invoke-OwnedCommand $endpoint 'component.add' @('CharacterGateActor', 'DecalComponent')
    $retired = Invoke-OwnedCommand $endpoint 'script.invoke' @('FSharpNativeReflectionProductProbe', 'CheckRetiredDecal')
    if ([string]$retired.returnValue -ine 'true') { throw 'A retired component handle survived remove/re-add.' }
    $null = Invoke-OwnedCommand $endpoint 'stop'
    Wait-PlayState $endpoint $false
    $null = Invoke-OwnedCommand $endpoint 'quit'
    if (!$editor.WaitForExit(30000) -or $editor.ExitCode -ne 0) { throw 'Editor did not shut down cleanly.' }
    $editorResult = Read-NativeReflectionAcceptance ([IO.File]::ReadAllText("$workRoot/editor.out")) `
        $editor.Id $expectedComponents $apiVersion
    $editorJournal = Read-RuntimeResults "$workRoot/editor.runtime.jsonl" $editor.Id
    $null = Assert-EditorFinalized $editorJournal
    $editorAudio = Assert-NativeReflectionAudioDevice $editorJournal
} finally {
    if ($null -ne $editor -and !$editor.HasExited) { $editor.Kill(); $editor.WaitForExit() }
    foreach ($name in $files) { Copy-Item "$backup/$name" "$target/$name" -Force }
    foreach ($name in $files) {
        if ((Get-FileHash "$backup/$name").Hash -ne (Get-FileHash "$target/$name").Hash) { throw "Failed to restore $name" }
    }
}

# The package gate proves native frame submission. Its short smoke is distinct
# from the owned Player run, which waits for all managed/native checks to finish.
& $tool package-game --repository $repo --config $Configuration --project $project --stage-root "$workRoot/Staging" `
    --input-mode Project --startup-scene PhysicsCharacterPlayer.creator --engine-distribution $distribution `
    --render-backend dx12 --smoke-offscreen --smoke-frames 360 --smoke-promotions 30 --smoke-timeout-sec 300 *> "$workRoot/package.log"
if ($LASTEXITCODE) { throw "Cooked Player packaging or rendering failed: $workRoot/package.log" }
$pointer = Get-Content "$workRoot/Staging/Project.current.json" -Raw | ConvertFrom-Json
if ($pointer.verification -cne 'passed') { throw 'Cooked Player rendering was not verified.' }
$stage = Join-Path "$workRoot/Staging" $pointer.releaseDirectory
Copy-Item "$workRoot/package.log" "$workRoot/package-smoke.out"
$before = @{}
Get-ChildItem $stage -Recurse -File | ForEach-Object { $before[$_.FullName] = (Get-FileHash $_.FullName).Hash }
$runtime = Join-Path $workRoot 'PlayerRuntime'
New-Item -ItemType Directory $runtime | Out-Null
$player = Start-ResultProcess -Exe (Join-Path $stage 'Player.exe') -WorkingDirectory $stage `
    -Arguments @('--command-service', '--smoke-offscreen') -Stdout "$workRoot/player.out" -Stderr "$workRoot/player.err" `
    -Results "$workRoot/player.runtime.jsonl" -Environment @{ TEMP = $runtime; TMP = $runtime }
try {
    $endpoint = Wait-OwnedEndpoint $player $runtime
    $playerResult = Wait-ProbeResult $player "$workRoot/player.out"
    $null = Invoke-OwnedCommand $endpoint 'quit'
    if (!$player.WaitForExit(30000) -or $player.ExitCode -ne 0) { throw 'Cooked Player did not shut down cleanly.' }
    $playerResult = Read-NativeReflectionAcceptance ([IO.File]::ReadAllText("$workRoot/player.out")) `
        $player.Id $expectedComponents $apiVersion
    $playerJournal = Read-RuntimeResults "$workRoot/player.runtime.jsonl" $player.Id
    Assert-PlayerRuntime $playerJournal
    $playerAudio = Assert-NativeReflectionAudioDevice $playerJournal
    foreach ($file in Get-ChildItem $stage -Recurse -File) {
        if (!$before.ContainsKey($file.FullName) -or $before[$file.FullName] -ne (Get-FileHash $file.FullName).Hash) {
            throw 'The cooked package changed during product validation.'
        }
    }
    if (@(Get-ChildItem $stage -Recurse -File).Count -ne $before.Count) { throw 'The cooked package file set changed.' }
} finally { if (!$player.HasExited) { $player.Kill(); $player.WaitForExit() } }

@{
    result = 'NATIVE_REFLECTION_PRODUCT_OK'; schemaVersion = 1; configuration = $Configuration
    revision = $revision; nativeApi = $apiVersion; distribution = $distribution; evidence = $workRoot
    scope = 'Actual Editor and cooked Player; native component catalogue, typed calls, PhysX movement, audio device callbacks, renderer submission, stale handles and clean shutdown'
    editor = $editorResult; editorAudio = $editorAudio; editorNativeClosure = $editorNativeClosure; retiredComponentRejected = $true
    player = $playerResult; playerAudio = $playerAudio
} | ConvertTo-Json -Depth 12 | Set-Content "$workRoot/result.json" -Encoding utf8
"NATIVE_REFLECTION_PRODUCT_OK evidence=$workRoot"

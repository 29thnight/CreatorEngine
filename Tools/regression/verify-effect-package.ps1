#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$CookedPak,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateRange(30,600)][int]$TimeoutSeconds = 240
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$fixture = [IO.Path]::GetFullPath($FixtureProject)
$pak = [IO.Path]::GetFullPath($CookedPak)
if (Test-Path -LiteralPath $output) { throw 'Use a new output directory; previous receipts are preserved.' }
if (!(Test-Path -LiteralPath "$fixture/Assets") -or !(Test-Path -LiteralPath $pak)) { throw 'Private source fixture and cooked PAK are required.' }
if (Get-Process CreatorEditor,Player -ErrorAction SilentlyContinue) { throw 'Close running Editor/Player before replacing their build outputs.' }
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path $output | Out-Null
Copy-Item -LiteralPath $PSCommandPath -Destination "$output/runner.ps1"
$utf8 = [Text.UTF8Encoding]::new($false)
$bin = Join-Path $repo "Bin/x64-$Configuration"

function Invoke-Captured([string]$Exe, [string[]]$Arguments, [string]$Prefix, [string]$Working, [string]$TempRoot = '') {
    $si = [Diagnostics.ProcessStartInfo]::new()
    $si.FileName = $Exe; $si.WorkingDirectory = $Working
    foreach ($argument in $Arguments) { $si.ArgumentList.Add($argument) }
    $si.UseShellExecute = $false; $si.CreateNoWindow = $true
    $si.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $si.RedirectStandardOutput = $true; $si.RedirectStandardError = $true
    if ($TempRoot) { $si.Environment['TEMP'] = $TempRoot; $si.Environment['TMP'] = $TempRoot }
    $process = [Diagnostics.Process]::Start($si)
    $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill($true); $process.WaitForExit(); throw "Timed out: $Prefix" }
        $text = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText("$Prefix.stdout.txt", $text, $utf8)
        [IO.File]::WriteAllText("$Prefix.stderr.txt", $stderr.GetAwaiter().GetResult(), $utf8)
        if ($process.ExitCode -ne 0) { throw "Exit $($process.ExitCode): $Prefix" }
        return $text
    } finally { $process.Dispose() }
}

# The contract probe checks owner cleanup and rejection behavior independently
# from the four actual product runs below; it is never their substitute.
$testExe = Join-Path $output 'contracts.exe'
$runtime = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
$batch = "@echo off`r`ncall `"$vcvars`" >nul`r`ncl /nologo /std:c++latest /EHsc /W4 /WX /DCE_CONTRACT_STANDALONE $runtime `"$repo/Tools/regression/EffectPackageContractTests.cpp`" /Fo`"$output/contracts.obj`" /Fe`"$testExe`"`r`nif errorlevel 1 exit /b 1`r`n`"$testExe`"`r`n"
[IO.File]::WriteAllText("$output/contracts.cmd", $batch, $utf8)
$contracts = Invoke-Captured $env:ComSpec @('/d','/c',"$output/contracts.cmd") "$output/contracts" $output
if ($contracts -notmatch 'EFFECT_PACKAGE_CONTRACT_OK') { throw 'Contract marker missing.' }

# The package now contributes RHI graph nodes. Its complete archive closure is
# tested in actual host builds/maps below, not a standalone link with RHI stubs.
$runs = @()
# Restore the normal included composition as the final build.
foreach ($enabled in @('false','true')) {
    $case = Join-Path $output $(if ($enabled -eq 'true') { 'included' } else { 'excluded' })
    New-Item -ItemType Directory -Path $case | Out-Null
    foreach ($hostName in @('CreatorEditor','Player')) {
        $project = if ($hostName -eq 'CreatorEditor') { 'Editor/CreatorEditor.vcxproj' } else { 'Player/Player.vcxproj' }
        $props = @("/p:Configuration=$Configuration",'/p:Platform=x64',"/p:EngineEffectPackage=$enabled",'/p:EngineEffectPackageMap=true')
        $buildLog = "$case/$hostName-build.log"
        $solutionTarget = if ($hostName -eq 'CreatorEditor') { 'Editor\CreatorEditor' } else { 'Player' }
        & $msbuild (Join-Path $repo 'CreatorEngine.sln') "/t:$solutionTarget" @props /m:4 /v:minimal /nologo /fl "/flp:logfile=$buildLog" *> "$case/$hostName-build.console.txt"
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $buildLog" }
        $references = & $msbuild (Join-Path $repo $project) @props /nologo /getItem:ProjectReference | Out-String
        [IO.File]::WriteAllText("$case/$hostName-references.json", $references, $utf8)
        $refs = ($references | ConvertFrom-Json).Items.ProjectReference
        $effectRefs = @($refs | Where-Object { $_.FullPath -match '[\\/]Effect[\\/]Effect.vcxproj$' })
        if (($effectRefs.Count -eq 1) -ne ($enabled -eq 'true')) { throw 'Effect project exclusion did not change the build graph.' }
        $map = Join-Path $repo "Build/Obj/$hostName/x64-$Configuration/effect-package.map"
        Copy-Item -LiteralPath $map -Destination "$case/$hostName.map"
        $mapText = [IO.File]::ReadAllText($map)
        if (($mapText.Contains('ce_effect_package_anchor')) -ne ($enabled -eq 'true')) { throw 'Link map does not match selected package.' }
        if (($mapText -match 'Effect:EffectPackage\.obj') -ne ($enabled -eq 'true')) { throw 'Effect archive member retention differs from selection.' }

        $prefix = "$case/$hostName"
        if ($hostName -eq 'CreatorEditor') {
            $commands = @('scene.new EffectPackageSmoke', 'wait 30', 'editor.viewport scene', 'object.create EffectCamera', 'component.add EffectCamera CameraComponent',
                'wait 120', ('render.pbr.capture "' + "$case/scene-capture" + '" editor'),
                'editor.viewport game', 'wait 30', ('render.pbr.capture "' + "$case/game-capture" + '" game'), 'quit')
            [IO.File]::WriteAllLines("$case/commands.txt", $commands, $utf8)
            $exe = Join-Path $bin 'Editor/CreatorEditor.exe'
            $text = Invoke-Captured $exe @('--development-project',$fixture,'--smoke-offscreen','--commandlet-script',"$case/commands.txt",'--result-file',"$case/results.jsonl") $prefix $repo
            $rows = @(Get-Content -LiteralPath "$case/results.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
            if ($rows.Count -ne $commands.Count -or @($rows | Where-Object status -ne 'succeeded').Count) { throw 'Editor did not complete its startup/quit commands.' }
        } else {
            $stage = "$case/PlayerStage"
            New-Item -ItemType Directory -Path $stage | Out-Null
            Copy-Item -LiteralPath $pak -Destination "$stage/GameAssets.pak"
            Copy-Item -LiteralPath "$bin/Runtime","$bin/Managed","$bin/Resources" -Destination $stage -Recurse
            Copy-Item -LiteralPath "$bin/Player/Player.exe","$bin/Player/Player.runtime.dll" -Destination $stage
            New-Item -ItemType Directory -Path "$case/temp" | Out-Null
            $exe = "$stage/Player.exe"
            $text = Invoke-Captured $exe @('--smoke','30','--smoke-offscreen','--smoke-promotions','2','--smoke-reload') $prefix $stage "$case/temp"
            if ($text -notmatch '\[player.smoke\] .*"ready":true' -or $text -notmatch '\[runtime.text-parser\] calls=0') { throw 'Player did not finish a cooked rendered smoke.' }
            $traces = @(Get-ChildItem "$case/temp" -Recurse -File -Filter shutdown_trace.txt)
            if ($traces.Count -ne 1) { throw 'Player shutdown trace missing.' }
            Copy-Item -LiteralPath $traces[0].FullName -Destination "$case/Player-shutdown.txt"
            if ([IO.File]::ReadAllText($traces[0].FullName) -notmatch '\[8\] atexit') { throw 'Player did not reach runtime teardown.' }
        }
        $initialized = [regex]::Matches($text, '\[effect.package\] initialized').Count
        $stopped = [regex]::Matches($text, '\[effect.package\] shutdown').Count
        $absent = [regex]::Matches($text, '\[effect.package\] unavailable').Count
        if ($enabled -eq 'true') {
            if ($initialized -ne 1 -or $stopped -ne 1 -or $absent -ne 0) { throw 'Effect startup/shutdown was not exactly once.' }
            if ($text.IndexOf('[effect.package] initialized') -ge $text.IndexOf('[effect.package] shutdown')) { throw 'Effect shutdown order is invalid.' }
            if ($text -notmatch '\[effect.bridge\] ticks=(\d+) snapshots=(\d+) hdrDeclarations=(\d+) hdrRecords=(\d+)') { throw 'Effect bridge observations missing.' }
            if ([long]$Matches[2] -le 0 -or [long]$Matches[3] -le 0 -or [long]$Matches[4] -le 0) { throw 'Owned effect snapshot did not reach HDR graph recording.' }
            if ($hostName -eq 'Player' -and [long]$Matches[1] -le 0) { throw 'Player never committed an effect simulation tick.' }
        } elseif ($initialized -ne 0 -or $stopped -ne 0 -or $absent -ne 1) { throw 'Excluded package still ran.' }
        $hostDll = if ($hostName -eq 'Player') { "$bin/Player/Player.runtime.dll" } else { "$bin/Editor/CreatorEditor.runtime.dll" }
        $runs += @{ host=$hostName; packageIncluded=($enabled -eq 'true'); exitCode=0; initialized=$initialized; shutdown=$stopped; absent=$absent; hostSha256=(Get-FileHash -LiteralPath $hostDll).Hash; map="$case/$hostName.map" }
        Write-Output "EFFECT_PACKAGE_PRODUCT_OK $Configuration $hostName included=$enabled"
    }
}
@{ passed=$true; configuration=$Configuration; cookedPak=$pak; pakSha256=(Get-FileHash -LiteralPath $pak).Hash; fixture=$fixture; runs=$runs } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$output/result.json" -Encoding utf8
"EFFECT_PACKAGE_MATRIX_OK $Configuration $output"

[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution, [ValidateSet('dx12','vulkan')][string]$RenderBackend='dx12', [string]$ExistingProject='', [switch]$ExcludeLegacySampleScenes, [string]$StartupScene='FT_Primitives.creator')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Import-Module (Join-Path $repo 'Tools/distribution/EngineDistribution.psm1')
Import-Module (Join-Path $repo 'Tools/runtime/RuntimeLayout.psm1')
$engine = [IO.Path]::GetFullPath($EngineDistribution)
$manifest = Read-EngineDistribution $engine
$owner = Join-Path $env:LOCALAPPDATA 'CreatorEngine/DistributionValidation'
$project = if ($ExistingProject) { Assert-EngineChildPath $ExistingProject $owner } else { Assert-EngineChildPath (Join-Path $owner ("외부 게임 $($manifest.configuration) " + [Guid]::NewGuid().ToString('N'))) $owner }
if (-not $ExistingProject) {
[void][IO.Directory]::CreateDirectory($project)
# Independent project copy: no engine source, vcxproj, csproj, vcpkg or repository access is needed by the child.
foreach ($directory in @('Assets','ProjectSetting')) {
    Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/$directory") -Destination (Join-Path $project $directory) -Recurse
}
# Model sidecars refer to committed generation IDs in the project's imported data.
# Preserve that project state; none of it is an engine/toolchain dependency.
$modelLibrary = Join-Path $project 'Library'
[void][IO.Directory]::CreateDirectory($modelLibrary)
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/Library/ModelAssetGenerations') -Destination (Join-Path $modelLibrary 'ModelAssetGenerations') -Recurse
$scripts = Join-Path $project 'Assets/Script/ValidationSamples'
[void][IO.Directory]::CreateDirectory($scripts)
foreach ($source in Get-ChildItem (Join-Path $repo 'GameScripts') -Filter '*.cs' -File) {
    Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $scripts $source.Name)
}
}
if ($ExcludeLegacySampleScenes) {
    # These legacy examples have unresolved GUIDs. Limit this opt-in fixture edit
    # to the independent validation copy; the source project remains untouched.
    foreach ($name in @('Test1.creator','Test1.creator.meta','Test2.creator','Test2.creator.meta')) {
        $path = Assert-EngineChildPath (Join-Path $project "Assets/Scenes/$name") $project
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    [IO.File]::WriteAllText((Join-Path $project 'validation-excluded-scenes.txt'), "Test1.creator`nTest2.creator`n")
}
$buildTool = Join-Path $engine $manifest.buildTool
if ($StartupScene -eq 'PrebuiltSmoke.creator' -and -not (Test-Path (Join-Path $project "Assets/Scenes/$StartupScene"))) {
    $source = Get-Content -LiteralPath (Join-Path $project 'Assets/Scenes/FT_Primitives.creator') -Raw
    $entities = [regex]::Matches($source, '(?m)^  - Entity:')
    if ($entities.Count -ne 11) { throw 'Unexpected primitive fixture schema.' }
    $scene = $source.Substring(0, $entities[3].Index).Replace('m_name: FT_Primitives', 'm_name: PrebuiltSmoke').Replace(
        'm_childrenIndices: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]', 'm_childrenIndices: [1, 2]')
    [IO.File]::WriteAllText((Join-Path $project "Assets/Scenes/$StartupScene"), $scene, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $project "Assets/Scenes/$StartupScene.meta"), ('guid: ' + [Guid]::NewGuid().ToString('D') + "`n"))
}
$stageRoot = Join-Path $project "Intermediate/Builds/$($manifest.configuration)-$RenderBackend"
& $buildTool select-engine -Project $project -EngineDistribution $engine
if ($LASTEXITCODE -ne 0) { throw 'Could not pin the external project.' }
$start = [Diagnostics.ProcessStartInfo]::new($buildTool)
foreach ($argument in @('package-game',
    '-Config',$manifest.configuration,'-InputMode','Project','-Project',$project,'-EngineDistribution',$engine,
    '-StartupScene',$StartupScene,'-RenderBackend',$RenderBackend,'-SmokeFrames','120',
    '-StageRoot',$stageRoot,'-LogPath',(Join-Path $project "Intermediate/package-$($manifest.configuration)-$RenderBackend.log"))) { $start.ArgumentList.Add($argument) }
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WorkingDirectory = $project
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.StandardOutputEncoding = [Text.Encoding]::UTF8
$start.StandardErrorEncoding = [Text.Encoding]::UTF8
$start.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
$start.Environment['DOTNET_ROOT'] = Join-Path $project 'NoSystemDotnet'
$start.Environment['DOTNET_MULTILEVEL_LOOKUP'] = '0'
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$deadline = [DateTime]::UtcNow.AddMinutes(30)
while (-not $process.WaitForExit(30000)) {
    if ([DateTime]::UtcNow -gt $deadline) { $process.Kill($true); $process.WaitForExit(); throw 'External project validation timed out after 30 minutes.' }
    Write-Host "Prebuilt $($manifest.configuration)/$RenderBackend validation running: $project"
}
$out = $stdout.GetAwaiter().GetResult()
$err = $stderr.GetAwaiter().GetResult()
[IO.File]::WriteAllText((Join-Path $project 'validation.stdout.log'),$out)
[IO.File]::WriteAllText((Join-Path $project 'validation.stderr.log'),$err)
if ($process.ExitCode -ne 0) { throw "Prebuilt package failed ($($process.ExitCode)): $project`n$err" }
$pointers = @(Get-ChildItem $stageRoot -Filter '*.current.json' -File)
if ($pointers.Count -ne 1) { throw 'Expected exactly one verified game package pointer.' }
Write-EngineJson (Join-Path $repo "Build/Tests/prebuilt-project-$($manifest.configuration)-$RenderBackend.json") @{
    project=$project; engine=$engine; engineBuildId=$manifest.buildId; pointer=$pointers[0].FullName;
    configuration=$manifest.configuration; backend=$RenderBackend; startupScene=$StartupScene; isolatedPath=$start.Environment['PATH']; exitCode=$process.ExitCode;
    excludedLegacyScenes=(Test-Path (Join-Path $project 'validation-excluded-scenes.txt'))
}
Write-Host "PREBUILT_PROJECT_VERIFIED $($manifest.configuration)/$RenderBackend $project"

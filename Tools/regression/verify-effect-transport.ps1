#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$CookedPak,
    [string]$ProductReceiptPath = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$fixture = [IO.Path]::GetFullPath($FixtureProject)
if (Test-Path -LiteralPath $output) { throw 'Use a new output directory.' }
if (Get-Process CreatorEditor,Player -ErrorAction SilentlyContinue) { throw 'Close Editor/Player before this isolated gate.' }
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$native = @()
foreach ($config in @('Debug','Release')) {
    foreach ($test in @('EffectStateTests','EffectDefinitionTests')) {
        & $msbuild "$PSScriptRoot/$test.vcxproj" /t:Build "/p:Configuration=$config" /p:Platform=x64 /p:EngineEffectPackage=true /m:2 /v:minimal /nologo *> "$output/$test-$config-build.log"
        if ($LASTEXITCODE) { throw "Native build failed: $test $config" }
        $folder = if ($test -eq 'EffectStateTests') { 'EffectState' } else { 'EffectDefinition' }
        $exe = "$repo/Build/Tests/$folder/x64-$config/$test.exe"
        $text = (& $exe 2>&1 | Out-String); $code = $LASTEXITCODE
        [IO.File]::WriteAllText("$output/$test-$config.log",$text,$utf8)
        if ($code -ne 0 -or $text -notmatch 'EFFECT_(STATE|DEFINITION)_OK checks=(\d+)') { throw "Native test failed: $text" }
        $native += @{ test=$test; configuration=$config; checks=[int]$Matches[2]; sha256=(Get-FileHash -LiteralPath $exe).Hash }
        Write-Output "EFFECT_TRANSPORT_NATIVE_OK $test $config"
    }
}
if (!$ProductReceiptPath) {
    & "$PSScriptRoot/verify-effect-package.ps1" -FixtureProject $fixture -CookedPak $CookedPak -OutputDirectory "$output/product" -Configuration Debug
    $ProductReceiptPath = "$output/product/result.json"
}
$product = Get-Content -LiteralPath $ProductReceiptPath -Raw | ConvertFrom-Json
if (!$product.passed -or $product.configuration -ne 'Debug' -or $product.runs.Count -ne 4) { throw 'A complete Debug package matrix is required.' }
foreach ($hostName in @('CreatorEditor','Player')) {
    $folder = if ($hostName -eq 'Player') { 'Player' } else { 'Editor' }
    $rows = @($product.runs | Where-Object { $_.host -eq $hostName -and $_.packageIncluded })
    if ($rows.Count -ne 1 -or (Get-FileHash -LiteralPath "$repo/Bin/x64-Debug/$folder/$hostName.runtime.dll").Hash -ne $rows[0].hostSha256) {
        throw 'Product receipt does not identify the currently installed included host.'
    }
}
$commands = @('scene.new EffectResizeSmoke', 'wait 30', 'editor.viewport scene', 'object.create EffectCamera', 'component.add EffectCamera CameraComponent',
    'window.resize 720 480', 'wait 120', ('render.pbr.capture "' + "$output/before-resize" + '" editor'),
    'window.resize 960 640', 'wait 120', ('render.pbr.capture "' + "$output/after-resize" + '" editor'), 'quit')
[IO.File]::WriteAllLines("$output/resize-commands.txt",$commands,$utf8)
$si = [Diagnostics.ProcessStartInfo]::new()
$si.FileName = "$repo/Bin/x64-Debug/Editor/CreatorEditor.exe"; $si.WorkingDirectory = $repo
foreach ($argument in @('--development-project',$fixture,'--smoke-offscreen','--commandlet-script',"$output/resize-commands.txt",'--result-file',"$output/resize-results.jsonl")) { $si.ArgumentList.Add($argument) }
$si.UseShellExecute = $false; $si.CreateNoWindow = $true; $si.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$si.RedirectStandardOutput = $true; $si.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($si)
$outTask=$process.StandardOutput.ReadToEndAsync(); $errTask=$process.StandardError.ReadToEndAsync()
try {
    if (!$process.WaitForExit(240000)) { $process.Kill($true); $process.WaitForExit(); throw 'Resize timed out.' }
    $text=$outTask.GetAwaiter().GetResult(); $errorText=$errTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText("$output/resize.stdout.txt",$text,$utf8); [IO.File]::WriteAllText("$output/resize.stderr.txt",$errorText,$utf8)
    $rows=@(Get-Content "$output/resize-results.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
    if ($process.ExitCode -ne 0 -or $rows.Count -ne $commands.Count -or @($rows | Where-Object status -ne 'succeeded').Count) { throw 'Resize commands failed.' }
    if ($text -notmatch '\[effect.bridge\] ticks=(\d+) snapshots=(\d+) hdrDeclarations=(\d+) hdrRecords=(\d+)' -or [long]$Matches[4] -lt 2) { throw 'Resize frames did not reach HDR recording.' }
    $dimensions=@()
    foreach($capture in @('before-resize','after-resize')) {
        $manifest=Get-Content "$output/$capture/manifest.json" -Raw | ConvertFrom-Json
        $dimensions += "$($manifest.width)x$($manifest.height)"
    }
    if ($dimensions[0] -eq $dimensions[1]) { throw 'Capture dimensions did not change after native resize.' }
} finally { $process.Dispose() }
$sourcePaths=@('Engine/RenderEngine/Effects/EffectFrame.h','Engine/RenderEngine/Effects/IEffectSystem.h','Engine/Effect/EffectState.h',
    'Engine/Effect/EffectState.cpp','Engine/Effect/EffectRenderContributor.h','Engine/Effect/EffectRenderContributor.cpp',
    'Engine/Effect/EffectPackage.cpp','Engine/Effect/Effect.vcxproj','Engine/Effect/EffectDefinition.h','Engine/Effect/EffectDefinition.cpp',
    'Engine/RuntimeHost/EngineBootstrap.h','Engine/SceneRuntime/SceneManager.cpp','Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h',
    'Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp','Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h',
    'Tools/regression/EffectStateTests.cpp','Tools/regression/EffectStateTests.vcxproj','Tools/regression/verify-effect-transport.ps1','Tools/regression/verify-effect-package.ps1')
$sources=@($sourcePaths | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash} })
@{ passed=$true; native=$native; productReceipt=[IO.Path]::GetFullPath($ProductReceiptPath); resizeDimensions=$dimensions; sources=$sources;
    limitations=@('HDR boundary records immutable ownership only; no particle draw or GPU simulation', 'Burst submission receipts are tested with a CPU protocol consumer; production HDR observer does not acknowledge simulation work') } |
    ConvertTo-Json -Depth 7 | Set-Content -LiteralPath "$output/result.json" -Encoding utf8
"EFFECT_TRANSPORT_OK $output"

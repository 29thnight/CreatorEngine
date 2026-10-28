[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $OutputPath) { $OutputPath = Join-Path $repo ('Build/Verification/ScriptNativeHeadless-' + [guid]::NewGuid().ToString('N')) }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw 'Use a new verification directory' }
$assets = Join-Path $OutputPath 'Assets/Scenes'
New-Item -ItemType Directory -Path $assets -Force | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/fixtures/script-native-headless/Scene.creator","$PSScriptRoot/fixtures/script-native-headless/Scene.creator.meta" -Destination $assets
$cooker = Join-Path $repo "Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
& $cooker --asset-root (Split-Path $assets) --scene "$assets/Scene.creator" --output "$OutputPath/Cooked" *> "$OutputPath/cook.log"
if ($LASTEXITCODE) { throw "Scene cook failed: $OutputPath/cook.log" }
$cooked = @(Get-ChildItem "$OutputPath/Cooked/Derived/Scenes" -Recurse -File -Filter '*.creator')
if ($cooked.Count -ne 1) { throw 'Expected exactly one cooked scene' }
& "$repo/Tools/build/stage-headless.ps1" -Configuration $Configuration -CookedScene $cooked[0].FullName -ManagedDirectory "$repo/Bin/x64-$Configuration/Managed" -OutputPath "$OutputPath/Package" -FSharpNative
$previous = $env:CREATOR_FSHARP_NATIVE
try {
    # A normal product must ignore the legacy comparison setting.
    $env:CREATOR_FSHARP_NATIVE = '0'
    & "$OutputPath/Package/HeadlessHost.exe" --scene "$OutputPath/Package/Scene.cedo" --managed "$OutputPath/Package/Managed" --report "$OutputPath/report.json" --frames 120 --frame-seconds 0.016666666666666666 *> "$OutputPath/host.log"
    if ($LASTEXITCODE) { throw "Headless host failed: $OutputPath/host.log" }
    $report = Get-Content -LiteralPath "$OutputPath/report.json" -Raw | ConvertFrom-Json
    $mover = @($report.scene.entities | Where-Object { $_.name -eq 'Mover' })
    if ($report.scene.managedErrors -or $report.scene.committedTicks -ne 120 -or $mover.Count -ne 1 -or [Math]::Abs($mover[0].x - 6.0) -gt 0.001) {
        throw "F# movement or lifecycle failed: $OutputPath/report.json"
    }
    if (Test-Path -LiteralPath "$OutputPath/Package/Managed/ScriptCore.dll") { throw 'Package contains ScriptCore.dll' }
    Write-Output "SCRIPT_NATIVE_HEADLESS_OK evidence=$OutputPath"
} finally {
    if ($null -eq $previous) { Remove-Item Env:CREATOR_FSHARP_NATIVE -ErrorAction SilentlyContinue }
    else { $env:CREATOR_FSHARP_NATIVE = $previous }
}

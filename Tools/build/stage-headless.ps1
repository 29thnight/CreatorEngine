[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory)][string]$CookedScene,
    [Parameter(Mandatory)][string]$ManagedDirectory,
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$Dependencies = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw 'Headless staging requires a new output directory' }
if (-not $Dependencies) { $Dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows' }
$nativeBin = if ($Configuration -eq 'Debug') { Join-Path $Dependencies 'debug/bin' } else { Join-Path $Dependencies 'bin' }
$bytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($CookedScene))
if ($bytes.Length -lt 16 -or [Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'CEDO') {
    throw 'Only an offline cooked CEDO scene can be staged'
}
New-Item -ItemType Directory -Path "$OutputPath/Managed/Scripts" -Force | Out-Null
Copy-Item -LiteralPath "$repo/Bin/x64-$Configuration/HeadlessHost/HeadlessHost.exe" -Destination $OutputPath
Copy-Item -LiteralPath "$repo/ThirdParty/DotNetHost/bin/nethost.dll" -Destination $OutputPath
Copy-Item -LiteralPath "$repo/Bin/x64-$Configuration/Runtime/Common/gcce.dll" -Destination $OutputPath

# Explicit CPU dependency inventory. Never copy Runtime/Common or the vcpkg bin tree.
foreach ($name in 'PhysX_64.dll','PhysXCommon_64.dll','PhysXFoundation_64.dll','PhysXCooking_64.dll') {
    Copy-Item -LiteralPath (Join-Path $nativeBin $name) -Destination $OutputPath
}
$formatLibrary = if ($Configuration -eq 'Debug') { 'fmtd.dll' } else { 'fmt.dll' }
Copy-Item -LiteralPath (Join-Path $nativeBin $formatLibrary) -Destination $OutputPath
foreach ($name in 'ScriptCore.dll','ScriptCore.deps.json','ScriptCore.runtimeconfig.json',
    'CreatorEngine.ManagedRuntime.dll','CreatorEngine.ManagedRuntime.deps.json',
    'CreatorEngine.ManagedRuntime.runtimeconfig.json','FSharp.Core.dll') {
    Copy-Item -LiteralPath (Join-Path $ManagedDirectory $name) -Destination "$OutputPath/Managed"
}
foreach ($name in 'GameScripts.dll','GameScripts.deps.json','CreatorEngine.FSharp.Runtime.dll','FSharp.Core.dll') {
    Copy-Item -LiteralPath (Join-Path $ManagedDirectory "Scripts/$name") -Destination "$OutputPath/Managed/Scripts"
}
Copy-Item -LiteralPath $CookedScene -Destination "$OutputPath/Scene.cedo"
$inventory = @(Get-ChildItem -LiteralPath $OutputPath -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($OutputPath.Length + 1).Replace('\','/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
@{ schemaVersion = 1; runtime = 'shared-scene-renderless'; configuration = $Configuration;
   prerequisites = 'Installed .NET 10 x64 and MSVC runtime required'; files = $inventory } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$OutputPath/package.json" -Encoding utf8
Write-Output "HEADLESS_PACKAGE_OK $OutputPath"

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$BinaryRoot,
    [Parameter(Mandatory)][string]$ViewerPath,
    [Parameter(Mandatory)][ValidateSet('Debug','Release')][string]$Config,
    [Parameter(Mandatory)][string]$VcpkgDirectory,
    [Parameter(Mandatory)][string]$VcToolsDirectory,
    [Parameter(Mandatory)][string]$VcRedistDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../runtime/RuntimeLayout.psm1')
$Repository = [IO.Path]::GetFullPath($Repository)
$BinaryRoot = [IO.Path]::GetFullPath($BinaryRoot)
$ViewerPath = Assert-EngineChildPath $ViewerPath $BinaryRoot
$viewerRoot = Assert-EngineChildPath (Join-Path $BinaryRoot 'Tools/ProfilerViewer') $BinaryRoot
if ([IO.Path]::GetFullPath($ViewerPath) -ine (Join-Path $viewerRoot 'ProfilerViewer.exe')) {
    throw 'ProfilerViewer must use the fixed Tools/ProfilerViewer/ProfilerViewer.exe layout.'
}
$lookup = @{}
$vcpkgBin = Join-Path $VcpkgDirectory $(if ($Config -eq 'Debug') { 'debug/bin' } else { 'bin' })
foreach ($file in @(Get-ChildItem -LiteralPath $vcpkgBin -Filter '*.dll' -File)) {
    $lookup[$file.Name] = $file.FullName
}
if (-not (Test-Path -LiteralPath (Join-Path $VcRedistDirectory 'x64'))) {
    $tools = [IO.DirectoryInfo]::new([IO.Path]::GetFullPath($VcToolsDirectory))
    $VcRedistDirectory = Join-Path $tools.Parent.Parent.Parent.FullName ('Redist/MSVC/' + $tools.Name)
}
foreach ($suffix in @('x64', 'debug_nonredist/x64')) {
    $directory = Join-Path $VcRedistDirectory $suffix
    if (Test-Path -LiteralPath $directory) {
        foreach ($file in @(Get-ChildItem -LiteralPath $directory -Filter '*.dll' -Recurse -File)) {
            $lookup[$file.Name] = $file.FullName
        }
    }
}
if ($Config -eq 'Debug') {
    $ucrt = Get-ChildItem -Path "${env:ProgramFiles(x86)}/Windows Kits/10/bin/*/x64/ucrt/ucrtbased.dll" -File |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($null -ne $ucrt) {
        $lookup['ucrtbased.dll'] = $ucrt.FullName
    }
}
$asan = Join-Path $VcToolsDirectory 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll'
if (Test-Path -LiteralPath $asan) {
    $lookup['clang_rt.asan_dynamic-x86_64.dll'] = $asan
}
$queue = [Collections.Generic.Queue[string]]::new()
foreach ($name in @(Get-EnginePeImports $ViewerPath)) {
    $queue.Enqueue($name)
}
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$paths = [Collections.Generic.List[string]]::new()
$paths.Add('Tools/ProfilerViewer/ProfilerViewer.exe')
while ($queue.Count -ne 0) {
    $name = $queue.Dequeue()
    if (-not $seen.Add($name)) {
        continue
    }
    if ([IO.Path]::GetFileName($name) -ne $name -or
        $name -match '(?i)(\.runtime\.dll$|^efsw|^physx|^slang|^nethost|^hostfxr|^fmod)') {
        throw "Non-viewer or invalid runtime dependency: $name"
    }
    if ($name -match '^(api-ms-|ext-ms-)') {
        continue
    }
    if (-not $lookup.ContainsKey($name)) {
        if ($name -notmatch '^(msvcp|vcruntime|concrt|ucrtbased)' -and
            (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32/$name"))) {
            continue
        }
        throw "Unresolved ProfilerViewer runtime dependency: $name"
    }
    $destination = Assert-EngineChildPath (Join-Path $viewerRoot $name) $BinaryRoot
    $source = $lookup[$name]
    if (-not (Test-Path -LiteralPath $destination) -or (Get-EngineFileHash $source) -ne (Get-EngineFileHash $destination)) {
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    $paths.Add("Tools/ProfilerViewer/$name")
    foreach ($dependency in @(Get-EnginePeImports $source)) {
        $queue.Enqueue($dependency)
    }
}
$fonts = Join-Path $Repository 'Resources/Editor/Fonts'
foreach ($required in @('Inter-Regular.ttf', 'MaterialSymbolsOutlined-Editor.ttf', 'LICENSE-Inter.txt', 'LICENSE-MaterialSymbols.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $fonts $required) -PathType Leaf)) {
        throw "ProfilerViewer shared font/license is missing: $required"
    }
}
foreach ($file in @(Get-ChildItem -LiteralPath $fonts -File -Recurse)) {
    $relative = 'Tools/ProfilerViewer/Resources/Fonts/' + [IO.Path]::GetRelativePath($fonts, $file.FullName).Replace('\', '/')
    $destination = Assert-EngineChildPath (Join-Path $BinaryRoot $relative) $BinaryRoot
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    $paths.Add($relative)
}
$version = Get-Content -Raw -LiteralPath (Join-Path $Repository 'EngineVersion.json') | ConvertFrom-Json
$fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($ViewerPath)
if ($fileVersion.FileVersion -ne $version.version -or $fileVersion.ProductVersion -ne $version.version) {
    throw 'ProfilerViewer version resource differs from EngineVersion.json. Regenerate version sources and rebuild.'
}
$entries = @(Get-EngineEntries $BinaryRoot $paths.ToArray())
Test-EngineEntries $BinaryRoot $entries
# This standalone application is never loaded or launched during deployment.
Write-EngineJson (Join-Path $viewerRoot 'deployment.json') ([ordered]@{
    schemaVersion = 1
    tool = 'ProfilerViewer'
    configuration = $Config
    version = $version.version
    entries = $entries
    digest = Get-EngineDigest $entries
})

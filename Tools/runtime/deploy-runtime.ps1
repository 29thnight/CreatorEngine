[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$BinaryRoot,
    [Parameter(Mandatory)][string]$HostPath,
    [Parameter(Mandatory)][ValidateSet('CreatorEditor','Player','AssetCooker','AssetPacker')][string]$HostName,
    [Parameter(Mandatory)][ValidateSet('Debug','Release')][string]$Config,
    [Parameter(Mandatory)][string]$VcpkgDirectory,
    [Parameter(Mandatory)][string]$VcToolsDirectory,
    [Parameter(Mandatory)][string]$VcRedistDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RuntimeLayout.psm1')
$Repository = [IO.Path]::GetFullPath($Repository)
$BinaryRoot = [IO.Path]::GetFullPath($BinaryRoot)
$HostPath = Assert-EngineChildPath $HostPath $BinaryRoot
$runtime = Assert-EngineChildPath (Join-Path $BinaryRoot 'Runtime') $BinaryRoot
[void][IO.Directory]::CreateDirectory($runtime)
$lock = $null
for ($attempt = 0; $attempt -lt 600 -and $null -eq $lock; $attempt++) {
    try { $lock = [IO.File]::Open((Join-Path $runtime '.deploy.lock'), 'OpenOrCreate', 'ReadWrite', 'None') }
    catch [IO.IOException] { Start-Sleep -Milliseconds 100 }
}
if ($null -eq $lock) { throw 'Timed out waiting for runtime deployment lock.' }
try {
    $lookup = @{}
    $vcpkgBin = Join-Path $VcpkgDirectory $(if ($Config -eq 'Debug') { 'debug\bin' } else { 'bin' })
    if (-not (Test-Path -LiteralPath $vcpkgBin)) { throw "vcpkg runtime directory missing: $vcpkgBin" }
    $roots = @($vcpkgBin, (Join-Path $Repository 'ThirdParty\Slang\bin'),
        (Join-Path $Repository 'ThirdParty\DotNetHost\bin'), (Join-Path $Repository 'ThirdParty\Fmod\bin\x64'))
    foreach ($dir in $roots) {
        foreach ($file in @(Get-ChildItem -LiteralPath $dir -Filter '*.dll' -File)) { $lookup[$file.Name] = $file.FullName }
    }
    $asan = Join-Path $VcToolsDirectory 'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll'
    if (Test-Path -LiteralPath $asan) { $lookup['clang_rt.asan_dynamic-x86_64.dll'] = $asan }
    if (-not (Test-Path -LiteralPath (Join-Path $VcRedistDirectory 'x64'))) {
        $toolDir = [IO.DirectoryInfo]::new([IO.Path]::GetFullPath($VcToolsDirectory))
        $VcRedistDirectory = Join-Path $toolDir.Parent.Parent.Parent.FullName ('Redist\MSVC\' + $toolDir.Name)
    }
    foreach ($suffix in @('x64', 'debug_nonredist\x64')) {
        $dir = Join-Path $VcRedistDirectory $suffix
        if (Test-Path -LiteralPath $dir) {
            foreach ($file in @(Get-ChildItem -LiteralPath $dir -Filter '*.dll' -Recurse -File)) { $lookup[$file.Name] = $file.FullName }
        }
    }
    # The debug UCRT is a development-only SDK dependency, never a release payload.
    if ($Config -eq 'Debug') {
        $ucrt = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\ucrt\ucrtbased.dll" -File |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($null -ne $ucrt) { $lookup['ucrtbased.dll'] = $ucrt.FullName }
    }
    $queue = [Collections.Generic.Queue[string]]::new()
    foreach ($name in @(Get-EnginePeImports $HostPath)) { $queue.Enqueue($name) }
    if ($HostName -in @('CreatorEditor','Player')) {
        foreach ($name in @('slang-compiler.dll','dxcompiler.dll','dxil.dll','PhysXGpu_64.dll','PhysXDevice64.dll')) { $queue.Enqueue($name) }
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $paths = [Collections.Generic.List[string]]::new()
    $systemImports = [Collections.Generic.List[string]]::new()
    [void][IO.Directory]::CreateDirectory((Join-Path $runtime 'Common'))
    [void][IO.Directory]::CreateDirectory((Join-Path $runtime 'Editor'))
    while ($queue.Count) {
        $name = $queue.Dequeue()
        if (-not $seen.Add($name)) { continue }
        if ([IO.Path]::GetFileName($name) -ne $name) { throw "Invalid import name: $name" }
        if ($name -match '^(api-ms-|ext-ms-)') { $systemImports.Add($name); continue }
        if (-not $lookup.ContainsKey($name)) {
            if ($name -notmatch '^(msvcp|vcruntime|concrt|ucrtbased)' -and
                (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$name"))) {
                $systemImports.Add($name); continue
            }
            throw "Unresolved runtime dependency: $HostName -> $name"
        }
        $editorOnly = $name -ieq 'efsw.dll'
        if ($editorOnly -and $HostName -ne 'CreatorEditor') { throw "Editor-only dependency leaked into $HostName : $name" }
        $group = if ($editorOnly) { 'Editor' } else { 'Common' }
        $relative = "Runtime/$group/$name"
        $destination = Assert-EngineChildPath (Join-Path $BinaryRoot $relative) $BinaryRoot
        $source = $lookup[$name]
        if (-not (Test-Path -LiteralPath $destination) -or (Get-EngineFileHash $source) -ne (Get-EngineFileHash $destination)) {
            Copy-Item -LiteralPath $source -Destination $destination -Force
        }
        $paths.Add($relative)
        foreach ($dependency in @(Get-EnginePeImports $source)) { $queue.Enqueue($dependency) }
    }
    [IO.File]::WriteAllText((Join-Path $runtime 'layout.version'), "1`n")
    $hostDirectory = Split-Path -Parent $HostPath
    $launcher = Join-Path $hostDirectory "$HostName.exe"
    foreach ($path in @($HostPath, $launcher, (Join-Path $runtime 'layout.version'))) {
        $paths.Add([IO.Path]::GetRelativePath($BinaryRoot, $path).Replace('\','/'))
    }
    $entries = @(Get-EngineEntries $BinaryRoot $paths.ToArray())
    Test-EngineEntries $BinaryRoot $entries
    # Only remove copies whose replacement has just been verified. Never remove host DLLs/plugins.
    foreach ($entry in $entries | Where-Object { $_.path -like 'Runtime/*/*.dll' }) {
        $legacy = Assert-EngineChildPath (Join-Path $hostDirectory ([IO.Path]::GetFileName($entry.path))) $BinaryRoot
        if (Test-Path -LiteralPath $legacy -PathType Leaf) { Remove-Item -LiteralPath $legacy -Force }
    }
    # Remove the retired app-local ASan deployment only when this host no longer imports it.
    if (-not $seen.Contains('clang_rt.asan_dynamic-x86_64.dll')) {
        $legacyAsan = Assert-EngineChildPath (Join-Path $hostDirectory 'clang_rt.asan_dynamic-x86_64.dll') $BinaryRoot
        if (Test-Path -LiteralPath $legacyAsan -PathType Leaf) { Remove-Item -LiteralPath $legacyAsan -Force }
    }
    $infoText = & $launcher --engine-info | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Host loading probe failed: $HostName ($LASTEXITCODE)" }
    $info = ($infoText | Select-Object -Last 1) | ConvertFrom-Json
    $manifest = [ordered]@{ schemaVersion = 1; host = $HostName; configuration = $Config;
        abi = $info; entries = $entries; digest = Get-EngineDigest $entries; systemImports = @($systemImports | Sort-Object) }
    Write-EngineJson (Join-Path $runtime "Manifests\$HostName.json") $manifest
    Write-Host "Shared runtime: $HostName, $($entries.Count) files, $($manifest.digest)"
}
finally { $lock.Dispose() }

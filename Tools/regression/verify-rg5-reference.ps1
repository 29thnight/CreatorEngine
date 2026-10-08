param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) {
    throw 'Use a new artifact directory; historical evidence is preserved'
}
New-Item -ItemType Directory -Path $output | Out-Null
$sources = @(Get-ChildItem "$repo/Engine/RenderEngine", "$repo/Tools/regression", "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Recurse -File |
    Where-Object Extension -In '.cpp', '.h', '.inl', '.slang', '.vcxproj', '.ps1') | Sort-Object FullName -Unique
$hashes = @($sources | ForEach-Object {
    [pscustomobject]@{path = $_.FullName; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$hashes | ConvertTo-Json -Depth 4 | Set-Content "$output/source-hashes.json" -Encoding utf8
$exe = "$repo/Bin/x64-$Configuration/Tools/MaterialRasterSurfaceProbe/MaterialRasterSurfaceProbe.exe"
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$previousPath = $env:PATH
$previousValidation = $env:CREATOR_DX12_VALIDATION
$proc = $null
try {
    $dependency = if ($Configuration -eq 'Debug') {'debug/bin'} else {'bin'}
    $env:PATH = "$repo/vcpkg_installed/x64-windows/x64-windows/$dependency;$repo/vcpkg_installed/x64-windows/$dependency;$(Split-Path $exe);$previousPath"
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    $proc = Start-Process $exe -ArgumentList @($repo, '--rg5-reference') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$output/stdout.log" -RedirectStandardError "$output/stderr.log"
    $deadline = [DateTime]::UtcNow.AddMinutes(20)
    while (!$proc.HasExited) {
        if ([DateTime]::UtcNow -gt $deadline) {
            $proc.Kill()
            $proc.WaitForExit()
            throw 'Reference GPU acceptance timed out'
        }
        Start-Sleep -Seconds 1
        $proc.Refresh()
    }
    $log = [string](Get-Content "$output/stdout.log" -Raw)
    $marker = [regex]::Match($log, '(?m)^LX_MATERIAL_RASTER_SURFACE_OK .* graphFrames=63 graphLists=105 graphFailures=6 sharedDepthFrames=72 sharedDepthPixels=[1-9][0-9]* .*\r?$')
    if ($proc.ExitCode -ne 0 -or !$marker.Success) {
        throw "Reference GPU acceptance failed: exitCode=$($proc.ExitCode); see $output"
    }
    foreach ($source in $hashes) {
        if ((Get-FileHash -LiteralPath $source.path -Algorithm SHA256).Hash -cne $source.sha256) {
            throw "Source changed during acceptance: $($source.path)"
        }
    }
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -cne $exeHash) {
        throw 'Executable changed during acceptance'
    }
    [ordered]@{configuration=$Configuration; passed=$true; exitCode=$proc.ExitCode; policies=@('DeclarationOrder', 'VersionedPreserve', 'VersionedDependency'); graphFrames=63; sharedDepthFrames=72; validationProblems=0; sourceCount=$hashes.Count; executable=$exe; executableSha256=$exeHash; productDefault='ExplicitVersioned/DependencyOrder'; rg5Complete=$false; marker=$marker.Value.Trim()} |
        ConvertTo-Json -Depth 5 | Set-Content "$output/result.json" -Encoding utf8
    "RG5_REFERENCE_CONFIGURATION_OK $Configuration"
} finally {
    $env:PATH = $previousPath
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    if ($proc) {
        $proc.Dispose()
    }
}

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
$sources = @(
    Get-ChildItem "$repo/Engine/RenderEngine", "$repo/Tools/regression", "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Recurse -File |
        Where-Object Extension -In '.cpp', '.h', '.inl', '.slang', '.vcxproj', '.ps1'
) | Sort-Object FullName -Unique
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
    $env:PATH = "$repo/vcpkg_installed/x64-windows/$dependency;$(Split-Path $exe);$previousPath"
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    $proc = Start-Process $exe -ArgumentList @($repo, '--rg5-mixed') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$output/stdout.log" -RedirectStandardError "$output/stderr.log"
    $deadline = [DateTime]::UtcNow.AddMinutes(10)
    while (!$proc.HasExited) {
        if ([DateTime]::UtcNow -gt $deadline) {
            $proc.Kill()
            $proc.WaitForExit()
            throw 'Mixed GPU acceptance timed out'
        }
        Start-Sleep -Seconds 1
        $proc.Refresh()
    }
    $log = Get-Content "$output/stdout.log" -Raw
    $marker = [regex]::Match($log, '(?m)^RG5_MIXED_GPU_OK policies=3 frames=72 components=6804 maxError=([0-9.eE+-]+) graphCompiles=0 validation=0\r?$')
    if ($proc.ExitCode -ne 0 -or !$marker.Success -or $log -notmatch '(?m)^RG5_MIXED_CACHE uploads=[1-9]\d* hits=[1-9]\d* transforms=[1-9]\d* transformHits=[1-9]\d*\r?$') {
        throw "Mixed GPU acceptance failed: exitCode=$($proc.ExitCode); see $output"
    }
    foreach ($source in $hashes) {
        if ((Get-FileHash -LiteralPath $source.path -Algorithm SHA256).Hash -cne $source.sha256) {
            throw "Source changed during acceptance: $($source.path)"
        }
    }
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -cne $exeHash) {
        throw 'Executable changed during acceptance'
    }
    [ordered]@{configuration=$Configuration; passed=$true; exitCode=$proc.ExitCode; policies=3; frames=72; components=6804; maxError=[double]::Parse($marker.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture); validationProblems=0; executable=$exe; executableSha256=$exeHash; productDefault='DeclarationOrder'; rg5Complete=$false} |
        ConvertTo-Json -Depth 5 | Set-Content "$output/result.json" -Encoding utf8
    "RG5_MIXED_CONFIGURATION_OK $Configuration"
} finally {
    $env:PATH = $previousPath
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    if ($proc) {
        $proc.Dispose()
    }
}
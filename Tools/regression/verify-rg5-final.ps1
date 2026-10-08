param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$FixtureProject = 'Build/Verification/RG5Final20261007/fixture/Project'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$fixture = [IO.Path]::GetFullPath($FixtureProject)
if (Test-Path -LiteralPath $output) { throw 'Use a new evidence directory' }
if (!(Test-Path -LiteralPath "$fixture/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang")) { throw 'Current shader fixture required' }
New-Item -ItemType Directory -Path $output | Out-Null
$sources = @(Get-ChildItem "$repo/Engine", "$repo/Editor", "$repo/Tools/regression", "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Recurse -File |
    Where-Object Extension -In '.cpp', '.h', '.inl', '.slang', '.vcxproj', '.ps1', '.props', '.targets') | Sort-Object FullName -Unique
$hashes = @($sources | ForEach-Object { [pscustomobject]@{path=$_.FullName; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} })
$hashes | ConvertTo-Json -Depth 4 | Set-Content "$output/source-hashes.json" -Encoding utf8
$exe = "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$runtime = "$(Split-Path $exe)/CreatorEditor.runtime.dll"
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$runtimeHash = (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash
$commands = @('dx12.rendergraph', 'dx12.fog', 'dx12.post', 'dx12.ui', 'dx12.grid', 'dx12.wireframe', 'dx12.gizmoicon', 'dx12.gizmoline', 'dx12.validation', 'quit')
[IO.File]::WriteAllText("$output/commands.txt", ($commands -join "`n") + "`n")
$previousPath = $env:PATH
$previousValidation = $env:CREATOR_DX12_VALIDATION
$proc = $null
try {
    $dependency = if ($Configuration -eq 'Debug') {'debug/bin'} else {'bin'}
    $env:PATH = "$repo/vcpkg_installed/x64-windows/x64-windows/$dependency;$repo/vcpkg_installed/x64-windows/$dependency;$(Split-Path $exe);$previousPath"
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    $proc = Start-Process $exe -ArgumentList @('--development-project', $fixture, '--commandlet-script', "$output/commands.txt", '--result-file', "$output/results.jsonl") -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$output/stdout.log" -RedirectStandardError "$output/stderr.log"
    $deadline = [DateTime]::UtcNow.AddMinutes(10)
    while (!$proc.HasExited) {
        if ([DateTime]::UtcNow -gt $deadline) { $proc.Kill(); $proc.WaitForExit(); throw 'Final acceptance timed out' }
        Start-Sleep -Seconds 1
        $proc.Refresh()
    }
    [ordered]@{exitCode=$proc.ExitCode; configuration=$Configuration; executableSha256=$exeHash; runtimeSha256=$runtimeHash} | ConvertTo-Json | Set-Content "$output/process.json" -Encoding utf8
    $results = @(Get-Content "$output/results.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
    if ($proc.ExitCode -ne 0 -or $results.Count -ne $commands.Count -or @($results | Where-Object status -Ne 'succeeded').Count) { throw "Final commandlet acceptance failed; see $output" }
    $log = [string]$results[0].data.log
    if ($log -notmatch 'RG5_FINAL_GPU_OK policies=3 frames=129 stages=9 maxError=0' -or $log -notmatch 'BASE0_GRAPH_FIXTURES_OK') { throw 'Final graph acceptance marker missing' }
    foreach ($source in $hashes) {
        if ((Get-FileHash -LiteralPath $source.path -Algorithm SHA256).Hash -cne $source.sha256) { throw "Source changed: $($source.path)" }
    }
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -cne $exeHash -or (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -cne $runtimeHash) { throw 'Runtime binary changed during acceptance' }
    [ordered]@{configuration=$Configuration; passed=$true; exitCode=$proc.ExitCode; policies=3; frames=129; stages=9; policyMaxError=0; sourceCount=$hashes.Count; executableSha256=$exeHash; runtimeSha256=$runtimeHash; commands=$commands; fixture=$fixture; rg5Complete=$false} | ConvertTo-Json -Depth 5 | Set-Content "$output/result.json" -Encoding utf8
    "RG5_FINAL_CONFIGURATION_OK $Configuration"
} finally {
    $env:PATH = $previousPath
    $env:CREATOR_DX12_VALIDATION = $previousValidation
    if ($proc) { $proc.Dispose() }
}

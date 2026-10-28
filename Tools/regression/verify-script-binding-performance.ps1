[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo ('Build/Verification/ScriptBindingPerformance-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& "$PSScriptRoot/verify-script-native-table.ps1" -Configuration $Configuration *> "$output/provider.log"
if ($LASTEXITCODE) { throw "Native provider regression failed: $output/provider.log" }
& dotnet build "$PSScriptRoot/ScriptBindingPerformance/ScriptBindingPerformance.csproj" -c $Configuration --nologo -v:q *> "$output/build.log"
if ($LASTEXITCODE) { throw "Binding comparison build failed: $output/build.log" }
$bin = Join-Path $PSScriptRoot "ScriptBindingPerformance/bin/$Configuration/net10.0"
Copy-Item -LiteralPath "$PSScriptRoot/ScriptNativeTableProbe/bin/$Configuration/net10.0/ScriptNativeProvider.dll" -Destination $bin -Force
& dotnet "$bin/CreatorEngine.FixedUpdate.Tests.dll" *> "$output/results.csv"
if ($LASTEXITCODE) { throw "Binding comparison failed: $output/results.csv" }
Write-Output "SCRIPT_BINDING_PERFORMANCE_OK configuration=$Configuration evidence=$output"

#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a fresh artifact directory.' }
New-Item -ItemType Directory -Path $output | Out-Null
$paths = @('Engine/Effect/EffectSimulation.h','Engine/Effect/EffectSimulation.cpp','Engine/Effect/Shaders/EffectSimulation.slang',
    'Engine/Effect/EffectModuleIR.h','Engine/Effect/EffectModuleIR.cpp','Engine/Effect/EffectDefinition.h','Engine/Effect/EffectDefinition.cpp',
    'Engine/Effect/Effect.vcxproj','Tools/regression/EffectSimulationGpuProbe.cpp','Tools/regression/EffectSimulationGpuProbe.vcxproj',
    'Tools/regression/verify-effect-simulation-gpu.ps1')
$sources = @($paths | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath "$repo/$_").Hash} })
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
& "$vs/MSBuild/Current/Bin/amd64/MSBuild.exe" "$PSScriptRoot/EffectSimulationGpuProbe.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:EngineEffectPackage=true /m:2 /v:minimal /nologo *> "$output/build.log"
if ($LASTEXITCODE) { throw "GPU probe build failed: $output/build.log" }
$exe = "$repo/Bin/x64-Debug/Tools/EffectSimulationGpuProbe/EffectSimulationGpuProbe.exe"
$runs = @()
foreach ($negative in @($false,$true)) {
    $name = if ($negative) {'negative'} else {'positive'}
    $si = [Diagnostics.ProcessStartInfo]::new()
    $si.FileName = $exe; $si.WorkingDirectory = $repo
    $si.UseShellExecute = $false; $si.CreateNoWindow = $true; $si.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $si.RedirectStandardOutput = $true; $si.RedirectStandardError = $true
    $si.Environment['CREATOR_DX12_VALIDATION'] = 'gpu'
    $si.Environment['PATH'] = "$repo/vcpkg_installed/x64-windows/x64-windows/debug/bin;$repo/Bin/x64-Debug/Runtime/Common;" + $env:PATH
    foreach ($argument in @("$repo/Engine/Effect/Shaders/EffectSimulation.slang","$output/$name-cache")) { $si.ArgumentList.Add($argument) }
    if ($negative) { $si.ArgumentList.Add('--bad-oracle') }
    $process = [Diagnostics.Process]::Start($si)
    $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if (!$process.WaitForExit(240000)) { $process.Kill($true); $process.WaitForExit(); throw "GPU $name timed out" }
        $text = $stdout.GetAwaiter().GetResult(); $errors = $stderr.GetAwaiter().GetResult()
        $text | Set-Content "$output/$name.stdout.txt" -Encoding utf8
        $errors | Set-Content "$output/$name.stderr.txt" -Encoding utf8
        if ($negative) {
            if ($process.ExitCode -ne 1 -or $errors -notmatch 'GPU/CPU mismatch') { throw 'Changed CPU oracle was not rejected.' }
        } else {
            if ($process.ExitCode -ne 0 -or $text -notmatch 'EFFECT_SIMULATION_GPU_OK cases=60 components=1082120 barriers=(\d+) validation=0' -or
                $text -notmatch 'GPUValidation=on') { throw "GPU numerical/validation gate failed: $output" }
        }
        $runs += @{name=$name;exitCode=$process.ExitCode}
        Write-Output "EFFECT_SIMULATION_GPU_RUN_OK $name"
    } finally { $process.Dispose() }
}
foreach ($source in $sources) { if ((Get-FileHash -LiteralPath "$repo/$($source.path)").Hash -ne $source.sha256) { throw 'Source drift during GPU gate.' } }
@{passed=$true;configuration='Debug';backend='DX12';cases=60;components=1082120;validation=0;runs=$runs;sources=$sources;
    executableSha256=(Get-FileHash -LiteralPath $exe).Hash;
    scope='Real RHI graph/compute/submission/completion/buffer readback, minimal fixed kernel; not product snapshot or general module GPU lowering';
    tolerance='abs(actual-reference) <= 2e-5 * max(1,abs(reference)); finite required'} |
    ConvertTo-Json -Depth 8 | Set-Content "$output/result.json" -Encoding utf8
"EFFECT_SIMULATION_GPU_MATRIX_OK $output"

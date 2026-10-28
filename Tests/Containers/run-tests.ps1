param([string]$CMake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot; . "$PSScriptRoot/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot
function Invoke-Checked([string[]]$Arguments){ & $CMake @Arguments; if($LASTEXITCODE -ne 0){throw "CMake failed: $Arguments"} }
Invoke-Checked @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build",'-G','Visual Studio 18 2026','-A','x64')
Invoke-Checked @('--build',"$root\build",'--config','Release')
1..3 | ForEach-Object { & "$root\build\Release\container_tests.exe" | Set-Content "$root\results\release-run$_.csv";if($LASTEXITCODE -ne 0){throw 'Benchmark failed'} }
Invoke-Checked @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-asan",'-G','Visual Studio 18 2026','-A','x64','-DCE_ASAN=ON')
Invoke-Checked @('--build',"$root\build-asan",'--config','Release')
& "$root\build-asan\Release\container_tests.exe" --test | Set-Content "$root\results\asan.txt"
if($LASTEXITCODE -ne 0){throw 'ASan failed'}
$receipt=[ordered]@{implementationRoot=$implementation;timestamp=(Get-Date -Format o);cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version);sources=@(Get-ChildItem "$implementation\include","$root\tests" -Recurse -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash);binary=(Get-FileHash "$root\build\Release\container_tests.exe" -Algorithm SHA256);note='Other applications remain active. Warm/reused-cache throughput; no cold-cache or cross-OS claim.'}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 "$root\results\environment.json"

param([string]$CMake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot; . "$PSScriptRoot/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot
$ctest=Join-Path (Split-Path $CMake) 'ctest.exe'
function Invoke-CMake([string[]]$Arguments){ & $CMake @Arguments;if($LASTEXITCODE -ne 0){throw "CMake failed: $Arguments"} }
Invoke-CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build",'-G','Visual Studio 18 2026','-A','x64')
Invoke-CMake @('--build',"$root\build",'--config','Release')
& $ctest --test-dir "$root\build" -C Release --output-on-failure | Set-Content "$root\results\concurrent-ctest.txt"
if($LASTEXITCODE -ne 0){throw 'CTest failed'}
1..3 | ForEach-Object { & "$root\build\Release\concurrent_tests.exe" | Set-Content "$root\results\concurrent-run$_.csv";if($LASTEXITCODE -ne 0){throw 'Concurrent benchmark failed'} }
Invoke-CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-asan",'-G','Visual Studio 18 2026','-A','x64','-DCE_ASAN=ON')
Invoke-CMake @('--build',"$root\build-asan",'--config','Release')
& "$root\build-asan\Release\concurrent_tests.exe" --test | Set-Content "$root\results\concurrent-asan.txt"
if($LASTEXITCODE -ne 0){throw 'Concurrent ASan failed'}
Invoke-CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-clang",'-G','Visual Studio 18 2026','-A','x64','-T','ClangCL')
Invoke-CMake @('--build',"$root\build-clang",'--config','Release')
& "$root\build-clang\Release\concurrent_tests.exe" --test | Set-Content "$root\results\concurrent-clang.txt"
if($LASTEXITCODE -ne 0){throw 'Concurrent ClangCL test failed'}
$receipt=[ordered]@{implementationRoot=$implementation;timestamp=(Get-Date -Format o);cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version);sources=@(Get-ChildItem "$implementation\include","$root\tests" -Recurse -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash);binary=(Get-FileHash "$root\build\Release\concurrent_tests.exe" -Algorithm SHA256);note='PPL baseline. Other applications active. No affinity/power changes. Throughput includes wake/join, excludes thread construction. No cross-OS or engine acceptance.'}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 "$root\results\concurrent-environment.json"

param([string]$CMake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot; . "$PSScriptRoot/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot
$ctest=Join-Path (Split-Path $CMake) 'ctest.exe'
function Invoke-CMake([string[]]$Arguments){ & $CMake @Arguments;if($LASTEXITCODE -ne 0){throw "CMake failed: $Arguments"} }
Invoke-CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build",'-G','Visual Studio 18 2026','-A','x64')
Invoke-CMake @('--build',"$root\build",'--config','Release')
& $ctest --test-dir "$root\build" -C Release --output-on-failure | Set-Content "$root\results\lock-free-ctest.txt"
if($LASTEXITCODE -ne 0){throw 'CTest failed'}
& $ctest --test-dir "$root\build" -C Release -R '^lock_free_correctness$' --repeat until-fail:10 --output-on-failure | Set-Content "$root\results\lock-free-repeat.txt"
if($LASTEXITCODE -ne 0){throw 'Repeated correctness failed'}
1..3 | ForEach-Object { & "$root\build\Release\lock_free_tests.exe" | Set-Content "$root\results\lock-free-run$_.csv";if($LASTEXITCODE -ne 0){throw 'Lock-free benchmark failed'} }
Invoke-CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-asan",'-G','Visual Studio 18 2026','-A','x64','-DCE_ASAN=ON')
Invoke-CMake @('--build',"$root\build-asan",'--config','Release')
& $ctest --test-dir "$root\build-asan" -C Release -R '^lock_free_correctness$' --repeat until-fail:5 --output-on-failure | Set-Content "$root\results\lock-free-asan.txt"
if($LASTEXITCODE -ne 0){throw 'Lock-free ASan failed'}
Invoke-CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-clang",'-G','Visual Studio 18 2026','-A','x64','-T','ClangCL')
Invoke-CMake @('--build',"$root\build-clang",'--config','Release')
& "$root\build-clang\Release\lock_free_tests.exe" --test | Set-Content "$root\results\lock-free-clang.txt"
if($LASTEXITCODE -ne 0){throw 'Lock-free ClangCL test failed'}
$receipt=[ordered]@{implementationRoot=$implementation;timestamp=(Get-Date -Format o);cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version);sources=@(Get-ChildItem "$implementation\include","$root\tests" -Recurse -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash);binary=(Get-FileHash "$root\build\Release\lock_free_tests.exe" -Algorithm SHA256);note='Allocating push + concurrent reclaim every 64 successful pops vs PPL. Other applications active. No affinity or power changes. Core publish/try_pop separated from allocation. No cross-OS or engine acceptance.'}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 "$root\results\lock-free-environment.json"

param([string]$CMake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
$ErrorActionPreference='Stop';$root=$PSScriptRoot; . "$PSScriptRoot/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot;$ctest=Join-Path (Split-Path $CMake) 'ctest.exe'
function CMake([string[]]$Arguments){& $CMake @Arguments;if($LASTEXITCODE -ne 0){throw "CMake failed: $Arguments"}}
function Run([string]$Program,[string]$Output,[string[]]$Arguments=@()){& $Program @Arguments | Set-Content -Encoding utf8 $Output;if($LASTEXITCODE -ne 0){throw "Run failed: $Program"}}
CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build",'-G','Visual Studio 18 2026','-A','x64')
CMake @('--build',"$root\build",'--config','Release')
Run $ctest "$root\results\optimization-ctest.txt" @('--test-dir',"$root\build",'-C','Release','--output-on-failure')
Run $ctest "$root\results\optimization-repeat.txt" @('--test-dir',"$root\build",'-C','Release','-R','^lock_free_correctness$','--repeat','until-fail:20','--output-on-failure')
foreach($shape in @(@('queue_profile_baseline','profile-before-large.csv'),@('queue_profile_baseline_small','profile-before-small.csv'),@('queue_profile_faa','profile-after-small.csv'),@('queue_profile_faa_large','profile-after-large.csv'))){Run "$root\build\Release\$($shape[0]).exe" "$root\results\$($shape[1])"}
foreach($run in 1..5){$order=if($run%2){@('baseline','final')}else{@('final','baseline')};foreach($variant in $order){Run "$root\build\Release\queue_acceptance_$variant.exe" "$root\results\acceptance-$variant-$run.csv"}}
CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-asan",'-G','Visual Studio 18 2026','-A','x64','-DCE_ASAN=ON')
CMake @('--build',"$root\build-asan",'--config','Release','--target','lock_free_tests')
Run $ctest "$root\results\optimization-asan.txt" @('--test-dir',"$root\build-asan",'-C','Release','-R','^lock_free_correctness$','--repeat','until-fail:10','--output-on-failure')
CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-clang",'-G','Visual Studio 18 2026','-A','x64','-T','ClangCL')
CMake @('--build',"$root\build-clang",'--config','Release','--target','lock_free_tests','lock_free_header_probe')
Run "$root\build-clang\Release\lock_free_tests.exe" "$root\results\optimization-clang.txt" @('--test')
Run "$root\build-clang\Release\lock_free_header_probe.exe" "$root\results\optimization-clang-header.txt"
$receipt=[ordered]@{implementationRoot=$implementation;timestamp=(Get-Date -Format o);cpu=(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);os=(Get-CimInstance Win32_OperatingSystem|Select-Object Caption,Version);sources=@(Get-ChildItem "$implementation\include","$root\tests","$root\baseline","$root\diagnostics" -Recurse -File|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash);buildFiles=@(Get-Item "$root\CMakeLists.txt","$root\run-queue-optimization.ps1","$root\run-linux-validation.ps1"|Get-FileHash|Select-Object Path,Hash);binaries=@(Get-Item "$root\build\Release\queue_acceptance_baseline.exe","$root\build\Release\queue_acceptance_final.exe","$root\build\Release\lock_free_tests.exe"|Get-FileHash|Select-Object Path,Hash);note='Five sequential process pairs, order alternates. Same /O2 /Ob3 /MD translation unit for queue and PPL. Default push/pop, allocation on cache/segment miss; maintenance every 64 pops. All payload words checked. Profiling is a separate binary. No affinity/power/process changes. No engine/cross-OS acceptance.'}
$receipt|ConvertTo-Json -Depth 6|Set-Content -Encoding utf8 "$root\results\optimization-environment.json"

param([string]$CMake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe', [switch]$SkipBenchmarks)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot; . "$PSScriptRoot/implementation-root.ps1"; $implementation=Get-ContainerImplementationRoot
$results=Join-Path $root 'results'
function Invoke-Logged([string]$Log,[string]$Executable,[string[]]$Arguments) {
  & $Executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $results $Log) -Append
  if($LASTEXITCODE -ne 0){throw "Failed: $Executable $Arguments"}
}
foreach($log in @('library-release.txt','library-asan.txt','library-clang.txt','library-linux.txt','library-linux-sanitizers.txt')) { Set-Content (Join-Path $results $log) '' }
Invoke-Logged 'library-release.txt' $CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build",'-G','Visual Studio 18 2026','-A','x64')
Invoke-Logged 'library-release.txt' $CMake @('--build',"$root\build",'--config','Release')
$ctest=Join-Path (Split-Path $CMake) 'ctest.exe'
Invoke-Logged 'library-release.txt' $ctest @('--test-dir',"$root\build",'-C','Release','--output-on-failure')
Invoke-Logged 'library-asan.txt' $CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-asan",'-DCE_ASAN=ON')
Invoke-Logged 'library-asan.txt' $CMake @('--build',"$root\build-asan",'--config','Release','--target','library_tests','container_tests','library_header_probes')
Invoke-Logged 'library-asan.txt' $ctest @('--test-dir',"$root\build-asan",'-C','Release','-R','^(library_correctness|correctness)$','--output-on-failure')
Invoke-Logged 'library-clang.txt' $CMake @('-S',$root,"-DCE_IMPLEMENTATION_ROOT=$implementation",'-B',"$root\build-clang",'-G','Visual Studio 18 2026','-A','x64','-T','ClangCL')
Invoke-Logged 'library-clang.txt' $CMake @('--build',"$root\build-clang",'--config','Release','--target','library_tests','container_tests','library_header_probes')
Invoke-Logged 'library-clang.txt' $ctest @('--test-dir',"$root\build-clang",'-C','Release','-R','^(library_correctness|correctness)$','--output-on-failure')
$unixRoot=(& wsl.exe -d Ubuntu -- wslpath -a ($root.Replace('\','/')) | Out-String).Trim()
if($LASTEXITCODE -ne 0){throw 'WSL mapping failed'}
Invoke-Logged 'library-linux.txt' 'wsl.exe' @('-d','Ubuntu','--','cmake','-S',$unixRoot,'-B','/tmp/creatorcontainers-library-20261025','-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_BUILD_TYPE=Release')
Invoke-Logged 'library-linux.txt' 'wsl.exe' @('-d','Ubuntu','--','cmake','--build','/tmp/creatorcontainers-library-20261025','--target','library_tests','container_tests','library_header_probes','-j2')
Invoke-Logged 'library-linux.txt' 'wsl.exe' @('-d','Ubuntu','--','ctest','--test-dir','/tmp/creatorcontainers-library-20261025','-R','^library_correctness$','--output-on-failure')
Invoke-Logged 'library-linux.txt' 'wsl.exe' @('-d','Ubuntu','--','ctest','--test-dir','/tmp/creatorcontainers-library-20261025','-R','^correctness$','--output-on-failure')
Invoke-Logged 'library-linux-sanitizers.txt' 'wsl.exe' @('-d','Ubuntu','--','cmake','-S',$unixRoot,'-B','/tmp/creatorcontainers-library-sanitizers-20261025','-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
Invoke-Logged 'library-linux-sanitizers.txt' 'wsl.exe' @('-d','Ubuntu','--','cmake','--build','/tmp/creatorcontainers-library-sanitizers-20261025','--target','library_tests','container_tests','-j2')
Invoke-Logged 'library-linux-sanitizers.txt' 'wsl.exe' @('-d','Ubuntu','--','env','UBSAN_OPTIONS=halt_on_error=1','ASAN_OPTIONS=detect_leaks=1:halt_on_error=1','ctest','--test-dir','/tmp/creatorcontainers-library-sanitizers-20261025','-R','^library_correctness$','--output-on-failure')
Invoke-Logged 'library-linux-sanitizers.txt' 'wsl.exe' @('-d','Ubuntu','--','env','UBSAN_OPTIONS=halt_on_error=1','ASAN_OPTIONS=detect_leaks=1:halt_on_error=1','ctest','--test-dir','/tmp/creatorcontainers-library-sanitizers-20261025','-R','^correctness$','--output-on-failure')
if(!$SkipBenchmarks) {
  # No validation builds or other benchmark processes run concurrently here.
  foreach($run in 1..3) {
    & "$root\build\Release\container_tests.exe" | Set-Content "$results\library-core-final-$run.csv"
    if($LASTEXITCODE -ne 0){throw 'Core measurement failed'}
    & "$root\build\Release\library_benchmark.exe" | Set-Content "$results\library-expanded-final-$run.csv"
    if($LASTEXITCODE -ne 0){throw 'Expanded measurement failed'}
  }
}
$receipt=[ordered]@{implementationRoot=$implementation;
  timestamp=(Get-Date -Format o)
  cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
  os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version)
  sources=@(Get-ChildItem "$implementation\include","$root\tests" -Recurse -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash)
  buildFiles=@(Get-FileHash "$root\CMakeLists.txt","$root\run-library-validation.ps1" -Algorithm SHA256 | Select-Object Path,Hash)
  binaries=@(Get-FileHash "$root\build\Release\container_tests.exe","$root\build\Release\library_tests.exe","$root\build\Release\library_benchmark.exe" -Algorithm SHA256 | Select-Object Path,Hash)
  note='MSVC Release, warm caches, alternating A/B, 15 samples, three processes. Other user apps remain active. String copy now consumes all bytes through a noinline function; old copy measurements are not directly comparable. Fixed ring/slot/LRU/sequence/arena comparisons have different memory and API contracts. No engine or cross-OS performance claim.'
}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 "$results\library-environment.json"

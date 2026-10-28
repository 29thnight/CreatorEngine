param([string]$Distribution='Ubuntu')
$ErrorActionPreference='Stop';$root=$PSScriptRoot
$unixRoot=(& wsl.exe -d $Distribution -- wslpath -a ($root.Replace('\','/')) | Out-String).Trim()
if($LASTEXITCODE -ne 0){throw 'WSL workspace mapping failed'}
function Linux([string]$Log,[string[]]$Arguments){& wsl.exe -d $Distribution -- @Arguments | Tee-Object -FilePath $Log -Append;if($LASTEXITCODE -ne 0){throw "Linux validation failed: $Arguments"}}
$release="$root\results\optimization-linux.txt";$tsan="$root\results\optimization-tsan.txt"
Set-Content -Encoding utf8 $release '';Set-Content -Encoding utf8 $tsan ''
Linux $release @('clang++','--version')
Linux $release @('cmake','-S',$unixRoot,'-B','/tmp/creatorcontainers-20261025-release','-DCMAKE_CXX_COMPILER=/usr/bin/clang++','-DCMAKE_BUILD_TYPE=Release')
Linux $release @('cmake','--build','/tmp/creatorcontainers-20261025-release','-j2','--target','lock_free_tests','lock_free_header_probe')
Linux $release @('ctest','--test-dir','/tmp/creatorcontainers-20261025-release','-R','^lock_free_','--output-on-failure')
Linux $tsan @('cmake','-S',$unixRoot,'-B','/tmp/creatorcontainers-20261025-tsan','-DCMAKE_CXX_COMPILER=/usr/bin/clang++','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_CXX_FLAGS=-fsanitize=thread -g -O1','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread')
Linux $tsan @('cmake','--build','/tmp/creatorcontainers-20261025-tsan','-j2','--target','lock_free_tests')
Linux $tsan @('env','TSAN_OPTIONS=halt_on_error=1','ctest','--test-dir','/tmp/creatorcontainers-20261025-tsan','-R','^lock_free_correctness$','--repeat','until-fail:5','--output-on-failure')

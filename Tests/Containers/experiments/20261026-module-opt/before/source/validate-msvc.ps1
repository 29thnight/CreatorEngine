param([Parameter(Mandatory)][string]$Tag,[switch]$AddressSanitizer)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$log=Join-Path $repo "results/$Tag.txt"
if(Test-Path $log){throw 'Use a fresh validation tag'}
$cmake=Get-Command cmake -ErrorAction SilentlyContinue
$exe=if($cmake){$cmake.Source}else{'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
$ctest=Join-Path (Split-Path $exe) 'ctest.exe'
$build=Join-Path $repo $(if($AddressSanitizer){'build-modules-asan'}else{'build-modules'})
function Run([string]$Program,[string[]]$Arguments){
 & $Program @Arguments 2>&1 | Tee-Object $log -Append
 if($LASTEXITCODE -ne 0){throw "Validation failed: $Program"}
}
& "$PSScriptRoot/sync-headers.ps1" -Check
Run $exe @('-S',$repo,'-B',$build,'-DCE_MODULES=ON',"-DCE_ASAN=$(if($AddressSanitizer){'ON'}else{'OFF'})")
Run $exe @('--build',$build,'--config','Release','--target','module_import_probe','module_container_tests','module_library_tests','module_bounded_mpsc_tests','module_concurrent_tests','module_lock_free_tests','module_alignment_tests','module_multi_tu','module_consumer_test')
Run $ctest @('--test-dir',$build,'-C','Release','-R','^module_','--output-on-failure')
Get-ChildItem "$repo/modules","$repo/include","$repo/tests/modules" -Recurse -File | Get-FileHash -Algorithm SHA256 | ConvertTo-Json | Set-Content "$repo/results/$Tag-hashes.json" -Encoding utf8

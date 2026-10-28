param([Parameter(Mandatory)][string]$Tag,[switch]$AddressSanitizer)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. "$repo/implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
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
$installation = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'MSVC installation not found' }
Run $exe @('-S',$repo,'-B',$build,'-G','Visual Studio 18 2026','-A','x64','-T','version=14.51.36231',"-DCMAKE_GENERATOR_INSTANCE=$installation","-DCE_ASAN=$(if($AddressSanitizer){'ON'}else{'OFF'})")
Run $exe @('--build',$build,'--config','Release','--target','module_utf8_copy_tests','module_unicode_tests','module_unicode_imgui_tests','module_import_probe','module_fixed_bitset_tests','module_node_map_tests','module_node_map_advanced_tests','module_array_migration_tests','module_container_tests','module_library_tests','module_bounded_mpsc_tests','module_concurrent_tests','module_lock_free_tests','module_alignment_tests','module_multi_tu','module_consumer_test')
Run $ctest @('--test-dir',$build,'-C','Release','-R','^module_','--output-on-failure')
Get-ChildItem "$implementation/modules","$implementation/include","$repo/tests/modules" -Recurse -File | Get-FileHash -Algorithm SHA256 | ConvertTo-Json | Set-Content "$repo/results/$Tag-hashes.json" -Encoding utf8

param([Parameter(Mandatory)][string]$Tag)
$unicodeTag=$Tag
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$exe=Join-Path $root 'build-node-opt/modules/Release/module_unicode_benchmark.exe'
$engine=[IO.Path]::GetFullPath((Join-Path $root '../CreatorEngine'))
if(Test-Path "$root/results/$unicodeTag-verdict.json"){throw 'Use a fresh tag'}
if(Get-Process cl,link -ErrorAction SilentlyContinue){throw 'Finish builds before measuring'}
$source=@(Get-ChildItem "$engine/Engine/Containers/modules","$engine/Engine/Containers/include","$engine/Engine/Containers/detail" -Recurse -File -ErrorAction SilentlyContinue)
$source+=Get-Item "$engine/Editor/ImGuiHelper/Utf16Text.h","$root/tests/unicode.cpp","$root/tests/unicode_imgui.cpp","$root/tests/unicode_benchmark.cpp","$root/tests/utf8_copy_correctness.cpp",$exe,$PSCommandPath
$snapshot=Join-Path $root "results/$unicodeTag-snapshot";New-Item -ItemType Directory $snapshot|Out-Null
$hashes=@($source|Sort-Object FullName -Unique|ForEach-Object {
 $relative=if($_.FullName.StartsWith($engine)){ 'engine/'+[IO.Path]::GetRelativePath($engine,$_.FullName)}else{'harness/'+[IO.Path]::GetRelativePath($root,$_.FullName)}
 $destination=Join-Path $snapshot $relative;New-Item -ItemType Directory (Split-Path $destination) -Force|Out-Null
 Copy-Item -LiteralPath $_.FullName -Destination $destination
 [ordered]@{path=$_.FullName;relative=$relative;sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}
})
$hashes|ConvertTo-Json -Depth 5|Set-Content "$root/results/$unicodeTag-inputs.json" -Encoding utf8
function Check-Inputs {foreach($entry in $hashes){if((Get-FileHash $entry.path -Algorithm SHA256).Hash -ne $entry.sha256){throw "Frozen input changed: $($entry.path)"}}}
& "$root/experiments/20261026-small-array/with-guard.ps1" -Tag "$unicodeTag-guard" -Work {
 for($run=1;$run -le 3;++$run){
  Check-Inputs
  $args=if($run -eq 2){@('--reverse')}else{@()}
  $process=Start-Process -FilePath $exe -ArgumentList $(if($args.Count){$args}else{@('--normal')}) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/results/$unicodeTag-$run.csv" -RedirectStandardError "$root/results/$unicodeTag-$run.stderr"
  if(!$process.WaitForExit(300000)){ $process.Kill();throw 'Benchmark timeout'}
  $process.WaitForExit();if($process.ExitCode -ne 0){throw "Benchmark exit=$($process.ExitCode)"}
  Check-Inputs
 }
}
$rows=@();$runs=@()
for($run=1;$run -le 3;++$run){
 $raw=@(Import-Csv "$root/results/$unicodeTag-$run.csv")
 foreach($group in $raw|Group-Object case){
  if($group.Count -ne 15){throw 'Expected all 15 raw samples'}
  $a=@($group.Group.ce_ns|ForEach-Object {[double]$_}|Sort-Object);$b=@($group.Group.stl_ns|ForEach-Object {[double]$_}|Sort-Object)
  $ratio=$a[7]/$b[7];$as=$a[13]/$a[1];$bs=$b[13]/$b[1]
  $rows+=[ordered]@{run=$run;case=$group.Name;custom_ns=$a[7];stl_ns=$b[7];ratio=$ratio;customSpread=$as;stlSpread=$bs;pass=($ratio -le 1.10 -and $as -le 1.30 -and $bs -le 1.30)}
 }
}
Check-Inputs
$pass=@($rows|Where-Object pass).Count -eq 57 -and $rows.Count -eq 57
[ordered]@{pass=$pass;passed=@($rows|Where-Object pass).Count;total=$rows.Count;ratioLimit=1.10;spreadLimit=1.30;samples=15;batchMilliseconds=100;warmupSeconds=1;cpu=4;smtHelperCpu=5;inputCount=$hashes.Count;frozenInputsVerified=$true;rows=$rows}|ConvertTo-Json -Depth 8|Set-Content "$root/results/$unicodeTag-verdict.json" -Encoding utf8
$rows|Where-Object {!$_.pass}|Format-Table
"UNICODE_PERFORMANCE: $(@($rows|Where-Object pass).Count)/$($rows.Count)"
if(!$pass){exit 1}

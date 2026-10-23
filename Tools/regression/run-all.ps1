# Explicit registered suite only. This does not revive the retired engine-wide
# unbounded run-all. Every registered NetworkHandoff gate is mandatory.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('NetworkHandoff')][string]$Suite,
    [ValidateSet('Debug','Release','All')][string]$Configuration='All',
    [switch]$Product,
    [string]$EditorExe='',
    [string]$CookedStage=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$manifest=Join-Path $PSScriptRoot 'network-handoff.suite.json'
$out=Join-Path $repo ('Build/Verification/NDA/Suite-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
if($Product -and (!$EditorExe -or !(Test-Path -LiteralPath $EditorExe))){throw '-Product requires an explicit existing -EditorExe'}
& python "$PSScriptRoot/network_handoff_suite.py" --root $repo --manifest $manifest *> "$out/registration.log"
if($LASTEXITCODE){Get-Content "$out/registration.log";throw 'Mandatory network regression wiring invalid'}
if($Product -and (!$CookedStage -or !(Test-Path -LiteralPath (Join-Path $CookedStage 'Player.runtime.dll')))){throw '-Product now requires -CookedStage for the N1 parser-free Player gate'}
$data=Get-Content $manifest -Raw|ConvertFrom-Json
$configs=if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)}
$shell=(Get-Process -Id $PID).Path
$passed=@();$failures=@()
foreach($gate in $data.gates) {
    $runs=if($gate.configuration -eq 'matrix'){$configs}elseif($gate.configuration -eq 'all'){@($Configuration)}else{@('None')}
    foreach($config in $runs) {
        $id=$gate.id+'-'+$config
        $arguments=@('-NoProfile','-File',(Join-Path $repo $gate.script))
        if($config -ne 'None'){$arguments+=@('-Configuration',$config)}
        & $shell @arguments *> "$out/$id.log"
        if($LASTEXITCODE){$failures+=@{gate=$id;exitCode=$LASTEXITCODE};Get-Content "$out/$id.log" -Tail 12}
        else{$passed+=$id;"NETWORK_HANDOFF_GATE_OK $id"}
    }
}
if($Product -and !$failures.Count) {
    & $shell -NoProfile -File "$PSScriptRoot/verify-simulation-exchange-http.ps1" -Configuration $configs[-1] -EditorExe $EditorExe *> "$out/Product.log"
    if($LASTEXITCODE){$failures+=@{gate='N3Product';exitCode=$LASTEXITCODE}}else{$passed+='N3Product'}
}
if($Product -and !$failures.Count) {
    & $shell -NoProfile -File "$PSScriptRoot/verify-scene-gc.ps1" -Configuration $configs[-1] -Iterations 2 -EditorExe $EditorExe *> "$out/W4NDBProduct.log"
    if($LASTEXITCODE){$failures+=@{gate='W4NDBProduct';exitCode=$LASTEXITCODE};Get-Content "$out/W4NDBProduct.log" -Tail 10}else{$passed+='W4NDBProduct'}
}
if($Product -and !$failures.Count) {
    & $shell -NoProfile -File "$PSScriptRoot/verify-archive-parser-product.ps1" -Stage $CookedStage -Configuration $configs[-1] *> "$out/N1Product.log"
    if($LASTEXITCODE){$failures+=@{gate='N1Product';exitCode=$LASTEXITCODE};Get-Content "$out/N1Product.log" -Tail 12}else{$passed+='N1Product'}
}
if($Product -and !$failures.Count) {
    & $shell -NoProfile -File "$PSScriptRoot/verify-n1-diagnostic-product.ps1" -Configuration $configs[-1] *> "$out/N1DiagnosticProduct.log"
    if($LASTEXITCODE){$failures+=@{gate='N1DiagnosticProduct';exitCode=$LASTEXITCODE};Get-Content "$out/N1DiagnosticProduct.log" -Tail 12}else{$passed+='N1DiagnosticProduct'}
}
if($Product -and !$failures.Count) {
    & $shell -NoProfile -File "$PSScriptRoot/verify-n3-pacing-product.ps1" -Configuration $configs[-1] *> "$out/N3PacingProduct.log"
    if($LASTEXITCODE){$failures+=@{gate='N3PacingProduct';exitCode=$LASTEXITCODE};Get-Content "$out/N3PacingProduct.log" -Tail 12}else{$passed+='N3PacingProduct'}
}
if($Product -and !$failures.Count) {
    & $shell -NoProfile -File "$PSScriptRoot/verify-n3-pacing-product.ps1" -Configuration $configs[-1] -Rates 144 -MovingQuality *> "$out/N3MovingGpuProduct.log"
    if($LASTEXITCODE){$failures+=@{gate='N3MovingGpuProduct';exitCode=$LASTEXITCODE};Get-Content "$out/N3MovingGpuProduct.log" -Tail 12}else{$passed+='N3MovingGpuProduct'}
}
$expected=0
foreach($gate in $data.gates){$expected+=if($gate.configuration -eq 'matrix'){$configs.Count}else{1}}
if($Product){$expected+=6}
if($passed.Count -ne $expected -and !$failures.Count){$failures+=@{gate='RequiredGateCoverage';exitCode=1}}
@{result=if($failures.Count){'NETWORK_HANDOFF_REGRESSIONS_FAILED'}else{'NETWORK_HANDOFF_REGRESSIONS_OK'};suite=$Suite;configuration=$configs;passed=$passed;failures=$failures;manifestHash=(Get-FileHash $manifest).Hash;scope='All registered NetworkHandoff gates; not all engine/GPU/device/soak tests';product=[bool]$Product}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
if($failures.Count){throw "Network handoff regressions failed: $out"}
"NETWORK_HANDOFF_REGRESSIONS_OK gates=$($passed.Count) evidence=$out"

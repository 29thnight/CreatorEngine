#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$DebugResult,
    [Parameter(Mandatory)][string]$ReleaseResult,
    [Parameter(Mandatory)][string]$Comparison,
    [Parameter(Mandatory)][string]$OutputFile
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$output=[IO.Path]::GetFullPath($OutputFile)
if(Test-Path -LiteralPath $output){throw 'Use a new completion file; historical evidence is preserved'}
$results=@()
foreach($pair in @(@('Debug',$DebugResult),@('Release',$ReleaseResult))){
    $path=[IO.Path]::GetFullPath($pair[1])
    $result=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
    if(!$result.PSObject.Properties['completionContract']){throw 'Historical result lacks the current completion contract; capture a fresh baseline'}
    if($result.configuration -ne $pair[0] -or $result.completionContract -ne 'dx12-static-baseline-v2' -or
        $result.completionScope -ne 'single-configuration-baseline' -or
        !$result.complete -or !$result.staticFixtureGatesComplete -or !$result.artifactGatesPassed -or
        $result.failures.Count -ne 0 -or $result.sourceChanges -ne 0){throw "Current $($pair[0]) baseline gates are incomplete"}
    $runs=@($result.runs|Where-Object backend -eq 'dx12')
    if($runs.Count -ne 2 -or @($runs|Where-Object {!$_.passed -or $_.exitCode -ne 0 -or $_.forcedTermination}).Count){
        throw 'Two independent clean DX12 processes are required per configuration'
    }
    $runtime=Join-Path (Split-Path $result.executable) 'CreatorEditor.runtime.dll'
    if((Get-FileHash -LiteralPath $result.executable -Algorithm SHA256).Hash -ne $result.executableSha256 -or
        (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne $result.runtimeSha256){throw 'Baseline binary differs from the current binary'}
    $hashes=Get-Content -LiteralPath (Join-Path (Split-Path $path) 'source-hashes.json') -Raw|ConvertFrom-Json
    foreach($source in $hashes){
        if(!(Test-Path -LiteralPath $source.path) -or (Get-FileHash -LiteralPath $source.path -Algorithm SHA256).Hash -ne $source.sha256){
            throw 'Baseline sources differ from current sources'
        }
    }
    $results+= $result
}
$compare=Get-Content -LiteralPath $Comparison -Raw|ConvertFrom-Json
if(!$compare.passed -or !$compare.sameBackend -or !$compare.gpuComplete -or $compare.attachments.Count -lt 7 -or
    [IO.Path]::GetFullPath($compare.left) -ne [IO.Path]::GetFullPath($results[0].runs[0].captures[0]) -or
    [IO.Path]::GetFullPath($compare.right) -ne [IO.Path]::GetFullPath($results[1].runs[0].captures[0]) -or
    @($compare.attachments|Where-Object {$_.exceededPixels -ne 0}).Count){throw 'Matching Debug/Release capture comparison is required'}
$report=[ordered]@{
    schemaVersion=1; completionContract='dx12-static-baseline-v2'; phaseComplete=$true
    debugResult=[IO.Path]::GetFullPath($DebugResult); releaseResult=[IO.Path]::GetFullPath($ReleaseResult)
    comparison=[IO.Path]::GetFullPath($Comparison); imageCount=$compare.attachments.Count
    excluded=@('generic packet/material/animation replay','MAT-9 quality/performance acceptance','PHASE 4.9 Vulkan comparison')
}
$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $output -Encoding utf8
Write-Output "BASE0_PHASE_COMPLETE $output"

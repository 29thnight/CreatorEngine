#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$previousCases=$env:CREATOR_METEOR_TRANSFORM_CASES
$previousAudit=$env:CREATOR_METEOR_TRANSFORM_AUDIT
try {
    $env:CREATOR_METEOR_TRANSFORM_CASES='1'
    $env:CREATOR_METEOR_TRANSFORM_AUDIT='1'
    & "$PSScriptRoot/verify-meteor-scene-cutover.ps1" -Configuration $Configuration -FixtureProject $FixtureProject -OutputDirectory $OutputDirectory -DeferRetention
    $out=[IO.Path]::GetFullPath($OutputDirectory)
    $verdict=Get-Content "$out/verdict.json" -Raw|ConvertFrom-Json
    $graph=@($verdict.tests|Where-Object command -eq 'dx12.rendergraph')
    $lines=@($graph[0].data.log -split "`n"|Where-Object {$_ -match '^METEOR_TRANSFORM_GPU_OK '})
    $expected=@('1:33','16:528','64:2112','256:8448','512:16896','512:2096640')
    $actual=@($lines|ForEach-Object {
        if($_ -notmatch '^METEOR_TRANSFORM_GPU_OK jobs=(\d+) points=(\d+) transformPasses=1 readyPasses=1 nativeDispatches=1 mixedStaticSkinned=(\d) oracle=independent$'){throw 'Malformed METEOR transform GPU result.'}
        if([int]$Matches[3] -ne [int]([int]$Matches[1] -gt 1)){throw 'Static/skinned coverage was mislabeled.'}
        "$($Matches[1]):$($Matches[2])"
    })
    if(($actual -join ',') -ne ($expected -join ',')){throw 'Required METEOR transform cases were skipped.'}
    $native=@(Get-Content "$out/stderr.log"|Where-Object {$_ -match '^METEOR_TRANSFORM_DISPATCH .*jobs=512 points=2096640 groups=65536 x=32768 y=2 native=1 '})
    if($native.Count -ne 1){throw 'Two-dimensional native dispatch boundary was not executed exactly once.'}
    $residency=@($graph[0].data.log -split "`n"|Where-Object {$_ -match '^LX_RESIDENCY_GPU_OK '})
    foreach($round in 0..3){
        $dispatch=[int]($round -eq 0 -or $round -eq 3)
        $expectedLine="LX_RESIDENCY_GPU_OK round=$round jobs=512 points=525312 transformPasses=$dispatch readyPasses=$dispatch nativeDispatches=$dispatch oracle=independent"
        if(@($residency|Where-Object {$_ -eq $expectedLine}).Count -ne 1){throw "Missing METEOR active residency oracle round=$round"}
    }
    if($residency.Count -ne 4 -or $graph[0].data.log -notmatch 'LX_RESIDENCY_RETIRE_OK activeOver32MiB=1 uniqueInputs=512 pinnedPreserved=1 idleOutputs=0'){
        throw 'METEOR active output retirement was not proven.'
    }
    if($graph[0].data.log -notmatch 'LX_RESIDENCY_PENDING_OK recordings=5 pendingOwnersPreserved=4 completedNativeDispatches=0'){
        throw 'METEOR active pending output lifetime was not proven.'
    }
    @{passed=$true;configuration=$Configuration;cases=$actual;twoDimensionalBoundary=$true;nativeDispatchesPerBatch=1;
      activeResidencyOver32MiB=$true;uniqueStaticInputs=512;warmNativeDispatches=0;changedInputNativeDispatches=1;
      pinnedOwnerPreserved=$true;idleOutputsAfterRetirement=0;
      pendingOwnersPreserved=4;idleInputBudgetEnforced=$true;
      scalarPositionTolerance=1e-4;frameBasisTolerance=1e-6;gpuValidationProblems=0;normalQuit=$verdict.normalQuit}|
        ConvertTo-Json -Depth 5|Set-Content "$out/transform-verdict.json"
    . "$PSScriptRoot/ArtifactRetention.ps1"
    Complete-ArtifactRetention $out "lx-transform-dispatch-$Configuration"
    "METEOR_TRANSFORM_DISPATCH_OK $Configuration $out"
}finally{
    $env:CREATOR_METEOR_TRANSFORM_CASES=$previousCases
    $env:CREATOR_METEOR_TRANSFORM_AUDIT=$previousAudit
}

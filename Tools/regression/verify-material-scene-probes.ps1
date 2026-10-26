#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][ValidateSet('--shadow-decal-only','--subsurface-only','--refraction-only','--refraction-film-only','--volume-only',
        '--forward-blend','--rg5-mixed','--rg5-decal','--rg5-reference','--scene-only','--lx-meshlet-only',
        '--temporal-transmission-only','--shadow-correctness-only','--skinned-cluster-lod-only','--cluster-lod-only','--object-space-only','--object-space-dx12-only','--geometry-preparation-scale-only','--atomic-reduction-only','--hybrid-visibility-only','--visibility-reconstruction-only','--hw-visibility-only','--cluster-streaming-only','--cluster-storage-only','--quantized-geometry-only','--geometry-access-only','--decal-only','--forward-transport','--meshlet-dispatch-only','--meshlet-hzb-only','--material-bucket-only','--temporal-bucket-only','--layer-bucket-only','--primitive-cache-only')][string[]]$Modes,
    [ValidateRange(60,1800)][int]$TimeoutSeconds=1200,
    [ValidateSet('gpu','basic','off')][string]$Validation='gpu',
    [string]$ProbeExecutable='',
    [string]$SourceRoot=''
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/ArtifactRetention.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Assert-ArtifactCapacity $repo
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh verification directory.'}
New-Item -ItemType Directory -Path $out|Out-Null
$exe=if($ProbeExecutable){[IO.Path]::GetFullPath($ProbeExecutable)}else{Join-Path $repo "Bin/x64-$Configuration/Tools/MaterialRasterSurfaceProbe/MaterialRasterSurfaceProbe.exe"}
if(!(Test-Path -LiteralPath $exe -PathType Leaf)){throw "Probe executable is missing: $exe"}
function Snapshot {
    $files=@(Get-ChildItem "$repo/Engine/RenderEngine","$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader",
        "$repo/Tools/regression" -Recurse -File|Where-Object {$_.Extension -in '.cpp','.h','.inl','.slang','.ps1','.vcxproj'}|
        Sort-Object FullName)
    @($files|ForEach-Object {@{path=[IO.Path]::GetRelativePath($repo,$_.FullName);sha256=(Get-FileHash $_.FullName).Hash}})+
        @(@{path=[IO.Path]::GetRelativePath($repo,$exe);sha256=(Get-FileHash $exe).Hash})
}
$source=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$repo}
# Product() exports fixed shader names; serialize only users of the same source scratch root.
$gateDirectory=Join-Path $source 'Build/Obj/MaterialProductProbe'
$null=[IO.Directory]::CreateDirectory($gateDirectory)
$gatePath=Join-Path $gateDirectory '.verification.lock'
$gate=$null
$gateWait=[Diagnostics.Stopwatch]::StartNew()
try
{
    while ($null -eq $gate)
    {
        try
        {
            $gate=[IO.FileStream]::new($gatePath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        }
        catch [IO.IOException]
        {
            $code=$_.Exception.GetBaseException().HResult -band 0xffff
            if ($code -notin 32,33 -or $gateWait.Elapsed.TotalSeconds -ge $TimeoutSeconds)
            {
                throw
            }
            Start-Sleep -Milliseconds 250
        }
    }
    $before=Snapshot
    $before|ConvertTo-Json -Depth 4|Set-Content "$out/identity-before.json"
    $required=@{
        '--cluster-storage-only'=@('METEOR_OCT_NORMAL_OK directions=70','METEOR_VARIABLE_SPARSE_POOL_OK floorPages=2 peakPages=6 gpuWords=80 activePin=1 pressure=1 globalReservation=1 regrow=1 hysteresis=1 stableAddress=1 validation=0','METEOR_COARSE_ADMISSION_OK physicalPages=2 rootWords=8 immutable=1 minimumReject=1 validation=0','METEOR_SHARED_SPARSE_POOL_OK assets=3 physicalPages=3 gpuWords=192 activePin=1 priority=1 lru=1 reuse=1 delta=1 abort=1 validation=0','METEOR_CLUSTER_STORAGE_OK','METEOR_CLUSTER_RESIDENCY_OK','METEOR_SPARSE_BUFFER_OK frames=6 words=384 malformed=6 stableAddress=1 validation=0')
        '--cluster-streaming-only'=@('METEOR_SPARSE_IDLE_SCENE_OK emptyViews=1 productPool=1 pressureTrim=1 validation=0','METEOR_CLUSTER_STREAMING_OK','METEOR_CLUSTER_LOD_OK','METEOR_STREAMING_FEEDBACK_OK frames=32 sparse=1 stableBuffers=1 completedGpuDemand=1 validation=0','METEOR_COLD_METADATA_OK','METEOR_GEOMETRY_PAGE_ONLY_OK schema=9 bodyBytes=0 oldSchemasRejected=3','METEOR_COLD_BACKING_OK loose=1 pak=1 capturedLifetime=1 pageHashes=1','METEOR_COLD_RENDER_OK frames=32 rawPayload=0 cpuConnectivity=0 bulkReads=0 validation=0')
        '--quantized-geometry-only'=@('METEOR_QUANTIZED_GEOMETRY_OK')
        '--shadow-correctness-only'=@('METEOR_SHADOW_CORRECTNESS_OK','METEOR_SHADOW_CAMERA_PREDICTION_OK cases=8 recovered=2 exactVersionRejects=6 retainedHistoryRejects=2 validation=0','METEOR_SHADOW_DEVICE_RECREATION_OK devices=2 validation=0')
        '--temporal-transmission-only'=@('METEOR_TEMPORAL_TRANSMISSION_OK frames=6 instances=2 validation=0')
        '--skinned-cluster-lod-only'=@('METEOR_SKIN_DEPTH_REGRESSION_OK focusedPixels=4','METEOR_SKIN_LOD_AUDIT_ALL_OK','METEOR_SKIN_LOD_BOUND_OK poses=6 malformed=2','METEOR_SKINNED_COARSE_SCENE_OK frames=32 poses=2 partitions=2','METEOR_STREAMING_FEEDBACK_OK frames=32 sparse=1 stableBuffers=1 completedGpuDemand=1 validation=0','METEOR_SPARSE_IDLE_SCENE_OK emptyViews=1 productPool=1 pressureTrim=1 validation=0')
        '--cluster-lod-only'=@('METEOR_CLUSTER_DEPTH_REGRESSION_OK focusedPixels=4','METEOR_CLUSTER_LOD_OK frames=32 partitions=2 malformed=9','METEOR_PRODUCTION_SHARED_QUEUE_OK headers=224 cameraFrames=32 shadowFrames=32 sharedReads=192 validation=0')
        '--object-space-only'=@('METEOR_OBJECT_SPACE_INSTANCES_OK frames=9 worlds=4 poses=2 partitions=3 sharedFrames=9 reuseFrames=3','METEOR_COLD_SKIN_OK frames=9 rawPayload=0 cpuConnectivity=0 bulkReads=0 validation=0')
        '--geometry-preparation-scale-only'=@('GEOMETRY_SCALE_OK')
        '--object-space-dx12-only'=@('METEOR_OBJECT_SPACE_INSTANCES_OK frames=9 worlds=4 poses=2 partitions=3 sharedFrames=9 reuseFrames=3','METEOR_COLD_SKIN_OK frames=9 rawPayload=0 cpuConnectivity=0 bulkReads=0 validation=0','METEOR_OBJECT_SPACE_DX12_OK cold=1 validation=0')
        '--hybrid-visibility-only'=@('METEOR_HYBRID_VISIBILITY_OK frames=24 cases=8 partitions=3 validation=0')
        '--atomic-reduction-only'=@('METEOR_ATOMIC_REDUCTION_OK permutations=24 frames=6 validation=0')
        '--visibility-reconstruction-only'=@('METEOR_VISIBILITY_RECONSTRUCTION_OK frames=32 partitions=3 footprintFrames=8 validation=0')
        '--primitive-cache-only'=@('METEOR_PRIMITIVE_CACHE_OK surfaceFrames=32 temporalFrames=12 validation=0','LX_TEMPORAL_BUCKET_OK frames=12 instances=2 opaquePackets=12 layerReplays=3')
        '--hw-visibility-only'=@('METEOR_HW_VISIBILITY_OK frames=24 partitions=3 validation=0')
        '--geometry-access-only'=@('METEOR_GEOMETRY_ACCESS_OK cases=6 lods=2 partitions=3 validation=0')
        '--layer-bucket-only'=@('LX_LAYER_BUCKET_OK mode=1 frames=24','LX_LAYER_BUCKET_OK mode=2 frames=24')
        '--temporal-bucket-only'=@('LX_TEMPORAL_BUCKET_OK frames=12 instances=2 opaquePackets=12 layerReplays=3')
        '--material-bucket-only'=@('LX_MATERIAL_BUCKET_OK frames=24','[meteor.material-buckets] commands=2 buckets=1 cpuInstanceSubmissions=0')
        # The native fixture asserts per-cascade command and submission counts.
        # The old combined-shadow diagnostic no longer exists after the stage split.
        '--shadow-decal-only'=@('CSM_CONTRACTS_OK','SHADOW_MATERIAL_BUCKET_OK frames=6','SHADOW_COOKED_CLUSTER_BOUNDARY_OK','LX_MATERIAL_SCENE_SHADOW_OK','LX_MATERIAL_SCENE_DECAL_OK')
        '--subsurface-only'=@('LX_MATERIAL_SCENE_SUBSURFACE_OK')
        '--refraction-only'=@('LX_MATERIAL_SCENE_REFRACTION_OK')
        '--refraction-film-only'=@('LX_MATERIAL_SCENE_REFRACTION_FILM_OK frames=3')
        '--volume-only'=@('LX_MATERIAL_SCENE_VOLUME_OK')
        '--forward-blend'=@('LX_MATERIAL_FORWARD_BLEND_OK','LX_BLENDED_BUCKET_OK runtime=0 frames=16','LX_BLENDED_BUCKET_OK runtime=1 frames=16')
        '--rg5-mixed'=@('RG5_MIXED_GPU_OK')
        '--rg5-decal'=@('RG5_DECAL_GPU_OK')
        '--rg5-reference'=@('LX_MATERIAL_RASTER_SURFACE_OK')
        '--scene-only'=@('LX_MATERIAL_SCENE_INTEGRATION_OK')
        '--meshlet-hzb-only'=@('METEOR_MESHLET_HZB_OK cases=8','METEOR_MESHLET_TEMPORAL_OK cases=8','METEOR_HISTORY_POOL_OK')
        '--lx-meshlet-only'=@('METEOR_MESHLET_HZB_OK cases=8','METEOR_MESHLET_TEMPORAL_OK cases=8','LX_MESHLET_CULL_LOD_HZB_OK','METEOR_CAMERA_MESHLET_DISPATCH_BOUNDARIES_OK cases=5 maxSurvivors=131071 ordered=1','METEOR_MESHLET_BATCH_DISPATCH_OK bins=4096')
        '--meshlet-dispatch-only'=@('METEOR_CAMERA_MESHLET_DISPATCH_BOUNDARIES_OK cases=5 maxSurvivors=131071 ordered=1','METEOR_MESHLET_BATCH_DISPATCH_OK bins=4096','METEOR_QUEUE_COUNT_OK cases=48 referenceCases=8 orderedCases=32 partialWaveCases=16 oneSlotCases=16 subdivisions=16 ignoredTotalHintCases=8 budgetBoundaries=3 validation=0','METEOR_INSTANCE_ADMISSION_OK cases=16 partialWaveCases=8 boundsWords=8 legacyParity=1 validation=0')
        '--decal-only'=@('LX_MATERIAL_SCENE_DECAL_OK')
        '--forward-transport'=@('LX_MATERIAL_FORWARD_TRANSPORT_OK')
    }
    $runs=@()
    foreach($mode in $Modes){
        Assert-ArtifactCapacity $repo
        $name=$mode.TrimStart('-')
        $start=[Diagnostics.ProcessStartInfo]::new($exe)
        $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden';$start.WorkingDirectory=$repo
        $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        $start.ArgumentList.Add($source);$start.ArgumentList.Add($mode)
        $relative=if($Configuration -eq 'Debug'){'debug/bin'}else{'bin'}
        $bins=@("$repo/vcpkg_installed/x64-windows/$relative","$repo/vcpkg_installed/x64-windows/x64-windows/$relative")|
            Where-Object {Test-Path -LiteralPath $_}
        $start.Environment['PATH']=($bins -join ';')+';'+$env:PATH
        $start.Environment['CREATOR_DX12_VALIDATION']=$Validation
        $proc=[Diagnostics.Process]::Start($start)
        $stdoutFile=[IO.FileStream]::new("$out/$name.stdout.log",[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);$stderrFile=[IO.FileStream]::new("$out/$name.stderr.log",[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
        $stdout=$proc.StandardOutput.BaseStream.CopyToAsync($stdoutFile);$stderr=$proc.StandardError.BaseStream.CopyToAsync($stderrFile)
        $timedOut=$false
        try{if(!$proc.WaitForExit($TimeoutSeconds*1000)){$timedOut=$true;$proc.Kill();$proc.WaitForExit()}}
        finally{
            $null=$stdout.GetAwaiter().GetResult();$null=$stderr.GetAwaiter().GetResult()
            $stdoutFile.Dispose();$stderrFile.Dispose()
        }
        $log=(Get-Content "$out/$name.stdout.log" -Raw)+(Get-Content "$out/$name.stderr.log" -Raw)
        $missing=@($required[$mode]|Where-Object {$log -notmatch ('(?m)^'+[regex]::Escape($_)+'(?: |\r?$)')})
        $passed=!$timedOut -and $proc.ExitCode -eq 0 -and !$missing.Count -and
            $log -match ('GPUValidation='+$(if($Validation -eq 'gpu'){'on'}else{'off'})) -and $log -notmatch 'PROBE_CHECK_FAIL|RASTER_SURFACE_FAIL'
        if($Validation -eq 'basic'){$passed=$passed -and $log -match 'DebugLayer=on GPUValidation=off'}
        $coverageNotices=[regex]::Matches($log,'(?m)^DX12_GBV_COVERAGE_LIMIT id=1044 ').Count
        $runs+=@{mode=$mode;passed=$passed;exitCode=$proc.ExitCode;timedOut=$timedOut;missingMarkers=$missing;
            gpuValidationCoverageNotices=$coverageNotices}
        $runs|ConvertTo-Json -Depth 5|Set-Content "$out/runs.json"
        "METEOR_PROBE_RESULT $Configuration $mode passed=$passed exit=$($proc.ExitCode)"
        if(!$passed){break}
    }
    $after=Snapshot
    $after|ConvertTo-Json -Depth 4|Set-Content "$out/identity-after.json"
    $stable=($before|ConvertTo-Json -Depth 4 -Compress) -eq ($after|ConvertTo-Json -Depth 4 -Compress)
    $passed=$stable -and $runs.Count -eq $Modes.Count -and @($runs|Where-Object {!$_.passed}).Count -eq 0
    @{passed=$passed;configuration=$Configuration;validationMode=$Validation;sourceStable=$stable;sourceRoot=$source;probeExecutable=$exe;
        auditRepeats=$env:CREATOR_SKIN_LOD_AUDIT_REPEATS;auditMode=$env:CREATOR_SKIN_LOD_AUDIT_MODE;gpuValidation=$(if($Validation -eq 'gpu'){'on'}else{'off'});runs=$runs}|
        ConvertTo-Json -Depth 6|Set-Content "$out/verdict.json"
    if(!$passed){throw "METEOR material probe gate failed: $out"}
}
finally
{
    if ($null -ne $gate)
    {
        $gate.Dispose()
    }
}

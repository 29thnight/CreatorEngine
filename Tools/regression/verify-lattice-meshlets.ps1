#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(1,8)][int]$Samples=4,
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$Python='python',
    [switch]$ValidateGpu,
    # Reuse the product Editor and its actual scene; never create a test project.
    [string]$ScenePath=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(!$ScenePath){$ScenePath="$repo/Dynamic_CPP/Assets/Scenes/TestShadow.creator"}
$ScenePath=[IO.Path]::GetFullPath($ScenePath)
if(!(Test-Path -LiteralPath $ScenePath)){throw "Missing explicit workload scene: $ScenePath"}
if(Test-Path $out){throw 'Use a new evidence directory'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close the existing Editor before measurement'}
New-Item -ItemType Directory $out | Out-Null
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$binary=@{head=(git -C $repo rev-parse HEAD);exe=(Get-FileHash $exe).Hash;runtime=(Get-FileHash "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll").Hash}
$binary | ConvertTo-Json | Set-Content "$out/binary.json"
@{schemaVersion=1; scene=$ScenePath; purpose='lattice indexed versus mesh shader correctness';
    performanceValidated=$false; configuration=$Configuration} |
    ConvertTo-Json | Set-Content "$out/measurement-context.json"
$runs=@()
$orders=@('Meshlet')
foreach($order in $orders)
{
    $modes=@(0,1)
    foreach($mode in $modes)
    {
        $case="$out/$order-$mode"
        New-Item -ItemType Directory "$case/workspace" -Force | Out-Null
        Copy-Item "$repo/Dynamic_CPP/Saved/Editor/Editor/Workspaces/active.workspace" "$case/workspace/active.workspace"
        $start=[Diagnostics.ProcessStartInfo]::new($exe)
        $start.WorkingDirectory=Split-Path $exe
        $start.UseShellExecute=$false
        $start.CreateNoWindow=$true
        $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $start.RedirectStandardOutput=$true
        $start.RedirectStandardError=$true
        foreach($arg in @('--development-project',"$repo/Dynamic_CPP",'--command-service'))
        {
            $start.ArgumentList.Add($arg)
        }
        $start.Environment['CREATOR_DX12_VALIDATION']=if($ValidateGpu){'gpu'}else{'off'}
        $start.Environment['CREATOR_RENDERGRAPH_QUEUE_EXECUTION']='0'
        $start.Environment['CREATOR_LX_MESHLETS']="$mode"
        $start.Environment.Remove('CREATOR_RG8_EVIDENCE') | Out-Null
        $start.Environment['CREATOR_RENDERGRAPH_ALIASING']='0'
        $start.Environment['CREATOR_RENDERGRAPH_EXTEND_LIFETIMES']='0'
        $start.Environment['CREATOR_EDITOR_WORKSPACE_DIR']="$case/workspace"
        $start.Environment['CREATOR_EDITOR_LEGACY_INI']="$case/workspace/legacy.ini"
        $start.Environment['CREATOR_GPU_MEMORY_SAMPLES']="$case/memory-continuous.jsonl"
        $process=[Diagnostics.Process]::Start($start)
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        try
        {
            $deadline=[DateTime]::UtcNow.AddSeconds(180)
            do
            {
                if($process.HasExited){throw 'Editor exited during startup'}
                $endpoint=$null
                if(Test-Path "$repo/Dynamic_CPP/Library/CommandService/endpoint.json")
                {
                    $endpoint=Get-Content "$repo/Dynamic_CPP/Library/CommandService/endpoint.json" -Raw | ConvertFrom-Json
                }
                if([DateTime]::UtcNow -gt $deadline){throw 'Endpoint timeout'}
                Start-Sleep -Milliseconds 100
            }until($endpoint -and $endpoint.pid -eq $process.Id)
            $base="http://127.0.0.1:$($endpoint.port)"
            $headers=@{Authorization="Bearer $($endpoint.token)"}
            function Cmd([string]$name,[string[]]$arguments=@(),[bool]$allowPreparing=$false)
            {
                @{command=$name;utcMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()} | ConvertTo-Json -Compress | Add-Content "$case/stages.jsonl"
                $body=@{command=$name;args=@($arguments);mode='async'} | ConvertTo-Json -Compress
                $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 20
                if($name -ne 'quit' -and $result.operationId)
                {
                    $poll=$result.poll;
                    # GPU validation can compile instrumented driver shaders on
                    # first use. Keep timing runs on their original short bound.
                    $until=[DateTime]::UtcNow.AddSeconds($(if($ValidateGpu){600}else{180}))
                    do
                    {
                        Start-Sleep -Milliseconds 100
                        $result=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 20
                        if([DateTime]::UtcNow -gt $until){throw "$name timeout"}
                    }until($result.state -eq 'completed')
                }
                @{command=$name;arguments=$arguments;result=$result} | ConvertTo-Json -Depth 70 -Compress | Add-Content "$case/commands.jsonl"
                if($name -eq 'quit'){return $result}
                if($allowPreparing -and $result.message -eq 'LX Scene requested material program is still preparing.'){return $null}
                if($result.status -ne 'succeeded'){throw "$name failed: $($result.message)"}
                return $result.data
            }
            $load=Cmd 'scene.switch' @($ScenePath)
            $deadline=[DateTime]::UtcNow.AddSeconds(180)
            do
            {
                $state=Cmd 'scene.load.status' @("$($load.requestId)")
                if([DateTime]::UtcNow -gt $deadline){throw 'Scene load timeout'}
                Start-Sleep -Milliseconds 100
            }until($state.complete)
            Cmd 'editor.viewport' @('scene') | Out-Null
            Cmd 'editor.renderscale' @('auto') | Out-Null
            for($ready=0;$ready -lt 30;++$ready)
            {
                Cmd 'dx12.live' | ConvertTo-Json -Depth 70 | Set-Content "$case/preflight-$ready.json"
                Start-Sleep -Milliseconds 500
            }
            $attempt=0
            do
            {
                $warm=Cmd 'render.live.capture' @("$case/warm-$attempt",'editor','controlled') $true
                ++$attempt
                if($attempt -gt 40){throw 'Warm preparation timeout'}
                Start-Sleep -Milliseconds 250
            }until($warm)
            Start-Sleep -Seconds 2
            $captureSamples=$Samples
            for($index=0;$index -lt $captureSamples;++$index)
            {
                Cmd 'render.live.capture' @("$case/sample-$index",'editor','controlled') | Out-Null
                Write-Output "LATTICE_MESH_SAMPLE order=$order mode=$mode sample=$index"
                Start-Sleep -Milliseconds 250
            }
            Cmd 'dx12.live' | ConvertTo-Json -Depth 70 | Set-Content "$case/live-status.json"
            if($ValidateGpu)
            {
                Cmd 'render.live.fence' @('600') | Out-Null
                $validation=Cmd 'dx12.validation'
                $validation | ConvertTo-Json -Depth 30 | Set-Content "$case/validation.json"
                if(!$validation.layerEnabled -or $validation.mode -ne 'gpu' -or
                    $validation.problems -ne 0 -or $validation.droppedMessages -ne 0)
                {
                    throw 'GPU validation is missing or reported a problem'
                }
            }
            Cmd 'quit' | Out-Null
            if(!$process.WaitForExit(30000)){throw 'Editor shutdown timeout'}
            if($process.ExitCode -ne 0){throw "Editor exit $($process.ExitCode)"}
            $runs+=@{schemaVersion=2;scene=$ScenePath;order=$order;mode=$mode;path=$case;samples=$captureSamples;gpuValidation=[bool]$ValidateGpu;exitCode=$process.ExitCode}
            $runs | ConvertTo-Json -Depth 5 | Set-Content "$out/runs.json"
        }
        finally
        {
            if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
            $stdout.GetAwaiter().GetResult() | Set-Content "$case/stdout.log"
            $stderr.GetAwaiter().GetResult() | Set-Content "$case/stderr.log"
            $process.Dispose()
        }
        $dispatches=Select-String -Path "$case/stdout.log" -Pattern '\[lx.meshlets\] (surface|shadow) dispatch groups=\d+ indirect=1'
        if($mode -eq 1 -and @($dispatches).Count -lt 2)
        {
            throw 'Surface and shadow mesh indirect dispatch evidence is missing'
        }
        if($mode -eq 0 -and @($dispatches).Count -ne 0)
        {
            throw 'Indexed baseline unexpectedly used mesh dispatch'
        }
        if((Get-FileHash $exe).Hash -ne $binary.exe -or (Get-FileHash "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll").Hash -ne $binary.runtime)
        {
            throw 'Binary changed during measurement'
        }
    }
}
$comparisons=@()
for($sample=0;$sample -lt $Samples;++$sample)
{
    $left="$out/Meshlet-0/sample-$sample"
    $right="$out/Meshlet-1/sample-$sample"
    & $Python "$repo/Tools/regression/compare-material-captures.py" $left $right "$out/comparison-$sample.json" | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Controlled input or pixel comparison failed'}
    foreach($file in Get-ChildItem -LiteralPath $left -Filter '*.f32')
    {
        $same=(Get-FileHash -LiteralPath $file.FullName).Hash -eq (Get-FileHash -LiteralPath "$right/$($file.Name)").Hash
        $comparisons+=@{sample=$sample;attachment=$file.Name;identical=$same}
        if(!$same){throw "Nonidentical meshlet attachment: $($file.Name)"}
    }
}
$comparisons | ConvertTo-Json | Set-Content "$out/exact-comparisons.json"
'LATTICE_MESH_CORRECTNESS_COMPLETE'

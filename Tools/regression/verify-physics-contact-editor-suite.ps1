[CmdletBinding()]
param([string]$Configuration='Release')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$pwsh=(Get-Command pwsh -ErrorAction Stop).Source
$out=Join-Path $repo ('Build/Verification/ContactStream/E0Suite-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
$cases=@(
    @{name='Basic';gate='stream';args=@()},
    @{name='LateOverlap';gate='stream';args=@('-LateOverlap')},
    @{name='SensorTargets';gate='stream';args=@('-GroupTargets')},
    @{name='Solid';gate='stream';args=@('-SolidContacts')},
    @{name='EntityRetirement';gate='stream';args=@('-Retirement')},
    @{name='ComponentRemoval';gate='stream';args=@('-RemoveComponent')},
    @{name='Topology';gate='stream';args=@('-Topology')},
    @{name='BurstTopology';gate='stream';args=@('-BurstTopology')},
    @{name='SensorTransition';gate='stream';args=@('-SensorTransition')},
    @{name='SubscriberLifetime';gate='stream';args=@('-SubscriberLifetime')},
    @{name='RoleTransition';gate='stream';args=@('-RoleTransition')},
    @{name='ExplicitBinding';gate='stream';args=@('-ExplicitBinding')},
    @{name='Overflow';gate='stream';args=@('-FaultCase','Overflow')},
    @{name='Exception';gate='stream';args=@('-FaultCase','Exception')},
    @{name='SceneReload';gate='scene';args=@()},
    @{name='Ddol';gate='scene';args=@('-Ddol')},
    @{name='AssemblyReload';gate='scene';args=@('-AssemblyReload')},
    @{name='HierarchyDdol';gate='hierarchy';args=@()}
)
$rows=@($cases | ForEach-Object {
    @{name=$_.name;editor='pending';editorReceipt='';commands=0;developmentPlayer=$(if($_.name -eq 'AssemblyReload'){'unsupported'}else{'pending'});shippingPlayer=$(if($_.name -eq 'AssemblyReload'){'unsupported'}else{'pending'})}
})
$hashes=@{}
foreach($file in Get-ChildItem "$repo/GameScripts" -Filter 'Physics*Contact*Probe.cs'){
    $hashes[$file.Name]=(Get-FileHash -LiteralPath $file.FullName).Hash
}
foreach($file in @('verify-physics-contact-stream-http.ps1','verify-physics-contact-scene-http.ps1','verify-physics-contact-hierarchy-http.ps1')){
    $hashes[$file]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file)).Hash
}
$editorHost=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll"
$hostHash=(Get-FileHash -LiteralPath $editorHost).Hash
function SaveIndex {
    @{result='E0_EDITOR_SUITE';configuration=$Configuration;editorHostSha256=$hostHash;sourceHashes=$hashes;rows=$rows;passed=@($rows|Where-Object editor -eq 'passed').Count;failed=@($rows|Where-Object editor -eq 'failed').Count;pending=@($rows|Where-Object editor -eq 'pending').Count;playerPending=@($rows|Where-Object developmentPlayer -eq 'pending').Count+@($rows|Where-Object shippingPlayer -eq 'pending').Count;complete=(@($rows|Where-Object editor -ne 'passed').Count -eq 0)} |
        ConvertTo-Json -Depth 10 | Set-Content "$out/result.json" -Encoding utf8
}
SaveIndex
for($i=0;$i -lt $cases.Count;$i++){
    $case=$cases[$i]
    $gate=Join-Path $PSScriptRoot "verify-physics-contact-$($case.gate)-http.ps1"
    $log=Join-Path $out ($case.name+'.log')
    try {
        & $pwsh -NoProfile -ExecutionPolicy Bypass -File $gate -Configuration $Configuration @($case.args) *> $log
        if($LASTEXITCODE){throw "Gate process failed: $($case.name)"}
        $match=Select-String -LiteralPath $log -Pattern '^CONTACT_.*_EDITOR_OK evidence=(.+)$' | Select-Object -Last 1
        if(!$match){throw "Gate produced no fresh receipt: $($case.name)"}
        $receipt=Join-Path $match.Matches[0].Groups[1].Value 'result.json'
        $result=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
        if($result.result -notmatch '^CONTACT_.*_EDITOR_OK$'){throw 'Invalid receipt'}
        $rows[$i].editor='passed'
        $rows[$i].editorReceipt=$receipt
        $rows[$i].commands=$result.commands
        SaveIndex
        "E0_EDITOR_CASE_OK case=$($case.name) commands=$($result.commands)"
    } catch {
        $rows[$i].editor='failed'
        SaveIndex
        throw
    }
}
if((Get-FileHash -LiteralPath $editorHost).Hash -ne $hostHash){throw 'Editor host changed during suite'}
"E0_EDITOR_SUITE_OK evidence=$out"
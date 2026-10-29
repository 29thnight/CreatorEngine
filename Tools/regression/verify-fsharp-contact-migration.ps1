[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CorpusManagedDirectory,
    [Parameter(Mandatory)][string]$EngineDistribution,
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string[]]$Cases=@('Basic','Grouped','Late','Solid','Retirement','RemoveComponent','Topology','BurstTopology','SensorTransition','SubscriberLifetime','RoleTransition','ExplicitBinding','Overflow','Exception')
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work=Join-Path $repo ('Build/Verification/FSharpContactMigration-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $work 'Project'
$backup=Join-Path $work 'Backup'
$target=Join-Path $repo "Bin/x64-$Configuration/Managed/FSharpNative/Scripts"
$files=@('GameScripts.dll','GameScripts.deps.json','GameScripts.pdb','GameScripts.scripts.metadata.json')
New-Item -ItemType Directory -Force "$project/Assets/Scenes","$project/Assets/Prefabs",$backup|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" $project -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets" -Recurse
foreach($name in $files){Copy-Item -LiteralPath "$target/$name" -Destination "$backup/$name"}
$results=@()
try {
    foreach($name in $files){Copy-Item -LiteralPath "$CorpusManagedDirectory/$name" -Destination "$target/$name" -Force}
    foreach($case in $Cases){
        $parameters=@{Configuration=$Configuration;ProjectRoot=$project}
        switch($case){
            'Basic' {}
            'Grouped' {$parameters.GroupTargets=$true}
            'Late' {$parameters.LateOverlap=$true}
            'Solid' {$parameters.SolidContacts=$true}
            'Retirement' {$parameters.Retirement=$true}
            'RemoveComponent' {$parameters.RemoveComponent=$true}
            'Topology' {$parameters.Topology=$true}
            'BurstTopology' {$parameters.BurstTopology=$true}
            'SensorTransition' {$parameters.SensorTransition=$true}
            'SubscriberLifetime' {$parameters.SubscriberLifetime=$true}
            'RoleTransition' {$parameters.RoleTransition=$true}
            'ExplicitBinding' {$parameters.ExplicitBinding=$true}
            'Overflow' {$parameters.FaultCase='Overflow'}
            'Exception' {$parameters.FaultCase='Exception'}
            default {throw "Unknown contact case: $case"}
        }
        $before=@(Get-ChildItem "$repo/Build/Verification/ContactStream" -Directory -Filter 'Product-*' -ErrorAction SilentlyContinue | ForEach-Object FullName)
        & "$PSScriptRoot/verify-physics-contact-stream-http.ps1" @parameters
        $new=@(Get-ChildItem "$repo/Build/Verification/ContactStream" -Directory -Filter 'Product-*' | Where-Object {$_.FullName -notin $before})
        if($new.Count -ne 1){throw "Expected one Editor result for $case"}
        $editorResult=Join-Path $new[0].FullName 'result.json'
        & "$PSScriptRoot/verify-physics-contact-stream-player.ps1" -EngineDistribution $EngineDistribution -EditorResult $editorResult -Configuration $Configuration
        $results+=@{case=$case;editorResult=$editorResult}
        $results|ConvertTo-Json -Depth 8|Set-Content "$work/results.json" -Encoding utf8
    }
} finally {
    foreach($name in $files){
        Copy-Item -LiteralPath "$backup/$name" -Destination "$target/$name" -Force
        if((Get-FileHash "$backup/$name").Hash -ne (Get-FileHash "$target/$name").Hash){throw "Failed to restore $name"}
    }
}
"FSHARP_CONTACT_MIGRATION_OK cases=$($results.Count) evidence=$work"

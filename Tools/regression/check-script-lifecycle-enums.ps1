$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$native=Get-Content "$repo/Engine/SceneRuntime/ScriptLifecyclePhase.h" -Raw
$managed=Get-Content "$repo/Engine/Scripting/Bindings/Runtime/RuntimeContracts.fs" -Raw
$nativePhases=@([regex]::Matches($native,'\b(OnInitialized|OnAddedToScene|OnBeginSimulation|OnEndSimulation|OnRemovingFromScene|OnUninitializing)\s*=\s*(\d+)')|ForEach-Object { $_.Groups[1].Value+'='+$_.Groups[2].Value })
$managedPhases=@([regex]::Matches($managed,'\|\s*(OnInitialized|OnAddedToScene|OnBeginSimulation|OnEndSimulation|OnRemovingFromScene|OnUninitializing)\s*=\s*(\d+)')|ForEach-Object { $_.Groups[1].Value+'='+$_.Groups[2].Value })
if($nativePhases.Count -ne 6 -or ($nativePhases -join ',') -cne ($managedPhases -join ',')){throw 'Native/F# lifecycle enum mismatch'}
$component=Get-Content "$repo/Engine/SceneRuntime/Component.h" -Raw
$declared=@([regex]::Matches($component,'(?m)^\s*virtual\s+void\s+(On\w*)\s*\(\s*\)')|Select-Object -First 6|ForEach-Object {$_.Groups[1].Value})
$expected=@($nativePhases|ForEach-Object {($_ -split '=')[0]})
if(($declared -join ',') -cne ($expected -join ',')){throw 'Component lifecycle declaration order mismatch'}
$header=Get-Content "$repo/Engine/SceneRuntime/ScriptComponent.h" -Raw
$source=Get-Content "$repo/Engine/SceneRuntime/ScriptComponent.cpp" -Raw
foreach($phase in $expected){
    if($header -notmatch ('void\s+'+$phase+'\s*\(\s*\)\s*override')){throw "Missing native override: $phase"}
    $body=[regex]::Match($source,('(?s)void\s+ScriptComponent::'+$phase+'\s*\(\s*\)\s*\{(.*?)\n\}')).Groups[1].Value
    if($body -notmatch ('NotifyManagedLifecycle\s*\(\s*ScriptLifecyclePhase::'+$phase+'\s*\)')){throw "Incorrect lifecycle forwarding: $phase"}
}
'LIFECYCLE_ENUMS_OK phases=6 backend=fsharp-native'

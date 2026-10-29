param([ValidateSet('Debug','Release','All')][string]$Configuration='All')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19M0Script'
New-Item -ItemType Directory -Force $out | Out-Null
$cpp=Get-Content (Join-Path $repo 'Engine/SceneRuntime/ClrHost.cpp') -Raw
$nativeHeader=Get-Content (Join-Path $repo 'Engine/SceneRuntime/ScriptApiTable.h') -Raw
$versionHeader=Get-Content (Join-Path $repo 'Engine/Utility_Framework/Runtime/ScriptApiVersion.h') -Raw
$nativeVersion=[int][regex]::Match($versionHeader,'CreatorScriptApiVersion\s*=\s*(\d+)').Groups[1].Value
$nativeTable=[regex]::Match($nativeHeader,'(?s)struct ScriptApiTable\s*\{(.*?)\n\s*\};').Groups[1].Value
$nativeNames=@([regex]::Matches($nativeTable,'__stdcall\*\s*(\w+)') | ForEach-Object { $_.Groups[1].Value })
foreach($name in @('Character_Find','Character_Read','Character_Velocity','Character_Teleport','Character_Jump','Character_Force','Character_CancelForce','Body_Find','Body_Read','Body_Velocity','Body_Force','Body_ShapeCount','Body_ShapeRead','Body_ShapeFlags','Body_Remove','Body_ShapeRole','Physics_Query','Physics_QueryBatch','Camera_NotifyCameraCut')) {
    if($cpp -notmatch ('g_apiTable\.'+$name+'\s*=\s*&Api_'+$name+';')) { throw "Missing initialized slot: $name" }
}
$configs=if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)}
foreach($config in $configs) {
    $log=Join-Path $out "abi-$config.log"
    $manifest=Join-Path $repo "Engine/Scripting/Bindings/obj/$config/native-bindings/reflgen_creator_script_native.json"
    & dotnet run --project (Join-Path $PSScriptRoot 'PhysicsScriptABI/PhysicsScriptABI.fsproj') -c $config -- $manifest *> $log
    if($LASTEXITCODE) { Get-Content $log -Tail 20; throw 'F# ABI gate failed' }
    $table=(Get-Content $manifest -Raw|ConvertFrom-Json).tables[0]
    $generatedNames=@($table.fields | Select-Object -Skip 2 | ForEach-Object name)
    if($nativeVersion -ne $table.version -or $nativeNames.Count -eq 0 -or ($nativeNames -join ',') -cne ($generatedNames -join ',')){throw 'Native header and generated F# table differ'}
    "PHYSICS_SCRIPT_TABLE_ORDER_OK slots=$($nativeNames.Count) configuration=$config" | Tee-Object (Join-Path $out "table-order-$config.log")
    Get-Content $log | ForEach-Object { "$config $_" }
}

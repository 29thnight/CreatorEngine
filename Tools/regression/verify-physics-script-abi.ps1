param([ValidateSet('Debug','Release','All')][string]$Configuration='All')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19M0Script'
New-Item -ItemType Directory -Force $out | Out-Null
$cpp=Get-Content (Join-Path $repo 'Engine/SceneRuntime/ClrHost.cpp') -Raw
$cs=Get-Content (Join-Path $repo 'ScriptCore/Native.cs') -Raw
$versionHeader=Get-Content (Join-Path $repo 'Engine/Utility_Framework/ScriptApiVersion.h') -Raw
$nativeVersion=[regex]::Match($versionHeader,'CreatorScriptApiVersion\s*=\s*(\d+)').Groups[1].Value
$managedVersion=[regex]::Match($cs,'ExpectedVersion\s*=\s*(\d+)').Groups[1].Value
if (!$nativeVersion -or $nativeVersion -ne $managedVersion) {
    throw 'Native/managed API version differs'
}
$nativeTable=[regex]::Match($cpp,'(?s)struct ScriptApiTable\s*\{(.*?)\n\s*\};').Groups[1].Value
$managedTable=[regex]::Match($cs,'(?s)struct ScriptApiTable\s*\{(.*?)\n\}').Groups[1].Value
$nativeNames=@([regex]::Matches($nativeTable,'__stdcall\*\s*(\w+)') | ForEach-Object { $_.Groups[1].Value })
$managedNames=@([regex]::Matches($managedTable,'delegate\* unmanaged<[^;]+>\s+(\w+)\s*;') | ForEach-Object { $_.Groups[1].Value })
if($nativeNames.Count -eq 0 -or ($nativeNames -join ',') -cne ($managedNames -join ',')) { throw 'Native/managed API table field order differs' }
foreach($name in @('Character_Find','Character_Read','Character_Velocity','Character_Teleport','Character_Jump','Character_Force','Character_CancelForce','Body_Find','Body_Read','Body_Velocity','Body_Force','Body_ShapeCount','Body_ShapeRead','Body_ShapeFlags','Physics_Query','Physics_QueryBatch','Camera_NotifyCameraCut')) {
    if($cpp -notmatch ('g_apiTable\.'+$name+'\s*=\s*&Api_'+$name+';')) { throw "Missing initialized slot: $name" }
}
"PHYSICS_SCRIPT_TABLE_ORDER_OK slots=$($nativeNames.Count)" | Tee-Object (Join-Path $out 'table-order.log')
$configs=if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)}
foreach($config in $configs) {
    $log=Join-Path $out "abi-$config.log"
    & dotnet run --project (Join-Path $PSScriptRoot 'PhysicsScriptABI/PhysicsScriptABI.csproj') -c $config *> $log
    if($LASTEXITCODE) { Get-Content $log -Tail 20; throw 'Managed ABI gate failed' }
    Get-Content $log | ForEach-Object { "$config $_" }
}

param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [string]$GeneratedDirectory = '',
    [string]$Declarations = '',
    [ValidateSet('true', 'false')][string]$EngineShipping = 'false'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19M0Script'
New-Item -ItemType Directory -Force $out | Out-Null
$cpp=Get-Content (Join-Path $repo 'Engine/SceneRuntime/ClrHost.cpp') -Raw
# The shared static checker expands the generated Light block at its real slot.
# Comparing only direct fields here would silently accept the remaining 174.
# This preflight never generates files; the selected build must already exist.
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configs)
{
    & (Join-Path $repo 'ScriptCore/check-api-table.ps1') -Configuration $config `
        -GeneratedDirectory $GeneratedDirectory -Declarations $Declarations -EngineShipping $EngineShipping
    if ($LASTEXITCODE -ne 0)
    {
        throw "Native/managed API table gate failed for $config."
    }
}
foreach($name in @('Character_Find','Character_Read','Character_Velocity','Character_Teleport','Character_Jump','Character_Force','Character_CancelForce','Body_Find','Body_Read','Body_Velocity','Body_Force','Body_ShapeCount','Body_ShapeRead','Body_ShapeFlags','Physics_Query','Physics_QueryBatch')) {
    if($cpp -notmatch ('g_apiTable\.'+$name+'\s*=\s*&Api_'+$name+';')) { throw "Missing initialized slot: $name" }
}
"PHYSICS_SCRIPT_TABLE_ORDER_OK slots=187" | Tee-Object (Join-Path $out 'table-order.log')
foreach($config in $configs) {
    $log=Join-Path $out "abi-$config.log"
    & dotnet run --project (Join-Path $PSScriptRoot 'PhysicsScriptABI/PhysicsScriptABI.csproj') -c $config *> $log
    if($LASTEXITCODE) { Get-Content $log -Tail 20; throw 'Managed ABI gate failed' }
    Get-Content $log | ForEach-Object { "$config $_" }
}

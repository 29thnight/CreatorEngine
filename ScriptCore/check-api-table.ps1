# 네이티브/C# API 표의 전체 순서를 검사한다. 생성된 Light 13칸도 제자리에서 편다.
# 생성은 이 검사의 역할이 아니다. 먼저 승인된 빌드로 생성물을 준비해야 한다.
# 사용법: pwsh ScriptCore/check-api-table.ps1 [-Configuration Release] [-EngineShipping true]
#         pwsh ScriptCore/check-api-table.ps1 -GeneratedDirectory <shared-output-directory>
[CmdletBinding()]
param(
    [string]$GeneratedDirectory = '',
    [string]$Declarations = '',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [ValidateSet('true', 'false')][string]$EngineShipping = 'false'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$configurationKey = $Configuration
if ($EngineShipping -eq 'true')
{
    $configurationKey += '-Shipping'
}
if ([string]::IsNullOrWhiteSpace($GeneratedDirectory))
{
    $GeneratedDirectory = Join-Path $root "Build/Generated/ScriptBindings/$Platform-$configurationKey"
}
if ([string]::IsNullOrWhiteSpace($Declarations))
{
    $Declarations = Join-Path $root "Build/Obj/SceneRuntime/$Platform-$configurationKey/reflgen/SceneRuntime.declarations.json"
}
if (-not (Test-Path -LiteralPath $Declarations -PathType Leaf))
{
    throw "Missing declaration manifest: $Declarations. Generate through the approved build first. If ReflgenOutputDirectory was overridden, pass its SceneRuntime.declarations.json with -Declarations. This checker never runs generation."
}

$requiredOutputs = @(
    'ScriptLightApi.g.h', 'ScriptLightApi.Thunks.g.inc', 'ScriptLightApi.Fill.g.inc',
    'ScriptLightApi.g.cs', 'Native.Light.g.cs', 'LightComponent.g.cs', 'ScriptBindings.contract.json'
)
foreach ($file in $requiredOutputs)
{
    $path = Join-Path $GeneratedDirectory $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        throw "Missing generated binding file: $path. Generate through the approved SceneRuntime/ScriptCore build first (Tools/ScriptBindings/BUILD.md), or supply -GeneratedDirectory. This checker never runs generation."
    }
}

function Get-TableFields
{
    param([string]$Path, [string]$Name, [bool]$Managed)

    $source = Get-Content -LiteralPath $Path -Raw
    $source = [regex]::Replace($source, '(?s)/\*.*?\*/|(?m)//[^\r\n]*', '')
    $declaration = if ($Managed) { 'internal\s+unsafe\s+struct' } else { 'struct' }
    $end = if ($Managed) { '\}' } else { '\};' }
    $pattern = '(?ms)^\s*' + $declaration + '\s+' + [regex]::Escape($Name) + '\s*\{(?<body>.*?)^\s*' + $end
    $tables = [regex]::Matches($source, $pattern)
    if ($tables.Count -ne 1)
    {
        throw "Expected exactly one $Name declaration in $Path, found $($tables.Count)."
    }
    $pointerPattern = if ($Managed)
    {
        '^public\s+delegate\*\s+unmanaged(?:\[Stdcall\])?\s*<[^>]+>\s+(?<name>\w+)\s*;$'
    }
    else
    {
        '^.+\(\s*__stdcall\s*\*\s*(?<name>\w+)\s*\)\s*\([^;]*\)\s*;$'
    }
    $fields = [Collections.Generic.List[object]]::new()
    foreach ($part in ($tables[0].Groups['body'].Value -split ';'))
    {
        $field = ($part -replace '\s+', ' ').Trim()
        if ($field.Length -eq 0)
        {
            continue
        }
        $field += ';'
        if ($field -cmatch $pointerPattern)
        {
            $fields.Add([pscustomobject]@{ Name = $Matches['name']; Kind = 'pointer' })
        }
        elseif ($field -cmatch '^(?:public )?ScriptLightApi Light;$')
        {
            $fields.Add([pscustomobject]@{ Name = 'Light'; Kind = 'nested' })
        }
        elseif ($field -cmatch '^(?:public )?int (?<name>[Vv]ersion|[Ss]tructSize);$')
        {
            $fields.Add([pscustomobject]@{ Name = $Matches['name'].ToLowerInvariant(); Kind = 'i32' })
        }
        elseif ($field -cmatch '^(?:public ulong AbiFingerprint|std::uint64_t abiFingerprint);$')
        {
            $fields.Add([pscustomobject]@{ Name = 'abiFingerprint'; Kind = 'u64' })
        }
        else
        {
            throw "Unrecognized $Name field in ${Path}: $field. Update the checker deliberately; do not silently skip fields."
        }
    }
    return $fields.ToArray()
}

function Expand-Table
{
    param([object[]]$Fields, [object[]]$LightFields, [string]$Label)

    if ($Fields.Count -ne 178 -or $Fields[0].Name -cne 'version' -or $Fields[0].Kind -cne 'i32' -or
        $Fields[1].Name -cne 'structsize' -or $Fields[1].Kind -cne 'i32' -or
        $Fields[-1].Name -cne 'abiFingerprint' -or $Fields[-1].Kind -cne 'u64')
    {
        throw "$Label must have the ABI-v34 envelope: two int headers, 174 pointers, one Light block, then the uint64 fingerprint."
    }
    $names = [Collections.Generic.List[string]]::new()
    $lightCount = 0
    foreach ($field in $Fields[2..($Fields.Count - 2)])
    {
        if ($field.Kind -ceq 'pointer')
        {
            $names.Add($field.Name)
        }
        elseif ($field.Kind -ceq 'nested')
        {
            $lightCount++
            if ($names.Count -ne 85)
            {
                throw "$Label Light block starts at slot $($names.Count), expected 85 (byte offset 688)."
            }
            foreach ($light in $LightFields)
            {
                $names.Add("Light_$($light.Name)")
            }
        }
        else
        {
            throw "$Label contains a non-pointer field inside the function slot region: $($field.Name)."
        }
    }
    if ($lightCount -ne 1 -or $names.Count -ne 187)
    {
        throw "$Label expanded to $($names.Count) pointers and $lightCount Light blocks; expected 187 and 1."
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($name in $names)
    {
        if (-not $seen.Add($name))
        {
            throw "$Label contains duplicate slot $name."
        }
    }
    if ($names[84] -cne 'Camera_GetPrimaryHandle' -or $names[98] -cne 'Mesh_Exists')
    {
        throw "$Label moved the slots immediately before or after Light."
    }
    return $names.ToArray()
}

$nativeLight = @(Get-TableFields (Join-Path $GeneratedDirectory 'ScriptLightApi.g.h') 'ScriptLightApi' $false)
$managedLight = @(Get-TableFields (Join-Path $GeneratedDirectory 'ScriptLightApi.g.cs') 'ScriptLightApi' $true)
$expectedLight = @(
    'Exists', 'GetColor', 'SetColor', 'GetIntensity', 'SetIntensity', 'GetRange', 'SetRange',
    'GetSpotAngle', 'SetSpotAngle', 'GetLightType', 'SetLightType', 'GetLightStatus', 'SetLightStatus'
)
foreach ($block in @(@{ Label = 'Native'; Fields = $nativeLight }, @{ Label = 'Managed'; Fields = $managedLight }))
{
    if ($block.Fields.Count -ne 13)
    {
        throw "$($block.Label) ScriptLightApi contains $($block.Fields.Count) fields; expected 13."
    }
    for ($i = 0; $i -lt 13; $i++)
    {
        if ($block.Fields[$i].Kind -cne 'pointer' -or $block.Fields[$i].Name -cne $expectedLight[$i])
        {
            throw "$($block.Label) Light slot $i is '$($block.Fields[$i].Name)', expected '$($expectedLight[$i])'."
        }
    }
}

$nativeFields = @(Get-TableFields (Join-Path $root 'Engine/SceneRuntime/ClrHost.cpp') 'ScriptApiTable' $false)
$managedFields = @(Get-TableFields (Join-Path $PSScriptRoot 'Native.cs') 'ScriptApiTable' $true)
$native = @(Expand-Table $nativeFields $nativeLight 'Native')
$managed = @(Expand-Table $managedFields $managedLight 'Managed')
$contract = Get-Content -LiteralPath (Join-Path $GeneratedDirectory 'ScriptBindings.contract.json') -Raw | ConvertFrom-Json
if ($contract.format -cne 'creator.script-bindings' -or $contract.version -ne 1 -or
    $contract.api_version -ne 34 -or $contract.function_slot_count -ne 187 -or
    $contract.light_first_slot -ne 85 -or $contract.light_slot_count -ne 13 -or
    $contract.table_size -ne 1512 -or $contract.fingerprint_offset -ne 1504 -or
    @($contract.slots).Count -ne 187)
{
    throw 'Generated contract does not describe the complete ABI-v34 table. Regenerate through the build.'
}

# Check producer inputs and every generated source as a set. Existing files from
# another build are not enough; stale output must not make this gate green.
$inputPaths = [ordered]@{
    declarations = $Declarations
    native_source = (Join-Path $root 'Engine/SceneRuntime/ClrHost.cpp')
    managed_source = (Join-Path $PSScriptRoot 'Native.cs')
    generator = (Join-Path $root 'Tools/ScriptBindings/Generate-ScriptBindings.ps1')
}
foreach ($name in $inputPaths.Keys)
{
    $expected = $contract.input_hashes.PSObject.Properties[$name]
    $actual = (Get-FileHash -LiteralPath $inputPaths[$name] -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($null -eq $expected -or $expected.Value -cne $actual)
    {
        throw "Stale generated bindings: $($inputPaths[$name]) differs from the generated input hash. Regenerate through the build; use -Declarations for a custom reflection manifest."
    }
}
foreach ($file in @($requiredOutputs | Where-Object { $_ -cne 'ScriptBindings.contract.json' }))
{
    $expected = $contract.output_hashes.PSObject.Properties[$file]
    $actual = (Get-FileHash -LiteralPath (Join-Path $GeneratedDirectory $file) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($null -eq $expected -or $expected.Value -cne $actual)
    {
        throw "Generated output differs from its contract: $file. Regenerate through the build."
    }
}

Write-Output "네이티브 $($native.Count)개 · 관리 $($managed.Count)개 (Light 13칸 포함)"
for ($i = 0; $i -lt $native.Count; $i++)
{
    if ($native[$i] -cne $managed[$i] -or $native[$i] -cne $contract.slots[$i].name)
    {
        Write-Output "[$i] 첫 불일치 (byte offset $($i * 8 + 8))"
        Write-Output "  네이티브: $($native[$i])"
        Write-Output "  관리:     $($managed[$i])"
        Write-Output "  생성 계약: $($contract.slots[$i].name)"
        exit 1
    }
}

Write-Output 'API 표 순서 일치: 187 pointers, Light @688, fingerprint @1504, size 1512'
exit 0

#requires -Version 7.0
<#
Engine-owned consumer of reflgen.declarations v1. This is deliberately not the
generic reflgen interop pass: only the Light component policy below is enabled.
The declaration manifest owns method signatures; this file owns ABI adapters,
handle resolution, fallback values, and the migration boundary.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Declarations,
    [Parameter(Mandatory)][string]$NativeSource,
    [Parameter(Mandatory)][string]$ManagedSource,
    [Parameter(Mandatory)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$apiVersion = 34
$slotCount = 187
$lightSlotCount = 13
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)

function Assert-Contract
{
    param([bool]$Condition, [string]$Message)
    if (-not $Condition)
    {
        throw "ScriptBindings: $Message"
    }
}

function Require-Keys
{
    param($Value, [string[]]$Keys, [string]$Context)
    Assert-Contract ($Value -is [System.Collections.IDictionary]) "$Context must be an object."
    foreach ($key in $Keys)
    {
        Assert-Contract ($Value.Contains($key)) "$Context is missing '$key'."
    }
}

function Require-Boolean
{
    param($Value, [string]$Context)
    Assert-Contract ($Value -is [bool]) "$Context must be a JSON boolean."
}

function Get-ScriptAttributes
{
    param($Declaration, [string]$Allowed, [string]$Context)
    Require-Keys $Declaration @('attributes') $Context
    foreach ($attribute in $Declaration.attributes)
    {
        Require-Keys $attribute @('scope', 'name', 'has_arguments', 'argument_tokens') "$Context attribute"
        if ($attribute.scope -ceq 'creator' -and $attribute.name -clike 'script_*')
        {
            Assert-Contract ($attribute.name -ceq $Allowed) "$Context has unsupported creator::$($attribute.name)."
            Require-Boolean $attribute.has_arguments "$Context attribute.has_arguments"
            Assert-Contract $attribute.has_arguments "$Context requires literal attribute arguments."
            $attribute
        }
    }
}

function Assert-TypeRecord
{
    param($Type, [string]$Context)
    Require-Keys $Type @('spelling', 'canonical_spelling', 'kind', 'declaration', 'is_const',
        'is_volatile', 'size_bytes', 'pointee', 'element_type', 'array_size') $Context
    Require-Boolean $Type.is_const "$Context.is_const"
    Require-Boolean $Type.is_volatile "$Context.is_volatile"
    Assert-Contract (-not $Type.is_volatile) "$Context is volatile."
    Assert-Contract ($Type.size_bytes -is [long] -or $Type.size_bytes -is [int]) "$Context has an unknown size."
    Assert-Contract ($null -eq $Type.element_type -and $Type.array_size -eq -1) "$Context has unsupported array metadata."
}

function Get-ValueAdapter
{
    param($Type, [string]$Context)
    Assert-TypeRecord $Type $Context
    $value = $Type
    $isReference = $Type.kind -ceq 'lvalue_reference'
    if ($isReference)
    {
        Assert-Contract (-not $Type.is_const -and $null -ne $Type.pointee) "$Context has an invalid reference qualifier."
        $value = $Type.pointee
        Assert-TypeRecord $value "$Context.pointee"
        Assert-Contract ($value.is_const) "$Context only permits const math::color references."
        Assert-Contract ($Type.size_bytes -eq $value.size_bytes) "$Context has inconsistent reference/referent size metadata."
    }
    else
    {
        Assert-Contract ($null -eq $Type.pointee) "$Context is not a supported value type."
        Assert-Contract (-not $Type.is_const) "$Context has an unsupported const value."
    }
    Assert-Contract ($null -eq $value.pointee) "$Context has an unsupported nested type."
    $canonical = ([regex]::Replace([string]$value.canonical_spelling, '^const\s+', '')).Trim()
    if ($value.kind -ceq 'record' -and $value.declaration -ceq 'math::color' -and
        $canonical -ceq 'math::color' -and $value.size_bytes -eq 16)
    {
        return [ordered]@{
            Key = 'color'; Native = 'Float4'; Managed = 'Color4'; Property = 'Color4'
            NativeFallback = 'Float4{ 1.f, 1.f, 1.f, 1.f }'; ManagedFallback = 'Color4.White'
        }
    }
    Assert-Contract (-not $isReference) "$Context only permits const math::color references."
    if ($value.kind -ceq 'floating_point' -and $canonical -ceq 'float' -and $value.size_bytes -eq 4)
    {
        return [ordered]@{
            Key = 'float'; Native = 'float'; Managed = 'float'; Property = 'float'
            NativeFallback = '0.f'; ManagedFallback = '0f'
        }
    }
    if ($value.kind -ceq 'enum' -and $value.size_bytes -eq 2 -and
        $value.declaration -cin @('LightType', 'LightStatus') -and $canonical -ceq $value.declaration)
    {
        $minimum = if ($value.declaration -ceq 'LightType') { 'DirectionalLight' } else { 'Disabled' }
        $maximum = if ($value.declaration -ceq 'LightType') { 'SpotLight' } else { 'StaticShadows' }
        return [ordered]@{
            Key = $value.declaration; Native = 'int'; Managed = 'int'; Property = $value.declaration
            NativeFallback = '0'; ManagedFallback = '0'; Minimum = $minimum; Maximum = $maximum
        }
    }
    throw "ScriptBindings: $Context has unsupported type '$($Type.canonical_spelling)' ($($Type.kind))."
}

function Remove-Comments
{
    param([string]$Text)
    # Table declarations have no string literals; reject any remaining syntax
    # below rather than guessing when the manually owned representation changes.
    return [regex]::Replace($Text, '(?s)/\*.*?\*/|(?m)//[^\r\n]*', '')
}

function Get-AbiType
{
    param([string]$Type, [ValidateSet('native', 'managed')][string]$Side)
    $typeName = [regex]::Replace($Type.Trim(), '\s+', ' ')
    $typeName = [regex]::Replace($typeName, '\s*\*\s*', '*')
    if ($Side -ceq 'native')
    {
        # Pointee constness does not change storage/calling convention. These
        # are explicit boundary adapters, not a general C++ type erasure rule.
        $map = @{
            'void' = 'void'; 'int' = 'i32'; 'unsigned int' = 'u32'; 'std::uint32_t' = 'u32'
            'unsigned long long' = 'u64'; 'std::uint64_t' = 'u64'; 'float' = 'f32'; 'double' = 'f64'
            'char*' = 'utf8*'; 'const char*' = 'utf8*'; 'int*' = 'i32*'
            'ScriptObjectHandle' = 'ObjectHandle'; 'Float2' = 'Float2'; 'Float3' = 'Float3'; 'Float4' = 'Float4'
            'ce::script::physics_character_state*' = 'CharacterMovementState*'
            'ce::script::physics_body_state*' = 'PhysicsBodyState*'
            'ce::script::physics_shape_state*' = 'PhysicsShapeState*'
            'ce::script::physics_hit*' = 'PhysicsHit*'
            'ce::script::physics_query_result*' = 'NativePhysicsQueryResult*'
            'const ce::script::physics_query_request*' = 'PhysicsQueryRequest*'
            'ce::script::physics_batch_result*' = 'PhysicsBatchResult*'
            'AudioAssetId' = 'AudioAssetId'; 'AudioAssetId*' = 'AudioAssetId*'
            'AudioPlaySettings*' = 'AudioPlaySettings*'; 'const AudioPlaySettings*' = 'AudioPlaySettings*'
            'const AudioParameter*' = 'AudioParameter*'; 'AudioPlaybackCompletion*' = 'AudioPlaybackCompletion*'
        }
    }
    else
    {
        $map = @{
            'void' = 'void'; 'int' = 'i32'; 'uint' = 'u32'; 'ulong' = 'u64'; 'float' = 'f32'; 'double' = 'f64'
            'byte*' = 'utf8*'; 'int*' = 'i32*'; 'ObjectHandle' = 'ObjectHandle'
            'Float2' = 'Float2'; 'Float3' = 'Float3'; 'Color4' = 'Float4'; 'Quaternion' = 'Float4'
            'CharacterMovementState*' = 'CharacterMovementState*'; 'PhysicsBodyState*' = 'PhysicsBodyState*'
            'PhysicsShapeState*' = 'PhysicsShapeState*'; 'PhysicsHit*' = 'PhysicsHit*'
            'NativePhysicsQueryResult*' = 'NativePhysicsQueryResult*'; 'PhysicsQueryRequest*' = 'PhysicsQueryRequest*'
            'PhysicsBatchResult*' = 'PhysicsBatchResult*'; 'AudioAssetId' = 'AudioAssetId'; 'AudioAssetId*' = 'AudioAssetId*'
            'AudioPlaySettings*' = 'AudioPlaySettings*'; 'Native.AudioParameterABI*' = 'AudioParameter*'
            'AudioPlaybackCompletion*' = 'AudioPlaybackCompletion*'
        }
    }
    Assert-Contract ($map.Keys -ccontains $typeName) "Unknown $Side ABI type '$Type'; add an explicit reviewed type adapter."
    return $map[$typeName]
}

function Get-TableSlots
{
    param([string]$Text, [ValidateSet('native', 'managed')][string]$Side, [object[]]$LightSlots)
    $clean = Remove-Comments $Text
    $tables = [regex]::Matches($clean, '(?s)\bstruct\s+ScriptApiTable\s*\{(?<body>[^{}]*)\}')
    Assert-Contract ($tables.Count -eq 1) "Expected one flat $Side ScriptApiTable declaration."
    $statements = @($tables[0].Groups['body'].Value.Split(';') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $prefix = if ($Side -ceq 'native') { @('int version', 'int structSize') } else { @('public int Version', 'public int StructSize') }
    Assert-Contract ($statements.Count -gt 3) "$Side ScriptApiTable is incomplete."
    for ($index = 0; $index -lt 2; $index++)
    {
        Assert-Contract (([regex]::Replace($statements[$index], '\s+', ' ')) -ceq $prefix[$index]) "$Side table header layout changed."
    }
    $tail = if ($Side -ceq 'native') { '^(?:std::)?uint64_t\s+abiFingerprint$' } else { '^public\s+ulong\s+AbiFingerprint$' }
    Assert-Contract ($statements[-1] -cmatch $tail) "$Side table must end in the ABI-v34 fingerprint."
    $slots = [System.Collections.Generic.List[object]]::new()
    $nestedCount = 0
    for ($index = 2; $index -lt $statements.Count - 1; $index++)
    {
        $statement = $statements[$index]
        $nested = if ($Side -ceq 'native') { '^ScriptLightApi\s+Light$' } else { '^public\s+ScriptLightApi\s+Light$' }
        if ($statement -cmatch $nested)
        {
            $nestedCount++
            foreach ($slot in $LightSlots)
            {
                $slots.Add($slot)
            }
            continue
        }
        if ($Side -ceq 'native')
        {
            $match = [regex]::Match($statement, '^(?<return>[\w:\s*]+?)\s*\(\s*__stdcall\s*\*\s*(?<name>\w+)\s*\)\s*\((?<args>[^()]*)\)$')
            Assert-Contract $match.Success "Unsupported native table declaration '$statement'."
            $name = $match.Groups['name'].Value
            $result = Get-AbiType $match.Groups['return'].Value native
            $arguments = @()
            if ($match.Groups['args'].Value.Trim())
            {
                foreach ($argument in $match.Groups['args'].Value.Split(','))
                {
                    $argumentMatch = [regex]::Match($argument.Trim(), '^(?<type>.+?)[\s*](?<name>[A-Za-z_]\w*)$')
                    Assert-Contract $argumentMatch.Success "Unsupported native parameter '$argument' in $name."
                    # Remove only the identifier, preserving its preceding '*'.
                    $type = $argument.Trim().Substring(0, $argument.Trim().Length - $argumentMatch.Groups['name'].Length).Trim()
                    $arguments += Get-AbiType $type native
                }
            }
        }
        else
        {
            $match = [regex]::Match($statement, '^public\s+delegate\*\s+unmanaged(?:\[(?<cc>\w+)\])?\s*<(?<types>[^<>]+)>\s+(?<name>\w+)$')
            Assert-Contract $match.Success "Unsupported managed table declaration '$statement'."
            Assert-Contract ($match.Groups['cc'].Value -cin @('', 'Stdcall')) "Unsupported managed calling convention in '$statement'."
            $name = $match.Groups['name'].Value
            $types = @($match.Groups['types'].Value.Split(','))
            $result = Get-AbiType $types[-1] managed
            $arguments = @()
            for ($argumentIndex = 0; $argumentIndex -lt $types.Count - 1; $argumentIndex++)
            {
                $arguments += Get-AbiType $types[$argumentIndex] managed
            }
        }
        Assert-Contract (-not $name.StartsWith('Light_', [StringComparison]::Ordinal)) "Manual $name duplicates the generated Light block."
        $slots.Add([ordered]@{ name = $name; calling_convention = 'win64-stdcall'; return_type = $result; parameters = @($arguments) })
    }
    Assert-Contract ($nestedCount -eq 1) "$Side table requires exactly one ScriptLightApi Light block."
    return ,$slots.ToArray()
}

function Get-SlotSignature
{
    param($Slot)
    return "$($Slot.name)|$($Slot.calling_convention)|$($Slot.return_type)($($Slot.parameters -join ','))"
}

function Add-Line
{
    param([System.Text.StringBuilder]$Builder, [string]$Line = '')
    [void]$Builder.Append($Line).Append("`n")
}

function Get-BytesHash
{
    param([byte[]]$Bytes)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try
    {
        return [BitConverter]::ToString($sha256.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $sha256.Dispose()
    }
}

# Read and validate every input before creating the output directory or files.
$declarationsPath = [IO.Path]::GetFullPath($Declarations)
$nativePath = [IO.Path]::GetFullPath($NativeSource)
$managedPath = [IO.Path]::GetFullPath($ManagedSource)
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$declarationsBytes = [IO.File]::ReadAllBytes($declarationsPath)
$nativeBytes = [IO.File]::ReadAllBytes($nativePath)
$managedBytes = [IO.File]::ReadAllBytes($managedPath)
$generatorBytes = [IO.File]::ReadAllBytes($PSCommandPath)
$manifest = $utf8.GetString($declarationsBytes).TrimStart([char]0xfeff) | ConvertFrom-Json -AsHashtable -Depth 100
$nativeText = $utf8.GetString($nativeBytes).TrimStart([char]0xfeff)
$managedText = $utf8.GetString($managedBytes).TrimStart([char]0xfeff)
Require-Keys $manifest @('format', 'version', 'module', 'target', 'headers') 'Manifest'
Assert-Contract ($manifest.format -ceq 'reflgen.declarations' -and $manifest.version -eq 1) 'Only reflgen.declarations version 1 is supported.'
Assert-Contract ($manifest.version -is [long] -or $manifest.version -is [int]) 'Manifest version must be an integer.'
Assert-Contract ($manifest.module -ceq 'SceneRuntime') 'Only the SceneRuntime declaration module is supported.'
Require-Keys $manifest.target @('triple', 'pointer_width') 'Manifest.target'
Assert-Contract ($manifest.target.pointer_width -is [long] -or $manifest.target.pointer_width -is [int]) 'Target pointer width must be an integer.'
Assert-Contract ($manifest.target.pointer_width -eq 64 -and
    $manifest.target.triple -cmatch '^x86_64-pc-windows-msvc(?:[0-9.]+)?$') 'Only the x64 Windows MSVC parse target is supported.'
Assert-Contract ((Remove-Comments $managedText) -cmatch '\bconst\s+int\s+ExpectedVersion\s*=\s*34\s*;') 'Managed source must declare ABI version 34.'

$protectedPaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($path in @($declarationsPath, $nativePath, $managedPath, $PSCommandPath))
{
    [void]$protectedPaths.Add([IO.Path]::GetFullPath($path))
}
$selected = [System.Collections.Generic.List[object]]::new()
foreach ($header in $manifest.headers)
{
    Require-Keys $header @('path', 'classes', 'enums') 'Header'
    [void]$protectedPaths.Add([IO.Path]::GetFullPath($header.path))
    foreach ($class in $header.classes)
    {
        Require-Keys $class @('name', 'qualified_name', 'nested', 'attributes', 'methods', 'fields') 'Class'
        $componentAttributes = @(Get-ScriptAttributes $class 'script_component' $class.qualified_name)
        Assert-Contract ($componentAttributes.Count -le 1) "$($class.qualified_name) has duplicate script_component annotations."
        if ($componentAttributes.Count -eq 1)
        {
            Require-Boolean $class.nested "$($class.qualified_name).nested"
            $tokens = @($componentAttributes[0].argument_tokens)
            Assert-Contract ($tokens.Count -eq 1 -and $tokens[0] -ceq '"Light"') 'Only creator::script_component("Light") is supported.'
            Assert-Contract ($class.qualified_name -ceq 'LightComponent' -and $class.name -ceq 'LightComponent' -and -not $class.nested) 'Light must be the top-level LightComponent class.'
            $selected.Add($class)
        }
        foreach ($field in $class.fields)
        {
            $null = @(Get-ScriptAttributes $field '' "$($class.qualified_name) field")
        }
        foreach ($method in $class.methods)
        {
            $exports = @(Get-ScriptAttributes $method 'script_export' "$($class.qualified_name) method")
            Assert-Contract ($exports.Count -le 1) "$($class.qualified_name) has a duplicate script_export."
            Assert-Contract ($exports.Count -eq 0 -or $componentAttributes.Count -eq 1) 'script_export requires a supported script_component owner.'
        }
    }
}
Assert-Contract ($selected.Count -eq 1) 'Exactly one annotated LightComponent is required.'
$component = $selected[0]
$exportsBySlot = @{}
$properties = [ordered]@{}
$methodNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($method in $component.methods)
{
    $attributes = @(Get-ScriptAttributes $method 'script_export' 'LightComponent method')
    if ($attributes.Count -eq 0)
    {
        continue
    }
    Require-Keys $method @('name', 'owner', 'access', 'signature', 'return_type', 'parameters',
        'is_static', 'is_const', 'is_volatile', 'is_variadic', 'is_deleted', 'is_overloaded',
        'ref_qualifier', 'qualifiers_known', 'calling_convention', 'exception_specification') 'Exported method'
    foreach ($flag in @('is_static', 'is_const', 'is_volatile', 'is_variadic', 'is_deleted', 'is_overloaded', 'qualifiers_known'))
    {
        Require-Boolean $method[$flag] "$($method.name).$flag"
    }
    Assert-Contract ($method.owner -ceq 'LightComponent' -and $method.access -ceq 'public') "$($method.name) must be a public LightComponent method."
    Assert-Contract (-not $method.is_static -and -not $method.is_volatile -and -not $method.is_variadic -and
        -not $method.is_deleted -and -not $method.is_overloaded -and $method.qualifiers_known -and
        $method.ref_qualifier -ceq 'none') "$($method.name) has unsupported method qualifiers."
    Assert-Contract ($method.calling_convention -cin @('default', 'c', 'win64')) "$($method.name) has an unsupported calling convention."
    Assert-Contract ($method.exception_specification -cin @('none', 'dynamic_none', 'dynamic', 'ms_any',
        'basic_noexcept', 'computed_noexcept', 'unevaluated', 'uninstantiated', 'unparsed', 'nothrow')) "$($method.name) has an unknown exception specification."
    Assert-Contract ($method.name -cmatch '^(Get|Set)[A-Z][A-Za-z0-9_]*$' -and $methodNames.Add($method.name)) 'Exported methods require unique Get/Set identifiers.'
    $hidden = @($method.attributes | Where-Object { $_.scope -ceq 'creator' -and $_.name -ceq 'hide_in_inspector' })
    Assert-Contract ($hidden.Count -eq 1 -and -not $hidden[0].has_arguments) "$($method.name) must be hidden from inspector method controls."
    $tokens = @($attributes[0].argument_tokens)
    Assert-Contract ($tokens.Count -eq 3 -and $tokens[0] -cmatch '^"[A-Z][A-Za-z0-9_]*"$' -and
        $tokens[1] -ceq ',' -and $tokens[2] -cmatch '^(?:[1-9]|1[0-2])$') "$($method.name) needs a property string and literal relative slot 1..12."
    $propertyName = $tokens[0].Substring(1, $tokens[0].Length - 2)
    $slot = [int]$tokens[2]
    Assert-Contract (-not $exportsBySlot.ContainsKey($slot)) "Duplicate Light slot $slot."
    $isGetter = $method.name.StartsWith('Get', [StringComparison]::Ordinal)
    if ($isGetter)
    {
        Assert-Contract (@($method.parameters).Count -eq 0) "$($method.name) getter must take no arguments."
        $adapter = Get-ValueAdapter $method.return_type "$($method.name) return"
        Assert-Contract ($slot % 2 -eq 1) "$($method.name) getter needs an odd relative slot."
    }
    else
    {
        Assert-TypeRecord $method.return_type "$($method.name) return"
        Assert-Contract ($method.return_type.kind -ceq 'void' -and $method.return_type.canonical_spelling -ceq 'void' -and
            -not $method.return_type.is_const -and $null -eq $method.return_type.pointee -and
            @($method.parameters).Count -eq 1 -and -not $method.is_const) "$($method.name) setter must be non-const, return void and take one argument."
        Require-Keys $method.parameters[0] @('type') "$($method.name) parameter"
        $adapter = Get-ValueAdapter $method.parameters[0].type "$($method.name) parameter"
        Assert-Contract ($slot % 2 -eq 0) "$($method.name) setter needs an even relative slot."
    }
    $entry = [ordered]@{ Slot = $slot; Property = $propertyName; Method = $method.name; Getter = $isGetter; Adapter = $adapter; Declaration = $method }
    $exportsBySlot[$slot] = $entry
    if (-not $properties.Contains($propertyName))
    {
        $properties[$propertyName] = @{}
    }
    $role = if ($isGetter) { 'Get' } else { 'Set' }
    Assert-Contract (-not $properties[$propertyName].ContainsKey($role)) "$propertyName has multiple $role methods."
    $properties[$propertyName][$role] = $entry
}
Assert-Contract ($exportsBySlot.Count -eq 12 -and $properties.Count -eq 6) 'Light ABI requires six getter/setter pairs and slots 1..12.'
foreach ($propertyName in $properties.Keys)
{
    $pair = $properties[$propertyName]
    Assert-Contract ($pair.ContainsKey('Get') -and $pair.ContainsKey('Set')) "$propertyName requires both a getter and setter."
    Assert-Contract ($pair.Get.Adapter.Key -ceq $pair.Set.Adapter.Key -and $pair.Set.Slot -eq $pair.Get.Slot + 1) "$propertyName getter/setter types or relative slots disagree."
    Assert-Contract ($pair.Get.Property -ceq $pair.Set.Property) "$propertyName getter/setter property names differ in case."
}
$exports = @(1..12 | ForEach-Object {
    Assert-Contract ($exportsBySlot.ContainsKey($_)) "Missing Light relative slot $_."
    $exportsBySlot[$_]
})
$lightSlots = @([ordered]@{ name = 'Light_Exists'; calling_convention = 'win64-stdcall'; return_type = 'i32'; parameters = @('ObjectHandle') })
foreach ($export in $exports)
{
    Assert-Contract ((Get-AbiType $export.Adapter.Native native) -ceq (Get-AbiType $export.Adapter.Managed managed)) "Native/managed adapter for $($export.Method) disagrees."
    $result = if ($export.Getter) { Get-AbiType $export.Adapter.Native native } else { 'void' }
    $arguments = @('ObjectHandle')
    if (-not $export.Getter)
    {
        $arguments += Get-AbiType $export.Adapter.Native native
    }
    $lightSlots += [ordered]@{ name = "Light_$($export.Method)"; calling_convention = 'win64-stdcall'; return_type = $result; parameters = @($arguments) }
}
$nativeSlots = Get-TableSlots $nativeText native $lightSlots
$managedSlots = Get-TableSlots $managedText managed $lightSlots
Assert-Contract ($nativeSlots.Count -eq $slotCount -and $managedSlots.Count -eq $slotCount) 'The ABI-v34 migration must preserve all 187 function slots.'
$allNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$lightStart = -1
for ($index = 0; $index -lt $slotCount; $index++)
{
    $nativeSignature = Get-SlotSignature $nativeSlots[$index]
    $managedSignature = Get-SlotSignature $managedSlots[$index]
    Assert-Contract ($nativeSignature -ceq $managedSignature) "ABI slot $index mismatch: native '$nativeSignature', managed '$managedSignature'."
    Assert-Contract ($allNames.Add($nativeSlots[$index].name)) "Duplicate API slot '$($nativeSlots[$index].name)'."
    if ($nativeSlots[$index].name -ceq 'Light_Exists')
    {
        $lightStart = $index
    }
}
Assert-Contract ($lightStart -eq 85) 'The ABI-v34 migration must retain Light at absolute function slot 85.'

# Only these POD layouts are migrated. Legacy physics/audio/other POD layouts
# remain outside this fingerprint; their names/signatures are checked above.
$layouts = @(
    'header:i32 version@0;i32 structSize@4;size=8',
    'ObjectHandle:size=8;align=4;u32 index/Index@0;u32 generation/Generation@4',
    'Color4/Float4:size=16;align=4;f32 x/R@0;f32 y/G@4;f32 z/B@8;f32 w/A@12',
    'math::color:size=16;align=4;f32 r@0;f32 g@4;f32 b@8;f32 a@12;copy-conversion',
    'LightType:native=u16;managed=i32;DirectionalLight/Directional=0;PointLight/Point=1;SpotLight/Spot=2;reject-outside=0..2',
    'LightStatus:native=u16;managed=i32;Disabled=0;Enabled=1;StaticShadows=2;reject-outside=0..2',
    "ScriptLightApi:size=104;align=8;relative-slots=13;absolute-start=$lightStart",
    'ScriptApiTable:pointer-width=64;slots=187;fingerprint=u64@1504;size=1512;align=8'
)
$canonicalLines = @('CreatorEngine.ScriptBindings/1', "api-version=$apiVersion", 'target=x86_64-pc-windows-msvc', 'calling-convention=win64-stdcall') + $layouts
for ($index = 0; $index -lt $nativeSlots.Count; $index++)
{
    $canonicalLines += "$index|$(Get-SlotSignature $nativeSlots[$index])"
}
foreach ($export in $exports)
{
    $canonicalLines += "Light-adapter|$($export.Slot)|$($export.Property)|$($export.Method)|$($export.Adapter.Key)"
}
$canonical = ($canonicalLines -join "`n") + "`n"
$digestHex = Get-BytesHash ($utf8.GetBytes($canonical))
$fingerprintHex = $digestHex.Substring(0, 16)
$lightOffset = 8 + 8 * $lightStart
$banner = '// <auto-generated> Tools/ScriptBindings/Generate-ScriptBindings.ps1; do not edit. </auto-generated>'

$header = [Text.StringBuilder]::new()
Add-Line $header $banner
Add-Line $header '#pragma once'
Add-Line $header '// Included inside ClrHost.cpp anonymous namespace, after boundary POD declarations.'
Add-Line $header "    constexpr std::uint64_t CreatorScriptBindingsFingerprint = 0x${fingerprintHex}ULL;"
Add-Line $header '    static_assert(CreatorScriptApiVersion == 34);'
Add-Line $header '    static_assert(sizeof(void*) == 8 && sizeof(int) == 4 && sizeof(float) == 4);'
Add-Line $header '    static_assert(std::is_standard_layout_v<ScriptObjectHandle> && std::is_trivially_copyable_v<ScriptObjectHandle>);'
Add-Line $header '    static_assert(sizeof(ScriptObjectHandle) == 8 && alignof(ScriptObjectHandle) == 4);'
Add-Line $header '    static_assert(offsetof(ScriptObjectHandle, index) == 0 && offsetof(ScriptObjectHandle, generation) == 4);'
Add-Line $header '    static_assert(std::is_same_v<decltype(ScriptObjectHandle::index), std::uint32_t>);'
Add-Line $header '    static_assert(std::is_same_v<decltype(ScriptObjectHandle::generation), std::uint32_t>);'
foreach ($pod in @(@{ Name = 'Float4'; Fields = @('x', 'y', 'z', 'w') }, @{ Name = 'math::color'; Fields = @('r', 'g', 'b', 'a') }))
{
    Add-Line $header "    static_assert(std::is_standard_layout_v<$($pod.Name)> && std::is_trivially_copyable_v<$($pod.Name)>);"
    Add-Line $header "    static_assert(sizeof($($pod.Name)) == 16 && alignof($($pod.Name)) == 4);"
    for ($index = 0; $index -lt 4; $index++)
    {
        Add-Line $header "    static_assert(offsetof($($pod.Name), $($pod.Fields[$index])) == $($index * 4));"
        Add-Line $header "    static_assert(std::is_same_v<decltype($($pod.Name)::$($pod.Fields[$index])), float>);"
    }
}
foreach ($enum in @('LightType', 'LightStatus'))
{
    Add-Line $header "    static_assert(sizeof($enum) == 2 && std::is_same_v<std::underlying_type_t<$enum>, std::uint16_t>);"
}
Add-Line $header '    static_assert(DirectionalLight == 0 && PointLight == 1 && SpotLight == 2);'
Add-Line $header '    static_assert(Disabled == 0 && Enabled == 1 && StaticShadows == 2);'
Add-Line $header ''
Add-Line $header '    struct ScriptLightApi'
Add-Line $header '    {'
Add-Line $header '        int (__stdcall* Exists)(ScriptObjectHandle handle);'
foreach ($export in $exports)
{
    $return = if ($export.Getter) { $export.Adapter.Native } else { 'void' }
    $arguments = if ($export.Getter) { 'ScriptObjectHandle handle' } else { "ScriptObjectHandle handle, $($export.Adapter.Native) value" }
    Add-Line $header "        $return (__stdcall* $($export.Method))($arguments);"
}
Add-Line $header '    };'
Add-Line $header '    static_assert(std::is_standard_layout_v<ScriptLightApi> && std::is_trivially_copyable_v<ScriptLightApi>);'
Add-Line $header '    static_assert(sizeof(ScriptLightApi) == 13 * sizeof(void*) && alignof(ScriptLightApi) == 8);'
Add-Line $header '    static_assert(offsetof(ScriptLightApi, Exists) == 0);'
foreach ($export in $exports)
{
    Add-Line $header "    static_assert(offsetof(ScriptLightApi, $($export.Method)) == $($export.Slot) * sizeof(void*));"
}

$thunks = [Text.StringBuilder]::new()
Add-Line $thunks $banner
Add-Line $thunks '    static_assert(noexcept(ReportScriptBindingException("Light")));'
Add-Line $thunks '    int __stdcall Api_Light_Exists(ScriptObjectHandle handle) noexcept'
Add-Line $thunks '    {'
Add-Line $thunks '        try'
Add-Line $thunks '        {'
Add-Line $thunks '            return ResolveScriptComponent<LightComponent>(handle) != nullptr ? 1 : 0;'
Add-Line $thunks '        }'
Add-Line $thunks '        catch (...)'
Add-Line $thunks '        {'
Add-Line $thunks '            ReportScriptBindingException("Light_Exists");'
Add-Line $thunks '            return 0;'
Add-Line $thunks '        }'
Add-Line $thunks '    }'
foreach ($export in $exports)
{
    $adapter = $export.Adapter
    $return = if ($export.Getter) { $adapter.Native } else { 'void' }
    $arguments = if ($export.Getter) { 'ScriptObjectHandle handle' } else { "ScriptObjectHandle handle, $($adapter.Native) value" }
    Add-Line $thunks ''
    Add-Line $thunks "    $return __stdcall Api_Light_$($export.Method)($arguments) noexcept"
    Add-Line $thunks '    {'
    Add-Line $thunks '        try'
    Add-Line $thunks '        {'
    Add-Line $thunks '            auto* component = ResolveScriptComponent<LightComponent>(handle);'
    Add-Line $thunks '            if (component == nullptr)'
    Add-Line $thunks '            {'
    $fallback = if ($export.Getter) { "return $($adapter.NativeFallback);" } else { 'return;' }
    Add-Line $thunks "                $fallback"
    Add-Line $thunks '            }'
    if ($export.Getter)
    {
        if ($adapter.Key -ceq 'color')
        {
            Add-Line $thunks "            const auto value = component->$($export.Method)();"
            Add-Line $thunks '            return Float4{ value.r, value.g, value.b, value.a };'
        }
        elseif ($adapter.Key -ceq 'float')
        {
            Add-Line $thunks "            return component->$($export.Method)();"
        }
        else
        {
            Add-Line $thunks "            return static_cast<int>(component->$($export.Method)());"
        }
    }
    else
    {
        $valueExpression = 'value'
        if ($adapter.Key -ceq 'color')
        {
            $valueExpression = 'math::color{ value.x, value.y, value.z, value.w }'
        }
        elseif ($adapter.Key -cin @('LightType', 'LightStatus'))
        {
            Add-Line $thunks "            if (value < $($adapter.Minimum) || value > $($adapter.Maximum))"
            Add-Line $thunks '            {'
            Add-Line $thunks '                return;'
            Add-Line $thunks '            }'
            $valueExpression = "static_cast<$($adapter.Property)>(value)"
        }
        Add-Line $thunks "            component->$($export.Method)($valueExpression);"
    }
    Add-Line $thunks '        }'
    Add-Line $thunks '        catch (...)'
    Add-Line $thunks '        {'
    Add-Line $thunks "            ReportScriptBindingException(`"Light_$($export.Method)`");"
    Add-Line $thunks "            $fallback"
    Add-Line $thunks '        }'
    Add-Line $thunks '    }'
}
$fill = [Text.StringBuilder]::new()
Add-Line $fill $banner
Add-Line $fill '        g_apiTable.Light.Exists = &Api_Light_Exists;'
foreach ($export in $exports)
{
    Add-Line $fill "        g_apiTable.Light.$($export.Method) = &Api_Light_$($export.Method);"
}

$managed = [Text.StringBuilder]::new()
Add-Line $managed $banner
Add-Line $managed 'using System;'
Add-Line $managed 'using System.Runtime.InteropServices;'
Add-Line $managed ''
Add-Line $managed 'namespace CreatorEngine'
Add-Line $managed '{'
Add-Line $managed '    [StructLayout(LayoutKind.Sequential)]'
Add-Line $managed '    internal unsafe struct ScriptLightApi'
Add-Line $managed '    {'
Add-Line $managed '        public delegate* unmanaged[Stdcall]<ObjectHandle, int> Exists;'
foreach ($export in $exports)
{
    $types = if ($export.Getter) { "ObjectHandle, $($export.Adapter.Managed)" } else { "ObjectHandle, $($export.Adapter.Managed), void" }
    Add-Line $managed "        public delegate* unmanaged[Stdcall]<$types> $($export.Method);"
}
Add-Line $managed '    }'
Add-Line $managed ''
Add-Line $managed '    internal static unsafe class ScriptBindingsContract'
Add-Line $managed '    {'
Add-Line $managed "        internal const ulong Fingerprint = 0x${fingerprintHex}UL;"
Add-Line $managed '        internal const int FunctionSlotCount = 187;'
Add-Line $managed "        internal const int LightFirstSlot = $lightStart;"
Add-Line $managed '        private static bool IsUInt32<T>(T value) where T : unmanaged => typeof(T) == typeof(uint);'
Add-Line $managed '        private static bool IsFloat32<T>(T value) where T : unmanaged => typeof(T) == typeof(float);'
Add-Line $managed '        // Called once by Native.Bind, never by a component property access.'
Add-Line $managed '        internal static bool ValidateLayout()'
Add-Line $managed '        {'
Add-Line $managed '            // Check actual unmanaged storage, not the runtime marshaler''s view of function pointers.'
Add-Line $managed '            ScriptApiTable table = default;'
Add-Line $managed '            ScriptLightApi light = default;'
Add-Line $managed '            ObjectHandle handle = new ObjectHandle(0x11223344u, 0x55667788u);'
Add-Line $managed '            uint* handleWords = (uint*)&handle;'
Add-Line $managed '            Color4 color = new Color4(1f, 2f, 3f, 4f);'
Add-Line $managed '            float* colorWords = (float*)&color;'
$layoutChecks = @(
    'IntPtr.Size == 8', 'sizeof(ObjectHandle) == 8', 'sizeof(Color4) == 16', 'sizeof(LightType) == 4', 'sizeof(LightStatus) == 4',
    '(int)LightType.Directional == 0', '(int)LightType.Point == 1', '(int)LightType.Spot == 2',
    '(int)LightStatus.Disabled == 0', '(int)LightStatus.Enabled == 1', '(int)LightStatus.StaticShadows == 2',
    'IsUInt32(handle.Index)', 'IsUInt32(handle.Generation)',
    'IsFloat32(color.R)', 'IsFloat32(color.G)', 'IsFloat32(color.B)', 'IsFloat32(color.A)',
    'handleWords[0] == 0x11223344u', 'handleWords[1] == 0x55667788u',
    'colorWords[0] == 1f', 'colorWords[1] == 2f', 'colorWords[2] == 3f', 'colorWords[3] == 4f',
    '(byte*)&color.R - (byte*)&color == 0', '(byte*)&color.G - (byte*)&color == 4',
    '(byte*)&color.B - (byte*)&color == 8', '(byte*)&color.A - (byte*)&color == 12',
    'sizeof(ScriptLightApi) == 104', '(byte*)&light.Exists - (byte*)&light == 0',
    'sizeof(ScriptApiTable) == 1512',
    '(byte*)&table.Version - (byte*)&table == 0',
    '(byte*)&table.StructSize - (byte*)&table == 4',
    '(byte*)&table.Camera_GetPrimaryHandle - (byte*)&table == 680',
    "(byte*)&table.Light - (byte*)&table == $lightOffset",
    '(byte*)&table.Mesh_Exists - (byte*)&table == 792',
    '(byte*)&table.AbiFingerprint - (byte*)&table == 1504'
)
foreach ($export in $exports)
{
    $layoutChecks += "(byte*)&light.$($export.Method) - (byte*)&light == $($export.Slot * 8)"
}
for ($index = 0; $index -lt $layoutChecks.Count; $index++)
{
    $prefix = if ($index -eq 0) { 'return ' } else { '    ' }
    $suffix = if ($index -eq $layoutChecks.Count - 1) { ';' } else { ' &&' }
    Add-Line $managed "            $prefix$($layoutChecks[$index])$suffix"
}
Add-Line $managed '        }'
Add-Line $managed '    }'
Add-Line $managed '}'

$wrappers = [Text.StringBuilder]::new()
Add-Line $wrappers $banner
Add-Line $wrappers 'namespace CreatorEngine'
Add-Line $wrappers '{'
Add-Line $wrappers '    internal static unsafe partial class Native'
Add-Line $wrappers '    {'
Add-Line $wrappers '        public static bool LightExists(ObjectHandle handle)'
Add-Line $wrappers '            => Entered() && _api.Light.Exists != null && _api.Light.Exists(handle) != 0;'
foreach ($export in $exports)
{
    Add-Line $wrappers ''
    if ($export.Getter)
    {
        Add-Line $wrappers "        public static $($export.Adapter.Managed) Light$($export.Method)(ObjectHandle handle)"
        Add-Line $wrappers "            => Entered() && _api.Light.$($export.Method) != null ? _api.Light.$($export.Method)(handle) : $($export.Adapter.ManagedFallback);"
    }
    else
    {
        Add-Line $wrappers "        public static void Light$($export.Method)(ObjectHandle handle, $($export.Adapter.Managed) value)"
        Add-Line $wrappers '        {'
        Add-Line $wrappers "            if (Entered() && _api.Light.$($export.Method) != null)"
        Add-Line $wrappers '            {'
        Add-Line $wrappers "                _api.Light.$($export.Method)(handle, value);"
        Add-Line $wrappers '            }'
        Add-Line $wrappers '        }'
    }
}
Add-Line $wrappers '    }'
Add-Line $wrappers '}'
$facade = [Text.StringBuilder]::new()
Add-Line $facade $banner
Add-Line $facade 'namespace CreatorEngine'
Add-Line $facade '{'
Add-Line $facade '    public sealed partial class LightComponent'
Add-Line $facade '    {'
foreach ($export in $exports)
{
    if (-not $export.Getter)
    {
        continue
    }
    $pair = $properties[$export.Property]
    $getCast = if ($export.Adapter.Key -cin @('LightType', 'LightStatus')) { "($($export.Adapter.Property))" } else { '' }
    $setCast = if ($export.Adapter.Key -cin @('LightType', 'LightStatus')) { '(int)' } else { '' }
    Add-Line $facade "        public $($export.Adapter.Property) $($export.Property)"
    Add-Line $facade '        {'
    Add-Line $facade "            get => ${getCast}Native.Light$($pair.Get.Method)(OwnerHandle);"
    Add-Line $facade "            set => Native.Light$($pair.Set.Method)(OwnerHandle, ${setCast}value);"
    Add-Line $facade '        }'
    Add-Line $facade ''
}
Add-Line $facade '    }'
Add-Line $facade '}'

$outputs = [ordered]@{
    'ScriptLightApi.g.h' = $header.ToString()
    'ScriptLightApi.Thunks.g.inc' = $thunks.ToString()
    'ScriptLightApi.Fill.g.inc' = $fill.ToString()
    'ScriptLightApi.g.cs' = $managed.ToString()
    'Native.Light.g.cs' = $wrappers.ToString()
    'LightComponent.g.cs' = $facade.ToString()
}
$inputHashes = [ordered]@{
    declarations = Get-BytesHash $declarationsBytes
    native_source = Get-BytesHash $nativeBytes
    managed_source = Get-BytesHash $managedBytes
    generator = Get-BytesHash $generatorBytes
}
$outputHashes = [ordered]@{}
foreach ($file in $outputs.Keys)
{
    $outputHashes[$file] = Get-BytesHash ($utf8.GetBytes($outputs[$file].Replace("`r`n", "`n")))
}
$contract = [ordered]@{
    format = 'creator.script-bindings'; version = 1; api_version = $apiVersion
    target = 'x86_64-pc-windows-msvc'; pointer_width = 64; calling_convention = 'win64-stdcall'
    fingerprint = "0x$fingerprintHex"; fingerprint_algorithm = 'SHA-256 first 64 bits, big-endian'
    canonical_sha256 = $digestHex; canonical_contract = $canonical
    input_hashes = $inputHashes; output_hashes = $outputHashes
    function_slot_count = $slotCount; light_first_slot = $lightStart; light_slot_count = $lightSlotCount
    table_size = 1512; fingerprint_offset = 1504
    layout_scope = 'ObjectHandle, Float4/Color4, math::color copy adapter, Light enums and API table envelope only; legacy POD field layouts are not migrated.'
    layouts = $layouts; slots = $nativeSlots
    exports = @($exports | ForEach-Object {
        [ordered]@{ relative_slot = $_.Slot; property = $_.Property; method = $_.Method; adapter = $_.Adapter.Key; cpp_signature = $_.Declaration.signature }
    })
}
$outputs['ScriptBindings.contract.json'] = ($contract | ConvertTo-Json -Depth 100) + "`n"
foreach ($file in $outputs.Keys)
{
    $destination = [IO.Path]::GetFullPath([IO.Path]::Combine($outputPath, $file))
    Assert-Contract (-not $protectedPaths.Contains($destination)) "Output '$destination' would overwrite an input."
    Assert-Contract (-not [IO.Directory]::Exists($destination)) "Output '$destination' is a directory."
}

# The build serializes this producer. Each replacement is atomic and unchanged
# content retains its timestamp; consumers run only after the target succeeds.
[void][IO.Directory]::CreateDirectory($outputPath)
foreach ($file in $outputs.Keys)
{
    $destination = [IO.Path]::Combine($outputPath, $file)
    $content = $outputs[$file].Replace("`r`n", "`n")
    if ([IO.File]::Exists($destination) -and $utf8.GetString([IO.File]::ReadAllBytes($destination)) -ceq $content)
    {
        continue
    }
    $temporary = "$destination.$([Guid]::NewGuid().ToString('N')).tmp"
    try
    {
        [IO.File]::WriteAllText($temporary, $content, $utf8)
        [IO.File]::Move($temporary, $destination, $true)
    }
    finally
    {
        if ([IO.File]::Exists($temporary))
        {
            [IO.File]::Delete($temporary)
        }
    }
}
Write-Output "Generated Light bindings: $slotCount checked API slots, fingerprint 0x$fingerprintHex."

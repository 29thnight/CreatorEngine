[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$ManifestPath,
    [switch]$ValidateManifestOnly
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project = Join-Path $PSScriptRoot 'ScriptNativeTableProbe/ScriptNativeTableProbe.fsproj'
$probe = Join-Path $PSScriptRoot 'ScriptNativeTableProbe'
$output = Join-Path $probe "bin/$Configuration/net10.0"
$proof = Join-Path $repo "Engine/Scripting/Bindings/obj/$Configuration/native-bindings/reflgen_creator_script_native.cpp"
if (!$ManifestPath) {
    $ManifestPath = Join-Path $repo "Engine/Scripting/Bindings/obj/$Configuration/native-bindings/reflgen_creator_script_native.json"
}

function Assert-NativeTableContract([string]$Path) {
    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($manifest.format -cne 'reflgen.fsharp-native' -or $manifest.version -ne 1 -or
        $manifest.module -cne 'creator_script' -or $manifest.namespace -cne 'CreatorEngine.Native' -or
        $manifest.pointer_width -ne 64 -or @($manifest.tables).Count -ne 1 -or
        $manifest.tables[0].name -cne 'ScriptApi') {
        throw 'Expected the direct CreatorEngine.Native 64-bit ScriptApi manifest.'
    }
    $table = $manifest.tables[0]
    $fields = @($table.fields)
    if ($fields.Count -lt 3 -or $fields[0].name -cne 'version' -or $fields[0].offset -ne 0 -or
        $fields[1].name -cne 'structSize' -or $fields[1].offset -ne 4) {
        throw 'The native table must begin with the version and structSize header.'
    }
    $nativeFields = @($fields | Select-Object -Skip 2 | ForEach-Object { $_.name })
    $source = Get-Content (Join-Path $repo 'Engine/SceneRuntime/ScriptApiTable.h') -Raw
    $start = $source.IndexOf('struct ScriptApiTable', [StringComparison]::Ordinal)
    $end = if ($start -ge 0) { $source.IndexOf('using ScriptApiBinding', $start, [StringComparison]::Ordinal) } else { -1 }
    if ($start -lt 0 -or $end -le $start) { throw 'The canonical native API table declaration was not found.' }
    $declaration = $source.Substring($start, $end - $start)
    $sourceFields = @([regex]::Matches($declaration, '\(__stdcall\s*\*\s*(\w+)\s*\)') |
        ForEach-Object { $_.Groups[1].Value })
    if ($nativeFields.Count -ne $sourceFields.Count) {
        throw "Native source and generated table slot counts differ: $($sourceFields.Count) and $($nativeFields.Count)."
    }
    for ($index = 0; $index -lt $nativeFields.Count; ++$index) {
        if ($nativeFields[$index] -cne $sourceFields[$index] -or
            $fields[$index + 2].offset -ne (8 + 8 * $index)) {
            throw "Generated native slot $index differs from ScriptApiTable.h: $($nativeFields[$index])."
        }
    }
    $versionSource = Get-Content (Join-Path $repo 'Engine/Utility_Framework/Runtime/ScriptApiVersion.h') -Raw
    $version = [regex]::Match($versionSource, 'CreatorScriptApiVersion\s*=\s*(\d+)')
    if (!$version.Success -or [int]$version.Groups[1].Value -lt 49 -or
        [int]$version.Groups[1].Value -ne $table.version -or
        $table.size -ne (8 + 8 * $nativeFields.Count)) {
        throw 'Native source and generated ABI version or table size differ.'
    }
    foreach ($required in @('NativeCallback_BeginGeneration', 'NativeCallback_Register',
        'NativeCallback_Disable', 'NativeCallback_TakeRetired', 'NativeCallback_EndGeneration')) {
        if ($nativeFields -cnotcontains $required) { throw "API 49 is missing the required callback slot: $required" }
    }
    [pscustomobject]@{ Manifest = $manifest; Slots = $nativeFields.Count; Version = $table.version }
}

if ($ValidateManifestOnly) {
    $contract = Assert-NativeTableContract $ManifestPath
    "SCRIPT_NATIVE_TABLE_SOURCE_OK slots=$($contract.Slots) version=$($contract.Version)"
    return
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC C++ tools are required.' }
Push-Location $repo
try {
    & dotnet build $project -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw 'F# table probe build failed.' }
    $contract = Assert-NativeTableContract $ManifestPath
    if ($contract.Manifest.target -notmatch '^x86_64-.*windows') {
        throw 'The Windows host probe requires a manifest generated for the Windows x64 target.'
    }
    # The native proof below compiles sizeof/offsetof/type checks from the same
    # manifest. Program.fs independently compares that manifest with the loaded
    # F# ScriptApi properties, constants and the provider's ABI fingerprint.
    "SCRIPT_NATIVE_TABLE_SOURCE_OK slots=$($contract.Slots) version=$($contract.Version)"
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    $native = Join-Path $probe 'native.cpp'
    $nativeDll = Join-Path $output 'ScriptNativeProvider.dll'
    $command = 'call "{0}" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /LD "{1}" "{2}" /Fe:"{3}" /Fo:"{4}\\"' -f $vcvars, $native, $proof, $nativeDll, $output
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Native table probe build failed.' }
    & dotnet (Join-Path $output 'ScriptNativeTableProbe.dll') $ManifestPath
    if ($LASTEXITCODE -ne 0) { throw 'Native table probe failed.' }
    $hostSource = Join-Path $probe 'hostfxr_probe.cpp'
    $hostExe = Join-Path $output 'ScriptNativeHostProbe.exe'
    $hostInclude = Join-Path $repo 'ThirdParty/DotNetHost/include'
    $hostLib = Join-Path $repo 'ThirdParty/DotNetHost/lib/nethost.lib'
    $command = 'call "{0}" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /I"{1}" "{2}" "{3}" /Fe:"{4}" /Fo:"{5}\\"' -f $vcvars, $hostInclude, $hostSource, $hostLib, $hostExe, $output
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw 'hostfxr probe build failed.' }
    $managed = Join-Path $repo "Bin/x64-$Configuration/Managed"
    $previousPath = $env:PATH
    try {
        $env:PATH = "$(Join-Path $repo 'ThirdParty/DotNetHost/bin');$previousPath"
        $game = Join-Path $PSScriptRoot "ScriptNativeGeneratorProbe/bin/$Configuration/net10.0/ScriptNativeGeneratorProbe.dll"
        & $hostExe (Join-Path $managed 'CreatorEngine.NativeBindings.runtimeconfig.json') (Join-Path $managed 'CreatorEngine.NativeBindings.dll') $nativeDll $game
        if ($LASTEXITCODE -ne 0) { throw "hostfxr probe failed: $LASTEXITCODE" }
    } finally {
        $env:PATH = $previousPath
    }
} finally {
    Pop-Location
}

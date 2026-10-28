[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project = Join-Path $PSScriptRoot 'ScriptNativeTableProbe/ScriptNativeTableProbe.fsproj'
$probe = Join-Path $PSScriptRoot 'ScriptNativeTableProbe'
$output = Join-Path $probe "bin/$Configuration/net10.0"
$proof = Join-Path $repo "ScriptNative/obj/$Configuration/native-bindings/reflgen_creator_script_native.cpp"
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC C++ tools are required.' }
Push-Location $repo
try {
    & dotnet build $project -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw 'F# table probe build failed.' }
    $manifest = Get-Content (Join-Path $repo "ScriptNative/obj/$Configuration/native-bindings/reflgen_creator_script_native.json") -Raw | ConvertFrom-Json
    $nativeFields = @($manifest.tables[0].fields | Select-Object -Skip 2 | ForEach-Object { $_.name })
    $coreSource = Get-Content (Join-Path $repo 'ScriptCore/Native.cs') -Raw
    $coreStart = $coreSource.IndexOf('internal unsafe struct ScriptApiTable')
    $coreEnd = $coreSource.IndexOf('internal static unsafe class Native', $coreStart)
    if ($coreStart -lt 0 -or $coreEnd -lt 0) { throw 'ScriptCore table declaration was not found.' }
    $coreTable = $coreSource.Substring($coreStart, $coreEnd - $coreStart)
    $managedFields = @([regex]::Matches($coreTable, 'public\s+delegate\*\s+unmanaged\s*<[^>]+>\s+(\w+)\s*;') | ForEach-Object { $_.Groups[1].Value })
    if ($nativeFields.Count -ne $managedFields.Count) { throw "Native and ScriptCore slot counts differ: $($nativeFields.Count) and $($managedFields.Count)." }
    for ($index = 0; $index -lt $nativeFields.Count; ++$index) {
        if ($nativeFields[$index] -cne $managedFields[$index]) { throw "Native and ScriptCore slot $index differs: $($nativeFields[$index]) and $($managedFields[$index])." }
    }
    $expectedVersion = [regex]::Match($coreSource, 'ExpectedVersion\s*=\s*(\d+)')
    $runtimeSource = Get-Content (Join-Path $repo 'ScriptRuntime/Runtime.fs') -Raw
    $runtimeVersion = [regex]::Match($runtimeSource, 'member\s+_\.ScriptApi\s*=\s*(\d+)')
    if (!$expectedVersion.Success -or !$runtimeVersion.Success -or
        [int]$expectedVersion.Groups[1].Value -ne $manifest.tables[0].version -or
        [int]$runtimeVersion.Groups[1].Value -ne $manifest.tables[0].version) {
        throw 'Native, ScriptCore and F# runtime API versions differ.'
    }
    "SCRIPT_NATIVE_TABLE_ORDER_OK slots=$($nativeFields.Count) version=$($manifest.tables[0].version)"
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    $native = Join-Path $probe 'native.cpp'
    $nativeDll = Join-Path $output 'ScriptNativeProvider.dll'
    $command = 'call "{0}" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /LD "{1}" "{2}" /Fe:"{3}" /Fo:"{4}\\"' -f $vcvars, $native, $proof, $nativeDll, $output
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Native table probe build failed.' }
    & dotnet (Join-Path $output 'ScriptNativeTableProbe.dll')
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

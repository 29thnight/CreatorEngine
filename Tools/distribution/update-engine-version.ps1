[CmdletBinding()]
param([string]$Repository = (Join-Path $PSScriptRoot '../..'), [switch]$Check)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'EngineVersion.psm1')
$Repository = [IO.Path]::GetFullPath($Repository)
$metadata = Read-EngineVersionMetadata (Join-Path $Repository 'EngineVersion.json')
$local = if ($metadata.localDevelopment) { 'true' } else { 'false' }
# Only explicit version edits change these files. No clock, Git state, build ID or channel is compiled in.
# Channel promotion is manifest metadata and preserves the payload bytes.
$header = @"
#pragma once
// Generated from EngineVersion.json by Tools/distribution/update-engine-version.ps1.
// Commit this file with the explicit version edit; builds check it without rewriting source.
namespace CreatorEngineVersion
{
    inline constexpr char ProductName[] = "$($metadata.productName)";
    inline constexpr char FeatureRelease[] = "$($metadata.featureRelease)";
    inline constexpr char Build[] = "$($metadata.version)";
    inline constexpr bool LocalDevelopment = $local;
}
"@
$numeric = $metadata.version.Replace('.', ',')
$productDisplay = $metadata.productName
if ($metadata.featureRelease) { $productDisplay += " - Version $($metadata.featureRelease)" }
elseif ($metadata.localDevelopment) { $productDisplay += ' - Local development (unassigned)' }
$properties = @"
<Project>
  <!-- Generated from EngineVersion.json; channel and build ID remain manifest metadata. -->
  <PropertyGroup>
    <CreatorEngineBuildVersion>$($metadata.version)</CreatorEngineBuildVersion>
    <CreatorEngineProductDisplay>$productDisplay</CreatorEngineProductDisplay>
  </PropertyGroup>
</Project>
"@
$resource = @"
// Generated from EngineVersion.json. Channel and distribution ID are manifest metadata.
#include <windows.h>
1 VERSIONINFO
 FILEVERSION $numeric
 PRODUCTVERSION $numeric
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0
 FILEOS VOS_NT_WINDOWS32
#ifdef CREATOR_ENGINE_RUNTIME_HOST
 FILETYPE VFT_DLL
#else
 FILETYPE VFT_APP
#endif
 FILESUBTYPE 0
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "ProductName", "$productDisplay\0"
            VALUE "ProductVersion", "$($metadata.version)\0"
            VALUE "FileVersion", "$($metadata.version)\0"
            VALUE "FileDescription", "$productDisplay\0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
"@
foreach ($entry in @(
    @{ Path = 'Engine/Utility_Framework/EngineVersion.h'; Text = $header },
    @{ Path = 'EngineVersion.props'; Text = $properties },
    @{ Path = 'Tools/runtime/EngineVersion.rc'; Text = $resource }
)) {
    $path = Join-Path $Repository $entry.Path
    $expected = $entry.Text.Replace("`r`n", "`n").TrimEnd() + "`n"
    $actual = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllText($path).Replace("`r`n", "`n") } else { '' }
    if ($actual -cne $expected) {
        if ($Check) { throw "Stale version source: $($entry.Path). Run Tools/distribution/update-engine-version.ps1 and review the version edit." }
        [IO.File]::WriteAllText($path, $expected, [Text.UTF8Encoding]::new($false))
    }
}
Write-Host "ENGINE_VERSION_OK $($metadata.productName) / $($metadata.version) / $($metadata.channel) / local=$($metadata.localDevelopment)"

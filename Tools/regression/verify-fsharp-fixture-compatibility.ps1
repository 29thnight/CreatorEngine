[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo ('Build/Verification/FSharp/Compatibility-' + [guid]::NewGuid().ToString('N'))
$legacy = Join-Path $output 'Legacy'
$migrated = Join-Path $output 'FSharp'

New-Item -ItemType Directory -Force $legacy, $migrated | Out-Null

# Build the migrated catalog using the same explicit source order as product authoring.
& dotnet build "$repo/GameScripts/GameScripts.fsproj" -c $Configuration `
    "-p:CreatorScriptSourceRoot=$repo/GameScripts" -v:q *> "$output/fsharp-build.log"

if ($LASTEXITCODE) { throw "F# fixture build failed: $output/fsharp-build.log" }

$managedOutput = Join-Path $repo "Bin/x64-$Configuration/Managed/Scripts"
Copy-Item "$managedOutput/*" $migrated -Recurse

# Compile only corresponding historical C# inputs, into an isolated comparison DLL.
# These sources never enter the Editor's or packaged game's authoring build.
$legacySources = foreach ($source in Get-Content "$repo/GameScripts/compile.order") {
    $candidate = Join-Path "$repo/GameScripts" ([IO.Path]::ChangeExtension($source, '.cs'))
    if (Test-Path -LiteralPath $candidate) { $candidate }
}

$compileItems = ($legacySources | ForEach-Object { '<Compile Include="' + $_ + '" />' }) -join "`n"
$scriptCore = Join-Path $repo "Bin/x64-$Configuration/Managed/ScriptCore.dll"
$generator = Join-Path $repo "Build/Tools/Managed/ScriptCore.Generators/$Configuration/netstandard2.0/ScriptCore.Generators.dll"

@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>LegacyFixtures</AssemblyName>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <OutputPath>$legacy/Output/</OutputPath>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>
  <ItemGroup>
    $compileItems
    <Reference Include="ScriptCore"><HintPath>$scriptCore</HintPath><Private>false</Private></Reference>
    <Analyzer Include="$generator" />
  </ItemGroup>
</Project>
"@ | Set-Content "$legacy/LegacyFixtures.csproj" -Encoding utf8

& dotnet build "$legacy/LegacyFixtures.csproj" -c $Configuration -v:q *> "$output/legacy-build.log"
if ($LASTEXITCODE) { throw "Legacy comparison build failed: $output/legacy-build.log" }

& dotnet run --project "$PSScriptRoot/FSharpFixtureCompatibility" -c $Configuration -- `
    "$legacy/Output/LegacyFixtures.dll" "$migrated/GameScripts.dll" "$output/result.json" *> "$output/compare.log"

if ($LASTEXITCODE) { throw "Fixture compatibility failed: $output/compare.log" }

Get-Content "$output/compare.log"
"FSHARP_FIXTURE_COMPATIBILITY_OK evidence=$output"

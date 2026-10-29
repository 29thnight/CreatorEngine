[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
foreach($path in @('ScriptNative','ScriptNative.AudioValues','ScriptNative.FSharpRuntime','ScriptNative.GameScripts','ScriptCore','ScriptCore.Generators','ScriptRuntime','GameScripts/GameScripts.csproj','GameScripts/GameScripts.fsproj')){
    if(Test-Path -LiteralPath (Join-Path $repo $path)){throw "Retired scripting path remains: $path"}
}
$bindings=Join-Path $repo 'Engine/Scripting/Bindings'
$looseSources=@(Get-ChildItem -LiteralPath $bindings -File -Filter '*.fs' | Where-Object Name -ne 'AssemblyInfo.fs')
if($looseSources.Count){throw ('Binding sources must be grouped by responsibility: '+($looseSources.Name -join ', '))}
$scriptRoots=@('GameScripts','Engine/Scripting/Bindings','Engine/Scripting/AudioValues','Engine/Scripting/Authoring','Engine/Scripting/GameScripts','Dynamic_CPP/Assets/Script','Tools/regression')
$sourceFiles=@($scriptRoots | ForEach-Object {Get-ChildItem -LiteralPath (Join-Path $repo $_) -Recurse -File} | Where-Object {$_.FullName -notmatch '\\(bin|obj)\\'})
$obsolete=@($sourceFiles|Where-Object {$_.Extension -in @('.cs','.csproj')})
if($obsolete.Count){throw ('C# source/project remains in scripting or regression sources: '+($obsolete.FullName -join ', '))}
$productWiring=@('CreatorEngine.sln','Engine/SceneRuntime/ClrHost.cpp','Engine/SceneRuntime/ClrHost.h','Editor/EngineEntry/Scripting/EditorScriptAuthoring.cpp','Engine/SceneRuntime/SceneRuntime.vcxproj','Engine/SceneRuntime/Headless/SceneRuntimeHeadless.vcxproj','Editor/Editor.vcxproj','Tools/build/stage-headless.ps1')
$productWiring+=@(Get-ChildItem "$repo/BuildTool" -File -Filter '*.cs'|ForEach-Object FullName)
foreach($path in $productWiring){
    $absolute=if([IO.Path]::IsPathRooted($path)){$path}else{Join-Path $repo $path}
    if(Select-String -LiteralPath $absolute -Pattern 'ScriptCore\.dll|legacy-script-baseline|CREATOR_LEGACY_SCRIPT_BASELINE|CreatorLegacyScriptBaseline|UsesFSharpNative' -Quiet){throw "Retired backend wiring remains: $path"}
}
'FSHARP_ONLY_LAYOUT_OK runtimeProjects=4 legacyBackends=0 csharpSources=0'

[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$key = 'NativeCorpus-' + [guid]::NewGuid().ToString('N')
$output = Join-Path $repo "Build/Verification/$key"
New-Item -ItemType Directory -Path $output | Out-Null
& dotnet build "$repo/ScriptNative.GameScripts/ScriptNative.GameScripts.fsproj" -c $Configuration `
    "-p:CreatorScriptSourceRoot=$repo/GameScripts" "-p:CreatorNativeGameOutDir=$output/Managed/" `
    "-p:CreatorScriptBuildKey=$key" --nologo -v:q *> "$output/build.log"
if ($LASTEXITCODE) { throw "F# corpus compile failed: $output/build.log" }
$assembly = Join-Path $output 'Managed/GameScripts.dll'
$deps = Join-Path $output 'Managed/GameScripts.deps.json'
if (-not (Test-Path -LiteralPath $assembly) -or -not (Test-Path -LiteralPath $deps)) { throw 'Corpus assembly or dependencies are missing' }
if ((Get-Content -LiteralPath $deps -Raw) -match 'ScriptCore' -or (Test-Path -LiteralPath "$output/Managed/ScriptCore.dll")) {
    throw 'Corpus build contains a ScriptCore dependency'
}
$count = @(Get-Content -LiteralPath "$repo/GameScripts/compile.order" | Where-Object { $_.Trim() }).Count
Write-Output "SCRIPT_NATIVE_CORPUS_OK sources=$count configuration=$Configuration evidence=$output"

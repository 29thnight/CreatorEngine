[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceRoots = @('Engine', 'Editor', 'Tools') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$sources = @($sourceRoots | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Recurse -File -Include *.cpp, *.cxx, *.h, *.hpp
})
if ($sources.Count -eq 0) {
    throw 'yaml-cpp retirement gate inspected zero C++ sources.'
}

function Remove-CppComments([string]$Text) {
    $withoutBlocks = [regex]::Replace($Text, '(?s)/\*.*?\*/', '')
    return [regex]::Replace($withoutBlocks, '(?m)//.*$', '')
}

$violations = [Collections.Generic.List[string]]::new()
foreach ($file in $sources) {
    $text = [Text.Encoding]::UTF8.GetString(
        [IO.File]::ReadAllBytes($file.FullName))
    $code = Remove-CppComments $text
    foreach ($rule in @(
        @{ Pattern = '#\s*include\s*[<"]yaml-cpp/'; Name = 'include' },
        @{ Pattern = '\b(?:YAML|MetaYml)::'; Name = 'backend symbol' },
        @{ Pattern = '\bnamespace\s+MetaYml\b'; Name = 'namespace alias' },
        @{ Pattern = '\bBackendNodeDuringTransition\b'; Name = 'transition escape' }
    )) {
        if ($code -match $rule.Pattern) {
            $relative = $file.FullName.Substring($repoRoot.Length).TrimStart([char]92)
            $violations.Add("$($rule.Name): $relative")
        }
    }
}

$manifest = Join-Path $repoRoot 'vcpkg.json'
$buildScript = Join-Path $repoRoot 'Tools\build.ps1'
if ((Get-Content -LiteralPath $manifest -Raw) -match '"name"\s*:\s*"yaml-cpp"') {
    $violations.Add('manifest dependency: vcpkg.json')
}
if ((Get-Content -LiteralPath $buildScript -Raw) -match 'yaml-cppd?\.dll') {
    $violations.Add('runtime packaging: Tools\build.ps1')
}

$debugBinary = Join-Path $repoRoot 'Bin\x64-Debug\Editor\CreatorEditor.exe'
$importEvidence = 'not-built'
if (Test-Path -LiteralPath $debugBinary -PathType Leaf) {
    $dumpbin = Get-ChildItem -Path ${env:ProgramFiles},${env:ProgramFiles(x86)} `
        -Filter dumpbin.exe -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($dumpbin) {
        $imports = (& $dumpbin.FullName /dependents $debugBinary 2>&1 | Out-String)
        if ($imports -match 'yaml-cppd?\.dll') {
            $violations.Add('PE import: Bin\x64-Debug\Editor\CreatorEditor.exe')
        }
        $importEvidence = 'checked'
    } else {
        $importEvidence = 'dumpbin-unavailable'
    }
}

"sources=$($sources.Count) violations=$($violations.Count) peImports=$importEvidence"
if ($violations.Count -gt 0) {
    $violations | Sort-Object -Unique | ForEach-Object { "  $_" }
    exit 1
}

'전체 통과 — yaml-cpp include/symbol/manifest/runtime packaging이 0이다'
exit 0

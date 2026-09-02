[CmdletBinding()]
param(
    [string]$PlayerExe = '',
    [string]$EditorExe = ''
)

# PHASE 17 D1 local acceptance gate.
#
# This intentionally remains separate from the canonical D1 gate.  It is
# compatible with PowerShell 7.6 scalar pipeline behavior and strips C/C++
# comments before source-symbol checks so an explanatory `efsw` comment is not
# mistaken for a runtime dependency.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$failures = [Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Resolve-FirstExisting([string[]]$Candidates) {
    foreach ($candidate in @($Candidates)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return ''
}

function Remove-CxxComments([string]$Text) {
    $withoutBlocks = [regex]::Replace($Text, '/\*.*?\*/', '',
        [Text.RegularExpressions.RegexOptions]::Singleline)
    return [regex]::Replace($withoutBlocks, '//.*$', '',
        [Text.RegularExpressions.RegexOptions]::Multiline)
}

function Test-BinaryContains([string]$Path, [string]$Needle) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
    return $text.IndexOf($Needle, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function Format-RepoRelative([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '(없음)' }
    if ($Path.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    return $Path
}

if ([string]::IsNullOrWhiteSpace($PlayerExe)) {
    $PlayerExe = Resolve-FirstExisting @(
        (Join-Path $repoRoot 'Bin\x64-Debug\Player\Player.exe'),
        (Join-Path $repoRoot 'Bin\x64-Release\Player\Player.exe'))
} else {
    $PlayerExe = Resolve-FirstExisting @($PlayerExe)
}
if ([string]::IsNullOrWhiteSpace($EditorExe)) {
    $EditorExe = Resolve-FirstExisting @(
        (Join-Path $repoRoot 'Bin\x64-Debug\Editor\CreatorEditor.exe'),
        (Join-Path $repoRoot 'Bin\x64-Release\Editor\CreatorEditor.exe'))
} else {
    $EditorExe = Resolve-FirstExisting @($EditorExe)
}

# Prove that the filter removes both comment forms before trusting a zero count.
$filterProbe = "int alive = 1; // efsw`n/* ScanAndGenerateMissingMeta */`n"
$filteredProbe = Remove-CxxComments $filterProbe
if ($filteredProbe -match '(?i)efsw|ScanAndGenerateMissingMeta' -or
    $filteredProbe -notmatch 'int alive = 1;') {
    Add-Failure 'C/C++ comment filter selfcheck가 실패했다'
}

$engineEfsw = [Collections.Generic.List[string]]::new()
$commentOnlyEfsw = [Collections.Generic.List[string]]::new()
$scanSymbols = [Collections.Generic.List[string]]::new()
$sourceFiles = @(Get-ChildItem -Path (Join-Path $repoRoot 'Engine'),
        (Join-Path $repoRoot 'Player') -Recurse -File `
        -Include '*.h', '*.hpp', '*.cpp' -ErrorAction SilentlyContinue)
foreach ($source in $sourceFiles) {
    $raw = Get-Content -LiteralPath $source.FullName -Raw
    $code = Remove-CxxComments $raw
    if ($source.FullName.StartsWith((Join-Path $repoRoot 'Engine'),
            [StringComparison]::OrdinalIgnoreCase) -and $raw -match '(?i)\befsw\b') {
        if ($code -match '(?i)\befsw\b') {
            $engineEfsw.Add($source.FullName)
        } else {
            $commentOnlyEfsw.Add($source.FullName)
        }
    }
    if ($code -match '\bScanAndGenerateMissingMeta\b') {
        $scanSymbols.Add($source.FullName)
    }
}
if ($engineEfsw.Count -gt 0) {
    Add-Failure "Engine 코드에 efsw 참조 $($engineEfsw.Count)건: " +
        (($engineEfsw | ForEach-Object { Format-RepoRelative $_ }) -join ', ')
}
if ($scanSymbols.Count -gt 0) {
    Add-Failure "Engine/Player 코드에 ScanAndGenerateMissingMeta $($scanSymbols.Count)건: " +
        (($scanSymbols | ForEach-Object { Format-RepoRelative $_ }) -join ', ')
}

$playerProject = Join-Path $repoRoot 'Player\Player.vcxproj'
$references = @()
if (-not (Test-Path -LiteralPath $playerProject -PathType Leaf)) {
    Add-Failure "Player.vcxproj가 없다: $playerProject"
} else {
    $projectText = Get-Content -LiteralPath $playerProject -Raw
    $references = @([regex]::Matches($projectText,
        '<ProjectReference\s+Include="([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value })
    if ($references.Count -eq 0) {
        Add-Failure 'Player ProjectReference를 하나도 읽지 못했다'
    }
    foreach ($forbidden in @('EngineEntry', 'EngineGUIWindow', 'ImGuiHelper', 'EditorRender')) {
        $hits = @($references | Where-Object {
            [IO.Path]::GetFileNameWithoutExtension($_) -eq $forbidden
        })
        if ($hits.Count -gt 0) {
            Add-Failure "Player가 Editor 프로젝트를 링크한다: $forbidden"
        }
    }
}

$binaryCompared = $false
if ([string]::IsNullOrWhiteSpace($PlayerExe)) {
    Add-Failure 'Player.exe를 찾지 못했다'
} elseif ([string]::IsNullOrWhiteSpace($EditorExe)) {
    Add-Failure '대조군 CreatorEditor.exe를 찾지 못했다'
} else {
    $playerHasEfsw = Test-BinaryContains -Path $PlayerExe -Needle 'efsw'
    $editorHasEfsw = Test-BinaryContains -Path $EditorExe -Needle 'efsw'
    $binaryCompared = $true
    if ($playerHasEfsw) { Add-Failure "Player.exe가 efsw를 참조한다: $PlayerExe" }
    if (-not $editorHasEfsw) {
        Add-Failure "CreatorEditor.exe 대조군에서 efsw를 찾지 못했다: $EditorExe"
    }
}

# Force an array after the pipeline.  PowerShell 7.6 unwraps a single directory
# to a scalar string, whose .Count access fails under StrictMode.
$playerDirs = @(@(
    (Join-Path $repoRoot 'Bin\x64-Debug\Player'),
    (Join-Path $repoRoot 'Bin\x64-Release\Player')) |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container })
foreach ($directory in $playerDirs) {
    $stray = @(Get-ChildItem -LiteralPath $directory -Filter 'efsw*.dll' -File `
        -ErrorAction SilentlyContinue)
    if ($stray.Count -gt 0) {
        Add-Failure "Player 배포에 efsw DLL이 있다: $($stray[0].FullName)"
    }
}

"phase17-local-d1 powershell=$($PSVersionTable.PSVersion)"
"playerExe=$(Format-RepoRelative $PlayerExe)"
"editorExe=$(Format-RepoRelative $EditorExe)"
"engineEfswCodeRefs=$($engineEfsw.Count) commentOnlyEfswRefs=$($commentOnlyEfsw.Count) scanSymbolCodeRefs=$($scanSymbols.Count)"
"projectReferences=$($references.Count) binaryCompared=$binaryCompared playerDirsChecked=$($playerDirs.Count)"

if ($failures.Count -gt 0) {
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — PowerShell 7.6 로컬 환경에서 Player runtime hygiene 계약과 Editor 대조군이 유효하다'
exit 0

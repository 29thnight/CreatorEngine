[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$mathfHeader = Join-Path $repoRoot 'Engine\Utility_Framework\Core.Mathf.h'
$sourceRoots = @(
    (Join-Path $repoRoot 'Engine'),
    (Join-Path $repoRoot 'Editor')
)
$extensions = @('.h', '.hpp', '.inl', '.cpp')

# Latin-1 preserves every ASCII source token byte-for-byte even in legacy
# CP949 files. The check only inspects ASCII C++ identifiers.
$latin1 = [Text.Encoding]::GetEncoding('iso-8859-1')
$usingPattern = [regex]::new(
    '(?m)^[ \t]*using[ \t]+namespace[ \t]+DirectX[ \t]*;',
    [Text.RegularExpressions.RegexOptions]::CultureInvariant)
$directXTokenPattern = [regex]::new(
    '(?<![A-Za-z0-9_:])(' +
    'XM(?:MATRIX|VECTOR|FLOAT[234](?:X[234])?|INT[234]|UINT[234])|' +
    'XM(?:Convert|Load|Matrix|Plane|Quaternion|Store|Vector)[A-Za-z0-9_]*|' +
    'XM_[A-Z0-9_]+|' +
    'Bounding(?:Box|Sphere|Frustum|OrientedBox)|ContainmentType|TriangleTests' +
    ')(?![A-Za-z0-9_])',
    [Text.RegularExpressions.RegexOptions]::CultureInvariant)

if (-not (Test-Path -LiteralPath $mathfHeader -PathType Leaf)) {
    throw "Core.Mathf.h was not found: $mathfHeader"
}

$mathfText = $latin1.GetString([IO.File]::ReadAllBytes($mathfHeader))
if ($usingPattern.IsMatch($mathfText)) {
    throw 'Core.Mathf.h must not export using namespace DirectX.'
}

$violations = [Collections.Generic.List[string]]::new()
$sourceFileCount = 0
$localUsingFileCount = 0

foreach ($file in Get-ChildItem -LiteralPath $sourceRoots -Recurse -File) {
    if ($extensions -notcontains $file.Extension.ToLowerInvariant()) {
        continue
    }

    ++$sourceFileCount
    $text = $latin1.GetString([IO.File]::ReadAllBytes($file.FullName))

    # A local implementation-file using is explicit and no longer leaks through
    # Core.Mathf.h. M1 guards the transitive dependency; local cleanup is separate.
    if ($usingPattern.IsMatch($text)) {
        ++$localUsingFileCount
        continue
    }

    $lineNumber = 0
    foreach ($line in [regex]::Split($text, "`r?`n")) {
        ++$lineNumber
        $trimmed = $line.TrimStart()
        if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*')) {
            continue
        }

        $match = $directXTokenPattern.Match($line)
        if (-not $match.Success) {
            continue
        }

        $relativePath = [IO.Path]::GetRelativePath($repoRoot, $file.FullName)
        $violations.Add(('{0}:{1}: unqualified {2}' -f
            $relativePath, $lineNumber, $match.Value))
    }
}

if ($violations.Count -gt 0) {
    $details = $violations -join [Environment]::NewLine
    throw "Unqualified DirectX identifiers still depend on transitive namespace import:`n$details"
}

Write-Host (('[DIRECTX NAMESPACE] passed: {0} source files, ' +
    '{1} files with explicit local using.') -f
    $sourceFileCount, $localUsingFileCount) -ForegroundColor Green

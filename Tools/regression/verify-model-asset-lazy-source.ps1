param()

# Structural guards only. This does not replace a compiled runtime probe.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$path = Join-Path $repoRoot 'Engine/RenderEngine/AssetDepot/ModelAssetRuntime.cpp'
$text = Get-Content -LiteralPath $path -Raw
# Exclude comments so a descriptive promise cannot satisfy an executable check.
$text = [regex]::Replace($text, '(?s)/\*.*?\*/|//[^\r\n]*', '')

function Region([string]$start, [string]$end) {
    $first = $text.IndexOf($start, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Model runtime region missing: $start" }
    $last = $text.IndexOf($end, $first + $start.Length, [StringComparison]::Ordinal)
    if ($last -lt 0) { throw "Model runtime region end missing: $end" }
    return $text.Substring($first, $last - $first)
}

function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$resolve = Region 'bool ResolveModelLoadable(' 'Error ReadModelBytes('
$descriptor = Region 'DecodeModelDescriptor(' 'template<class T>'
$reader = Region 'Error ReadModelBytes(' 'DecodeModelSkeleton('
$request = Region 'DataSystem::RequestResolvedModelAssetAsync(' 'DataSystem::StartModelAssetWorkLocked('

foreach ($metadata in @($resolve, $descriptor)) {
    Require ($metadata -notmatch '\b(?:CaptureArtifactSource|CaptureArtifact|ReadModelBytes|ReadAt|Size|PinModelBacking|fopen|open)\s*\(') `
        'Descriptor/loadable metadata path opened or read child artifact backing'
}
Require ($resolve -match 'catalog\.Find\(reference, child\)') 'Loadable lookup is not captured metadata'
Require ($resolve -match '(?s)status == model_cooked::AssetLookupStatus::NotMounted\)\s*\{\s*child\.entry\.asset = reference;\s*child\.blob\.kind = reference\.kind;\s*child\.resolverRevision = catalog\.ResolverRevision\(\);\s*return true;') `
    'Missing Loadable is not retained as a typed absence at the captured revision'
Require ($resolve -match 'status != model_cooked::AssetLookupStatus::Found \|\| !SupportedModelRepresentation\(child\)') `
    'Absent Loadable and incompatible present metadata were conflated'
Require ($descriptor -match 'result->clips\.reserve\(result->summary\.clips\.size\(\)\)') `
    'Descriptor clip slots no longer preserve summary indexing'
Require ($descriptor -match 'result->clips\.push_back\(std::move\(child\)\)') `
    'Descriptor drops an unresolved Loadable clip slot'
Require ($descriptor -notmatch '\bcontinue\s*;|erase\s*\(') 'Descriptor skips or removes a missing clip slot'
Require ($descriptor -match 'child\.byteSource && !HasClipSkeletonDependency\(child, skeletonReference\)') `
    'Present clip metadata no longer checks its declared hard skeleton identity'

$missing = $request.IndexOf('exactGeneration && !resolved.byteSource', [StringComparison]::Ordinal)
$representation = $request.IndexOf('!SupportedModelRepresentation(resolved)', [StringComparison]::Ordinal)
$submit = $request.IndexOf('StartModelAssetWorkLocked<T>', [StringComparison]::Ordinal)
Require ($missing -ge 0 -and $representation -gt $missing -and $submit -gt $representation) `
    'Missing exact child is not handled before representation checks and work submission'
Require ($request.Substring($missing, $representation - $missing) -match 'return fail\(Status::Failed, Error::NotMounted\);') `
    'Missing exact child must report NotMounted'
$hardMissing = $request.IndexOf('exactGeneration && !skeleton.byteSource', [StringComparison]::Ordinal)
Require ($hardMissing -gt $representation -and $submit -gt $hardMissing) `
    'Missing captured hard skeleton is not checked before submission'
Require ($request.Substring($hardMissing, $submit - $hardMissing) -match 'return fail\(Status::Failed, Error::NotMounted\);') `
    'Missing captured hard skeleton must report NotMounted'
Require ($request -notmatch '(?:m_cookedCatalog|catalog)->Find\s*\(') `
    'Exact request silently resolves a Loadable from a newer catalog'
Require ($reader -match 'CaptureArtifactSource\(resolved\.byteSource, resolved\.blob\.artifactPath, failure\)') `
    'Selected payload worker no longer narrows its exact source before reading'
Require ($reader -match 'hash != resolved\.blob\.contentSha256') `
    'Selected payload worker no longer validates the captured content digest'

'model asset lazy source contract: PASS (structural checks only)'

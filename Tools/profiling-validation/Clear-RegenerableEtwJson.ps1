[CmdletBinding()]
param(
    [switch]$Delete
)

$ErrorActionPreference = 'Stop'
$artifactRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Artifacts'))
$captures = @(
    'container-reuse-20261021/after',
    'container-reuse-20261021/before',
    'container-reuse-isolated-20261021/off-a',
    'container-reuse-isolated-20261021/on-a',
    'gpu-marker-blob-20261022/off-a',
    'gpu-marker-blob-20261022/on-a',
    'node-declare-parallel-20261021/after',
    'node-declare-parallel-20261021/before',
    'owner-borrow-20261022/off-a',
    'owner-borrow-20261022/on-a',
    'ownership-update-20261022/off-a',
    'ownership-update-20261022/on-a',
    'packet-group-reuse-20261021/on-a',
    'packet-group-reuse-final-20261021/off-a',
    'packet-group-reuse-final-20261021/on-a',
    'scene-preparation-ownership-20261021/off-a',
    'scene-preparation-ownership-20261021/on-a'
)

function Assert-ArtifactPath([string]$Path)
{
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($artifactRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Path is outside Artifacts: $absolute"
    }
    $cursor = $absolute
    while ($cursor)
    {
        if (Test-Path -LiteralPath $cursor)
        {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
            {
                throw "Reparse point is not allowed: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

# Preflight every capture before deleting any file. No directory is deleted.
$targets = [System.Collections.Generic.List[object]]::new()
$summary = @()
foreach ($capture in $captures)
{
    $parts = $capture.Split('/')
    $measurement = Join-Path $artifactRoot $parts[0]
    $etl = Join-Path $measurement ($parts[1] + '/creator-robot.etl')
    $export = Join-Path $measurement ('json/' + $parts[1] + '/etw/all-events')
    Assert-ArtifactPath $etl
    Assert-ArtifactPath $export
    if (-not (Test-Path -LiteralPath $etl -PathType Leaf))
    {
        throw "Original ETL is missing; nothing will be deleted: $etl"
    }
    if ((Get-Item -LiteralPath $etl).Length -le 0)
    {
        throw "Original ETL is empty: $etl"
    }
    $files = @()
    if (Test-Path -LiteralPath $export -PathType Container)
    {
        $files = @(Get-ChildItem -LiteralPath $export -File -Force |
            Where-Object { $_.Name -cmatch '^lines-[0-9]+\.json$' })
    }
    foreach ($file in $files)
    {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point is not allowed: $($file.FullName)" }
        $targets.Add([pscustomobject]@{
            Path = $file.FullName
            Bytes = $file.Length
            LastWriteUtc = $file.LastWriteTimeUtc
            OriginalEtl = $etl
        })
    }
    $bytes = ($files | Measure-Object Length -Sum).Sum
    $summary += [pscustomobject]@{
        Capture = $capture
        Files = $files.Count
        GiB = [math]::Round($bytes / 1GB, 3)
    }
}

$summary | Format-Table -AutoSize
$totalBytes = [long](($targets | Measure-Object Bytes -Sum).Sum)
Write-Host ("Total: {0} files, {1:N3} GiB" -f $targets.Count, ($totalBytes / 1GB))
if (-not $Delete)
{
    Write-Host 'Preview only. Run again with -Delete to remove only these export shards.'
    return
}

$auditDirectory = Join-Path $artifactRoot 'artifact-maintenance-20261022'
Assert-ArtifactPath $auditDirectory
New-Item -ItemType Directory -Path $auditDirectory -Force | Out-Null
$auditPath = Join-Path $auditDirectory ('etw-json-cleanup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fffffff') + '.json')
$audit = [ordered]@{
    StartedUtc = [DateTime]::UtcNow.ToString('o')
    Status = 'planned'
    PlannedBytes = $totalBytes
    RemovedBytes = 0L
    RemovedFiles = 0
    Targets = $targets
}
$audit | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $auditPath -Encoding utf8
try
{
    foreach ($target in $targets)
    {
        Assert-ArtifactPath $target.Path
        Assert-ArtifactPath $target.OriginalEtl
        if (-not (Test-Path -LiteralPath $target.OriginalEtl -PathType Leaf))
        {
            throw "Original ETL disappeared: $($target.OriginalEtl)"
        }
        $current = Get-Item -LiteralPath $target.Path -Force
        if ($current.Length -ne $target.Bytes -or $current.LastWriteTimeUtc -ne $target.LastWriteUtc)
        {
            throw "Export changed after preflight: $($target.Path)"
        }
        Remove-Item -LiteralPath $target.Path -Force
        $audit.RemovedBytes += $target.Bytes
        $audit.RemovedFiles++
    }
    $audit.Status = 'completed'
}
catch
{
    $audit.Status = 'stopped'
    $audit['Error'] = $_.Exception.Message
    throw
}
finally
{
    $audit['FinishedUtc'] = [DateTime]::UtcNow.ToString('o')
    $audit | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $auditPath -Encoding utf8
    Write-Host ("Removed: {0} files, {1:N3} GiB. Audit: {2}" -f
        $audit.RemovedFiles, ($audit.RemovedBytes / 1GB), $auditPath)
}

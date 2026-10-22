param([Parameter(Mandatory=$true)][string]$SnapshotDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$directory=[IO.Path]::GetFullPath($SnapshotDirectory)
$paths=[Collections.Generic.List[string]]::new()
$paths.Add($directory)
$index=Join-Path $repo 'Artifacts/artifact-maintenance-20261022/pdb-references.json'
if(Test-Path -LiteralPath $index)
{
    $references=(Get-Content -LiteralPath $index -Raw|ConvertFrom-Json).references
    foreach($entry in $references)
    {
        $original=[IO.Path]::GetFullPath((Join-Path $repo $entry.path))
        if([IO.Path]::GetDirectoryName($original) -eq $directory)
        {
            $canonical=[IO.Path]::GetFullPath((Join-Path $repo $entry.canonical))
            if(!(Test-Path -LiteralPath $canonical))
            {
                throw "Preserved PDB missing: $canonical"
            }
            $parent=[IO.Path]::GetDirectoryName($canonical)
            if(!$paths.Contains($parent))
            {
                $paths.Add($parent)
            }
        }
    }
}
$paths -join ';'

#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [string]$Python = 'python',
    [ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceDirectory)
$rows = @()
foreach ($configuration in $Configurations)
{
    $identity = $null
    foreach ($mode in @('Off','On'))
    {
        $path = Join-Path $root "$configuration-$mode"
        $result = Get-Content "$path/result.json" -Raw | ConvertFrom-Json
        $binary = Get-Content "$path/compiler-binary-hashes.json" -Raw | ConvertFrom-Json
        $environment = Get-Content "$path/rg7-environment.json" -Raw | ConvertFrom-Json
        $expectedAliasing = if ($mode -eq 'On') { '1' } else { '0' }
        if ($environment.aliasing -ne $expectedAliasing -or $environment.extendedLifetimes -ne '0')
        {
            throw "Unexpected aliasing configuration: $path"
        }
        if (!$result.complete -or $result.failures.Count -ne 0 -or $result.exitCode -ne 0 -or
            !$result.validation.layerEnabled -or $result.validation.mode -ne 'gpu' -or
            $result.validation.problems -ne 0 -or $result.validation.droppedMessages -ne 0)
        {
            throw "Incomplete product run: $path"
        }
        $currentIdentity = "$($binary.exe)/$($binary.runtime)"
        if ($identity -and $identity -ne $currentIdentity)
        {
            throw "OFF/ON executable differs: $configuration"
        }
        $identity = $currentIdentity
        if ($result.sceneChange.before -eq $result.sceneChange.after -or
            $result.resize.before[0] -lt 1000 -or $result.resize.before[1] -lt 500 -or
            $result.resize.after[0] -eq $result.resize.before[0])
        {
            throw "Missing representative scene/extent change: $path"
        }
        $views = @()
        foreach ($target in @('scene','game','preview'))
        {
            $graph = Get-Content "$path/graph-$target.json" -Raw | ConvertFrom-Json
            $shared = @($graph.resources | Where-Object { $_.used -and $_.aliasGroup -ge 0 })
            $activations = @($graph.passes | ForEach-Object barriers | Where-Object aliasing)
            if (!$graph.ready -or @($graph.resources | Where-Object { $_.imported -and $_.aliasGroup -ge 0 }).Count)
            {
                throw "Unready view or imported/history sharing: $path/$target"
            }
            if ($mode -eq 'Off' -and ($shared.Count -ne 0 -or $activations.Count -ne 0))
            {
                throw "OFF graph contains heap sharing: $path/$target"
            }
            if ($shared.Count -ne $activations.Count)
            {
                throw "Member/activation mismatch: $path/$target"
            }
            foreach ($group in @($shared | Group-Object aliasGroup))
            {
                $members = @($group.Group | Sort-Object firstUse)
                for ($index = 1; $index -lt $members.Count; ++$index)
                {
                    if ($members[$index - 1].lastUse -ge $members[$index].firstUse)
                    {
                        throw "Shared view lifetimes overlap: $path/$target"
                    }
                }
            }
            $views += [ordered]@{target=$target; view=$graph.view; frame=$graph.frame;
                width=$graph.width; height=$graph.height; members=$shared.Count; activations=$activations.Count}
        }
        if (@($views.view | Select-Object -Unique).Count -ne 3 -or
            ($mode -eq 'On' -and ($views[0].members -eq 0 -or $views[1].members -eq 0)))
        {
            throw "Missing independent scene/game aliasing: $path"
        }
        $rows += [ordered]@{configuration=$configuration; mode=$mode; views=$views;
            sceneChange=$result.sceneChange; resize=$result.resize; validation=0; exitCode=0}
    }
    $off = Get-Content "$root/$configuration-Off/result.json" -Raw | ConvertFrom-Json
    $on = Get-Content "$root/$configuration-On/result.json" -Raw | ConvertFrom-Json
    & $Python (Join-Path $PSScriptRoot 'compare-material-captures.py') $off.captures[0].path $on.captures[0].path `
        "$root/comparison-$configuration.json" > "$root/comparison-$configuration.log"
    if ($LASTEXITCODE -ne 0)
    {
        throw "Representative OFF/ON capture mismatch: $configuration"
    }
}
[ordered]@{passed=$true; phaseComplete=$false; aliasingDefault='off'; runs=$rows;
    scope='Product view/scene/resize and controlled Editor capture parity; not peak VRAM or performance acceptance'} |
    ConvertTo-Json -Depth 30 | Set-Content "$root/rg7-product-result.json" -Encoding utf8
"RG7_PRODUCT_VIEWS_OK $root"

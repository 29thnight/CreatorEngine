param([Parameter(Mandatory)][string]$Work, [switch]$Apply)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$PSScriptRoot/CppContainerTokens.ps1"
$kinds = 'pair|tuple|make_pair|make_tuple|tie|apply|forward_as_tuple|get'
$pattern = $script:CppContainerProtected + '|(?<value>\bstd::(?<kind>' + $kinds + ')\b)'
$files = @(& git -C $repo -c core.quotepath=false ls-files --cached --others --exclude-standard -- Engine Editor Lattice Player HeadlessHost |
    Where-Object { $_ -match '\.(h|hpp|cpp|ixx|inl)$' -and $_ -notlike 'Engine/Containers/*' } | Sort-Object -Unique)
$changes = @()
foreach ($file in $files) {
    $path = Join-Path $repo $file
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $source = Read-CppMigrationSource $path
    $matches = @([regex]::Matches($source.Text, $pattern) | Where-Object { $_.Groups['value'].Success })
    if (!$matches.Count) { continue }
    $changes += [pscustomobject]@{ path = $file; count = $matches.Count; before = (Get-FileHash $path).Hash
        kinds = @($matches | ForEach-Object { $_.Groups['kind'].Value } | Group-Object | Select-Object Name, Count) }
    if (!$Apply) { continue }
    $backup = Join-Path $Work "before-consumers/$file"
    if (Test-Path $backup) { throw "Backup already exists: $backup" }
    [void](New-Item -ItemType Directory -Force (Split-Path $backup))
    Copy-Item -LiteralPath $path -Destination $backup
    $text = [regex]::Replace($source.Text, $pattern, [Text.RegularExpressions.MatchEvaluator]{param($match)
        if ($match.Groups['value'].Success) { return $match.Value.Replace('std::', 'ce::') }
        return $match.Value
    })
    if ($file -eq 'Engine/Contracts/Core/Creator.Core.Arena.ixx') {
        # std::map requires its allocator value_type to be exactly std::pair.
        $text = $text.Replace('arena_allocator<ce::pair<const Key, Value>>>', 'arena_allocator<std::pair<const Key, Value>>>')
    }
    if ($file -eq 'Engine/RenderEngine/SpriteSheet.h') {
        $text = $text.Replace('mSprites.insert(ce::pair<std::wstring, SpriteFrame>(std::wstring(name), frame));',
                              'mSprites.emplace(std::wstring(name), frame);')
    }
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $module = [regex]::Match($text, '(?m)^(?:export )?module [\w.:]+;[^\r\n]*')
    if ($module.Success) {
        $text = $text.Insert($module.Index + $module.Length, $newline + 'import Creator.Containers.Values;')
    } else {
        $relative = [IO.Path]::GetRelativePath((Split-Path $path), (Join-Path $repo 'Engine/Containers/ContainerAPI.h')).Replace('\','/')
        $include = '#include "' + $relative + '"' + $newline
        if (!$source.Text.Contains($include.TrimEnd())) {
            $pragma = [regex]::Match($text, '(?m)^#pragma once[^\r\n]*(?:\r?\n)?')
            if ($pragma.Success) { $text = $text.Insert($pragma.Index + $pragma.Length, $include) }
            else { $text = $include + $text }
        }
    }
    [IO.File]::WriteAllText($path, $text, $source.Encoding)
}
[void](New-Item -ItemType Directory -Force $Work)
$changes | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Work 'consumer-changes.json') -Encoding utf8
"VALUE_TYPES_CONSUMERS files=$($changes.Count) references=$(($changes | Measure-Object -Property count -Sum).Sum) applied=$Apply"

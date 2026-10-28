param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$PSScriptRoot/CppContainerTokens.ps1"
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $output){throw 'Use a fresh classification directory'}
New-Item -ItemType Directory $output|Out-Null
$scopes=@('Engine','Editor','Lattice','Player','HeadlessHost')
$files=@(& git -C $repo -c core.quotepath=false ls-files --cached --others --exclude-standard -- $scopes|Where-Object {
    $_ -match '\.(h|hpp|cpp|ixx|inl)$' -and $_ -notlike 'Engine/Containers/*' -and (Test-Path (Join-Path $repo $_))
}|Sort-Object -Unique)
$tracked=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
& git -C $repo -c core.quotepath=false ls-files -- $scopes|ForEach-Object {[void]$tracked.Add($_)}
$stringKinds=@('basic_string','string','wstring','u8string','u16string','u32string')
$rows=[Collections.Generic.List[object]]::new()
$deferred=[Collections.Generic.List[object]]::new()
$callables=[Collections.Generic.List[object]]::new()
$supplement=[Collections.Generic.List[object]]::new()
$sourceHashes=[Collections.Generic.List[object]]::new()
function Read-Template([string]$text,[int]$start){
    $open=$start
    while($open -lt $text.Length -and [char]::IsWhiteSpace($text[$open])){++$open}
    if($open -ge $text.Length -or $text[$open] -ne '<'){return ''}
    $depth=0
    for($n=$open;$n -lt $text.Length -and $n-$open -lt 8192;++$n){
        if($text[$n] -eq '<'){++$depth}
        if($text[$n] -eq '>'){--$depth;if($depth -eq 0){return ($text.Substring($open+1,$n-$open-1) -replace '\s+',' ').Trim()}}
    }
    return ''
}
foreach($file in $files){
    $absolute=Join-Path $repo $file
    $source=Read-CppMigrationSource $absolute
    # Preserve offsets and newlines while hiding literals/comments from type scans.
    $clean=[regex]::Replace($source.Text,$script:CppContainerProtected,[Text.RegularExpressions.MatchEvaluator]{param($m) [regex]::Replace($m.Value,'[^\r\n]',' ')})
    $sourceHashes.Add([ordered]@{path=$file;tracked=$tracked.Contains($file);sha256=(Get-FileHash $absolute).Hash})
    $line=1;$lineStart=0;$next=$clean.IndexOf("`n")
    foreach($match in Get-CppContainerReferences $source.Text){
        while($next -ge 0 -and $next -lt $match.Index){++$line;$lineStart=$next+1;$next=$clean.IndexOf("`n",$lineStart)}
        $end=if($next -ge 0){$next}else{$clean.Length}
        $kind=$match.Groups['kind'].Value
        $item=[ordered]@{id="${file}:${line}:$($match.Index-$lineStart+1)";path=$file;line=$line;column=$match.Index-$lineStart+1;
            tracked=$tracked.Contains($file);kind=$kind;pmr=$match.Groups['pmr'].Success;text=$source.Text.Substring($lineStart,$end-$lineStart).Trim()}
        if($kind -in $stringKinds){$deferred.Add($item);continue}
        if($kind -eq 'function'){$callables.Add($item);continue}
        $args=Read-Template $clean ($match.Index+$match.Length)
        $category=switch($kind){
            {$_ -in @('vector','array','bitset')}{'value_batch_candidate';break}
            'unordered_map' {'hash_map_contract_review';break}
            'unordered_set' {'hash_set_api_review';break}
            {$_ -in @('map','set','multimap','multiset')}{'ordered_design_required';break}
            'deque' {'deque_lifetime_design';break}
            {$_ -in @('stack','queue','priority_queue')}{'adaptor_contract_review';break}
            default {'unclassified'}
        }
        # Verified exact SDK override and its matching returned buffer. Do not
        # classify a whole importer/backend file as an external STL boundary.
        $external=$file -eq 'Engine/Utility_Framework/Reflection/ReflgenAuthoring.h' -and $kind -eq 'vector' -and
            ($item.text -match 'read_bytes\(\)\s+override|std::vector<std::byte> bytes\(decoded.size\(\)\)')
        if($external){$category='external_abi_verified'}
        $flags=[Collections.Generic.List[string]]::new()
        if($file -match '\.(h|hpp|ixx)$'){$flags.Add('header_or_module_consumer_batch')}
        if($args -match '\bstd::(?:basic_string|string|wstring|u8string|u16string|u32string|string_view|wstring_view)\b|\bDefinitionId\b'){$flags.Add('keep_string_type_check_hash_compare_and_views')}
        if($args -match '\b(?:own::|std::(?:shared_ptr|weak_ptr|unique_ptr)|\w+\s*\*)'){$flags.Add('explicit_owner_or_pointer_type')}
        if($file -match 'AssetDepot|DataSystem|SceneRuntime|Render/|RHI/|Diagnostics|ClusterStreaming|ReflectionUndo'){$flags.Add('lifetime_domain_review_hint')}
        if($kind -eq 'unordered_set'){$flags.Add('flat_set_is_not_STL_iterator_or_node_storage_compatible')}
        if($kind -eq 'unordered_map'){$flags.Add('check_hash_equal_api_node_release_and_iteration')}
        if($category -eq 'ordered_design_required'){$flags.Add('preserve_order_ranges_duplicate_keys_and_node_identity')}
        if($category -eq 'deque_lifetime_design'){$flags.Add('preserve_stable_addresses_pop_destruction_and_growth')}
        $item['templateArguments']=$args;$item['category']=$category;$item['reviewFlags']=@($flags)
        $item['classificationBasis']=if($external){'source-reviewed SDK return boundary'}else{'type-family triage; flags are conservative hints, not semantic clearance'}
        $rows.Add($item)
    }
    $pattern='\bstd::(?:(?:pmr)::)?(?:span|basic_string_view|string_view|wstring_view|u8string_view|u16string_view|u32string_view)\b|\bconcurrency::concurrent_\w+\b|\busing\s+std::(?:'+$script:CppContainerKinds+')\b'
    foreach($match in [regex]::Matches($clean,$pattern)){
        $before=$clean.Substring(0,$match.Index)
        $supplement.Add([ordered]@{path=$file;line=1+[regex]::Matches($before,"`n").Count;token=$match.Value})
    }
}
$summary=@($rows|Group-Object {$_.category}|Sort-Object Name|ForEach-Object {[ordered]@{category=$_.Name;references=$_.Count;files=@($_.Group.path|Sort-Object -Unique).Count}})
$allCount=$rows.Count+$deferred.Count+$callables.Count
if(@($rows|Where-Object {$_.category -eq 'unclassified'}).Count){throw 'Unclassified storage types'}
$report=[ordered]@{created=(Get-Date -Format o);branch=(& git -C $repo branch --show-current);head=(& git -C $repo rev-parse HEAD);
    scope=$scopes;policy='Lexical type references, not distinct objects/declarations. Includes tracked and untracked sources; excludes canonical implementation, Tools, ThirdParty and generated output. String types deferred; string-valued containers retained. Categories are implementation routing, not automatic migration approval.';
    sourceFiles=$files.Count;trackedSources=@($sourceHashes|Where-Object tracked).Count;untrackedSources=@($sourceHashes|Where-Object {!$_.tracked}).Count;
    totalReferences=$allCount;deferredStringReferences=$deferred.Count;callableReferences=$callables.Count;
    storageReferences=$rows.Count;storageFiles=@($rows.path|Sort-Object -Unique).Count;
    trackedStorageReferences=@($rows|Where-Object tracked).Count;untrackedStorageReferences=@($rows|Where-Object {!$_.tracked}).Count;
    summary=$summary;locations=@($rows);supplemental=@($supplement)}
$report|ConvertTo-Json -Depth 9|Set-Content "$output/classification.json" -Encoding utf8
$rows|ForEach-Object {[pscustomobject]@{category=$_.category;path=$_.path;line=$_.line;column=$_.column;kind=$_.kind;tracked=$_.tracked;templateArguments=$_.templateArguments;flags=($_.reviewFlags -join ';');text=$_.text}}|
    Export-Csv "$output/classification.csv" -NoTypeInformation -Encoding utf8
@($sourceHashes)|ConvertTo-Json -Depth 4|Set-Content "$output/source-hashes.json" -Encoding utf8
@($deferred)|ConvertTo-Json -Depth 4|Set-Content "$output/deferred-strings.json" -Encoding utf8
@($callables)|ConvertTo-Json -Depth 4|Set-Content "$output/callables.json" -Encoding utf8
$summary|ForEach-Object {"$($_.category): $($_.references) references / $($_.files) files"}
Write-Output "BOUNDARY_CLASSIFICATION: storage=$($rows.Count); strings=$($deferred.Count); callables=$($callables.Count); total=$allCount"

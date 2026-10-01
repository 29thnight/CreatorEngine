function Assert-ThinFilmNumericBaseline($Domain, $Directory, $Fresh, $Historical, $View) {
    # Keep RGB-3, spectral and cutoff baselines immutable. The version-4 GGX
    # baseline changes rendered closure fields, with unchanged optical/profile
    # fields checked separately against those earlier references.
    if ($Domain -eq 'Layered') {
        $expectedHash = 'ABA798DF54E1EB73D73037822CA14DAF54729544E5D020DC157390FDE2E80EF8'
        $allowed = @('all_layers','all_layers_alpha_zero','all_layers_ao_zero','film_200nm','film_400nm','film_800nm',
            'film_no_substrate_interface','film_sub_nm_transition','film_top_tir','linked_gain_and_clamps','metal_film')
        $count = 1960
    } else {
        $expectedHash = '94370D5E127F63DF031CE0150414BDB40CE8F1F223AA720FF1F20196DCA8AB78'
        $allowed = @('all_layers_alpha_zero','all_layers_ao_zero','all_layers_special','glass_thin_film')
        $count = 4410
    }
    $path = Join-Path $Directory 'numeric-golden-spectral.csv'
    $bytes = [Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($path).Replace("`r`n", "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','') } finally { $sha.Dispose() }
    $manifest = Get-Content (Join-Path $Directory 'spectral-manifest.json') -Raw | ConvertFrom-Json
    if ($hash -ne $expectedHash -or $manifest.numeric_golden_sha256 -ne $hash -or
        $manifest.numeric_golden_rows -ne $count -or $manifest.schema_version -ne 2 -or
        (@($manifest.changed_cases) -join ',') -ne ($allowed -join ',')) { throw 'Pinned spectral numeric baseline identity differs' }
    $golden = @(Import-Csv -LiteralPath $path)
    if ($golden.Count -ne $count) { throw 'Spectral numeric row count differs' }
    if ($Domain -eq 'Layered') {
        # Keep the prior spectral baseline immutable. These two 0.1nm fixtures
        # now use the pinned Cycles cutoff, verified against no-film controls.
        $transitionCases = @('film_sub_nm_transition','film_no_substrate_interface')
        $transitionPath = Join-Path $Directory 'numeric-golden-transition.csv'
        $transitionBytes = [Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($transitionPath).Replace("`r`n", "`n"))
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $transitionHash = [BitConverter]::ToString($sha.ComputeHash($transitionBytes)).Replace('-','') } finally { $sha.Dispose() }
        $transitionManifest = Get-Content (Join-Path $Directory 'transition-manifest.json') -Raw | ConvertFrom-Json
        $transition = @(Import-Csv -LiteralPath $transitionPath)
        if ($transitionHash -ne '0A3B41BDD05F8B1CF7458D3865539B46D6206D5CEC9AD79733C5AFE669950CB4' -or
            $transitionManifest.numeric_golden_sha256 -ne $transitionHash -or
            $transitionManifest.schema_version -ne 3 -or $transitionManifest.numeric_golden_rows -ne 112 -or
            (@($transitionManifest.changed_cases) -join ',') -ne ($transitionCases -join ',') -or
            $transition.Count -ne 112) { throw 'Pinned subnanometer transition baseline differs' }
        $expectedKeys = @($golden | Where-Object {$_.case -in $transitionCases} | ForEach-Object { $_.case+'/'+$_.angle+'/'+$_.field } | Sort-Object)
        $actualKeys = @($transition | ForEach-Object { $_.case+'/'+$_.angle+'/'+$_.field } | Sort-Object)
        if (($actualKeys -join ',') -ne ($expectedKeys -join ',')) { throw 'Transition baseline case/field identity differs' }
        foreach ($row in @($transition | Where-Object {$_.case -eq 'film_sub_nm_transition'})) {
            $controlKey = 'blender_defaults/'+$row.angle+'/'+$row.field
            $freshKey = $row.case+'/'+$row.angle+'/'+$row.field
            if (!$Fresh.ContainsKey($controlKey)) { throw 'Missing no-film boundary control' }
            foreach ($channel in @('x','y','z','w')) {
                $actual = [double]::Parse($Fresh[$freshKey].$channel, [Globalization.CultureInfo]::InvariantCulture)
                $control = [double]::Parse($Fresh[$controlKey].$channel, [Globalization.CultureInfo]::InvariantCulture)
                if (![double]::IsFinite($actual) -or [Math]::Abs($actual-$control) -gt 0.0000001) {
                    throw "0.1nm cutoff differs from no-film control: $freshKey/$channel"
                }
            }
        }
        Write-Output 'PRINCIPLED_FILM_CUTOFF_CONTROL_OK components=224'
        $golden = @($golden | Where-Object {$_.case -notin $transitionCases}) + $transition
    }
    $rows = @($Historical | Where-Object { $_.case -notin $allowed }) + $golden
    $rows = @(Assert-GgxEnergyBaseline -Domain $Domain -Directory $Directory -Earlier $rows -View $View)
    foreach ($row in $rows) {
        $key = $row.case + '/' + $row.$View + '/' + $row.field
        if (!$Fresh.ContainsKey($key)) { throw "Missing numeric reference: $key" }
        foreach ($channel in @('x','y','z','w')) {
            $a = [double]::Parse($Fresh[$key].$channel, [Globalization.CultureInfo]::InvariantCulture)
            $b = [double]::Parse($row.$channel, [Globalization.CultureInfo]::InvariantCulture)
            if ([double]::IsNaN($a) -or [double]::IsInfinity($a) -or [Math]::Abs($a-$b)/[Math]::Max(1,[Math]::Abs($b)) -gt 0.000001) {
                throw "Pinned GGX closure numeric golden differs: $key/$channel"
            }
        }
    }
    Write-Output "PRINCIPLED_${Domain}_GGX_GOLDEN_OK rows=$count historicalArtifactsPreserved=true"
}

function Assert-GgxEnergyBaseline($Domain, $Directory, $Earlier, $View) {
    # The spectral file also includes non-film rows; collapse the historical
    # overlay to one key, letting the newer immutable optical baseline win.
    $previous = @{}
    foreach ($row in $Earlier) { $previous[$row.case+'/'+$row.$View+'/'+$row.field] = $row }
    $Earlier = @($previous.Values)
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    $tableHashes = @{
        'Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/GgxEnergyTables.slang' = '250AA6A77726AED183D10C81B2189F0FBD7E7D05724F1177C5AA9818DA482DEA'
        'Tools/regression/principled_ggx_energy_tables.h' = '76269B1A2948F2A0C4B1802B0417AC1A4226FF661F642A30C7591556DD1DDDFC'
    }
    foreach ($entry in $tableHashes.GetEnumerator()) {
        $text = [IO.File]::ReadAllText((Join-Path $repo $entry.Key)).Replace("`r`n", "`n")
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-','') } finally { $sha.Dispose() }
        if ($hash -ne $entry.Value) { throw "Pinned GGX table differs: $($entry.Key)" }
    }
    $path = Join-Path $Directory 'numeric-golden-ggx.csv'
    $text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-','') } finally { $sha.Dispose() }
    $expected = if ($Domain -eq 'Layered') { '4ACCFE3D61372D4252256BBE5C3C10F5764D14A8607CB1DE06475A776D1C8E81' } else { '5791BB5A3A9CCFF1AAF765C1E3EF495C76406882ECE19F1FDBF56269B3F93AD6' }
    $invariants = if ($Domain -eq 'Layered') { @(20,24,25) } else { @(0,6,7,18,19,20,21,22,23,24,25) }
    $manifest = Get-Content (Join-Path $Directory 'ggx-manifest.json') -Raw | ConvertFrom-Json
    if ($hash -ne $expected -or $manifest.numeric_golden_sha256 -ne $hash -or $manifest.schema_version -ne 4 -or
        $manifest.numeric_golden_rows -ne $Earlier.Count -or $manifest.blender_version -ne '5.1.1') {
        throw 'Pinned GGX numeric identity differs'
    }
    $energy = @(Import-Csv -LiteralPath $path)
    $index = @{}
    foreach ($row in $energy) {
        $key = $row.case+'/'+$row.$View+'/'+$row.field
        if ($index.ContainsKey($key)) { throw "Duplicate GGX reference: $key" }
        $index[$key] = $row
    }
    if ($index.Count -ne $Earlier.Count) { throw 'GGX reference row count differs' }
    foreach ($row in $Earlier) {
        $key = $row.case+'/'+$row.$View+'/'+$row.field
        if (!$index.ContainsKey($key)) { throw "Missing GGX reference: $key" }
        if ([int]$row.field -notin $invariants) { continue }
        foreach ($channel in @('x','y','z','w')) {
            $a = [double]::Parse($index[$key].$channel, [Globalization.CultureInfo]::InvariantCulture)
            $b = [double]::Parse($row.$channel, [Globalization.CultureInfo]::InvariantCulture)
            if (![double]::IsFinite($a) -or [Math]::Abs($a-$b)/[Math]::Max(1,[Math]::Abs($b)) -gt 0.000001) {
                throw "Unchanged optical/profile reference differs: $key/$channel"
            }
        }
    }
    return $energy
}

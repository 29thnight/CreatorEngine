function Assert-ThinFilmNumericBaseline($Domain, $Directory, $Fresh, $Historical, $View) {
    # The historical RGB-3 baseline remains immutable. Only these fixtures have
    # film inputs; all other values must still match the historical baseline.
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
    $rows = @($Historical | Where-Object { $_.case -notin $allowed }) + $golden
    foreach ($row in $rows) {
        $key = $row.case + '/' + $row.$View + '/' + $row.field
        if (!$Fresh.ContainsKey($key)) { throw "Missing numeric reference: $key" }
        foreach ($channel in @('x','y','z','w')) {
            $a = [double]::Parse($Fresh[$key].$channel, [Globalization.CultureInfo]::InvariantCulture)
            $b = [double]::Parse($row.$channel, [Globalization.CultureInfo]::InvariantCulture)
            if ([double]::IsNaN($a) -or [double]::IsInfinity($a) -or [Math]::Abs($a-$b)/[Math]::Max(1,[Math]::Abs($b)) -gt 0.000001) {
                throw "Pinned spectral/non-film numeric golden differs: $key/$channel"
            }
        }
    }
    Write-Output "PRINCIPLED_${Domain}_SPECTRAL_GOLDEN_OK rows=$count nonFilmHistoricalPreserved=true"
}

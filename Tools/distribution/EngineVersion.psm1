Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-EngineVersionMetadata($Value) {
    if ($Value.schemaVersion -ne 1 -or $Value.productName -cne 'CreatorEngine 2') { throw 'Unsupported engine product metadata.' }
    if ($Value.version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Engine version must be Major.Minor.Build.Revision (four integers).'
    }
    foreach ($component in $Value.version.Split('.')) {
        $number = 0
        if (-not [int]::TryParse($component, [ref]$number) -or $number -gt 65535) { throw 'Engine version components must be in 0..65535.' }
    }
    if ($Value.channel -cnotin @('preview','stable')) { throw 'Channel must be preview or stable.' }
    if ($Value.localDevelopment -isnot [bool]) { throw 'localDevelopment must be boolean.' }
    if ($Value.featureRelease -isnot [string] -or ($Value.featureRelease -and $Value.featureRelease -cnotmatch '^[0-9]{2}H[12]$')) {
        throw 'Feature release must be YYH1/YYH2, or empty for unassigned local development.'
    }
    if ($Value.localDevelopment) {
        if ($Value.channel -ne 'preview') { throw 'A local development build cannot claim Stable.' }
    } elseif (-not $Value.featureRelease -or $Value.version -eq '0.0.0.0') {
        throw 'Publishing requires an explicitly assigned feature release and engine build.'
    }
}

function Read-EngineVersionMetadata([string]$Path) {
    $value = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    Assert-EngineVersionMetadata $value
    $value
}

Export-ModuleMember -Function Assert-EngineVersionMetadata,Read-EngineVersionMetadata

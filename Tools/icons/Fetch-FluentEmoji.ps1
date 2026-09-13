# Restore the exact official PNGs and MIT notice recorded in the manifest.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$iconRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Resources/Editor/Icons'))
$manifest = Get-Content -LiteralPath (Join-Path $iconRoot 'FluentEmoji.provenance.json') -Raw | ConvertFrom-Json
$downloads = @($manifest.files) + @(@{file=$manifest.licenseFile;url=$manifest.licenseUrl;sha256=$manifest.licenseSha256})
foreach ($entry in $downloads) {
    $destination = Join-Path $iconRoot $entry.file
    if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination).Hash -eq $entry.sha256) { continue }
    $temp = [IO.Path]::GetTempFileName()
    try {
        Invoke-WebRequest -Uri $entry.url -OutFile $temp
        if ((Get-FileHash -LiteralPath $temp).Hash -ne $entry.sha256) { throw ('Source checksum mismatch: '+$entry.file) }
        Copy-Item -LiteralPath $temp -Destination $destination -Force
    } finally { Remove-Item -LiteralPath $temp }
}

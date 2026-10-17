function New-ContractProbeConfiguration
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
        [Parameter(Mandatory)][string]$OutputDirectory
    )

    # This helper writes configuration only. It never invokes a compiler.
    $contractProbeDirectory = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) "contracts-$Configuration"
    [IO.Directory]::CreateDirectory($contractProbeDirectory) | Out-Null
    $enabled = if ($Configuration -eq 'Debug') { 'true' } else { 'false' }
    $content = "#pragma once`nnamespace ce::contract_detail {`n    inline constexpr bool assertions_enabled = $enabled;`n}`n"
    $destination = Join-Path $contractProbeDirectory 'CreatorContractConfig.h'
    if (-not [IO.File]::Exists($destination) -or [IO.File]::ReadAllText($destination) -cne $content)
    {
        [IO.File]::WriteAllText($destination, $content, [Text.UTF8Encoding]::new($false))
    }
    [pscustomobject]@{
        Arguments = @('/DCE_CONTRACT_STANDALONE=1', ('/I' + $contractProbeDirectory))
        CommandLine = (' /DCE_CONTRACT_STANDALONE=1 /I"' + $contractProbeDirectory + '" ')
        IncludeDirectory = $contractProbeDirectory
    }
}

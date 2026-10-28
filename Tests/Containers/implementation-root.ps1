function Get-ContainerImplementationRoot {
    $candidate = if ($env:CE_IMPLEMENTATION_ROOT) { $env:CE_IMPLEMENTATION_ROOT } else { Join-Path $PSScriptRoot '../../Engine/Containers' }
    $resolved = (Resolve-Path -LiteralPath $candidate).Path
    if (!(Test-Path "$resolved/modules/Creator.Containers.ixx")) { throw 'Canonical container module missing' }
    return $resolved
}

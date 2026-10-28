param([switch]$Check)
. "$PSScriptRoot/../implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
& "$implementation/modules/sync-headers.ps1" -Check:$Check

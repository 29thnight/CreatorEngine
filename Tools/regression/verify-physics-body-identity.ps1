[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
& "$PSScriptRoot/verify-script-native-table.ps1" -Configuration $Configuration
if($LASTEXITCODE){throw 'F# physics identity regression failed'}

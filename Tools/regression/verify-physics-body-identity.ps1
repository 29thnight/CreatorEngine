[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
& dotnet run --project "$PSScriptRoot/PhysicsBodyIdentityProbe" -c $Configuration
if($LASTEXITCODE){throw 'Managed physics identity regression failed'}

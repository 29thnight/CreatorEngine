[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Config = 'Debug', [string]$EngineDistribution = '', [switch]$StaticOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Import-Module (Join-Path $repo 'Tools/distribution/EngineVersion.psm1')
Import-Module (Join-Path $repo 'Tools/distribution/EngineDistribution.psm1')
Import-Module (Join-Path $repo 'Tools/runtime/RuntimeLayout.psm1')
$checks = 0
function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Reject([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Check $rejected $Message
}
$metadata = Read-EngineVersionMetadata (Join-Path $repo 'EngineVersion.json')
& (Join-Path $repo 'Tools/distribution/update-engine-version.ps1') -Check
foreach ($invalid in @('1.2.3','1.2.3.4-preview','1.2.65536.0','1.2.99999999999999.0','01.2.3.4','1.2.-1.0')) {
    $bad = $metadata | ConvertTo-Json | ConvertFrom-Json
    $bad.version = $invalid
    Reject { Assert-EngineVersionMetadata $bad } "Invalid engine version accepted: $invalid"
}
$bad = $metadata | ConvertTo-Json | ConvertFrom-Json
$bad.localDevelopment = $false
$bad.featureRelease = ''
Reject { Assert-EngineVersionMetadata $bad } 'Unassigned release was accepted as published.'
$bad = $metadata | ConvertTo-Json | ConvertFrom-Json
$bad.channel = 'stable'
Reject { Assert-EngineVersionMetadata $bad } 'Local development was accepted as Stable.'

$fixtureRoot = Join-Path $repo 'Build/Tests/EngineDistribution'
$fixture = Assert-EngineChildPath (Join-Path $fixtureRoot ([Guid]::NewGuid().ToString('N'))) $fixtureRoot
[void][IO.Directory]::CreateDirectory($fixture)
try {
    [IO.File]::WriteAllText((Join-Path $fixture 'payload.txt'), 'test payload')
    $entries = @(Get-EngineEntries $fixture @('payload.txt'))
    $manifest = [ordered]@{
        schemaVersion=1; productName=$metadata.productName; featureRelease=$metadata.featureRelease;
        version=$metadata.version; channel=$metadata.channel; localDevelopment=$metadata.localDevelopment;
        buildId=[Guid]::NewGuid().ToString('D'); payloadDigest=Get-EngineDigest $entries;
        configuration=$Config; shipping=$false; platform='win-x64'; binaryRoot="Bin/x64-$Config"; files=$entries
    }
    Write-EngineJson (Join-Path $fixture 'engine.manifest.json') $manifest
    Write-EngineMetadataFile (Join-Path $fixture 'engine.info') $manifest
    $read = Read-EngineDistribution $fixture
    Check ($read.buildId -ne $read.payloadDigest) 'Distribution ID must be independent of payload hash.'
    [IO.File]::WriteAllText((Join-Path $fixture 'engine.info'), 'changed')
    Reject { Read-EngineDistribution $fixture } 'Conflicting native display metadata was accepted.'
    Write-EngineMetadataFile (Join-Path $fixture 'engine.info') $manifest
    $extra = Join-Path $fixture 'unlisted.dll'
    [IO.File]::WriteAllText($extra, 'unexpected dependency')
    Reject { Read-EngineDistribution $fixture } 'Unlisted distribution DLL was accepted.'
    Remove-Item -LiteralPath (Assert-EngineChildPath $extra $fixture) -Force
    [IO.File]::WriteAllText((Join-Path $fixture 'payload.txt'), 'tampered')
    Reject { Read-EngineDistribution $fixture } 'Changed payload was accepted.'
    [void][IO.Directory]::CreateDirectory((Join-Path $fixture 'ProjectSetting'))
    Write-EngineJson (Join-Path $fixture "ProjectSetting/Engine.$Config.lock.json") @{ schemaVersion=1; configuration=$Config; version=$manifest.version; buildId=[Guid]::NewGuid().ToString('D') }
    Reject { Assert-EngineProjectPin $fixture $read -Require } 'Wrong exact distribution pin was accepted.'

    if (-not $StaticOnly) {
        $binary = Join-Path $repo "Bin/x64-$Config"
        if ($EngineDistribution) {
            $distribution = Read-EngineDistribution $EngineDistribution
            Check ($distribution.configuration -eq $Config) 'Unexpected distribution configuration.'
            $binary = Join-Path $EngineDistribution $distribution.binaryRoot
        }
        foreach ($role in @(
            @{Name='CreatorEditor'; Folder='Editor'}, @{Name='Player'; Folder='Player'},
            @{Name='AssetCooker'; Folder='Tools/AssetCooker'}, @{Name='AssetPacker'; Folder='Tools/AssetPacker'}
        )) {
            $record = Get-Content (Join-Path $binary "Runtime/Manifests/$($role.Name).json") -Raw | ConvertFrom-Json
            Check ((Get-EngineDigest @($record.entries)) -eq $record.digest) "Invalid host record: $($role.Name)"
            Test-EngineEntries $binary @($record.entries)
            $folder = Join-Path $binary $role.Folder
            $exe = Join-Path $folder "$($role.Name).exe"
            $hostDll = Join-Path $folder "$($role.Name).runtime.dll"
            $legacy = @(Get-ChildItem $folder -Filter '*.dll' -File | Where-Object FullName -NE $hostDll)
            Check ($legacy.Count -eq 0) "Legacy DLL copies remain at $folder : $(@($legacy | ForEach-Object Name) -join ',')"
            Check (-not @(Get-EnginePeImports $exe | Where-Object { $_ -match '^(vcruntime|msvcp|concrt|ucrtbased)' }).Count) 'Launcher requires dynamic CRT before search-path initialization.'
            if ($role.Name -ne 'CreatorEditor') {
                Check (-not @($record.entries | Where-Object path -like 'Runtime/Editor/*').Count) "Editor dependency leaked into $($role.Name)"
            }
            $start = [Diagnostics.ProcessStartInfo]::new($exe)
            $start.ArgumentList.Add('--engine-info')
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardOutput = $true
            $start.RedirectStandardError = $true
            $start.WorkingDirectory = $fixture
            $start.Environment['PATH'] = "$env:SystemRoot\System32"
            $process = [Diagnostics.Process]::Start($start)
            $stdoutTask = $process.StandardOutput.ReadToEndAsync()
            $stderrTask = $process.StandardError.ReadToEndAsync()
            $finished = $process.WaitForExit(30000)
            if (-not $finished) { $process.Kill($true); $process.WaitForExit() }
            $output = $stdoutTask.GetAwaiter().GetResult()
            $errorText = $stderrTask.GetAwaiter().GetResult()
            Check ($finished -and $process.ExitCode -eq 0) "Isolated DLL loading failed: $($role.Name) $errorText"
            $info = $output | ConvertFrom-Json
            Check ($info.version -eq $metadata.version -and $info.productName -eq $metadata.productName -and
                $info.featureRelease -eq $metadata.featureRelease -and $info.localDevelopment -eq $metadata.localDevelopment) 'Compiled host version differs from source metadata.'
            foreach ($path in @($exe,$hostDll)) {
                $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
                Check ($version.FileVersion -eq $metadata.version -and $version.ProductVersion -eq $metadata.version) "Windows version mismatch: $path"
            }
        }
        # Exercise the cooker token protocol beyond CreateProcess's command-line limit,
        # including a long Unicode filename and malformed/nested input rejection.
        $argumentRoot = $fixture
        while ($argumentRoot.Length -lt 270) { $argumentRoot = Join-Path $argumentRoot '인수 파일 경로 with spaces' }
        [void][IO.Directory]::CreateDirectory($argumentRoot)
        $argumentFile = Join-Path $argumentRoot 'cook.arguments'
        $cooker = Join-Path $binary 'Tools/AssetCooker/AssetCooker.exe'
        function Run-CookerArguments {
            $start = [Diagnostics.ProcessStartInfo]::new($cooker)
            $start.UseShellExecute = $false; $start.CreateNoWindow = $true
            $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
            $start.ArgumentList.Add('--arguments-file'); $start.ArgumentList.Add($argumentFile)
            $start.Environment['PATH'] = "$env:SystemRoot\System32"
            $process = [Diagnostics.Process]::Start($start)
            $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
            if (-not $process.WaitForExit(30000)) { $process.Kill($true); $process.WaitForExit(); throw 'Cooker argument protocol timed out.' }
            [void]$stdout.GetAwaiter().GetResult(); [void]$stderr.GetAwaiter().GetResult()
            return $process.ExitCode
        }
        [IO.File]::WriteAllText($argumentFile, "--help`n" + (("한글 공백 보존 token`n") * 4000), [Text.UTF8Encoding]::new($false))
        Check ((Run-CookerArguments) -eq 0) 'Large UTF-8 argument file at a long path was rejected.'
        [IO.File]::WriteAllBytes($argumentFile, [byte[]]@(255,255,10))
        Check ((Run-CookerArguments) -eq 2) 'Malformed UTF-8 arguments were accepted.'
        [IO.File]::WriteAllText($argumentFile, "--arguments-file`nnested`n", [Text.UTF8Encoding]::new($false))
        Check ((Run-CookerArguments) -eq 2) 'Nested argument file was accepted.'
    }
} finally {
    $fixture = Assert-EngineChildPath $fixture $fixtureRoot
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
Write-Host "ENGINE_DISTRIBUTION_VERIFIED configuration=$Config staticOnly=$StaticOnly checks=$checks"

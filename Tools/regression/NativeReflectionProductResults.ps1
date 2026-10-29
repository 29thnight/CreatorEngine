. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')
Import-Module (Join-Path $PSScriptRoot '../runtime/RuntimeLayout.psm1') -DisableNameChecking

function Assert-NativeReflectionEditorClosure(
    [string]$BinaryRoot, [string]$DistributionBinaryRoot, [string]$Configuration, [int]$ApiVersion) {
    # The caller has verified the immutable distribution. Its deployment record,
    # not the mutable repository record, defines the native files to compare.
    $recordPath = Assert-EngineChildPath (Join-Path $DistributionBinaryRoot 'Runtime/Manifests/CreatorEditor.json') $DistributionBinaryRoot
    $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    Assert-ResultInteger $record.schemaVersion 'native deployment schemaVersion' 1
    Assert-ResultInteger $record.abi.scriptApi 'native deployment scriptApi' 48
    $entries = @($record.entries)
    if ($record.schemaVersion -ne 1 -or $record.host -cne 'CreatorEditor' -or
        $record.configuration -cne $Configuration -or $record.abi.scriptApi -ne $ApiVersion -or
        (Get-EngineDigest $entries) -cne $record.digest) {
        throw 'The validated distribution has an incompatible Editor native deployment record.'
    }
    foreach ($required in @('Editor/CreatorEditor.exe', 'Editor/CreatorEditor.runtime.dll', 'Runtime/layout.version')) {
        if (@($entries | Where-Object path -CEQ $required).Count -ne 1) {
            throw "Editor native deployment record is incomplete: $required"
        }
    }
    # CreatorEditor.exe is only a launcher. Match the actual runtime DLL and
    # every recorded shared native dependency before running the repository host.
    Test-EngineEntries $BinaryRoot $entries
    $runtime = @($entries | Where-Object path -CEQ 'Editor/CreatorEditor.runtime.dll')[0]
    return [pscustomobject]@{
        schemaVersion = 1; host = 'CreatorEditor'; configuration = $Configuration; nativeApi = $ApiVersion
        digest = $record.digest; files = $entries.Count; runtimeSha256 = $runtime.sha256
    }
}

function Assert-NativeReflectionAudioDevice($Results) {
    $audio = Get-RuntimeResult $Results 'audio.host'
    Assert-ResultInteger $audio.data.outputMode 'outputMode'
    Assert-ResultBoolean $audio.data.backendCountersAvailable 'backendCountersAvailable'
    Assert-ResultInteger $audio.data.callbackCount 'callbackCount' 1
    Assert-ResultInteger $audio.data.backendFailures 'backendFailures'
    if ($audio.data.outputMode -ne 1 -or $audio.data.backendCountersAvailable -cne $true -or
        $audio.data.backendFailures -ne 0) {
        throw 'Audio device validation requires Device mode, actual callbacks, and no backend failures.'
    }
    return $audio.data
}

function Read-NativeReflectionAcceptance(
    [string]$Text, [int]$ProcessId, [string[]]$ExpectedComponents, [int]$ApiVersion) {
    if ($Text.Contains('[native.reflection.failure]')) { throw 'The managed product probe reported a failure.' }
    $result = Read-TaggedResult $Text '[native.reflection.acceptance]' $ProcessId
    Assert-ResultInteger $result.nativeApi 'nativeApi' 48
    Assert-ResultInteger $result.catalogTypes 'catalogTypes' 1
    Assert-ResultInteger $result.fixedTicks 'fixedTicks' 30
    if ($result.protocol -cne 'creator-native-reflection-v1' -or $result.nativeApi -ne $ApiVersion -or
        $result.fixedTicks -gt 180 -or $result.catalogTypes -le $ExpectedComponents.Count) {
        throw 'Native reflection product protocol, API, catalogue, or fixed-tick contract did not match.'
    }
    $observed = @($result.catalogComponents)
    if ($observed.Count -ne $ExpectedComponents.Count -or
        @($observed | Sort-Object -Unique).Count -ne $observed.Count -or
        @($observed | Where-Object { $_ -isnot [string] -or $_ -cnotin $ExpectedComponents }).Count -ne 0) {
        throw 'The actual product component catalogue differs from the native source inventory.'
    }
    if (($result.moved -isnot [double] -and $result.moved -isnot [decimal] -and
         $result.moved -isnot [int] -and $result.moved -isnot [long]) -or
        [double]::IsNaN([double]$result.moved) -or [double]::IsInfinity([double]$result.moved) -or
        $result.moved -le 0.1) {
        throw 'The actual native character did not move.'
    }
    foreach ($name in @('catalogUniqueIds', 'catalogScriptComponents', 'characterExists', 'characterDefinition',
        'lightScalarAndRecord', 'nestedProfileWrite', 'decalFields', 'unicodeField', 'borrowedNullResult',
        'soundValueResult', 'typedAudioPlayback', 'audioSurvivesGc', 'fixedStep', 'simulationOrder',
        'nativePhysicsRead', 'nativePhysicsWrite', 'typedAudioPause', 'typedAudioResume', 'typedAudioStop',
        'physicsMoved', 'audioCompletion')) {
        Assert-ResultBoolean $result.checks.$name $name
        if ($result.checks.$name -cne $true) { throw "Native reflection product check failed: $name" }
    }
    return $result
}

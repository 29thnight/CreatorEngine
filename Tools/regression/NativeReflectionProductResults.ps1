. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')

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

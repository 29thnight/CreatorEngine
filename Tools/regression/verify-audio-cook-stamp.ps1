param(
    [string]$AssetCooker = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Tools\AssetCooker\AssetCooker.exe'),
    [string]$FormatFixtures = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$run = Join-Path $repo ('Build\Validation\AudioCookStamp\' + [guid]::NewGuid().ToString('N'))
$assets = Join-Path $run 'Assets'
$textureDirectory = Join-Path $assets 'Cloud'
$audioDirectory = Join-Path $assets 'Sounds'
New-Item -ItemType Directory -Path $textureDirectory,$audioDirectory -Force | Out-Null

$texture = Join-Path $textureDirectory 'Cloud.png'
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP\Assets\Cloud\Cloud.png') -Destination $texture
Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP\Assets\Cloud\Cloud.png.meta') -Destination ($texture + '.meta')

$wav = Join-Path $audioDirectory 'probe.wav'
$bytes = [byte[]]::new(48)
[Text.Encoding]::ASCII.GetBytes('RIFF').CopyTo($bytes, 0)
[BitConverter]::GetBytes([uint32]40).CopyTo($bytes, 4)
[Text.Encoding]::ASCII.GetBytes('WAVEfmt ').CopyTo($bytes, 8)
[BitConverter]::GetBytes([uint32]16).CopyTo($bytes, 16)
[BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 20)
[BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 22)
[BitConverter]::GetBytes([uint32]48000).CopyTo($bytes, 24)
[BitConverter]::GetBytes([uint32]96000).CopyTo($bytes, 28)
[BitConverter]::GetBytes([uint16]2).CopyTo($bytes, 32)
[BitConverter]::GetBytes([uint16]16).CopyTo($bytes, 34)
[Text.Encoding]::ASCII.GetBytes('data').CopyTo($bytes, 36)
[BitConverter]::GetBytes([uint32]4).CopyTo($bytes, 40)
[IO.File]::WriteAllBytes($wav, $bytes)

$hash = (Get-FileHash -LiteralPath $wav -Algorithm SHA256).Hash.ToLowerInvariant()
$meta = @"
guid: 0ff53a5b-bdb0-438a-abab-645a99062fd8
audioClip:
  schemaVersion: 1
  loadMode: Auto
  spatialKind: NonSpatial
  codec: Wav
  payloadSize: 48
  sourceContentHash: $hash
"@
$wavMeta = $wav + '.meta'
[IO.File]::WriteAllText($wavMeta, $meta + "`n", [Text.UTF8Encoding]::new($false))

function Invoke-Cook([string]$name) {
    $output = Join-Path $run $name
    $lines = & $AssetCooker --asset-root $assets --output $output --texture $texture 2>&1
    [pscustomobject]@{ Code = $LASTEXITCODE; Output = $output; Text = ($lines -join "`n") }
}

function Assert-AudioArtifact([string]$output, [string]$guid, [string]$source,
    [int]$codec, [int]$channels) {
    $virtualPath = 'Derived/Audio/' + $guid.Substring(0, 2) + '/' + $guid + '.ceac'
    $artifact = Join-Path $output ($virtualPath.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "audio artifact is missing: $artifact"
    }
    $bytes = [IO.File]::ReadAllBytes($artifact)
    $sourceBytes = [IO.File]::ReadAllBytes($source)
    if ($bytes.Length -ne 72 + $sourceBytes.Length -or
        [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'CEAC' -or
        [BitConverter]::ToUInt16($bytes, 4) -ne 1 -or
        [BitConverter]::ToUInt16($bytes, 6) -ne 72 -or
        $bytes[8] -ne $codec -or $bytes[9] -ne 0 -or $bytes[10] -ne 1 -or
        $bytes[11] -ne $channels -or
        [BitConverter]::ToUInt64($bytes, 24) -ne 72 -or
        [BitConverter]::ToUInt64($bytes, 32) -ne $sourceBytes.Length -or
        [BitConverter]::ToUInt32($bytes, 12) -eq 0 -or
        [BitConverter]::ToUInt64($bytes, 16) -eq 0) {
        throw "audio artifact header is invalid: $artifact"
    }
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $payloadHash = [Convert]::ToHexString([byte[]]$bytes[40..71])
    $payloadActual = [Convert]::ToHexString([byte[]]$bytes[72..($bytes.Length - 1)])
    if ($payloadHash -ne $sourceHash -or
        $payloadActual -ne [Convert]::ToHexString($sourceBytes)) {
        throw "audio artifact payload differs from the validated source: $artifact"
    }

    $manifest = [IO.File]::ReadAllBytes((Join-Path $output 'Derived\asset-manifest.cemf'))
    $entryCount = [BitConverter]::ToUInt32($manifest, 8)
    $dependencyCount = [BitConverter]::ToUInt32($manifest, 12)
    $sourceCount = [BitConverter]::ToUInt32($manifest, 20)
    $pathBase = 32 + 80 * $entryCount + 24 * $sourceCount + 16 * $dependencyCount
    $expectedHash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash
    $found = $false
    for ($index = 0; $index -lt $entryCount; ++$index) {
        $entry = 32 + 80 * $index
        $pathOffset = [BitConverter]::ToUInt32($manifest, $entry + 64)
        $pathLength = [BitConverter]::ToUInt32($manifest, $entry + 68)
        $path = [Text.Encoding]::UTF8.GetString($manifest, $pathBase + $pathOffset,
            $pathLength)
        if ($path -ne $virtualPath) { continue }
        $found = $true
        $manifestHash = [Convert]::ToHexString([byte[]]$manifest[($entry + 32)..($entry + 63)])
        if ($manifest[$entry + 16] -ne 7 -or
            [BitConverter]::ToUInt32($manifest, $entry + 20) -ne 1 -or
            [BitConverter]::ToUInt64($manifest, $entry + 24) -ne $bytes.Length -or
            $manifestHash -ne $expectedHash) {
            throw "audio manifest entry differs from artifact: $virtualPath"
        }
    }
    if (-not $found) { throw "audio manifest entry is missing: $virtualPath" }
}

$first = Invoke-Cook 'valid-a'
$second = Invoke-Cook 'valid-b'
if ($first.Code -ne 0 -or $second.Code -ne 0) {
    throw "valid audio stamp cook failed: $($first.Text)`n$($second.Text)"
}
Assert-AudioArtifact $first.Output '0ff53a5b-bdb0-438a-abab-645a99062fd8' $wav 0 1
$audioOnlyAssets = Join-Path $run 'AudioOnlyAssets'
$audioOnlyDirectory = Join-Path $audioOnlyAssets 'Sounds'
New-Item -ItemType Directory -Path $audioOnlyDirectory -Force | Out-Null
$audioOnlyWav = Join-Path $audioOnlyDirectory 'probe.wav'
Copy-Item -LiteralPath $wav -Destination $audioOnlyWav
Copy-Item -LiteralPath $wavMeta -Destination ($audioOnlyWav + '.meta')
$audioOnlyOutput = Join-Path $run 'audio-only-output'
$audioOnlyLines = & $AssetCooker --asset-root $audioOnlyAssets --output $audioOnlyOutput 2>&1
if ($LASTEXITCODE -ne 0 -or ($audioOnlyLines -join "`n") -notmatch 'sourceIdentities=1' -or
    ($audioOnlyLines -join "`n") -notmatch 'audioClips=1') {
    throw "audio-only cook failed: $($audioOnlyLines -join "`n")"
}
Assert-AudioArtifact $audioOnlyOutput '0ff53a5b-bdb0-438a-abab-645a99062fd8' $audioOnlyWav 0 1
$manifestA = Join-Path $first.Output 'Derived\asset-manifest.cemf'
$manifestB = Join-Path $second.Output 'Derived\asset-manifest.cemf'
if (-not (Test-Path -LiteralPath $manifestA -PathType Leaf) -or
    -not (Test-Path -LiteralPath $manifestB -PathType Leaf) -or
    (Get-FileHash -LiteralPath $manifestA -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $manifestB -Algorithm SHA256).Hash -or
    $first.Text -notmatch 'sourceIdentities=2') {
    throw 'valid cook source identity table is missing or nondeterministic'
}

$bytes[47] = 1
[IO.File]::WriteAllBytes($wav, $bytes)
$stale = Invoke-Cook 'stale-output'
if ($stale.Code -eq 0 -or (Test-Path -LiteralPath $stale.Output) -or
    $stale.Text -notmatch 'audio source/meta stamp') {
    throw "stale audio source was not rejected before publication: $($stale.Text)"
}
$bytes[47] = 0
[IO.File]::WriteAllBytes($wav, $bytes)

$badSettings = $meta.Replace('loadMode: Auto', 'loadMode: Invalid')
[IO.File]::WriteAllText($wavMeta, $badSettings + "`n", [Text.UTF8Encoding]::new($false))
$invalid = Invoke-Cook 'invalid-settings-output'
if ($invalid.Code -eq 0 -or (Test-Path -LiteralPath $invalid.Output)) {
    throw "invalid audio import setting was not rejected: $($invalid.Text)"
}
[IO.File]::WriteAllText($wavMeta, $meta + "`n", [Text.UTF8Encoding]::new($false))

[IO.File]::WriteAllText((Join-Path $audioDirectory 'unsupported.ogg'), 'OggS')
$ogg = Invoke-Cook 'ogg-output'
if ($ogg.Code -eq 0 -or (Test-Path -LiteralPath $ogg.Output) -or
    $ogg.Text -notmatch 'OGG') {
    throw "OGG source was not rejected: $($ogg.Text)"
}
Remove-Item -LiteralPath (Join-Path $audioDirectory 'unsupported.ogg')

Remove-Item -LiteralPath $wavMeta
$missing = Invoke-Cook 'missing-meta-output'
if ($missing.Code -eq 0 -or (Test-Path -LiteralPath $missing.Output) -or
    $missing.Text -notmatch 'sidecar') {
    throw "audio source without meta was not rejected: $($missing.Text)"
}
[IO.File]::WriteAllText($wavMeta, $meta + "`n", [Text.UTF8Encoding]::new($false))

$stereo = Join-Path $audioDirectory 'stereo.wav'
$stereoBytes = [byte[]]$bytes.Clone()
[BitConverter]::GetBytes([uint16]2).CopyTo($stereoBytes, 22)
[BitConverter]::GetBytes([uint32]192000).CopyTo($stereoBytes, 28)
[BitConverter]::GetBytes([uint16]4).CopyTo($stereoBytes, 32)
[IO.File]::WriteAllBytes($stereo, $stereoBytes)
$stereoHash = (Get-FileHash -LiteralPath $stereo -Algorithm SHA256).Hash.ToLowerInvariant()
$stereoMeta = $meta.Replace('0ff53a5b-bdb0-438a-abab-645a99062fd8',
    'ad5532f6-5d94-4683-878b-d09a34345ee4').Replace($hash, $stereoHash)
[IO.File]::WriteAllText($stereo + '.meta', $stereoMeta + "`n", [Text.UTF8Encoding]::new($false))
$nonSpatial = Invoke-Cook 'stereo-nonspatial-output'
if ($nonSpatial.Code -ne 0) { throw "non-spatial stereo was rejected: $($nonSpatial.Text)" }
$pointMonoMeta = $stereoMeta.Replace('spatialKind: NonSpatial', 'spatialKind: PointMono')
[IO.File]::WriteAllText($stereo + '.meta', $pointMonoMeta + "`n", [Text.UTF8Encoding]::new($false))
$pointMono = Invoke-Cook 'stereo-pointmono-output'
if ($pointMono.Code -eq 0 -or (Test-Path -LiteralPath $pointMono.Output) -or
    $pointMono.Text -notmatch 'PointMono') {
    throw "PointMono accepted stereo source: $($pointMono.Text)"
}
Remove-Item -LiteralPath $stereo,($stereo + '.meta')

$formatMatrix = 'not-run'
if (-not [string]::IsNullOrWhiteSpace($FormatFixtures)) {
    $fixtureRoot = (Resolve-Path -LiteralPath $FormatFixtures).Path
    $formatCases = @(
        @{ Extension = 'wav'; Codec = 'Wav'; Original = 'sine.wav'; Tail = 'tail.wav'; Guid = '9a576ca5-e426-4ead-81f8-60d1f3074ad7' },
        @{ Extension = 'mp3'; Codec = 'Mp3'; Original = 'silent.mp3'; Tail = 'tail.mp3'; Guid = 'd348a706-44d6-45c9-a669-4e465d40f9d0' },
        @{ Extension = 'flac'; Codec = 'Flac'; Original = 'silent.flac'; Tail = 'tail.flac'; Guid = 'f24d735f-4497-4c35-831e-1167c31b5158' }
    )
    function Write-FormatMeta($case, [string]$source) {
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        $sourceSize = (Get-Item -LiteralPath $source).Length
        $text = "guid: $($case.Guid)`naudioClip:`n  schemaVersion: 1`n  loadMode: Auto`n  spatialKind: NonSpatial`n  codec: $($case.Codec)`n  payloadSize: $sourceSize`n  sourceContentHash: $sourceHash`n"
        [IO.File]::WriteAllText($source + '.meta', $text, [Text.UTF8Encoding]::new($false))
    }
    foreach ($case in $formatCases) {
        $original = Join-Path $fixtureRoot $case.Original
        $tail = Join-Path $fixtureRoot $case.Tail
        if (-not (Test-Path -LiteralPath $original -PathType Leaf) -or
            -not (Test-Path -LiteralPath $tail -PathType Leaf)) {
            throw "format fixture is missing: $original or $tail"
        }
        $target = Join-Path $audioDirectory ('valid.' + $case.Extension)
        Copy-Item -LiteralPath $original -Destination $target
        Write-FormatMeta $case $target
    }
    $complete = Invoke-Cook 'format-valid-output'
    if ($complete.Code -ne 0 -or $complete.Text -notmatch 'sourceIdentities=5') {
        throw "three-format cook failed: $($complete.Text)"
    }
    Assert-AudioArtifact $complete.Output '0ff53a5b-bdb0-438a-abab-645a99062fd8' $wav 0 1
    foreach ($case in $formatCases) {
        $target = Join-Path $audioDirectory ('valid.' + $case.Extension)
        $codecNumber = switch ($case.Extension) { 'wav' { 0 } 'mp3' { 1 } 'flac' { 2 } }
        Assert-AudioArtifact $complete.Output $case.Guid $target $codecNumber 1
    }
    foreach ($case in $formatCases) {
        $target = Join-Path $audioDirectory ('valid.' + $case.Extension)
        Copy-Item -LiteralPath (Join-Path $fixtureRoot $case.Tail) -Destination $target -Force
        Write-FormatMeta $case $target
        $truncated = Invoke-Cook ('tail-' + $case.Extension + '-output')
        if ($truncated.Code -eq 0 -or (Test-Path -LiteralPath $truncated.Output) -or
            $truncated.Text -notmatch 'audio encoded stream') {
            throw "self-consistent tail $($case.Extension) was not rejected: $($truncated.Text)"
        }
        Copy-Item -LiteralPath (Join-Path $fixtureRoot $case.Original) -Destination $target -Force
        Write-FormatMeta $case $target
    }
    foreach ($case in $formatCases) {
        $target = Join-Path $audioDirectory ('valid.' + $case.Extension)
        foreach ($shape in @('corrupt', 'truncated', 'oversized')) {
            Copy-Item -LiteralPath (Join-Path $fixtureRoot ($shape + '.' + $case.Extension)) `
                -Destination $target -Force
            Write-FormatMeta $case $target
            $malformed = Invoke-Cook ($shape + '-' + $case.Extension + '-output')
            if ($malformed.Code -eq 0 -or (Test-Path -LiteralPath $malformed.Output)) {
                throw "self-consistent $shape $($case.Extension) was not rejected: $($malformed.Text)"
            }
        }
        Copy-Item -LiteralPath (Join-Path $fixtureRoot $case.Original) -Destination $target -Force
        Write-FormatMeta $case $target
    }
    $formatMatrix = 'three-valid-three-tail-nine-malformed-reject'
}

Write-Output "AUDIO_COOK_STAMP_OK valid=2 audioOnly=pass artifact=manifest-header-payload-verified stale=reject invalidSettings=reject ogg=reject missingMeta=reject pointMonoStereo=reject formatMatrix=$formatMatrix output=$run"

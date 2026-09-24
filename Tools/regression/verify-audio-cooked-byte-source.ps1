param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$AssetPacker = '',
    [string]$FormatFixtures = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($AssetPacker)) {
    $AssetPacker = Join-Path $repo ("Bin\x64-$Configuration\Tools\AssetPacker\AssetPacker.exe")
}
$stampScript = Join-Path $PSScriptRoot 'verify-audio-cook-stamp.ps1'
$cooker = Join-Path $repo ("Bin\x64-$Configuration\Tools\AssetCooker\AssetCooker.exe")
$stampArguments = @{ AssetCooker = $cooker }
if (-not [string]::IsNullOrWhiteSpace($FormatFixtures)) {
    $stampArguments.FormatFixtures = $FormatFixtures
}
$stamp = @(& $stampScript @stampArguments 2>&1)
$outputLine = @($stamp | Where-Object { $_ -match '^AUDIO_COOK_STAMP_OK ' })
if ($outputLine.Count -ne 1 -or $outputLine[0] -notmatch ' output=(.+)$') {
    throw "audio cook fixture failed: $($stamp -join "`n")"
}
$run = $Matches[1]
$cooked = if ($FormatFixtures) { Join-Path $run 'format-valid-output' }
          else { Join-Path $run 'audio-only-output' }
$sourceDirectory = if ($FormatFixtures) { Join-Path $run 'Assets\Sounds' }
                   else { Join-Path $run 'AudioOnlyAssets\Sounds' }
$settings = Join-Path $run 'ProjectSetting'
New-Item -ItemType Directory -Path $settings -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $settings 'probe.txt'), 'audio cooked source probe')
$pak = Join-Path $run 'audio-cooked.pak'
$packed = @(& $AssetPacker --assets $cooked --settings $settings `
    --output $pak --list-entries 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($packed -join "`n") -notmatch '\[PAK-ENTRY\] Assets/Derived/Audio/0f/0ff53a5b-bdb0-438a-abab-645a99062fd8\.ceac') {
    throw "audio artifact was not packaged: $($packed -join "`n")"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'vswhere.exe was not found'
}
$install = @(& $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath)
if ($LASTEXITCODE -ne 0 -or $install.Count -eq 0) {
    throw 'MSVC x64 installation was not found'
}
$vcvars = Join-Path $install[0] 'VC\Auxiliary\Build\vcvars64.bat'
$probe = Join-Path $PSScriptRoot 'audio_cooked_source_probe.cpp'
$exe = Join-Path $run 'audio_cooked_source_probe.exe'
$objectDirectory = Join-Path $run 'audio-cooked-playback-objects'
New-Item -ItemType Directory -Path $objectDirectory -Force | Out-Null
$includeRender = Join-Path $repo 'Engine\RenderEngine'
$includeScene = Join-Path $repo 'Engine\SceneRuntime'
$includeUtility = Join-Path $repo 'Engine\Utility_Framework'
$includeMath = Join-Path $repo 'ThirdParty\Mathematics\include'
$renderLib = Join-Path $repo ("Build\Lib\x64-$Configuration\RenderEngine.lib")
if (-not (Test-Path -LiteralPath $renderLib -PathType Leaf)) {
    throw "$Configuration RenderEngine.lib is missing; build RenderEngine first"
}
$runtimeFlag = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc ' +
    '/std:c++latest /permissive- /Zc:__cplusplus /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN ' + $runtimeFlag + ' /W3 ' +
    '/I"' + $includeScene + '" /I"' + $includeRender + '" /I"' + $includeUtility +
    '" /I"' + $includeMath + '" /Fo:"' + $objectDirectory + '\\"' +
    ' /Fe:"' + $exe + '" "' + $probe +
    '" "' + (Join-Path $repo 'Engine\SceneRuntime\Audio\VoiceTable.cpp') +
    '" "' + (Join-Path $repo 'Engine\SceneRuntime\Audio\AudioRuntime.cpp') +
    '" "' + (Join-Path $repo 'Engine\SceneRuntime\Audio\NullAudioBackend.cpp') +
    '" "' + (Join-Path $repo 'Engine\SceneRuntime\Audio\MiniaudioBackend.cpp') +
    '" "' + $renderLib + '" normaliz.lib ole32.lib user32.lib advapi32.lib psapi.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "audio cooked source probe compile failed: exit $LASTEXITCODE"
}

$cases = @(
    @{ Guid = '0ff53a5b-bdb0-438a-abab-645a99062fd8'; Source = 'probe.wav' }
)
$deviceCases = 0
if ($FormatFixtures) {
    $cases += @(
        @{ Guid = '9a576ca5-e426-4ead-81f8-60d1f3074ad7'; Source = 'valid.wav' },
        @{ Guid = 'd348a706-44d6-45c9-a669-4e465d40f9d0'; Source = 'valid.mp3' },
        @{ Guid = 'f24d735f-4497-4c35-831e-1167c31b5158'; Source = 'valid.flac' }
    )
}
foreach ($case in $cases) {
    $guid = $case.Guid
    $virtualPath = "Derived/Audio/$($guid.Substring(0, 2))/$guid.ceac"
    $source = Join-Path $sourceDirectory $case.Source
    $probeOutput = @(& $exe $cooked $pak $source $guid $virtualPath 2>&1)
    if ($LASTEXITCODE -ne 0 -or
        ($probeOutput -join "`n") -notmatch 'AUDIO_COOKED_SOURCE_OK ') {
        throw "audio cooked source probe failed ($($case.Source)): $($probeOutput -join "`n")"
    }
    if (($probeOutput -join "`n") -match 'AUDIO_COOKED_PLAYBACK_OK device=pass') {
        ++$deviceCases
    }
}
Write-Output "AUDIO_COOKED_BYTE_SOURCE_OK configuration=$Configuration clips=$($cases.Count) deviceCases=$deviceCases loose=pass pak=pass encryptedCrossChunk=pass bounded=pass tamper=reject output=$run"

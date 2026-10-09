[CmdletBinding()]
param(
    [ValidateRange(1,3600)][int]$Seconds = 1800,
    [string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = Join-Path $repository 'Build\Validation\Phase22DeviceTransition'
[IO.Directory]::CreateDirectory($output) | Out-Null
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw 'VS 2026(v18) compiler environment was not found'
}
$sources = @('Tools\regression\audio\windows_device_transition_probe.cpp')
foreach ($name in @('VoiceTable','NullAudioBackend','AudioRuntime','AudioHost','ClipDirectory','MiniaudioBackend','SoundGraph','PlaybackService')) {
    $sources += "Engine\SceneRuntime\Audio\$name.cpp"
}
$inputs = @($sources | ForEach-Object { Join-Path $repository $_ })
$inputs += Join-Path $repository 'Tools\regression\audio_voice_contract_probe.cpp'
$inputs += @(Get-ChildItem (Join-Path $repository 'Engine\SceneRuntime\Audio') -File -Filter '*.h' | Select-Object -ExpandProperty FullName)
$inputs += @(Get-ChildItem (Join-Path $repository 'Tools\regression\audio') -File -Filter '*.inl' | Select-Object -ExpandProperty FullName)
$inputs += Join-Path $repository 'Tools\regression\audio_fixture_decoder.h'
$before = @{}
foreach ($inputPath in $inputs) {
    $before[$inputPath] = (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash
}
$before | ConvertTo-Json | Set-Content (Join-Path $output 'source-sha256.json') -Encoding UTF8
$includes = @('Engine\SceneRuntime','Engine\RenderEngine','Engine\Utility_Framework','ThirdParty\Mathematics\include','Tools\regression')
$includeArgs = ($includes | ForEach-Object { '/I"' + (Join-Path $repository $_) + '"' }) -join ' '
$sourceArgs = ($sources | ForEach-Object { '"' + (Join-Path $repository $_) + '"' }) -join ' '
$executable = Join-Path $output 'windows_device_transition_probe.exe'
$batch = Join-Path $output 'build.cmd'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "$output"
cl.exe /nologo /std:c++latest /EHsc /utf-8 /W4 /DNOMINMAX /DWAVE_AUDIO_PROBE /DMA_ENABLE_ONLY_SPECIFIC_BACKENDS /DMA_ENABLE_WASAPI /O2 /DNDEBUG /MT $includeArgs $sourceArgs /Fe"$executable" /link ole32.lib winmm.lib psapi.lib
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ASCII
& cmd.exe /d /c $batch *> (Join-Path $output 'build.log')
if ($LASTEXITCODE -ne 0) {
    throw 'Windows hardware probe compilation failed; see build.log'
}

Write-Output ('AUDIO_DEVICE_TRANSITION_BUILD_OK executable=' + $executable)

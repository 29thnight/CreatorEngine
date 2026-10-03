param([Parameter(Mandatory)][string]$Project)

# Archived fixtures keep render settings, but must not reset the user's UI scale.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$currentSettings = Join-Path $repo 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
$fixtureSettings = Join-Path $Project 'ProjectSetting/EngineSettings.asset'
if (!(Test-Path -LiteralPath $currentSettings)) { return }
$pattern = '(?m)^imguiScale:[^\r\n]*'
$current = [IO.File]::ReadAllText($currentSettings)
$match = [regex]::Match($current, $pattern)
if (!$match.Success) { return }
$text = $match.Value.Substring('imguiScale:'.Length).Trim()
[float]$scale = 0
if (![float]::TryParse($text, [Globalization.NumberStyles]::Float,
        [Globalization.CultureInfo]::InvariantCulture, [ref]$scale) -or
    [float]::IsNaN($scale) -or [float]::IsInfinity($scale) -or $scale -le 0) {
    throw 'Current project imguiScale must be a positive finite value.'
}
$fixture = if (Test-Path -LiteralPath $fixtureSettings) { [IO.File]::ReadAllText($fixtureSettings) } else { '' }
$updated = if ([regex]::IsMatch($fixture, $pattern)) {
    [regex]::Replace($fixture, $pattern, $match.Value)
} else { $fixture.TrimEnd() + "`n" + $match.Value + "`n" }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fixtureSettings)) | Out-Null
[IO.File]::WriteAllText($fixtureSettings, $updated)

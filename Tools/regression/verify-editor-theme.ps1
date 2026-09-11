[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-theme'),
    [ValidateSet('dx12', 'vulkan')][string[]]$Backends = @('dx12', 'vulkan')
)
# W1: backend별 시작 user scale 1.0 -> 1.5 -> 1.0, token/font/DPI 제품 관측.
# selftest의 합성 DPI 왕복과 실제 모니터 DPI 관측을 구분한다.
# 실제 창을 100%/150% 모니터 사이로 이동하는 검사는 이 게이트에 없다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$settingsPath = Join-Path $repoRoot 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
$iniPath = Join-Path (Split-Path $Exe) 'Saved/Config/imgui.ini'
$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function SameBytes([byte[]]$Left, [byte[]]$Right) {
    return [Convert]::ToBase64String($Left) -ceq [Convert]::ToBase64String($Right)
}
Assert (Test-Path -LiteralPath $Exe) "Editor executable not found: $Exe"
Assert (Test-Path -LiteralPath $settingsPath) "Project settings not found: $settingsPath"
Assert ($Backends.Count -gt 0) 'No backend requested; the gate would be vacuous.'
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw 'Close the existing editor before running this isolated gate.'
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)
$settingsText = [IO.File]::ReadAllText($settingsPath)
$hadIni = Test-Path -LiteralPath $iniPath
$iniBytes = if ($hadIni) { [IO.File]::ReadAllBytes($iniPath) } else { [byte[]]@() }
$backendPattern = '(?m)(^render:\r?\n\s{2}backend: )\w+'
$scalePattern = '(?m)(^imguiScale: )[^\r\n]+'
Assert ([regex]::Matches($settingsText, $backendPattern).Count -eq 1) 'Expected one top-level render.backend.'
Assert ([regex]::Matches($settingsText, $scalePattern).Count -eq 1) 'Expected one imguiScale setting.'
$utf8 = [Text.UTF8Encoding]::new($false)
$summary = [Collections.Generic.List[object]]::new()
$ownedProcess = $null
$completed = $false
try {
    foreach ($backend in $Backends) {
        $index = 0
        foreach ($userScale in @(1.0, 1.5, 1.0)) {
            if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
                throw 'An editor is already running; no existing process will be stopped.'
            }
            $index++
            $case = "$backend-$index"
            $scaleText = $userScale.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture)
            $configured = [regex]::Replace($settingsText, $backendPattern, '${1}' + $backend)
            $configured = [regex]::Replace($configured, $scalePattern, '${1}' + $scaleText)
            [IO.File]::WriteAllText($settingsPath, $configured, $utf8)
            $scriptPath = Join-Path $Work "$case.txt"
            $resultPath = Join-Path $Work "$case.jsonl"
            $stdoutPath = Join-Path $Work "$case.out"
            $stderrPath = Join-Path $Work "$case.err"
            foreach ($path in @($resultPath, $stdoutPath, $stderrPath)) {
                if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
            }
            Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @('wait 45', 'editor.selftest', 'editor.theme', 'quit')
            $ownedProcess = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
                -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                                '--result-file', ('"' + $resultPath + '"')) `
                -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
            if (-not $ownedProcess.WaitForExit(180000)) {
                $ownedProcess.Kill()
                $ownedProcess.WaitForExit()
                throw "$case timed out after 180s; only the gate-owned process was stopped. See $stdoutPath"
            }
            $exitCode = $ownedProcess.ExitCode
            $ownedProcess = $null
            # 결과를 먼저 읽는다. 명령 실패의 상세 원인을 exited 4로 덮지 않는다.
            Assert (Test-Path -LiteralPath $resultPath) "${case}: no result file (exit $exitCode); see $stdoutPath"
            $results = @{}
            foreach ($line in Get-Content -LiteralPath $resultPath) {
                if ([string]::IsNullOrWhiteSpace($line)) { continue }
                $parsed = $line | ConvertFrom-Json
                $results[$parsed.command] = $parsed
            }
            foreach ($command in @('editor.selftest', 'editor.theme')) {
                Assert ($results.ContainsKey($command)) "$case missing $command result."
                Assert ($results[$command].status -eq 'succeeded') "$case $command failed: $($results[$command].message)"
            }
            $selftest = $results['editor.selftest'].data
            Assert ($selftest.windows -and $selftest.menus -and $selftest.theme) "$case selftest failed: $($results['editor.selftest'].message)"
            $theme = $results['editor.theme'].data
            Assert ($theme.clean -and $theme.scaleMatches) "$case theme audit failed: $($results['editor.theme'].message)"
            Assert ([Math]::Abs($theme.fontScaleMain - $userScale) -lt 0.001) "$case user scale differs from $scaleText."
            Assert ($theme.windowDpiScale -gt 0) "$case OS window DPI is unavailable."
            Assert ($theme.perMonitorDpiAware -eq $true) "$case executable did not enable PerMonitorV2 DPI awareness."
            Assert ($theme.dpiMatches -and [Math]::Abs($theme.fontScaleDpi - $theme.windowDpiScale) -lt 0.001 `
                -and [Math]::Abs($theme.viewportDpiScale - $theme.windowDpiScale) -lt 0.001) "$case window/viewport/font DPI differ."
            Assert ($theme.geometryMatches -and $theme.themeMappingMatches) "$case geometry or theme mapping differs."
            Assert ($theme.bodyFontPresent -and $theme.iconFontMerged -and $theme.fontFallbackProbeOk) "$case font or fallback contract failed."
            $stdout = [IO.File]::ReadAllText($stdoutPath)
            Assert ($stdout -match "\[RenderBackend\].*active=$backend scene=$backend imgui=$backend") "$case did not run the requested scene/ImGui backend."
            Assert ($stdout -match '(?m)^body\t[^\r\n]*Fonts[\\/]Inter-Regular\.ttf\s*$') "$case bundled Inter was not consumed; see $stdoutPath"
            Assert ($exitCode -eq 0) "$case exited $exitCode despite successful command data."
            $summary.Add([pscustomobject]@{ Backend=$backend; StartupUserScale=$userScale;
                ObservedWindowDpi=$theme.windowDpiScale; FontDpi=$theme.fontScaleDpi;
                RenderedFontSize=$theme.renderedFontSize; Result='PASS' })
        }
    }
    $completed = $true
}
finally {
    if ($null -ne $ownedProcess -and -not $ownedProcess.HasExited) {
        $ownedProcess.Kill()
        $ownedProcess.WaitForExit()
    }
    [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
    if ($hadIni) { [IO.File]::WriteAllBytes($iniPath, $iniBytes) }
    elseif (Test-Path -LiteralPath $iniPath) { Remove-Item -LiteralPath $iniPath }
    Assert (SameBytes ([IO.File]::ReadAllBytes($settingsPath)) $settingsBytes) 'Settings byte restoration failed.'
    if ($hadIni) { Assert (SameBytes ([IO.File]::ReadAllBytes($iniPath)) $iniBytes) 'imgui.ini byte restoration failed.' }
    else { Assert (-not (Test-Path -LiteralPath $iniPath)) 'Unexpected imgui.ini remains after restoration.' }
    $summary | Export-Csv -LiteralPath (Join-Path $Work 'summary.csv') -NoTypeInformation -Encoding UTF8
}
if ($completed) {
    $summary | Format-Table -AutoSize
    "Editor theme PASS: $($summary.Count) startup cases, $script:checks checks; settings/ini restored byte-for-byte."
    'User scale: 100 -> 150 -> 100% across starts. DPI: current OS value observed; 100/150% transitions were synthetic selftests, not physical monitor moves.'
}

[CmdletBinding()]
param(
    [ValidateSet('Debug')][string]$Configuration = 'Debug',
    [string]$VisualStudioInstallation = '',
    [string]$ExpectedYaml = ''
)

# Compile against the actual generated RenderEngine descriptors and check a
# representative RenderPassSettings YAML round trip. Debug requires a fresh
# engine build and generated descriptors.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GCCEProbe.ps1')

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $ExpectedYaml) {
    $ExpectedYaml = Join-Path $PSScriptRoot 'fixtures/RenderPassSettings.baseline.yaml'
}
$probe = Join-Path $PSScriptRoot 'RenderPassSettingsRoundtripProbe.cpp'
$utilityInclude = Join-Path $repoRoot 'Engine\Utility_Framework'
$contractInclude = Join-Path $repoRoot 'Build/Generated/Contracts/x64-Debug'
$mathInclude = Join-Path $repoRoot 'ThirdParty\Mathematics\include'
$vcpkgRoot = Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows'
$vcpkgInclude = Join-Path $vcpkgRoot 'include'
$vcpkgLib = Join-Path $vcpkgRoot 'debug\lib'
$vcpkgBin = Join-Path $vcpkgRoot 'debug\bin'
$engineLib = Join-Path $repoRoot 'Build\Lib\x64-Debug'
$utilityLib = Join-Path $engineLib 'Utility_Framework.lib'
$gcce = Get-GCCEProbeSettings -Repository $repoRoot -Configuration $Configuration

foreach ($required in @($probe, $utilityInclude, $mathInclude, $vcpkgInclude, $vcpkgLib)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "리플렉션 왕복 게이트 입력이 없다: $required"
    }
}
if (-not (Test-Path -LiteralPath $utilityLib -PathType Leaf)) {
    # 건너뛰지 않는다. 없으면 없다고 말한다 — 조용히 건너뛴 칸은 세트에서
    # 사라진 것과 구별되지 않는다.
    throw "Utility_Framework.lib 이 없다(먼저 Debug 로 빌드하라): $utilityLib"
}

if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe was not found.'
    }
    $installations = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) {
        throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
    }
    $VisualStudioInstallation = $installations[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "vcvars64.bat was not found: $vcvars"
}

$outputDirectory = Join-Path $repoRoot 'Build\Obj\RenderPassSettingsRoundTrip\x64-Debug'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$executable = Join-Path $outputDirectory 'render_pass_settings_roundtrip_probe.exe'
if (Test-Path -LiteralPath $executable -PathType Leaf) {
    # 낡은 exe 를 재는 사고를 구조적으로 막는다 — 컴파일이 실패해도 앞선 실행분이
    # 남아 초록을 내는 것이 이 저장소가 이미 겪은 실패 양식이다.
    Remove-Item -LiteralPath $executable -Force
}

# /wd4828: 엔진 헤더 주석 일부가 CP949 바이트로 남아 있다(별도 이관 대상).
$command = 'call "' + $vcvars + '" >nul && cl.exe ' +
    '/nologo /TP /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
    '/Zc:preprocessor /DCE_CONTRACT_STANDALONE /utf-8 /W3 /wd4828 /MDd /Od /D_DEBUG ' + $gcce.CompileArguments + ' ' +
    '/I"' + $utilityInclude + '" /I"' + $contractInclude + '" /I"' + $mathInclude + '" ' +
    '/external:I"' + $vcpkgInclude + '" /external:W0 ' +
    '/Fo:"' + $outputDirectory + '\\" /Fe:"' + $executable + '" "' + $probe + '" ' +
    '/link /LIBPATH:"' + $vcpkgLib + '" /LIBPATH:"' + $engineLib + '" ' +
    'Utility_Framework.lib ryml.lib c4core.lib spdlogd.lib fmtd.lib Ole32.lib ' + $gcce.LinkArguments

Write-Host '[RENDER SETTINGS ROUNDTRIP] compiling Debug'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "리플렉션 왕복 프로브 컴파일이 실패했다 (exit $LASTEXITCODE)."
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw '컴파일은 성공했다는데 프로브 실행 파일이 없다.'
}

$previousPath = $env:PATH
$env:PATH = $gcce.RuntimeDirectory + ";$vcpkgBin;$previousPath"
try {
    $output = & $executable 2>&1
    $exitCode = $LASTEXITCODE
} finally {
    $env:PATH = $previousPath
}

$output | ForEach-Object { Write-Host $_ }

if ($exitCode -ne 0) {
    throw "리플렉션 컨테이너 왕복이 실패했다 (exit $exitCode)."
}


$yaml = ($output -join "`n").TrimEnd("`r", "`n")
foreach ($required in @('aa:', 'bloom:', 'ssgi:', 'colorGrading:', 'skyboxTextureName: Assets/sky')) {
    if (-not $yaml.Contains($required)) { throw "Missing serialized field: $required" }
}
if ($ExpectedYaml) {
    $expected = (Get-Content -LiteralPath $ExpectedYaml -Raw).Replace("`r`n", "`n").TrimEnd("`r", "`n")
    if ($yaml -cne $expected) { throw 'Render settings YAML changed from the baseline.' }
}
Write-Host '[RENDER SETTINGS ROUNDTRIP] PASS'

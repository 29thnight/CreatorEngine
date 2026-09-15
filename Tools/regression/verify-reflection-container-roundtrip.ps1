[CmdletBinding()]
param(
    [ValidateSet('Debug')]
    [string]$Configuration = 'Debug',
    [string]$VisualStudioInstallation = '',

    # 기대 축 수. 프로브가 재는 축이 줄어들면 붉어진다 — 단정을 지워 놓고
    # "전부 통과" 로 남는 것이 골든류 검사가 눈머는 가장 흔한 경로다.
    [int]$ExpectedAxes = 28
)

# 리플렉션 컨테이너 값 왕복 계약 (range 일반화 · 런타임 축)
#
# ── 분류 게이트와 무엇이 다른가 ─────────────────────────────────────────
#
# `verify-reflection-container.ps1` 은 **형상 판정**을 잡는다: 무엇이 시퀀스이고
# 무엇이 맵이며 어떤 수단으로 채우는가. 그 판정이 옳아도 값이 돌아온다는 보장은
# 없다 — 키를 적는 표기와 읽는 표기가 어긋나거나, 빈 컨테이너가 한쪽 방향에서만
# 널로 읽히거나, 고정 크기 배열의 꼬리가 갱신되지 않을 수 있다.
#
# 이 게이트는 값을 실제로 저장하고 다시 읽는다. 더해서 **방출 텍스트의 형상**도
# 본다 — 값이 왕복해도 형상은 틀릴 수 있고(맵을 쌍의 시퀀스로 적어도 왕복은
# 성립한다), 디스크 호환의 정본은 형상이다.
#
# ── 어느 바이너리를 재는가 ──────────────────────────────────────────────
#
# 직렬화기는 전부 템플릿이라 **프로브 TU 에서 새로 컴파일된다.** 재는 축은 항상
# 현재 소스다. 링크되는 `Utility_Framework.lib` 는 로거·레지스트리·ryml 경계 같은
# 비템플릿 곁다리만 공급하므로 그쪽이 낡아도 판정 축은 낡지 않는다.
#
# 그래서 Debug 만 돈다 — 링크 대상 lib 이 Debug 런타임(/MDd)으로 지어져 있다.
# 최적화·어설션 제거에 기대지 않는다는 축은 분류 게이트가 Debug/Release 양쪽으로
# 이미 든다.
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-reflection-container-roundtrip.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$probe = Join-Path $PSScriptRoot 'reflection_container_roundtrip_probe.cpp'
$utilityInclude = Join-Path $repoRoot 'Engine\Utility_Framework'
$mathInclude = Join-Path $repoRoot 'ThirdParty\Mathematics\include'
$vcpkgRoot = Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows'
$vcpkgInclude = Join-Path $vcpkgRoot 'include'
$vcpkgLib = Join-Path $vcpkgRoot 'debug\lib'
$vcpkgBin = Join-Path $vcpkgRoot 'debug\bin'
$engineLib = Join-Path $repoRoot 'Build\Lib\x64-Debug'
$utilityLib = Join-Path $engineLib 'Utility_Framework.lib'

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

$outputDirectory = Join-Path $repoRoot 'Build\Obj\ReflectionContainerRoundTrip\x64-Debug'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$executable = Join-Path $outputDirectory 'reflection_container_roundtrip_probe.exe'
if (Test-Path -LiteralPath $executable -PathType Leaf) {
    # 낡은 exe 를 재는 사고를 구조적으로 막는다 — 컴파일이 실패해도 앞선 실행분이
    # 남아 초록을 내는 것이 이 저장소가 이미 겪은 실패 양식이다.
    Remove-Item -LiteralPath $executable -Force
}

# /wd4828: 엔진 헤더 주석 일부가 CP949 바이트로 남아 있다(별도 이관 대상).
$command = 'call "' + $vcvars + '" >nul && cl.exe ' +
    '/nologo /TP /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
    '/Zc:preprocessor /utf-8 /W3 /wd4828 /MDd /Od /D_DEBUG ' +
    '/I"' + $utilityInclude + '" /I"' + $mathInclude + '" ' +
    '/external:I"' + $vcpkgInclude + '" /external:W0 ' +
    '/Fo:"' + $outputDirectory + '\\" /Fe:"' + $executable + '" "' + $probe + '" ' +
    '/link /LIBPATH:"' + $vcpkgLib + '" /LIBPATH:"' + $engineLib + '" ' +
    'Utility_Framework.lib ryml.lib c4core.lib spdlogd.lib fmtd.lib Ole32.lib'

Write-Host '[REFLECTION ROUNDTRIP] compiling Debug'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "리플렉션 왕복 프로브 컴파일이 실패했다 (exit $LASTEXITCODE)."
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw '컴파일은 성공했다는데 프로브 실행 파일이 없다.'
}

$previousPath = $env:PATH
$env:PATH = "$vcpkgBin;$previousPath"
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

# ── 판정을 종료 코드 하나에 걸지 않는다 ─────────────────────────────────
#
# 프로브가 축을 하나도 재지 않아도 종료 코드는 0 이다. 축 수와 요약 줄을 따로
# 단정해 "빈 집합을 성공으로 읽는" 길을 막는다.
$summary = $output | Select-String -Pattern '^\[REFLECTION ROUNDTRIP\] axes=(\d+) failures=(\d+)$'
if (-not $summary) {
    throw '요약 줄이 없다 — 프로브가 판정을 내지 않았다.'
}
$axes = [int]$summary.Matches[0].Groups[1].Value
$failures = [int]$summary.Matches[0].Groups[2].Value

if ($failures -ne 0) {
    throw "왕복 실패 축 $failures 건."
}
if ($axes -lt $ExpectedAxes) {
    throw "재는 축이 줄었다: $axes (기대 $ExpectedAxes 이상). 단정을 지웠는지 확인하라."
}

$mismatches = @($output | Select-String -Pattern 'MISMATCH')
if ($mismatches.Count -ne 0) {
    throw ("축 표기와 요약이 어긋난다 — MISMATCH {0}건인데 요약은 0 이다." -f $mismatches.Count)
}

Write-Host ("[REFLECTION ROUNDTRIP] Debug verification passed ({0} axes)." -f $axes) -ForegroundColor Green

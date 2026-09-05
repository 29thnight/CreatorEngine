[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = '',
    [string]$Only = ''
)

# experiment 합성 계약 게이트 — 에디터를 띄우지 않는다.
#
# ★ 이 검사들은 원래 `experiment.*` CLI 명령이었다. 엔진 프로세스가 필요 없는
#   합성 단정이라 명령 registry 에 있을 이유가 없었다. 선례
#   (`verify-authoring-base64.ps1` · `verify-hashing-string.ps1` ·
#   `verify-mathematics-contract.ps1`)와 같은 모양이다.
#
# ★★ **경고 수준을 소스 출처별로 나눈다.** probe 와 self-test 는 `/W4 /WX` 로,
#   엔진 소스는 `/W0` 로 짓는다. 엔진 `.cpp` 는 이 게이트가 고칠 코드가 아닌데
#   `/WX` 를 걸면 남의 경고로 이 게이트가 붉어진다(실제로
#   `CookedAssetManifest.cpp` 가 그렇게 걸렸다). 엔진 **헤더**는 `/external:W0`
#   으로 같은 이유에서 뺀다.
#
# ★★★ **include 순서가 계약의 일부다.** `VertexCacheOptimization.cpp` 는 vcpkg 의
#   `meshoptimizer.h` 를 노리고 `"meshoptimizer.h"` 를 include 하는데, Windows 는
#   대소문자를 구분하지 않아 `Engine/RenderEngine/MeshOptimizer.h`(엔진 자신의
#   클래스)로 풀린다. 엔진이 무사한 것은 RenderEngine 과 RenderTests 두 프로젝트가
#   include 순서를 서로 다르게 두기 때문이다. 이 게이트는 그 TU 를 쓰지 않으므로
#   지금은 걸리지 않지만, `cacheopt` 를 여기로 옮길 때 되살아난다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$probe = Join-Path $PSScriptRoot 'experiment_contract_probe.cpp'
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    throw "probe 원본이 없다: $probe"
}

if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe 를 찾지 못했다.'
    }
    $installations = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) {
        throw 'x64 C++ 툴체인이 있는 Visual Studio 설치를 찾지 못했다.'
    }
    $VisualStudioInstallation = $installations[0]
}

$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "vcvars64.bat 을 찾지 못했다: $vcvars"
}

$vcpkgInclude = Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows\include'
$includeDirs = @(
    (Join-Path $repoRoot 'Engine\RenderEngine')
    (Join-Path $repoRoot 'Engine\RenderEngine\Experiment')
    (Join-Path $repoRoot 'Engine\Utility_Framework')
    (Join-Path $repoRoot 'Engine\SceneRuntime')
    (Join-Path $repoRoot 'ThirdParty\Mathematics\include')
    (Join-Path $repoRoot 'ThirdParty\mikktspace')
    $vcpkgInclude
)
foreach ($dir in $includeDirs) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        throw "include 디렉터리가 없다: $dir"
    }
}

# 엔진 소스 — 검사 대상 알고리즘의 구현. `/W0` 로 짓는다.
$engineSources = @(
    'Engine\RenderEngine\Experiment\Import\ImportedScene.cpp'
    'Engine\RenderEngine\Experiment\Import\VertexWelding.cpp'
    'Engine\RenderEngine\Experiment\Import\SceneToModelDraft.cpp'
    'Engine\RenderEngine\Experiment\Import\NormalGeneration.cpp'
    'Engine\RenderEngine\Experiment\Import\TangentGeneration.cpp'
    'Engine\RenderEngine\Experiment\Cooked\ResolvingModelDecoder.cpp'
    'Engine\RenderEngine\Experiment\PoseSampler.cpp'
    'ThirdParty\mikktspace\mikktspace.c'
) | ForEach-Object { Join-Path $repoRoot $_ }

# probe 와 self-test — 이 게이트가 소유하는 코드. `/W4 /WX`.
$probeSources = @($probe) + (@(
    'ExperimentResolverSelfTest'
    'ExperimentSamplerSelfTest'
    'ExperimentWeldSelfTest'
) | ForEach-Object {
    Join-Path $repoRoot ("Editor\RenderTests\ExperimentParity\{0}.cpp" -f $_)
})

foreach ($source in ($engineSources + $probeSources)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "원본이 없다: $source"
    }
}

$includeFlags  = ($includeDirs | ForEach-Object { '/I"' + $_ + '"' }) -join ' '
$externalFlags = '/external:W0 ' +
    (($includeDirs | ForEach-Object { '/external:I"' + $_ + '"' }) -join ' ')
$probeIncludes = '/I"' + (Join-Path $repoRoot 'Editor\RenderTests') + '"'

$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') }
                  else { @($Configuration) }

foreach ($current in $configurations) {
    $outputDirectory = Join-Path $repoRoot `
        ("Build\Obj\ExperimentContract\x64-{0}" -f $current)
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'experiment_contract_probe.exe'

    $configurationArguments = if ($current -eq 'Debug') {
        '/MDd /Od /RTC1 /Zi /D_DEBUG'
    } else {
        '/MD /O2 /Ob2 /DNDEBUG'
    }
    # ★ `/Fd:` 를 반드시 준다. Debug 는 `/Zi` 라 PDB 를 쓰는데, 주지 않으면
    #   **작업 디렉터리**(저장소 루트)에 `vc<버전>.pdb` 를 만들려 하고 그것이
    #   실패하면 컴파일 전체가 exit 2 로 죽는다 — 원인이 소스에 없어서 오래 찾는다.
    #
    # ★★ 아래 `/Fo:` 의 `\\"` 는 오타가 아니다. 디렉터리로 주려면 경로가
    #   백슬래시로 끝나야 하는데 `"...\"` 로 쓰면 그 백슬래시가 **닫는 따옴표를
    #   이스케이프**해서 뒤에 오는 소스 목록이 통째로 먹힌다. cl 은 그때
    #   `D8003: 소스 파일 이름이 없습니다` 만 내고, 원인이 인용 부호라는 말은
    #   하지 않는다.
    $common = '/nologo /c /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
        '/utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fd:"' + $outputDirectory +
        '\experiment_contract.pdb" ' + $configurationArguments

    Write-Host ("[EXPERIMENT CONTRACT] {0} 엔진 소스 컴파일" -f $current)
    $engineCommand = 'call "' + $vcvars + '" >nul && cl.exe ' + $common +
        ' /W0 ' + $includeFlags + ' /Fo:"' + $outputDirectory + '\\" ' +
        (($engineSources | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    & $env:ComSpec /d /s /c $engineCommand
    if ($LASTEXITCODE -ne 0) {
        throw "엔진 소스 컴파일 실패 ($current): exit $LASTEXITCODE"
    }

    Write-Host ("[EXPERIMENT CONTRACT] {0} probe 컴파일 (/W4 /WX)" -f $current)
    $probeCommand = 'call "' + $vcvars + '" >nul && cl.exe ' + $common +
        ' /W4 /WX ' + $probeIncludes + ' ' + $externalFlags +
        ' /Fo:"' + $outputDirectory + '\\" ' +
        (($probeSources | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    & $env:ComSpec /d /s /c $probeCommand
    if ($LASTEXITCODE -ne 0) {
        throw "probe 컴파일 실패 ($current): exit $LASTEXITCODE"
    }

    $objects = @(Get-ChildItem -LiteralPath $outputDirectory -Filter '*.obj' |
        ForEach-Object { '"' + $_.FullName + '"' })
    if ($objects.Count -eq 0) {
        throw "링크할 obj 가 없다 ($current) — 컴파일이 조용히 아무것도 내지 않았다."
    }

    $linkCommand = 'call "' + $vcvars + '" >nul && link.exe /nologo /OUT:"' +
        $executable + '" ' + ($objects -join ' ')
    & $env:ComSpec /d /s /c $linkCommand
    if ($LASTEXITCODE -ne 0) {
        throw "링크 실패 ($current): exit $LASTEXITCODE"
    }

    # ★ exe 가 실제로 생겼는지 본다. 링크가 0 을 내고도 산출물이 없으면 아래
    #   실행이 조용히 건너뛰어질 수 있다 — 그런 초록은 없는 것만 못하다.
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "링크가 성공을 냈는데 산출물이 없다: $executable"
    }

    Write-Host ("[EXPERIMENT CONTRACT] {0} 실행" -f $current)
    if ([string]::IsNullOrWhiteSpace($Only)) { & $executable }
    else { & $executable $Only }
    if ($LASTEXITCODE -ne 0) {
        throw "experiment 합성 계약 실패 ($current): exit $LASTEXITCODE"
    }
}

Write-Host ("[EXPERIMENT CONTRACT] {0} 통과 — 에디터 없이 검증했다." -f
    ($configurations -join '/')) -ForegroundColor Green

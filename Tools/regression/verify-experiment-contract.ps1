[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = '',
    [string]$Only = ''
)

# experiment 합성 계약 게이트 — 에디터를 띄우지 않는다.
#
# ★ 이 검사들은 원래 `experiment.*` CLI 명령이었다. 엔진 **프로세스**가 필요 없는
#   합성 단정이라 명령 registry 에 있을 이유가 없었다. 씬도, 디바이스도, registry
#   도 건드리지 않는다.
#
# ★★ **선례 셋과 다른 점이 하나 있다.** `verify-authoring-base64.ps1` ·
#   `verify-hashing-string.ps1` · `verify-mathematics-contract.ps1` 은 probe 하나만
#   컴파일해 링크 없이 돈다. 이쪽은 `Mesh` · `MeshOptimizer` · `Core::TimeSystem` ·
#   `Material` · `Texture` · rapidyaml 의 **정의**를 요구해서 엔진 정적 라이브러리를
#   링크한다.
#
#   그러므로 이 게이트는 **엔진이 먼저 지어져 있어야 한다**(`Build/Lib/x64-<구성>`
#   과 `Bin/x64-<구성>/Editor` 의 DLL). 링크할 .cpp 를 하나씩 찾아 끌어오는 대안도
#   있었지만 폐포가 계속 자랐다 — 실측으로 `texcook` 하나가 rapidyaml 전체를 끌고
#   왔다. 라이브러리를 링크하는 편이 정직하고 작다.
#
#   **얻는 것은 그대로다**: 에디터 프로세스 없이, GPU 없이, 씬 없이 돈다.
#
# ★★★ **경고 수준을 소스 출처별로 나눈다.** probe 와 self-test 는 `/W4 /WX` 로
#   짓고, 엔진 **헤더**는 `/external:W0` 로 뺀다. 엔진 헤더의 기존 경고로 이
#   게이트가 붉어지면 고칠 수 없는 것을 눈금에 넣는 셈이다.

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

$vcpkgRoot    = Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows'
$vcpkgInclude = Join-Path $vcpkgRoot 'include'
$includeDirs = @(
    (Join-Path $repoRoot 'Engine\RenderEngine')
    (Join-Path $repoRoot 'Engine\RenderEngine\Experiment')
    (Join-Path $repoRoot 'Engine\Utility_Framework')
    (Join-Path $repoRoot 'Engine\SceneRuntime')
    (Join-Path $repoRoot 'ThirdParty\Mathematics\include')
    $vcpkgInclude
)
foreach ($dir in $includeDirs) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        throw "include 디렉터리가 없다: $dir"
    }
}

# probe 와 self-test — 이 게이트가 소유하는 코드.
$probeSources = @($probe) + (@(
    'ExperimentCacheOptSelfTest'
    'ExperimentMaterialCodecSelfTest'
    'ExperimentMaterialInstanceSelfTest'
    'ExperimentMaterialSealSelfTest'
    'ExperimentResolverSelfTest'
    'ExperimentSamplerSelfTest'
    'ExperimentShaderMetaCookSelfTest'
    'ExperimentTextureCookSelfTest'
    'ExperimentVertexLayoutSelfTest'
    'ExperimentWeldSelfTest'
) | ForEach-Object {
    Join-Path $repoRoot ("Editor\RenderTests\ExperimentParity\{0}.cpp" -f $_)
})
foreach ($source in $probeSources) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "원본이 없다: $source"
    }
}

$engineLibNames = @('RenderEngine', 'SceneRuntime', 'Utility_Framework')

# ★ vcpkg 는 몇몇 포트의 Debug 산출물에 `d` 접미사를 붙인다(`fmtd` · `spdlogd` ·
#   `lz4d`). 붙이지 않는 것도 같이 있어서(`ryml` · `c4core` · `meshoptimizer` ·
#   `DirectXTex`) 구성별로 이름을 따로 적는다. 한쪽 이름만 쓰면 다른 구성에서
#   "라이브러리가 없다" 로 죽는다.
$vendorLibNamesByConfig = @{
    Debug   = @('meshoptimizer', 'ryml', 'c4core', 'DirectXTex', 'lz4d', 'fmtd', 'spdlogd')
    Release = @('meshoptimizer', 'ryml', 'c4core', 'DirectXTex', 'lz4',  'fmt',  'spdlog')
}

$externalFlags = '/external:W0 ' +
    (($includeDirs | ForEach-Object { '/external:I"' + $_ + '"' }) -join ' ')
$probeIncludes = '/I"' + (Join-Path $repoRoot 'Editor\RenderTests') + '"'

$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') }
                  else { @($Configuration) }

foreach ($current in $configurations) {
    $libDir = Join-Path $repoRoot ("Build\Lib\x64-{0}" -f $current)
    $dllDir = Join-Path $repoRoot ("Bin\x64-{0}\Editor" -f $current)
    # vcpkg 는 Debug 산출물을 debug\lib 아래 둔다.
    $vendorLibDir = if ($current -eq 'Debug') { Join-Path $vcpkgRoot 'debug\lib' }
                    else { Join-Path $vcpkgRoot 'lib' }

    # ★ 전제를 먼저 단정한다. 라이브러리가 없으면 "검사가 통과했다" 가 아니라
    #   "검사를 못 했다" 이고, 둘을 같은 초록으로 내면 안 된다.
    foreach ($name in $engineLibNames) {
        $lib = Join-Path $libDir ($name + '.lib')
        if (-not (Test-Path -LiteralPath $lib -PathType Leaf)) {
            throw ("엔진 라이브러리가 없다: $lib`n" +
                   "  이 게이트는 엔진이 먼저 지어져 있어야 한다 " +
                   "(msbuild Editor\CreatorEditor.vcxproj /p:Configuration=$current)")
        }
    }
    if (-not (Test-Path -LiteralPath $dllDir -PathType Container)) {
        throw "엔진 DLL 디렉터리가 없다: $dllDir"
    }

    $outputDirectory = Join-Path $repoRoot `
        ("Build\Obj\ExperimentContract\x64-{0}" -f $current)
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'experiment_contract_probe.exe'

    # ★ 런타임 라이브러리를 엔진과 맞춘다. 어긋나면 링크가 조용히 이상한 곳에서
    #   터지거나, 더 나쁘게는 붙어서 실행 중에 깨진다.
    $configurationArguments = if ($current -eq 'Debug') {
        '/MDd /Od /RTC1 /Zi /D_DEBUG'
    } else {
        '/MD /O2 /Ob2 /DNDEBUG'
    }

    # ★★ `/Fd:` 를 반드시 준다. Debug 는 `/Zi` 라 PDB 를 쓰는데, 주지 않으면
    #   **작업 디렉터리**(저장소 루트)에 `vc<버전>.pdb` 를 만들려 하고 그것이
    #   실패하면 컴파일 전체가 exit 2 로 죽는다 — 원인이 소스에 없어서 오래 찾는다.
    #
    # ★★★ 아래 `/Fo:` 의 `\\"` 는 오타가 아니다. 디렉터리로 주려면 경로가
    #   백슬래시로 끝나야 하는데 `"...\"` 로 쓰면 그 백슬래시가 **닫는 따옴표를
    #   이스케이프**해서 뒤에 오는 소스 목록이 통째로 먹힌다. cl 은 그때
    #   `D8003: 소스 파일 이름이 없습니다` 만 내고, 원인이 인용 부호라는 말은
    #   하지 않는다.
    $common = '/nologo /c /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
        '/utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fd:"' + $outputDirectory +
        '\experiment_contract.pdb" ' + $configurationArguments

    Write-Host ("[EXPERIMENT CONTRACT] {0} 컴파일 (/W4 /WX)" -f $current)
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

    $libArguments = @()
    foreach ($name in $engineLibNames) {
        $libArguments += '"' + (Join-Path $libDir ($name + '.lib')) + '"'
    }
    foreach ($name in $vendorLibNamesByConfig[$current]) {
        $lib = Join-Path $vendorLibDir ($name + '.lib')
        if (-not (Test-Path -LiteralPath $lib -PathType Leaf)) {
            throw "vcpkg 라이브러리가 없다: $lib"
        }
        $libArguments += '"' + $lib + '"'
    }

    Write-Host ("[EXPERIMENT CONTRACT] {0} 링크" -f $current)
    $linkCommand = 'call "' + $vcvars + '" >nul && link.exe /nologo /OUT:"' +
        $executable + '" ' + ($objects -join ' ') + ' ' + ($libArguments -join ' ')
    & $env:ComSpec /d /s /c $linkCommand
    if ($LASTEXITCODE -ne 0) {
        throw "링크 실패 ($current): exit $LASTEXITCODE"
    }

    # ★ exe 가 실제로 생겼는지 본다. 링크가 0 을 내고도 산출물이 없으면 아래
    #   실행이 조용히 건너뛰어질 수 있다 — 그런 초록은 없는 것만 못하다.
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "링크가 성공을 냈는데 산출물이 없다: $executable"
    }

    # 엔진 DLL(PhysX 등)을 찾게 해 준다. 없으면 실행이 0xC0000135 로 죽는데
    # 그 코드는 "무엇이 없는지" 를 말해 주지 않는다.
    Write-Host ("[EXPERIMENT CONTRACT] {0} 실행" -f $current)
    $previousPath = $env:PATH
    $env:PATH = $dllDir + ';' + $previousPath
    try {
        if ([string]::IsNullOrWhiteSpace($Only)) { & $executable }
        else { & $executable $Only }
        $probeExit = $LASTEXITCODE
    }
    finally { $env:PATH = $previousPath }

    if ($probeExit -ne 0) {
        throw "experiment 합성 계약 실패 ($current): exit $probeExit"
    }
}

Write-Host ("[EXPERIMENT CONTRACT] {0} 통과 — 에디터 없이 검증했다." -f
    ($configurations -join '/')) -ForegroundColor Green

# PHASE 15 트랙 H — HashingString 계약 게이트.
#
# ── 무엇을 재는가 ──
#
#   1) 정적 래칫 — 헤더가 내부 버퍼의 쓰기 권한을 다시 열지 않는가, 해시 특수화가
#      남아 있는가, ==가 <=>와 같은 기준을 쓰는가, 인스펙터 경로가 값 복사본에
#      직접 써 넣는 옛 모양으로 되돌아가지 않았는가.
#   2) 계약 프로브 — hashing_string_contract_probe.cpp를 **Debug와 Release로 각각**
#      컴파일해 실행한다. 실행 축은 캐시 불변식·부분 string_view 길이·해시 컨테이너
#      키 계약이고, 접근자 형상은 static_assert라 컴파일에서 갈린다.
#
# ── 어느 바이너리를 재는가 ──
#
# 아무 산출물도 재지 않는다. 이 게이트는 제품 헤더를 **자기가 그 자리에서 컴파일**
# 하므로 Bin\ 아래의 exe와 무관하고, 낡은 바이너리를 재는 함정(-Exe 기본값이
# 빌드한 구성과 다른 경우)이 원리적으로 없다. 그래서 run-all에서 -Exe를 넘기지
# 않는다. 대신 링크에 TimeSystem.cpp가 필요하다 — HashingString.h가 Core.Minimal.h를
# 통해 TimeSystem.h를 끌고 오고, 그 헤더가 네임스페이스 스코프에서 싱글턴을
# 생성하기 때문이다(전이 include의 실물 증거이기도 하다).
#
# ── 이 게이트가 못 잡는 것 ──
#
# 인스펙터 편집은 ImGui 컨텍스트가 있어야 실행되므로 프로브가 태우지 못한다.
# 그쪽은 정적 래칫(아래 $typedDraw 블록)만 본다 — 브랜치가 옛 모양으로 되돌아가는
# 변이는 잡지만, 같은 모양을 유지한 채 커밋 조건을 바꾸는 변이는 못 잡는다.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$header = Join-Path $repoRoot 'Engine\Utility_Framework\HashingString.h'
$timeSystem = Join-Path $repoRoot 'Engine\Utility_Framework\TimeSystem.cpp'
$typedDraw = Join-Path $repoRoot 'Editor\EngineGUIWindow\ReflectionTypedDraw.h'
$probe = Join-Path $PSScriptRoot 'hashing_string_contract_probe.cpp'

foreach ($required in @($header, $timeSystem, $typedDraw, $probe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "HashingString contract input is missing: $required"
    }
}

$headerText = Get-Content -LiteralPath $header -Raw

# ── 래칫 1. 내부 버퍼의 쓰기 권한을 밖으로 주지 않는다 ──
#
# `char* data()`가 열려 있으면 밖에서 문자열을 고쳐도 m_hash가 그대로라 캐시가
# 어긋난다. 인스펙터 파손(§1.8 H-a)의 근본 원인이 정확히 이것이다.
if ($headerText -match 'char\s*\*\s*data\s*\(\s*\)\s*(?!\s*const)') {
    throw 'HashingString exposes a mutable internal buffer through data().'
}
if ($headerText -notmatch 'const\s+char\s*\*\s*data\s*\(\s*\)\s*const') {
    throw 'HashingString::data() is not a const accessor returning const char*.'
}
if ($headerText -notmatch '(?m)size\s*\(\s*\)\s*const') {
    throw 'HashingString::size() is not callable on a const instance.'
}
if ($headerText -notmatch 'GetHash\s*\(\s*\)\s*const') {
    throw 'HashingString does not expose a const GetHash() accessor.'
}
if ($headerText -notmatch 'const\s+std::string\s*&\s*ToString\s*\(\s*\)\s*const') {
    throw 'HashingString::ToString() still returns by value (allocates per call).'
}

# ── 래칫 2. 해시 컨테이너 키 계약 ──
if ($headerText -notmatch 'struct\s+hash\s*<\s*HashingString\s*>') {
    throw 'std::hash<HashingString> specialization is missing.'
}

# ── 래칫 3. ==와 <=>가 같은 기준을 쓴다 ──
#
# 해시만 비교하는 ==는 충돌 시 서로 다른 문자열을 같다고 말하고, 그때 <=>는
# 다르다고 말한다. 표준 알고리즘·컨테이너가 둘을 함께 쓰므로 이 어긋남은
# 조용한 오작동이 된다. 충돌을 실행 축에서 만들 수 없어(64비트) 정적으로 본다.
$equalityIndex = $headerText.IndexOf('operator==')
if ($equalityIndex -lt 0) {
    throw 'HashingString does not define operator==.'
}
$equalityBodyEnd = $headerText.IndexOf('}', $equalityIndex)
if ($equalityBodyEnd -lt 0) {
    throw 'HashingString::operator== body could not be delimited.'
}
$equalityBody = $headerText.Substring($equalityIndex, $equalityBodyEnd - $equalityIndex)
if ($equalityBody -notmatch 'm_string') {
    throw 'HashingString::operator== compares the cached hash only; <=> also compares the string.'
}

# ── 래칫 4. 인스펙터 경로 ──
#
# 옛 모양은 값 복사본을 만들어 ImGui에 그 내부 버퍼를 넘기고(해시 미갱신),
# 버퍼 크기로 현재 길이를 넘겨(이름을 늘릴 수 없음) 편집을 파괴했다.
$typedDrawText = Get-Content -LiteralPath $typedDraw -Raw
$branchStart = $typedDrawText.IndexOf('std::is_same_v<MemberT, HashingString>')
if ($branchStart -lt 0) {
    throw 'ReflectionTypedDraw no longer handles HashingString members.'
}
$branchEnd = $typedDrawText.IndexOf('else if constexpr', $branchStart)
if ($branchEnd -lt 0) {
    $branchEnd = $typedDrawText.Length
}
$branch = $typedDrawText.Substring($branchStart, $branchEnd - $branchStart)
if ($branch -match 'HashingString\s+\w+\s*=\s*value\s*;') {
    throw 'Inspector HashingString branch edits a value copy of the member again.'
}
if ($branch -match '\bv\.data\(\)' -or $branch -match '\bv\.size\(\)') {
    throw 'Inspector HashingString branch writes into the HashingString internal buffer again.'
}
if ($branch -notmatch 'ImGuiInputTextFlags_CallbackResize') {
    throw 'Inspector HashingString branch cannot grow the name (no resize callback).'
}
if ($branch -notmatch 'CommitMemberChange') {
    throw 'Inspector HashingString branch does not commit through the member setter.'
}

# ── 계약 프로브 ──
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

$utilityInclude = Join-Path $repoRoot 'Engine\Utility_Framework'
$interfacesInclude = Join-Path $repoRoot 'Engine\RenderEngine\Interfaces'
$mathematicsInclude = Join-Path $repoRoot 'ThirdParty\Mathematics\include'
$manifestInclude = @(
    (Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows\include'),
    (Join-Path $repoRoot 'vcpkg_installed\x64-windows\include')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($manifestInclude)) {
    throw 'The vcpkg manifest install tree was not found. Restore dependencies first.'
}

$configurations = if ($Configuration -eq 'All') {
    @('Debug', 'Release')
} else {
    @($Configuration)
}

foreach ($current in $configurations) {
    $outputDirectory = Join-Path $repoRoot `
        ("Build\Obj\HashingStringContract\x64-{0}" -f $current)
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'hashing_string_contract_probe.exe'

    # Debug는 assert와 /RTC1이 살아 있고 Release는 /O2·NDEBUG다. 둘을 각각 도는
    # 이유는 캐시 불변식이 최적화나 어설션 제거에 기대지 않는다는 것까지 재기
    # 위해서다.
    $configurationArguments = if ($current -eq 'Debug') {
        '/MDd /Od /RTC1 /D_DEBUG'
    } else {
        '/MD /O2 /Ob2 /DNDEBUG'
    }

    # /wd4828: 엔진 헤더 주석 일부가 CP949 바이트로 남아 있다(별도 이관 대상).
    # 이 게이트의 축이 아니므로 경고만 끄고, 나머지는 /W4 /WX로 조인다.
    $command = 'call "' + $vcvars + '" >nul && cl.exe ' +
        '/nologo /TP /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
        '/Zc:preprocessor /utf-8 /W4 /WX /wd4828 ' + $configurationArguments + ' ' +
        '/I"' + $utilityInclude + '" /I"' + $interfacesInclude + '" ' +
        '/I"' + $mathematicsInclude + '" ' +
        '/external:I"' + $manifestInclude + '" /external:W0 ' +
        '/Fo:"' + $outputDirectory + '\\" /Fe:"' + $executable + '" "' +
        $probe + '" "' + $timeSystem + '" && "' + $executable + '"'

    Write-Host "[HASHING STRING] compiling/running $current"
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "HashingString $current contract failed with exit code $LASTEXITCODE."
    }
}

Write-Host (('[HASHING STRING] {0} verification passed.' -f
    ($configurations -join '/'))) -ForegroundColor Green

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = ''
)

# 리플렉션 컨테이너 형상 판정 계약 (range 일반화 · 컴파일타임 축)
#
# ── 이 게이트가 메우는 구멍 ─────────────────────────────────────────────
#
# 직렬화기의 컨테이너 디스패치는 전부 `if constexpr` 다. 고르지 않은 갈래는
# **코드가 생성되지 않으므로** 판정이 틀려도 런타임 게이트에는 볼 것이 없다.
# 잘못 고른 갈래만 돌고, 그쪽은 정상으로 보인다.
#
# 그래서 두 축을 든다:
#
#   ① 실행 축 — 제품 헤더를 Debug/Release 로 각각 컴파일해 판정 단정 60여 건을
#      건다(`reflection_container_probe.cpp`). 단정이 전부 컴파일타임이므로
#      "컴파일에 성공하고 표지를 찍었다" 가 곧 판정이다.
#
#   ② 정적 래칫 — 판정을 **타입 이름으로 되돌리는 것**을 막는다. 이건 실행
#      축으로 못 잡는다: `is_vector_v` 로 되돌려도 vector 만 쓰는 한 모든 단정이
#      그대로 통과하기 때문이다. 되돌림은 기능이 아니라 **범위**를 줄인다.
#
# ── 어느 바이너리를 재는가 ──────────────────────────────────────────────
#
# 아무것도 재지 않는다. Bin\ 산출물과 무관하므로 -Exe 를 받지 않는다
# (verify-hashing-string 과 같은 관례).
#
# 값이 실제로 왕복하는지는 이 게이트의 축이 **아니다**. 컨테이너에 담긴 값이
# 저장을 건너 돌아오는지는 제품 바이너리로 재야 한다 — 그쪽은 별도 게이트다.
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-reflection-container.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$probe = Join-Path $PSScriptRoot 'reflection_container_probe.cpp'
$containerHeader = Join-Path $repoRoot 'Engine\Utility_Framework\ReflectionContainer.h'
$typedYml = Join-Path $repoRoot 'Engine\Utility_Framework\ReflectionTypedYml.h'
$typeTrait = Join-Path $repoRoot 'Engine\Utility_Framework\TypeTrait.h'

foreach ($required in @($probe, $containerHeader, $typedYml, $typeTrait)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "리플렉션 컨테이너 게이트 입력이 없다: $required"
    }
}

# ── 정적 래칫 ───────────────────────────────────────────────────────────
#
# 주석은 세지 않는다. `is_vector_v` 는 이 이행의 **이유**로 여러 머리글에
# 언급되고, 그 언급까지 막으면 왜 바뀌었는지 적을 수 없게 된다. 코드 줄만 본다.

function Get-CodeLines([string]$path) {
    # 한 줄 주석(//)과 블록 주석의 본문 줄을 걷어 낸 코드 줄만 돌려준다.
    $lines = Get-Content -LiteralPath $path
    $code = New-Object System.Collections.Generic.List[string]
    $inBlock = $false
    foreach ($line in $lines) {
        $text = $line
        if ($inBlock) {
            $close = $text.IndexOf('*/')
            if ($close -lt 0) { continue }
            $text = $text.Substring($close + 2)
            $inBlock = $false
        }
        $open = $text.IndexOf('/*')
        if ($open -ge 0) {
            $inBlock = $true
            $text = $text.Substring(0, $open)
        }
        $slash = $text.IndexOf('//')
        if ($slash -ge 0) { $text = $text.Substring(0, $slash) }
        if (-not [string]::IsNullOrWhiteSpace($text)) { $code.Add($text) }
    }
    return $code
}

$ratchetFailures = @()

$typedYmlCode = Get-CodeLines $typedYml
$typeTraitCode = Get-CodeLines $typeTrait

# ① 타입 이름 디스패치로의 되돌림 금지.
foreach ($pair in @(@{ Name = 'ReflectionTypedYml.h'; Code = $typedYmlCode },
                    @{ Name = 'TypeTrait.h'; Code = $typeTraitCode })) {
    $hits = @($pair.Code | Where-Object { $_ -match 'is_vector_v|VectorElementTypeT?\b' })
    if ($hits.Count -gt 0) {
        $ratchetFailures += ("{0}: 컨테이너 판정이 타입 이름으로 되돌아왔다 ({1}줄) — range 판정(ReflectionContainer.h)으로 유지하라" -f $pair.Name, $hits.Count)
    }
}

# ② 순서 계약이 타입 판정에 박혀 있는가. if/else 의 줄 순서에만 기대면
#    누군가 분기를 옮기는 순간 std::string 이 시퀀스로 샌다.
foreach ($needle in @('SerializedAsSequence', 'SerializedAsKeyedRange')) {
    if (-not (@($typedYmlCode | Where-Object { $_ -match [regex]::Escape($needle) }).Count -gt 0)) {
        $ratchetFailures += "ReflectionTypedYml.h: 순서 계약 콘셉트 $needle 이 없다"
    }
}
if (-not (@($typedYmlCode | Where-Object { $_ -match 'concept\s+SerializedAsSequence\s*=\s*!YamlScalar' }).Count -gt 0)) {
    $ratchetFailures += 'ReflectionTypedYml.h: SerializedAsSequence 가 YamlScalar 를 먼저 배제하지 않는다 — 스칼라가 range 로 샌다'
}
if (-not (@($typedYmlCode | Where-Object { $_ -match 'concept\s+SerializedAsKeyedRange\s*=\s*!YamlScalar' }).Count -gt 0)) {
    $ratchetFailures += 'ReflectionTypedYml.h: SerializedAsKeyedRange 가 YamlScalar 를 먼저 배제하지 않는다'
}

# ③ 빈 컨테이너 표기 규칙이 **양쪽** 갈래에 같게 걸려 있는가.
#    시퀀스만 `~` 고 맵은 `[]`/`{}` 가 되면, 같은 자리에서 컨테이너를 바꾸는
#    것만으로 파일 형상이 갈린다. 실행 축은 이걸 못 본다(형상은 런타임 산출이다).
$emptyRuleCount = @($typedYmlCode | Where-Object { $_ -match 'std::ranges::empty\(value\)' }).Count
if ($emptyRuleCount -ne 2) {
    $ratchetFailures += ("ReflectionTypedYml.h: 빈 컨테이너 규칙이 {0}자리다 — 시퀀스와 맵 두 갈래 모두에 있어야 한다" -f $emptyRuleCount)
}

# ④ 판정의 출처가 하나인가.
if (-not (@($typedYmlCode | Where-Object { $_ -match '#include\s+"ReflectionContainer\.h"' }).Count -gt 0)) {
    $ratchetFailures += 'ReflectionTypedYml.h: ReflectionContainer.h 를 물지 않는다'
}

if ($ratchetFailures.Count -gt 0) {
    foreach ($failure in $ratchetFailures) { Write-Host "[REFLECTION CONTAINER] 래칫 실패: $failure" -ForegroundColor Red }
    throw ("정적 래칫 {0}건 실패." -f $ratchetFailures.Count)
}
Write-Host '[REFLECTION CONTAINER] 정적 래칫 통과 (되돌림 금지·순서 계약·빈 표기 양쪽·단일 출처)'

# ── 실행 축 ─────────────────────────────────────────────────────────────

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

$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }

foreach ($current in $configurations) {
    $outputDirectory = Join-Path $repoRoot `
        ("Build\Obj\ReflectionContainerContract\x64-{0}" -f $current)
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'reflection_container_probe.exe'

    # Debug 와 Release 를 각각 도는 이유는 판정이 최적화나 어설션 제거에 기대지
    # 않는다는 것까지 재기 위해서다(HashingString 게이트와 같은 근거).
    $configurationArguments = if ($current -eq 'Debug') {
        '/MDd /Od /RTC1 /D_DEBUG'
    } else {
        '/MD /O2 /Ob2 /DNDEBUG'
    }

    # /wd4828: 엔진 헤더 주석 일부가 CP949 바이트로 남아 있다(별도 이관 대상).
    # /wd4189: TypeTrait.h 의 GenerateGUID 가 HRESULT 를 받고 쓰지 않는다 —
    #          이 게이트의 축이 아닌 선재 코드이므로 여기서 고치지 않는다.
    $command = 'call "' + $vcvars + '" >nul && cl.exe ' +
        '/nologo /TP /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
        '/Zc:preprocessor /utf-8 /W4 /WX /wd4828 /wd4189 ' + $configurationArguments + ' ' +
        '/I"' + $utilityInclude + '" ' +
        '/Fo:"' + $outputDirectory + '\\" /Fe:"' + $executable + '" "' +
        $probe + '" /link Ole32.lib'

    Write-Host "[REFLECTION CONTAINER] compiling $current"
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "리플렉션 컨테이너 판정 계약이 $current 컴파일에서 실패했다 (exit $LASTEXITCODE)."
    }

    # 표지를 확인한다. 컴파일만 보고 통과시키면, 단정이 통째로 지워져도 초록이다.
    $output = & $executable 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "리플렉션 컨테이너 프로브가 $current 실행에서 실패했다 (exit $LASTEXITCODE)."
    }
    if (-not ($output -match 'classification contract ok')) {
        throw "리플렉션 컨테이너 프로브가 $current 에서 표지를 찍지 않았다: $output"
    }
    Write-Host "[REFLECTION CONTAINER] $current $output"
}

Write-Host (('[REFLECTION CONTAINER] {0} verification passed.' -f
    ($configurations -join '/'))) -ForegroundColor Green

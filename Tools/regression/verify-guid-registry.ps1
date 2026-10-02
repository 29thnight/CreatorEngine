#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [string]$OutputRoot
)

# 인스턴스 GUID 장부(TypeTrait::GUIDCreator)의 계약 검사.
#
# 장부는 TypeTrait.h 의 `static std::set<HashedGuid> g_guids;` 였다 — include 한 TU 마다
# 표가 따로 있었고, inline 인 Insert/Erase/MakeGUID 는 링커가 고른 TU 하나의 표를
# 만졌으며, 잠금이 없었다(docs/analysis/PlayerModuleBoundaryAnalysis.md §10.6).
# 지금은 표 하나가 TypeTrait.cpp 에 있고 잠금을 건다. 이 검사가 무는 것은 둘이다.
#
#   ① 구조 — 프로브를 TypeTrait.cpp **없이** 링크하면 GUIDCreator 의 정의를 못
#      찾아 실패해야 한다. 링크가 된다면 정의가 헤더로 돌아온 것이고, 그러면 TU 마다
#      사본으로 돌아간 것이다. 실패의 **이유**까지 본다 — 다른 이유로 실패한 것을
#      통과로 읽지 않는다.
#   ② 동작 — 두 TU 의 여러 스레드가 동시에 발급하고 다른 스레드가 표를 흔드는 동안
#      발급된 값이 겹치지 않는다. 비용(CoCreateGuid 만 · MakeGUID · 경합)을 INFO 로
#      남긴다 — §10.6 이 요구한 "락 비용(Release 기준)" 의 기록이다.
#
# 엔진을 띄우지 않는다. TypeTrait.cpp 와 프로브 두 TU 만 cl 로 직접 컴파일한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$core = Join-Path $repo 'Engine/Utility_Framework'
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "VS18 toolchain not found: $vcvars" }
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repo ('Build/Validation/GuidRegistry/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$null = New-Item -ItemType Directory -Force -Path $OutputRoot
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }

# 링크 오류 문안을 영어로 고정한다 — ① 의 이유 대조가 설치 언어에 흔들리지 않게.
$env:VSLANG = '1033'

$probeSources = @(
    (Join-Path $PSScriptRoot 'guid_registry_probe.cpp'),
    (Join-Path $PSScriptRoot 'guid_registry_peer.cpp')
)
$registrySource = Join-Path $core 'TypeTrait.cpp'

function Invoke-Cl {
    param([string]$Config, [string]$OutDir, [string]$ExeName, [string[]]$Sources, [string]$LogName)

    # 엔진과 같은 축: NOMINMAX(Directory.Build.targets) · Unicode 문자 집합.
    # CoCreateGuid 는 ole32 에 있다 — 엔진 exe 는 기본 라이브러리로 받지만 cl 직접
    # 링크에는 기본으로 들어오지 않는다.
    $flags = if ($Config -eq 'Debug') { '/MDd /Od /RTC1 /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
    $quoted = ($Sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $exe = Join-Path $OutDir $ExeName
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 ' +
        '/DNOMINMAX /DUNICODE /D_UNICODE ' + $flags + ' /I"' + $core + '" /Fo"' + $OutDir + '/" /Fd"' +
        (Join-Path $OutDir "$LogName.pdb") + '" /Fe"' + $exe + '" ' + $quoted + ' ole32.lib'
    $log = & $env:ComSpec /d /s /c $command 2>&1
    $logPath = Join-Path $OutDir "$LogName.log"
    $log | Set-Content -LiteralPath $logPath -Encoding UTF8
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Exe = $exe; Log = $logPath; Text = ($log -join "`n") }
}

$failures = New-Object System.Collections.Generic.List[string]

foreach ($config in $configs) {
    $out = Join-Path $OutputRoot $config
    $null = New-Item -ItemType Directory -Force -Path $out

    # ── ① 구조: TypeTrait.cpp 없이 링크하면 GUIDCreator 를 못 찾아야 한다 ──
    $structureOut = Join-Path $out 'structure'
    $null = New-Item -ItemType Directory -Force -Path $structureOut
    $structure = Invoke-Cl -Config $config -OutDir $structureOut -ExeName 'guid_registry_structure.exe' `
        -Sources $probeSources -LogName 'structure'
    if ($structure.ExitCode -eq 0) {
        $failures.Add("$config structure — TypeTrait.cpp 없이 링크됐다. GUIDCreator 정의가 헤더로 돌아와 TU 마다 사본이 된다: $($structure.Log)")
    } elseif ($structure.Text -notmatch 'unresolved external symbol[^\r\n]*GUIDCreator::(MakeGUID|InsertGUID|EraseGUID)') {
        $failures.Add("$config structure — 링크가 실패했지만 이유가 GUIDCreator 정의 부재가 아니다: $($structure.Log)")
    } else {
        Write-Host "[OK]   $config structure — 정의가 TypeTrait.cpp 하나에만 있다"
    }

    # ── ② 동작: 동시 발급 유일성 + 비용 ──
    $build = Invoke-Cl -Config $config -OutDir $out -ExeName 'guid_registry_probe.exe' `
        -Sources ($probeSources + @($registrySource)) -LogName 'build'
    if ($build.ExitCode -ne 0) {
        $failures.Add("$config build 실패: $($build.Log)")
        continue
    }

    $stdout = Join-Path $out 'stdout.log'
    $stderr = Join-Path $out 'stderr.log'
    $proc = Start-Process -FilePath $build.Exe -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $proc.WaitForExit(120000)) {
        try { $proc.Kill() } catch { }
        $failures.Add("$config probe 시간 초과: $stderr")
        continue
    }
    $proc.WaitForExit()
    $text = Get-Content -LiteralPath $stdout -Raw -Encoding UTF8
    if ($null -eq $text) { $text = '' }
    if ($proc.ExitCode -ne 0 -or $text -notmatch 'GUID_REGISTRY_OK=true') {
        $failures.Add("$config probe 실패(exit $($proc.ExitCode)): $stdout / $stderr")
        continue
    }
    $info = @([regex]::Matches($text, '(?m)^INFO:\s*(.+?)\s*$') | ForEach-Object { $_.Groups[1].Value }) -join ' · '
    Write-Host "[OK]   $config behavior — $info"
}

if ($failures.Count -gt 0) {
    Write-Host '[FAIL] verify-guid-registry' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
Write-Host "GUID_REGISTRY_OK=$OutputRoot" -ForegroundColor Green
exit 0

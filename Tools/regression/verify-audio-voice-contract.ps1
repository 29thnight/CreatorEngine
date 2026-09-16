[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$VisualStudioInstallation = '',
    [int]$ShutdownTimeoutSeconds = 10
)

# 오디오 보이스 계약 게이트 — 로컬 검증 전용.
#
# ★ **run-all.ps1 에 배선하지 않는다.** 실제 오디오 장치를 요구하므로 무인 회귀
#   세트의 전제와 맞지 않는다. 배선을 다시 긋는 동안 손으로 돌리는 자다.
#
# ★★ 음원은 저장소에 커밋하지 않는다. probe 가 실행마다 sine WAV 를 생성해
#   `Build/Validation/AudioVoiceContract`(git 무시) 아래에 쓴다.
#
# ★★★ 하네스 모양은 `verify-experiment-contract.ps1` 을 따랐다 — cl.exe 로 probe 를
#   짓고 엔진 정적 라이브러리를 링크한다. 새 기전을 만들지 않았다.
#
# 종료 코드: 0 통과 · 1 실패 · 3 장치 없음(검사 불가)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$probe = Join-Path $PSScriptRoot 'audio_voice_contract_probe.cpp'
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
    (Join-Path $repoRoot 'Engine\SceneRuntime')
    (Join-Path $repoRoot 'Engine\Utility_Framework')
    (Join-Path $repoRoot 'Engine\RenderEngine')
    (Join-Path $repoRoot 'Engine\RenderEngine\Interfaces')
    (Join-Path $repoRoot 'Engine\Physics')
    (Join-Path $repoRoot 'Engine\EngineDiagnostics')
    (Join-Path $repoRoot 'Editor\EngineEntry')
    (Join-Path $repoRoot 'ThirdParty\Mathematics\include')
    $vcpkgInclude
)
foreach ($dir in $includeDirs) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        throw "include 디렉터리가 없다: $dir"
    }
}

# ★ `SceneRuntime.lib` 를 링크하지 않는다. SoundManager 의 obj 하나를 끌면
#   **유니티 blob 이 통째로** 딸려 와 Physics·PhysX·GameInput 까지 요구했다
#   (실측: LNK2019 49건 → Physics 추가 → PhysX 19건). 오디오만 떼어 검증할 수
#   없다는 뜻이고, 그것 자체가 재배선의 근거다. 여기서는 필요한 .cpp 를 직접
#   컴파일해 폐포를 오디오에 묶어 둔다.
$engineSources = @(
    (Join-Path $repoRoot 'Engine\SceneRuntime\SoundManager.cpp')
)
# wave — 새 배선. 이 게이트가 함께 짓는다. 우리가 지금 쓰는 코드라 probe 와 같은
# 경고 수준(/W4 /WX)으로 짓는다.
$waveSources = @(
    (Join-Path $repoRoot 'Engine\SceneRuntime\Audio\VoiceTable.cpp')
    (Join-Path $repoRoot 'Engine\SceneRuntime\Audio\NullAudioBackend.cpp')
    (Join-Path $repoRoot 'Engine\SceneRuntime\Audio\AudioRuntime.cpp')
    (Join-Path $repoRoot 'Engine\SceneRuntime\Audio\MiniaudioBackend.cpp')
    (Join-Path $repoRoot 'Engine\SceneRuntime\Audio\ClipDirectory.cpp')
)
foreach ($source in $waveSources) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "원본이 없다: $source" }
}
foreach ($source in $engineSources) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "원본이 없다: $source" }
}
$engineLibNames = @('Utility_Framework')
$vendorLibNamesByConfig = @{
    Debug   = @('fmtd', 'spdlogd')
    Release = @('fmt',  'spdlog')
}

$libDir = Join-Path $repoRoot ("Build\Lib\x64-{0}" -f $Configuration)
$dllDir = Join-Path $repoRoot ("Bin\x64-{0}\Editor" -f $Configuration)
$runtimeDir = Join-Path $repoRoot ("Bin\x64-{0}\Runtime\Common" -f $Configuration)
$vendorLibDir = if ($Configuration -eq 'Debug') { Join-Path $vcpkgRoot 'debug\lib' }
                else { Join-Path $vcpkgRoot 'lib' }

# 전제를 먼저 단정한다. 없으면 "통과" 가 아니라 "검사를 못 했다" 이다.
foreach ($name in $engineLibNames) {
    $lib = Join-Path $libDir ($name + '.lib')
    if (-not (Test-Path -LiteralPath $lib -PathType Leaf)) {
        throw ("엔진 라이브러리가 없다: $lib`n" +
               "  먼저 엔진을 지어야 한다 " +
               "(msbuild Editor\CreatorEditor.vcxproj /p:Configuration=$Configuration)")
    }
}

$outputDirectory = Join-Path $repoRoot ("Build\Obj\AudioVoiceContract\x64-{0}" -f $Configuration)
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$executable = Join-Path $outputDirectory 'audio_voice_contract_probe.exe'

$configurationArguments = if ($Configuration -eq 'Debug') {
    '/MDd /Od /RTC1 /Zi /D_DEBUG'
} else {
    '/MD /O2 /Ob2 /DNDEBUG'
}
$externalFlags = '/external:W0 ' +
    (($includeDirs | ForEach-Object { '/external:I"' + $_ + '"' }) -join ' ')

# `/Fd:` 와 `/Fo:` 의 `\\"` 는 선례와 같은 이유로 그대로 둔다 — 주석은
# verify-experiment-contract.ps1 참고.
$common = '/nologo /c /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
    '/utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fd:"' + $outputDirectory +
    '\audio_voice_contract.pdb" ' + $configurationArguments

# ★ 경고 수준을 출처별로 나눈다. probe 는 이 게이트가 소유하므로 /W4 /WX 로 짓고,
#   엔진 .cpp 는 /W0 로 짓는다 — 기존 경고로 게이트가 붉어지면 고칠 수 없는 것을
#   눈금에 넣는 셈이다(`SoundManager.cpp` 는 주석이 CP949 로 깨져 있어 /utf-8 아래
#   C4828 을 낸다).
Remove-Item -LiteralPath $outputDirectory -Recurse -Force -ErrorAction SilentlyContinue
$probeObjectDir = Join-Path $outputDirectory 'probe'
$engineObjectDir = Join-Path $outputDirectory 'engine'
New-Item -ItemType Directory -Path $probeObjectDir -Force | Out-Null
New-Item -ItemType Directory -Path $engineObjectDir -Force | Out-Null

Write-Host ("[AUDIO VOICE] {0} probe 컴파일 (/W4 /WX)" -f $Configuration)
$compileCommand = 'call "' + $vcvars + '" >nul && cl.exe ' + $common +
    ' /W4 /WX ' + $externalFlags + ' /Fo:"' + $probeObjectDir + '\\" "' + $probe + '"'
& $env:ComSpec /d /s /c $compileCommand
if ($LASTEXITCODE -ne 0) { throw "probe 컴파일 실패: exit $LASTEXITCODE" }

Write-Host ("[AUDIO VOICE] {0} wave 컴파일 (/W4 /WX)" -f $Configuration)
$waveObjectDir = Join-Path $outputDirectory 'wave'
New-Item -ItemType Directory -Path $waveObjectDir -Force | Out-Null
$waveCompileCommand = 'call "' + $vcvars + '" >nul && cl.exe ' + $common +
    ' /W4 /WX ' + $externalFlags + ' /Fo:"' + $waveObjectDir + '\\" ' +
    (($waveSources | ForEach-Object { '"' + $_ + '"' }) -join ' ')
& $env:ComSpec /d /s /c $waveCompileCommand
if ($LASTEXITCODE -ne 0) { throw "wave 컴파일 실패: exit $LASTEXITCODE" }

Write-Host ("[AUDIO VOICE] {0} 엔진 원본 컴파일 (/W0)" -f $Configuration)
$engineIncludes = ($includeDirs | ForEach-Object { '/I"' + $_ + '"' }) -join ' '
$engineCompileCommand = 'call "' + $vcvars + '" >nul && cl.exe ' + $common +
    ' /W0 ' + $engineIncludes + ' /Fo:"' + $engineObjectDir + '\\" ' +
    (($engineSources | ForEach-Object { '"' + $_ + '"' }) -join ' ')
& $env:ComSpec /d /s /c $engineCompileCommand
if ($LASTEXITCODE -ne 0) { throw "엔진 원본 컴파일 실패: exit $LASTEXITCODE" }

$objects = @(Get-ChildItem -LiteralPath $outputDirectory -Filter '*.obj' -Recurse |
    ForEach-Object { '"' + $_.FullName + '"' })
if ($objects.Count -eq 0) { throw '링크할 obj 가 없다 — 컴파일이 조용히 아무것도 내지 않았다.' }

$libArguments = @()
foreach ($name in $engineLibNames) {
    $libArguments += '"' + (Join-Path $libDir ($name + '.lib')) + '"'
}
foreach ($name in $vendorLibNamesByConfig[$Configuration]) {
    $lib = Join-Path $vendorLibDir ($name + '.lib')
    if (-not (Test-Path -LiteralPath $lib -PathType Leaf)) { throw "vcpkg 라이브러리가 없다: $lib" }
    $libArguments += '"' + $lib + '"'
}

# FMOD — SoundManager 가 건다. 구성별로 이름이 다르다(Debug 는 로깅판).
$fmodLibName = if ($Configuration -eq 'Debug') { 'fmodL_vc.lib' } else { 'fmod_vc.lib' }
$fmodLib = Join-Path $repoRoot ('ThirdParty\Fmod\lib\x64\' + $fmodLibName)
if (-not (Test-Path -LiteralPath $fmodLib -PathType Leaf)) {
    throw "FMOD 라이브러리가 없다: $fmodLib"
}
$libArguments += '"' + $fmodLib + '"'
# miniaudio 의 Windows 백엔드가 쓰는 시스템 라이브러리. 벤더 소스는
# `#pragma comment(lib, ...)` 를 심지 않으므로 여기서 준다.
$libArguments += @('ole32.lib', 'user32.lib', 'advapi32.lib')

Write-Host ("[AUDIO VOICE] {0} 링크" -f $Configuration)
$linkCommand = 'call "' + $vcvars + '" >nul && link.exe /nologo /OUT:"' + $executable +
    '" ' + ($objects -join ' ') + ' ' + ($libArguments -join ' ')
& $env:ComSpec /d /s /c $linkCommand
if ($LASTEXITCODE -ne 0) { throw "링크 실패: exit $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "링크가 성공을 냈는데 산출물이 없다: $executable"
}

$previousPath = $env:PATH
$searchPath = @($dllDir, $runtimeDir) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$env:PATH = (($searchPath + $previousPath) -join ';')
try {
    Write-Host ("[AUDIO VOICE] {0} 실행 — 기능 계약" -f $Configuration)
    & $executable 'run' $repoRoot
    $runExit = $LASTEXITCODE

    if ($runExit -eq 3) {
        Write-Host '[AUDIO VOICE] 오디오 장치가 없다 — 검사 불가(통과 아님)'
        exit 3
    }

    # ── 종료 canary ────────────────────────────────────────────────────────
    #
    # 같은 물음을 두 배선에 던진다: **클립이 하나도 없을 때 종료가 끝나는가.**
    #
    # ★ 옛 배선은 끝나지 않는다(정찰 §2.2). 그것을 단순히 '실패' 로 두면 게이트가
    #   영원히 붉어 아무도 판정에 쓰지 못한다. 그래서 **기대 모양**을 적는다 —
    #   옛 배선의 빈 상태는 멈추는 것이 맞고, 멈추지 않으면 그것도 알아야 한다
    #   (누가 고쳤거나, 이 canary 가 결함 조건을 잃었거나 둘 중 하나다).
    # ★★ 클립이 있으면 `LoadSounds()` 가 한 번 돌아 플래그가 내려가므로 옛 배선도
    #   끝난다 — **fixture 를 넣는 순간 결함 조건이 지워진다.** 두 상태를 다 잰다.
    $shutdownExit = 0
    $noDevice = $false
    foreach ($canary in @(
        @{ Mode = 'shutdown';            Label = 'FMOD · 클립 있음';                   Expect = $true  },
        @{ Mode = 'shutdown-empty';      Label = 'FMOD · 클립 없음 — 제품의 실제 상태'; Expect = $false },
        @{ Mode = 'wave-shutdown';       Label = 'wave · 클립 있음';                   Expect = $true  },
        @{ Mode = 'wave-shutdown-empty'; Label = 'wave · 클립 없음 — 같은 조건';        Expect = $true  })) {
        $expectText = if ($canary.Expect) { '끝난다' } else { '멈춘다(기대)' }
        Write-Host ("[AUDIO VOICE] 종료 canary ({0}, 기대: {1}, 제한 {2}초)" -f `
            $canary.Label, $expectText, $ShutdownTimeoutSeconds)
        $process = Start-Process -FilePath $executable -ArgumentList @($canary.Mode, $repoRoot) `
            -PassThru -NoNewWindow
        $finished = $process.WaitForExit($ShutdownTimeoutSeconds * 1000)
        if (-not $finished) { try { $process.Kill($true) } catch { } }

        # 장치가 없으면 판정이 아니라 '검사 불가' 다.
        if ($finished -and $process.ExitCode -eq 3) {
            Write-Host '  오디오 장치가 없다 — 검사 불가'
            $noDevice = $true
            continue
        }

        $ended = $finished -and ($process.ExitCode -eq 0)
        if ($ended -eq $canary.Expect) {
            if ($ended) { Write-Host '  [ ok ] 제한 시간 안에 끝났다' }
            else { Write-Host ('  [ ok ] 끝나지 않았다 — 기대한 붉음이다(제한 {0}초)' -f $ShutdownTimeoutSeconds) }
        }
        elseif ($canary.Expect) {
            Write-Host ('  [FAIL] {0}초 안에 끝나지 않았다 (exit {1})' -f `
                $ShutdownTimeoutSeconds, $(if ($finished) { $process.ExitCode } else { '시간 초과' }))
            $shutdownExit = 1
        }
        else {
            Write-Host '  [FAIL] 멈출 줄 알았는데 끝났다 — 결함이 고쳐졌거나 canary 가 조건을 잃었다'
            $shutdownExit = 1
        }
    }
    if ($noDevice) {
        Write-Host '[AUDIO VOICE] 오디오 장치가 없다 — 검사 불가(통과 아님)'
        exit 3
    }
}
finally { $env:PATH = $previousPath }

if ($runExit -ne 0 -or $shutdownExit -ne 0) {
    Write-Host ("[AUDIO VOICE] 실패 — 기능 exit {0} · 종료 canary exit {1}" -f $runExit, $shutdownExit)
    exit 1
}
Write-Host '[AUDIO VOICE] 전체 통과'
exit 0

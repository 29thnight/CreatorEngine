#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','All')][string]$Configuration = 'All',
    [ValidateSet('Development','Shipping','All')][string]$Variant = 'All',
    # Directory.Build.props 의 stdcpp23 이 v145 에서 매핑되는 값이다. 엔진 프로젝트와
    # 다른 표준으로 재면 그 결과는 엔진에 대해 아무것도 말하지 않는다 — 비교 실험일
    # 때만 바꾼다(예: c++latest).
    [string]$LanguageStandard = 'c++23preview',
    # 비우면 vswhere 로 찾는다. 이미 개발자 프롬프트(cl 이 PATH 에 있다)라면 그대로 쓴다.
    [string]$VcVars = '',
    # v145 = MSVC 14.50. 낮은 도구 집합의 결과를 v145 의 판정으로 읽지 않게 막는다.
    [string]$MinimumToolsVersion = '14.50',
    # 벤치 반복 횟수(include/import 를 번갈아 컴파일한다). 0 이면 벤치를 건너뛴다.
    [ValidateRange(0, 50)][int]$BenchRepeat = 5,
    [string]$OutRoot = ''
)

# C++ 모듈 전환 1단계 검사 — 엔진을 띄우지 않고 cl 로 직접 컴파일해 판정한다.
#
# Tools/regression/verify-profile-core.ps1 과 같은 수법이다. MSBuild·vcxproj 를
# 건드리지 않으므로 솔루션 빌드와 독립이고, 판정이 빌드 시스템의 모듈 스캔이
# 아니라 **컴파일러의 모듈 동작** 하나에 걸린다.
#
# ── 판정의 세 갈래 ──
#
#   Gate        붉으면 종료 코드 1. 1단계가 성립하려면 반드시 초록이어야 하는 것.
#   ExpectFail  **실패해야** 초록. 그것도 기대한 문안으로 실패해야 한다 — 엉뚱한
#               이유(모듈을 못 찾음 등)로 실패한 것을 통과로 읽지 않는다.
#   Observe     종료 코드에 반영하지 않는다. 컴파일러가 실제로 어떻게 하는지를
#               기록해 다음 단계의 설계가 그 값을 보고 정하게 한다.
#
# ── 산출물 ──
#
#   Build/Obj/CppModules/<구성>/std/                     std.ifc · std.obj
#   Build/Obj/CppModules/<구성>/core/                    ce.core · 표본 모듈
#   Build/Obj/CppModules/<구성>/<Development|Shipping>/  ce.diagnostics BMI 와 프로브
#   Build/Obj/CppModules/logs/                           단계별 컴파일·실행 출력
#   Build/Obj/CppModules/results.tsv · bench.tsv
#
#   ★ ce.diagnostics 의 BMI 는 Development/Shipping 을 **다른 디렉터리**에 만든다.
#     BMI 는 CE_SHIPPING 값을 굳혀 담는다. 같은 자리를 쓰면 방금 덮어쓴 쪽만 보고
#     다른 구성의 소비자가 그것을 import 한다(Directory.Build.props 의
#     EngineConfigKey 가 IntDir 을 가르는 이유와 같다).
#
# ★ PowerShell 7 이 필요하다. 인자를 ProcessStartInfo.ArgumentList 로 넘겨, 경로에
#   공백이 있어도 cmd 따옴표 규칙을 거치지 않는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$probeDir = Join-Path $PSScriptRoot 'probes'
$benchDir = Join-Path $probeDir 'bench'
$diagDir = Join-Path $repo 'Engine/EngineDiagnostics'
$coreDir = Join-Path $repo 'Engine/Utility_Framework'
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
    $OutRoot = Join-Path $repo 'Build/Obj/CppModules'
}
$OutRoot = [IO.Path]::GetFullPath($OutRoot)
$logDir = Join-Path $OutRoot 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null

$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
$variants = if ($Variant -eq 'All') { @('Development', 'Shipping') } else { @($Variant) }

$diagSources = @(
    'ProfileMarker.cpp',
    'ProfileThreadStream.cpp',
    'ProfileCapture.cpp',
    'ProfileCaptureFile.cpp',
    'ProfileAggregate.cpp',
    'ProfileReader.cpp',
    'ProfileService.cpp'
) | ForEach-Object { Join-Path $diagDir $_ }

# ─────────────────────────────────────────────────────────────────────────────
# 도구 사슬
# ─────────────────────────────────────────────────────────────────────────────

function Resolve-VcVars {
    param([string]$Explicit)

    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (-not (Test-Path -LiteralPath $Explicit -PathType Leaf)) {
            throw "vcvars64.bat 이 없다: $Explicit"
        }
        return $Explicit
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        # 정식판을 먼저 보고, 없을 때만 미리 보기판을 본다.
        $installs = @(& $vswhere -latest -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
        if ($installs.Count -eq 0) {
            $installs = @(& $vswhere -latest -prerelease -products '*' `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
        }
        foreach ($install in $installs) {
            $candidate = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    # 다른 회귀 스크립트들이 고정해 둔 자리.
    $fallback = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
    if (Test-Path -LiteralPath $fallback -PathType Leaf) {
        return $fallback
    }
    throw 'vcvars64.bat 을 찾지 못했다. -VcVars 로 경로를 준다.'
}

# vcvars 를 한 번만 부르고 그 환경을 이 프로세스에 옮긴다. 단계마다 vcvars 를 다시
# 부르면 호출마다 1~3 초가 붙어 벤치가 그 비용을 함께 잰다.
function Import-VcEnvironment {
    param([string]$Path)

    $command = 'call "' + $Path + '" >nul && set'
    $lines = & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "vcvars 실행 실패: $Path"
    }
    foreach ($line in $lines) {
        $index = $line.IndexOf('=')
        if ($index -gt 0) {
            Set-Item -LiteralPath ('env:' + $line.Substring(0, $index)) -Value $line.Substring($index + 1)
        }
    }
}

function Invoke-Tool {
    param(
        [string]   $FilePath,
        [string[]] $Arguments,
        [string]   $WorkingDirectory,
        [int]      $TimeoutMs = 600000
    )

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    foreach ($argument in $Arguments) {
        [void]$info.ArgumentList.Add($argument)
    }
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutMs)
    if ($timedOut) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    $watch.Stop()

    return [pscustomobject]@{
        ExitCode     = if ($timedOut) { -1 } else { $process.ExitCode }
        TimedOut     = $timedOut
        StdOut       = $stdout.Result
        Output       = $stdout.Result + $stderr.Result
        Milliseconds = [math]::Round($watch.Elapsed.TotalMilliseconds, 1)
    }
}

if ($null -eq (Get-Command 'cl.exe' -ErrorAction SilentlyContinue) -or [string]::IsNullOrEmpty($env:VCToolsInstallDir)) {
    $resolved = Resolve-VcVars -Explicit $VcVars
    Write-Host "vcvars: $resolved"
    Import-VcEnvironment -Path $resolved
}
# 진단 문안을 영어로 고정한다. ExpectFail 의 문안 대조가 설치 언어에 흔들리지 않게 한다.
$env:VSLANG = '1033'

$cl = (Get-Command 'cl.exe').Source
$toolsVersion = $env:VCToolsVersion
$banner = (Invoke-Tool -FilePath $cl -Arguments @() -WorkingDirectory $OutRoot -TimeoutMs 30000).Output
$compilerVersion = if ($banner -match 'Version\s+(\d+(?:\.\d+)+)') { $Matches[1] } else { 'unknown' }
$gitCommit = try { (& git -C $repo rev-parse --short HEAD 2>$null) } catch { '' }
$gitBranch = try { (& git -C $repo rev-parse --abbrev-ref HEAD 2>$null) } catch { '' }

Write-Host ''
Write-Host "repo       : $repo ($gitBranch @ $gitCommit)"
Write-Host "cl         : $cl"
Write-Host "compiler   : $compilerVersion   VCToolsVersion=$toolsVersion"
Write-Host "/std       : $LanguageStandard"
Write-Host "out        : $OutRoot"
Write-Host ''

if ([string]::IsNullOrEmpty($toolsVersion) -or ([version]$toolsVersion -lt [version]$MinimumToolsVersion)) {
    Write-Host "[FAIL] 도구 집합 $toolsVersion 이 최소 $MinimumToolsVersion(v145) 보다 낮다 — 이 결과를 v145 의 판정으로 읽을 수 없다" -ForegroundColor Red
    exit 1
}

# ─────────────────────────────────────────────────────────────────────────────
# 판정 장부
# ─────────────────────────────────────────────────────────────────────────────

$results = [Collections.Generic.List[object]]::new()

function Add-Result {
    param(
        [ValidateSet('Gate','ExpectFail','Observe')][string]$Kind,
        [string]$Name,
        [string]$Config,
        [string]$VariantName,
        [ValidateSet('PASS','FAIL','SKIP')][string]$Status,
        [double]$Milliseconds = 0,
        [string]$Detail = ''
    )

    $results.Add([pscustomobject]@{
        Kind = $Kind; Name = $Name; Config = $Config; Variant = $VariantName
        Status = $Status; Milliseconds = $Milliseconds; Detail = $Detail
    })

    $color = switch ($Status) {
        'PASS' { 'Green' }
        'SKIP' { 'DarkGray' }
        default { if ($Kind -eq 'Observe') { 'Yellow' } else { 'Red' } }
    }
    $label = '{0,-4} {1,-10} {2,-7} {3,-11} {4}' -f $Status, $Kind, $Config, $VariantName, $Name
    if ($Detail) { $label += " — $Detail" }
    Write-Host $label -ForegroundColor $color
}

function Save-Log {
    param([string]$Name, [object]$Result)
    $safe = $Name -replace '[^A-Za-z0-9_.-]', '_'
    $path = Join-Path $logDir "$safe.log"
    [IO.File]::WriteAllText($path, [string]$Result.Output)
    return $path
}

function Get-BaseFlags {
    param([string]$Config)

    # 엔진 vcxproj 와 같은 축: ConformanceMode(/permissive-) · /utf-8 · Unicode 문자 집합 ·
    # Directory.Build.targets 의 NOMINMAX. 다르게 두면 BMI 호환성 경고(C5050)가 뜨거나,
    # 뜨지 않더라도 엔진과 다른 조건을 잰다.
    #
    # ★ /we5050 — "가져오는 모듈과 환경이 다를 수 있다" 경고를 오류로 올린다. 구성이
    #   섞인 BMI 를 조용히 쓰는 일을 컴파일러가 아는 범위에서는 여기서 막는다.
    $flags = @('/nologo', '/EHsc', "/std:$LanguageStandard", '/utf-8', '/permissive-',
               '/W4', '/we5050', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE')
    if ($Config -eq 'Debug') {
        $flags += @('/MDd', '/Od', '/RTC1', '/D_DEBUG')
    } else {
        $flags += @('/MD', '/O2', '/DNDEBUG')
    }
    return $flags
}

function Get-VariantFlags {
    param([string]$VariantName)
    # Directory.Build.targets 처럼 **둘 다** 정의하고 값으로 가른다.
    if ($VariantName -eq 'Shipping') {
        return @('/DCE_SHIPPING=1', '/DCE_DEVELOPMENT=0')
    }
    return @('/DCE_SHIPPING=0', '/DCE_DEVELOPMENT=1')
}

function Invoke-Compile {
    param(
        [string]   $LogName,
        [string[]] $Flags,
        [string]   $Source,
        [string]   $ObjectPath,
        [string]   $IfcPath = '',
        [string[]] $Extra = @()
    )

    $arguments = @($Flags) + @($Extra) + @('/c', $Source, "/Fo$ObjectPath")
    if ($IfcPath) {
        $arguments += @('/ifcOutput', $IfcPath)
    }
    # 비어 있는 -Extra 가 @($null) 로 들어와 빈 인자가 되지 않게 한다.
    $arguments = @($arguments | Where-Object { -not [string]::IsNullOrEmpty($_) })
    $result = Invoke-Tool -FilePath $cl -Arguments $arguments -WorkingDirectory (Split-Path $ObjectPath)
    $result | Add-Member -NotePropertyName Log -NotePropertyValue (Save-Log -Name $LogName -Result $result)
    return $result
}

function Invoke-Link {
    param([string]$LogName, [string[]]$Objects, [string]$ExePath)

    $arguments = @('/nologo') + @($Objects) + @("/Fe$ExePath")
    $result = Invoke-Tool -FilePath $cl -Arguments $arguments -WorkingDirectory (Split-Path $ExePath)
    $result | Add-Member -NotePropertyName Log -NotePropertyValue (Save-Log -Name $LogName -Result $result)
    return $result
}

function Invoke-Probe {
    param([string]$LogName, [string]$ExePath, [string]$Marker)

    $result = Invoke-Tool -FilePath $ExePath -Arguments @() -WorkingDirectory (Split-Path $ExePath) -TimeoutMs 60000
    $result | Add-Member -NotePropertyName Log -NotePropertyValue (Save-Log -Name $LogName -Result $result)
    $result | Add-Member -NotePropertyName Passed -NotePropertyValue (
        $result.ExitCode -eq 0 -and $result.StdOut -match [regex]::Escape("$Marker=true"))
    # 관찰 항목이 남기는 값(INFO: ...)을 장부에 싣는다.
    $info = @([regex]::Matches($result.Output, '(?m)^INFO:\s*(.+?)\s*$') | ForEach-Object { $_.Groups[1].Value })
    $result | Add-Member -NotePropertyName Info -NotePropertyValue ($info -join '; ')
    return $result
}

function Get-FailureSummary {
    param([object]$Result)
    if ($Result.TimedOut) { return "시간 초과 · log: $($Result.Log)" }
    $first = @(($Result.Output -split "`r?`n") |
        Where-Object { $_ -match '(error|FAIL|fatal)' } |
        Select-Object -First 2 |
        ForEach-Object { $_.Trim() })
    if ($first.Count -eq 0) {
        return "exit $($Result.ExitCode) · log: $($Result.Log)"
    }
    return ($first -join ' | ') + " · log: $($Result.Log)"
}

# 관찰 항목을 컴파일만 해서 판정한다.
function Test-ObserveCompile {
    param([string]$Name, [string]$Config, [string]$VariantName, [string[]]$Flags,
          [string]$Source, [string]$ObjectPath, [string[]]$Extra = @())

    $result = Invoke-Compile -LogName "$Config-$VariantName-$Name" -Flags $Flags -Source $Source -ObjectPath $ObjectPath -Extra $Extra
    if ($result.ExitCode -eq 0) {
        Add-Result -Kind Observe -Name $Name -Config $Config -VariantName $VariantName -Status PASS -Milliseconds $result.Milliseconds
    } else {
        Add-Result -Kind Observe -Name $Name -Config $Config -VariantName $VariantName -Status FAIL -Milliseconds $result.Milliseconds -Detail (Get-FailureSummary $result)
    }
    return $result
}

# 실패해야 하는 항목. 실패했고, 그 이유가 기대한 문안이어야 초록이다.
function Test-ExpectFail {
    param([string]$Name, [string]$Config, [string]$VariantName, [string[]]$Flags,
          [string]$Source, [string]$ObjectPath, [string[]]$Extra = @(), [string]$ExpectPattern)

    $result = Invoke-Compile -LogName "$Config-$VariantName-$Name" -Flags $Flags -Source $Source -ObjectPath $ObjectPath -Extra $Extra
    if ($result.ExitCode -eq 0) {
        Add-Result -Kind ExpectFail -Name $Name -Config $Config -VariantName $VariantName -Status FAIL `
            -Detail "컴파일됐다 — 막혀야 할 것이 통과했다 · log: $($result.Log)"
    } elseif ($result.Output -match $ExpectPattern) {
        Add-Result -Kind ExpectFail -Name $Name -Config $Config -VariantName $VariantName -Status PASS -Detail "기대한 실패 ($ExpectPattern)"
    } else {
        Add-Result -Kind ExpectFail -Name $Name -Config $Config -VariantName $VariantName -Status FAIL `
            -Detail "실패했지만 이유가 다르다 — '$ExpectPattern' 이 없다 · log: $($result.Log)"
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# 검사
# ─────────────────────────────────────────────────────────────────────────────

$stdIxx = Join-Path $env:VCToolsInstallDir 'modules\std.ixx'
$benchRows = [Collections.Generic.List[object]]::new()
$bmiCost = [ordered]@{}

foreach ($config in $configs) {
    $base = Get-BaseFlags -Config $config
    $configOut = Join-Path $OutRoot $config
    $stdOut = Join-Path $configOut 'std'
    $coreOut = Join-Path $configOut 'core'
    New-Item -ItemType Directory -Force $stdOut, $coreOut | Out-Null

    # ── 0. /std 값이 실제로 먹는가 ─────────────────────────────────────────
    # 모르는 /std 값은 D9002 경고 하나로 무시되고 기본 표준으로 컴파일된다. 그러면
    # 아래 모든 모듈 항목이 엉뚱한 이유로 붉어진다 — 여기서 먼저 가른다.
    $stdCheck = Join-Path $stdOut 'language_standard_check.cpp'
    [IO.File]::WriteAllText($stdCheck, "static_assert(_MSVC_LANG > 202002L, `"LANGUAGE_STANDARD_BELOW_CXX23`");`n")
    $result = Invoke-Compile -LogName "$config-toolchain-std-flag" -Flags $base -Source $stdCheck -ObjectPath (Join-Path $stdOut 'language_standard_check.obj')
    if ($result.ExitCode -ne 0 -or $result.Output -match 'D9002|D8021') {
        Add-Result -Kind Gate -Name 'toolchain/std-flag' -Config $config -VariantName '-' -Status FAIL -Detail "/std:$LanguageStandard 이 C++23 으로 먹지 않는다 · log: $($result.Log)"
        continue
    }
    Add-Result -Kind Gate -Name 'toolchain/std-flag' -Config $config -VariantName '-' -Status PASS

    # ── 1. std 모듈 ────────────────────────────────────────────────────────
    $stdIfc = Join-Path $stdOut 'std.ifc'
    $stdObj = Join-Path $stdOut 'std.obj'
    $stdReady = $false
    if (-not (Test-Path -LiteralPath $stdIxx -PathType Leaf)) {
        Add-Result -Kind Gate -Name 'std/bmi' -Config $config -VariantName '-' -Status FAIL -Detail "std.ixx 가 없다: $stdIxx"
    } else {
        $result = Invoke-Compile -LogName "$config-std-bmi" -Flags $base -Source $stdIxx -ObjectPath $stdObj -IfcPath $stdIfc
        if ($result.ExitCode -eq 0) {
            $stdReady = $true
            $bmiCost["$config/std"] = $result.Milliseconds
            Add-Result -Kind Gate -Name 'std/bmi' -Config $config -VariantName '-' -Status PASS -Milliseconds $result.Milliseconds
        } else {
            Add-Result -Kind Gate -Name 'std/bmi' -Config $config -VariantName '-' -Status FAIL -Milliseconds $result.Milliseconds -Detail (Get-FailureSummary $result)
        }
    }
    $stdRef = @('/reference', "std=$stdIfc")

    if ($stdReady) {
        # import std 만 쓰는 번역 단위 — Gate.
        $obj = Join-Path $stdOut 'std_import_probe.obj'
        $exe = Join-Path $stdOut 'std_import_probe.exe'
        $result = Invoke-Compile -LogName "$config-std-import-compile" -Flags $base -Source (Join-Path $probeDir 'std_import_probe.cpp') -ObjectPath $obj -Extra $stdRef
        if ($result.ExitCode -ne 0) {
            Add-Result -Kind Gate -Name 'std/import' -Config $config -VariantName '-' -Status FAIL -Detail (Get-FailureSummary $result)
        } else {
            $link = Invoke-Link -LogName "$config-std-import-link" -Objects @($obj, $stdObj) -ExePath $exe
            if ($link.ExitCode -ne 0) {
                Add-Result -Kind Gate -Name 'std/import' -Config $config -VariantName '-' -Status FAIL -Detail ('링크 실패 · ' + (Get-FailureSummary $link))
            } else {
                $run = Invoke-Probe -LogName "$config-std-import-run" -ExePath $exe -Marker 'CPP_MODULE_STD_OK'
                $status = if ($run.Passed) { 'PASS' } else { 'FAIL' }
                $detail = if ($run.Passed) { '' } else { Get-FailureSummary $run }
                Add-Result -Kind Gate -Name 'std/import' -Config $config -VariantName '-' -Status $status -Milliseconds $result.Milliseconds -Detail $detail
            }
        }

        # include 와 import std 를 한 번역 단위에 섞는다 — Observe. 순서별로 따로 잰다.
        foreach ($mixed in @('std_include_then_import', 'std_import_then_include')) {
            $obj = Join-Path $stdOut "$mixed.obj"
            $exe = Join-Path $stdOut "$mixed.exe"
            $result = Invoke-Compile -LogName "$config-$mixed-compile" -Flags $base -Source (Join-Path $probeDir "$mixed.cpp") -ObjectPath $obj -Extra $stdRef
            if ($result.ExitCode -ne 0) {
                Add-Result -Kind Observe -Name "std/$mixed" -Config $config -VariantName '-' -Status FAIL -Detail (Get-FailureSummary $result)
                continue
            }
            $link = Invoke-Link -LogName "$config-$mixed-link" -Objects @($obj, $stdObj) -ExePath $exe
            if ($link.ExitCode -ne 0) {
                Add-Result -Kind Observe -Name "std/$mixed" -Config $config -VariantName '-' -Status FAIL -Detail ('링크 실패 · ' + (Get-FailureSummary $link))
                continue
            }
            $run = Invoke-Probe -LogName "$config-$mixed-run" -ExePath $exe -Marker 'CPP_MODULE_STD_MIXED_OK'
            $status = if ($run.Passed) { 'PASS' } else { 'FAIL' }
            $detail = if ($run.Passed) { '' } else { Get-FailureSummary $run }
            Add-Result -Kind Observe -Name "std/$mixed" -Config $config -VariantName '-' -Status $status -Milliseconds $result.Milliseconds -Detail $detail
        }
    } else {
        foreach ($name in @('std/import', 'std/std_include_then_import', 'std/std_import_then_include')) {
            $kind = if ($name -eq 'std/import') { 'Gate' } else { 'Observe' }
            Add-Result -Kind $kind -Name $name -Config $config -VariantName '-' -Status SKIP -Detail 'std BMI 없음'
        }
    }

    # ── 2. ce.core ─────────────────────────────────────────────────────────
    $coreFlags = $base + @('/I', $coreDir, '/I', $probeDir)
    $coreIfc = Join-Path $coreOut 'ce.core.ifc'
    $coreObj = Join-Path $coreOut 'ce.core.obj'
    $result = Invoke-Compile -LogName "$config-core-bmi" -Flags $coreFlags -Source (Join-Path $coreDir 'ce.core.ixx') -ObjectPath $coreObj -IfcPath $coreIfc
    if ($result.ExitCode -ne 0) {
        Add-Result -Kind Gate -Name 'core/bmi' -Config $config -VariantName '-' -Status FAIL -Milliseconds $result.Milliseconds -Detail (Get-FailureSummary $result)
        Add-Result -Kind Gate -Name 'core/identity' -Config $config -VariantName '-' -Status SKIP -Detail 'ce.core BMI 없음'
    } else {
        $bmiCost["$config/ce.core"] = $result.Milliseconds
        Add-Result -Kind Gate -Name 'core/bmi' -Config $config -VariantName '-' -Status PASS -Milliseconds $result.Milliseconds

        $moduleObj = Join-Path $coreOut 'core_module_probe.obj'
        $peerObj = Join-Path $coreOut 'core_header_peer.obj'
        $exe = Join-Path $coreOut 'core_module_probe.exe'
        $compiled = Invoke-Compile -LogName "$config-core-module-compile" -Flags $coreFlags -Source (Join-Path $probeDir 'core_module_probe.cpp') -ObjectPath $moduleObj -Extra @('/reference', "ce.core=$coreIfc")
        $peer = Invoke-Compile -LogName "$config-core-peer-compile" -Flags $coreFlags -Source (Join-Path $probeDir 'core_header_peer.cpp') -ObjectPath $peerObj
        if ($compiled.ExitCode -ne 0 -or $peer.ExitCode -ne 0) {
            $failed = if ($compiled.ExitCode -ne 0) { $compiled } else { $peer }
            Add-Result -Kind Gate -Name 'core/identity' -Config $config -VariantName '-' -Status FAIL -Detail (Get-FailureSummary $failed)
        } else {
            $link = Invoke-Link -LogName "$config-core-link" -Objects @($moduleObj, $peerObj, $coreObj) -ExePath $exe
            if ($link.ExitCode -ne 0) {
                Add-Result -Kind Gate -Name 'core/identity' -Config $config -VariantName '-' -Status FAIL -Detail ('링크 실패 — 헤더 쪽과 모듈 쪽이 다른 엔터티를 본다 · ' + (Get-FailureSummary $link))
            } else {
                $run = Invoke-Probe -LogName "$config-core-run" -ExePath $exe -Marker 'CPP_MODULE_CORE_OK'
                $status = if ($run.Passed) { 'PASS' } else { 'FAIL' }
                $detail = if ($run.Passed) { '' } else { Get-FailureSummary $run }
                Add-Result -Kind Gate -Name 'core/identity' -Config $config -VariantName '-' -Status $status -Detail $detail
            }
        }
    }

    # ── 3. TypeTrait.h 경계(관찰) ──────────────────────────────────────────
    $traitIfc = Join-Path $coreOut 'ce.probe.typetrait.ifc'
    $traitObj = Join-Path $coreOut 'ce.probe.typetrait.obj'
    $result = Invoke-Compile -LogName "$config-typetrait-bmi" -Flags $coreFlags -Source (Join-Path $probeDir 'typetrait_boundary.ixx') -ObjectPath $traitObj -IfcPath $traitIfc
    if ($result.ExitCode -ne 0) {
        Add-Result -Kind Observe -Name 'typetrait/bmi' -Config $config -VariantName '-' -Status FAIL -Milliseconds $result.Milliseconds -Detail (Get-FailureSummary $result)
        Add-Result -Kind Observe -Name 'typetrait/use' -Config $config -VariantName '-' -Status SKIP -Detail 'BMI 없음'
        # ★ 관찰 항목(BMI)이 붉어서 못 잰 것이다. ExpectFail 로 적으면 건너뜀이 Gate
        #   붉음으로 집계되어, 관찰 결과 하나가 1단계 판정 전체를 붉게 만든다.
        Add-Result -Kind Observe -Name 'typetrait/macro-boundary' -Config $config -VariantName '-' -Status SKIP -Detail 'BMI 없음 — 매크로 경계를 재지 못했다'
    } else {
        # 경고 없이 섰는지도 남긴다 — TU-local 노출(g_guids)을 경고로만 알리는 경우가 있다.
        $warnings = @(($result.Output -split "`r?`n") | Where-Object { $_ -match 'warning C\d+' })
        $detail = if ($warnings.Count -gt 0) { "경고 $($warnings.Count) 건: " + ($warnings[0].Trim()) } else { '' }
        Add-Result -Kind Observe -Name 'typetrait/bmi' -Config $config -VariantName '-' -Status PASS -Milliseconds $result.Milliseconds -Detail $detail

        $traitRef = @('/reference', "ce.probe.typetrait=$traitIfc")
        $null = Test-ObserveCompile -Name 'typetrait/use' -Config $config -VariantName '-' -Flags $coreFlags `
            -Source (Join-Path $probeDir 'typetrait_module_use.cpp') -ObjectPath (Join-Path $coreOut 'typetrait_module_use.obj') -Extra $traitRef
        Test-ExpectFail -Name 'typetrait/macro-boundary' -Config $config -VariantName '-' -Flags $coreFlags `
            -Source (Join-Path $probeDir 'typetrait_macro_boundary.cpp') -ObjectPath (Join-Path $coreOut 'typetrait_macro_boundary.obj') `
            -Extra $traitRef -ExpectPattern 'type_guid'
    }

    # 모듈에 붙은 타입의 이름(관찰) — 2단계에서 타입을 모듈로 옮길 때 TypeID 가 흔들리는가.
    $ownerIfc = Join-Path $coreOut 'ce.probe.typename_owner.ifc'
    $ownerObj = Join-Path $coreOut 'ce.probe.typename_owner.obj'
    $result = Invoke-Compile -LogName "$config-typename-owner-bmi" -Flags $coreFlags -Source (Join-Path $probeDir 'typename_owner.ixx') -ObjectPath $ownerObj -IfcPath $ownerIfc
    if ($result.ExitCode -ne 0) {
        Add-Result -Kind Observe -Name 'typetrait/module-owned-name' -Config $config -VariantName '-' -Status FAIL -Detail (Get-FailureSummary $result)
    } else {
        $obj = Join-Path $coreOut 'typename_module_owned.obj'
        $exe = Join-Path $coreOut 'typename_module_owned.exe'
        $compiled = Invoke-Compile -LogName "$config-typename-compile" -Flags $coreFlags -Source (Join-Path $probeDir 'typename_module_owned.cpp') -ObjectPath $obj -Extra @('/reference', "ce.probe.typename_owner=$ownerIfc")
        if ($compiled.ExitCode -ne 0) {
            Add-Result -Kind Observe -Name 'typetrait/module-owned-name' -Config $config -VariantName '-' -Status FAIL -Detail (Get-FailureSummary $compiled)
        } else {
            $link = Invoke-Link -LogName "$config-typename-link" -Objects @($obj, $ownerObj) -ExePath $exe
            if ($link.ExitCode -ne 0) {
                Add-Result -Kind Observe -Name 'typetrait/module-owned-name' -Config $config -VariantName '-' -Status FAIL -Detail ('링크 실패 · ' + (Get-FailureSummary $link))
            } else {
                $run = Invoke-Probe -LogName "$config-typename-run" -ExePath $exe -Marker 'CPP_MODULE_TYPENAME_OK'
                $status = if ($run.Passed) { 'PASS' } else { 'FAIL' }
                Add-Result -Kind Observe -Name 'typetrait/module-owned-name' -Config $config -VariantName '-' -Status $status -Detail $run.Info
            }
        }
    }

    # ── 4. ce.diagnostics — 구성마다 따로 ───────────────────────────────────
    foreach ($variantName in $variants) {
        $variantOut = Join-Path $configOut $variantName
        New-Item -ItemType Directory -Force $variantOut | Out-Null
        $diagFlags = $base + (Get-VariantFlags -VariantName $variantName) + @('/I', $diagDir, '/I', $probeDir)
        # ★ /WX — EngineDiagnostics 는 경고 0 이 계약이다(verify-profile-core.ps1). 코어
        #   .cpp 와 모듈 인터페이스에만 건다. 모듈 인터페이스가 이 계약 아래서 경고를
        #   내면 그것 자체가 알아야 할 결과다. 프로브 TU 에는 걸지 않는다 — 프로브의
        #   경고로 1단계 판정이 붉어지면 무엇을 잰 것인지 흐려진다.
        $diagStrict = $diagFlags + @('/WX')

        # 4-a. 코어 .cpp 일곱 개. 헤더 쪽 빌드와 같은 소스다.
        $coreObjects = [Collections.Generic.List[string]]::new()
        $coreFailed = $null
        foreach ($source in $diagSources) {
            $obj = Join-Path $variantOut ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
            $result = Invoke-Compile -LogName "$config-$variantName-diag-$([IO.Path]::GetFileName($source))" -Flags $diagStrict -Source $source -ObjectPath $obj
            if ($result.ExitCode -ne 0) { $coreFailed = $result; break }
            $coreObjects.Add($obj)
        }
        if ($null -ne $coreFailed) {
            Add-Result -Kind Gate -Name 'diag/sources' -Config $config -VariantName $variantName -Status FAIL -Detail (Get-FailureSummary $coreFailed)
            continue
        }
        Add-Result -Kind Gate -Name 'diag/sources' -Config $config -VariantName $variantName -Status PASS

        # 4-b. BMI.
        $diagIfc = Join-Path $variantOut 'ce.diagnostics.ifc'
        $diagObj = Join-Path $variantOut 'ce.diagnostics.obj'
        $result = Invoke-Compile -LogName "$config-$variantName-diag-bmi" -Flags $diagStrict -Source (Join-Path $diagDir 'ce.diagnostics.ixx') -ObjectPath $diagObj -IfcPath $diagIfc
        if ($result.ExitCode -ne 0) {
            Add-Result -Kind Gate -Name 'diag/bmi' -Config $config -VariantName $variantName -Status FAIL -Milliseconds $result.Milliseconds -Detail (Get-FailureSummary $result)
            continue
        }
        $bmiCost["$config/$variantName/ce.diagnostics"] = $result.Milliseconds
        Add-Result -Kind Gate -Name 'diag/bmi' -Config $config -VariantName $variantName -Status PASS -Milliseconds $result.Milliseconds
        $diagRef = @('/reference', "ce.diagnostics=$diagIfc")

        # 4-c. 동일성 — import 로만 본 TU 와 헤더로만 본 TU 를 한 실행 파일로.
        $moduleObj = Join-Path $variantOut 'diagnostics_module_probe.obj'
        $peerObj = Join-Path $variantOut 'diagnostics_header_peer.obj'
        $exe = Join-Path $variantOut 'diagnostics_module_probe.exe'
        $compiled = Invoke-Compile -LogName "$config-$variantName-diag-module-compile" -Flags $diagFlags -Source (Join-Path $probeDir 'diagnostics_module_probe.cpp') -ObjectPath $moduleObj -Extra $diagRef
        $peer = Invoke-Compile -LogName "$config-$variantName-diag-peer-compile" -Flags $diagFlags -Source (Join-Path $probeDir 'diagnostics_header_peer.cpp') -ObjectPath $peerObj
        if ($compiled.ExitCode -ne 0 -or $peer.ExitCode -ne 0) {
            $failed = if ($compiled.ExitCode -ne 0) { $compiled } else { $peer }
            Add-Result -Kind Gate -Name 'diag/identity' -Config $config -VariantName $variantName -Status FAIL -Detail (Get-FailureSummary $failed)
        } else {
            $link = Invoke-Link -LogName "$config-$variantName-diag-link" -Objects (@($moduleObj, $peerObj, $diagObj) + $coreObjects) -ExePath $exe
            if ($link.ExitCode -ne 0) {
                Add-Result -Kind Gate -Name 'diag/identity' -Config $config -VariantName $variantName -Status FAIL -Detail ('링크 실패 — 헤더 쪽과 모듈 쪽이 다른 엔터티를 본다 · ' + (Get-FailureSummary $link))
            } else {
                $run = Invoke-Probe -LogName "$config-$variantName-diag-run" -ExePath $exe -Marker 'CPP_MODULE_DIAG_OK'
                $status = if ($run.Passed) { 'PASS' } else { 'FAIL' }
                $detail = if ($run.Passed) { '' } else { Get-FailureSummary $run }
                Add-Result -Kind Gate -Name 'diag/identity' -Config $config -VariantName $variantName -Status $status -Milliseconds $compiled.Milliseconds -Detail $detail
            }
        }

        # 4-d. 구성이 어긋난 BMI 는 소비자에서 막혀야 한다 — ExpectFail.
        $opposite = if ($variantName -eq 'Shipping') { 'Development' } else { 'Shipping' }
        $mismatchFlags = $base + (Get-VariantFlags -VariantName $opposite) + @('/I', $diagDir, '/I', $probeDir)
        Test-ExpectFail -Name "diag/shipping-mismatch(consumer=$opposite)" -Config $config -VariantName $variantName -Flags $mismatchFlags `
            -Source (Join-Path $probeDir 'diagnostics_shipping_mismatch.cpp') -ObjectPath (Join-Path $variantOut 'diagnostics_shipping_mismatch.obj') `
            -Extra $diagRef -ExpectPattern 'CE_MODULE_SHIPPING_MISMATCH'

        # 4-e. 한 번역 단위에 include 와 import 를 섞는다 — Observe. 순서별로.
        foreach ($mixed in @('diagnostics_mixed_include_import', 'diagnostics_mixed_import_include')) {
            $null = Test-ObserveCompile -Name "diag/$mixed" -Config $config -VariantName $variantName -Flags $diagFlags `
                -Source (Join-Path $probeDir "$mixed.cpp") -ObjectPath (Join-Path $variantOut "$mixed.obj") -Extra $diagRef
        }

        # 4-f. import std 와 import ce.diagnostics 를 함께 — Observe.
        if ($stdReady) {
            $null = Test-ObserveCompile -Name 'diag/std_with_diagnostics' -Config $config -VariantName $variantName -Flags $diagFlags `
                -Source (Join-Path $probeDir 'std_with_diagnostics.cpp') -ObjectPath (Join-Path $variantOut 'std_with_diagnostics.obj') -Extra ($diagRef + $stdRef)
        } else {
            Add-Result -Kind Observe -Name 'diag/std_with_diagnostics' -Config $config -VariantName $variantName -Status SKIP -Detail 'std BMI 없음'
        }

        # ── 5. 벤치(Development 만) ─────────────────────────────────────────
        # include 와 import 를 **번갈아** 컴파일한다. 한쪽을 몰아서 재면 디스크 캐시와
        # 열 상태가 뒤쪽에 유리하게 쏠린다. 재는 것은 소비자 TU 하나의 컴파일
        # 시간이다 — BMI 를 만드는 비용(위 bmi 항목)은 따로 적고, 프로젝트당 한 번이다.
        if ($BenchRepeat -gt 0 -and $variantName -eq 'Development') {
            $benchOut = Join-Path $variantOut 'bench'
            New-Item -ItemType Directory -Force $benchOut | Out-Null
            $pairs = @()
            if ($stdReady) {
                $pairs += [pscustomobject]@{ Pair = 'std'; Include = 'std_include.cpp'; Import = 'std_import.cpp'; Flags = $base; Ref = $stdRef }
            }
            $pairs += [pscustomobject]@{ Pair = 'diagnostics'; Include = 'diag_include.cpp'; Import = 'diag_import.cpp'; Flags = $diagFlags; Ref = $diagRef }

            foreach ($pair in $pairs) {
                $broken = $false
                for ($run = 1; $run -le $BenchRepeat -and -not $broken; $run++) {
                    foreach ($mode in @('include', 'import')) {
                        $file = if ($mode -eq 'include') { $pair.Include } else { $pair.Import }
                        $extra = if ($mode -eq 'import') { $pair.Ref } else { @() }
                        $result = Invoke-Compile -LogName "$config-bench-$($pair.Pair)-$mode" -Flags ($pair.Flags + @('/I', $benchDir)) `
                            -Source (Join-Path $benchDir $file) -ObjectPath (Join-Path $benchOut "$($pair.Pair)-$mode.obj") -Extra $extra
                        if ($result.ExitCode -ne 0) {
                            Add-Result -Kind Observe -Name "bench/$($pair.Pair)-$mode" -Config $config -VariantName $variantName -Status FAIL -Detail (Get-FailureSummary $result)
                            $broken = $true
                            break
                        }
                        $benchRows.Add([pscustomobject]@{
                            Config = $config; Pair = $pair.Pair; Mode = $mode; Run = $run; Milliseconds = $result.Milliseconds })
                    }
                }
            }
        }
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# 기록
# ─────────────────────────────────────────────────────────────────────────────

$header = "# cl=$compilerVersion VCToolsVersion=$toolsVersion std=$LanguageStandard git=$gitBranch@$gitCommit time=$((Get-Date).ToString('s'))"
$resultsPath = Join-Path $OutRoot 'results.tsv'
$lines = @($header, "kind`tname`tconfig`tvariant`tstatus`tms`tdetail")
$lines += $results | ForEach-Object { "$($_.Kind)`t$($_.Name)`t$($_.Config)`t$($_.Variant)`t$($_.Status)`t$($_.Milliseconds)`t$($_.Detail)" }
[IO.File]::WriteAllLines($resultsPath, [string[]]$lines)

if ($benchRows.Count -gt 0) {
    $benchPath = Join-Path $OutRoot 'bench.tsv'
    $lines = @($header, "config`tpair`tmode`trun`tms")
    $lines += $benchRows | ForEach-Object { "$($_.Config)`t$($_.Pair)`t$($_.Mode)`t$($_.Run)`t$($_.Milliseconds)" }
    [IO.File]::WriteAllLines($benchPath, [string[]]$lines)

    Write-Host ''
    Write-Host '── 벤치: 소비자 TU 하나의 컴파일 시간(중앙값, 프로세스 기동 포함) ──'
    foreach ($group in ($benchRows | Group-Object Config, Pair)) {
        $first = $group.Group[0]
        $medians = @{}
        foreach ($mode in @('include', 'import')) {
            $values = @($group.Group | Where-Object Mode -eq $mode | ForEach-Object Milliseconds | Sort-Object)
            $medians[$mode] = if ($values.Count -gt 0) { $values[[int][math]::Floor(($values.Count - 1) / 2)] } else { $null }
        }
        if ($null -ne $medians['include'] -and $null -ne $medians['import'] -and $medians['include'] -gt 0) {
            $ratio = [math]::Round($medians['import'] / $medians['include'], 2)
            Write-Host ('{0,-7} {1,-12} include {2,8} ms   import {3,8} ms   import/include {4}' -f `
                $first.Config, $first.Pair, $medians['include'], $medians['import'], $ratio)
        }
    }
    if ($bmiCost.Count -gt 0) {
        Write-Host '── BMI 1회 생성 비용 ──'
        foreach ($key in $bmiCost.Keys) {
            Write-Host ('{0,-40} {1,8} ms' -f $key, $bmiCost[$key])
        }
    }
}

$gateFailures = @($results | Where-Object { $_.Kind -ne 'Observe' -and $_.Status -eq 'FAIL' })
$observeFailures = @($results | Where-Object { $_.Kind -eq 'Observe' -and $_.Status -eq 'FAIL' })
$skipped = @($results | Where-Object { $_.Kind -ne 'Observe' -and $_.Status -eq 'SKIP' })

Write-Host ''
Write-Host "results : $resultsPath"
Write-Host ("판정     : Gate/ExpectFail 붉음 {0} · 건너뜀 {1} · Observe 붉음 {2}" -f $gateFailures.Count, $skipped.Count, $observeFailures.Count)

# ★ 건너뛴 Gate 도 초록이 아니다. 앞 단계가 붉어 뒤를 못 잰 것을 통과로 읽지 않는다.
if ($gateFailures.Count -gt 0 -or $skipped.Count -gt 0) {
    Write-Host 'CPP_MODULE_PROBE_OK=false' -ForegroundColor Red
    exit 1
}
Write-Host 'CPP_MODULE_PROBE_OK=true' -ForegroundColor Green
exit 0

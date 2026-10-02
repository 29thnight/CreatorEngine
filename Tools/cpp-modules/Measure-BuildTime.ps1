#Requires -Version 7.0
[CmdletBinding()]
param(
    # 이 측정의 이름. 같은 결과 파일에 여러 워크트리를 쌓고 이 이름으로 견준다.
    # 예: baseline (master 워크트리) · modules (cpp_module_ixx 워크트리)
    [Parameter(Mandatory)][string]$Label,
    # 잴 워크트리. 비우면 이 스크립트가 든 저장소다. 기준선 워크트리에는 이 스크립트가
    # 없으므로, 모듈 워크트리의 스크립트로 기준선 워크트리를 가리켜 잰다.
    [string]$RepoRoot = '',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$Platform = 'x64',
    [switch]$Shipping,
    # 빌드 대상(RepoRoot 상대 또는 절대). 비우면 CreatorEngine.sln —
    # Tools/profiling-validation 의 Build 와 같은 대상이다.
    [string]$Target = '',
    [ValidateSet('clean','noop','touch-cpp','touch-header')]
    [string[]]$Scenario = @('clean', 'noop', 'touch-cpp', 'touch-header'),
    [ValidateRange(1, 20)][int]$Repeat = 3,
    # 클린 빌드는 비싸다. 기본은 한 번이다.
    [ValidateRange(1, 20)][int]$CleanRepeat = 1,
    # touch-cpp: 유니티 블롭 하나가 통째로 다시 도는 비용을 본다.
    [string[]]$TouchCpp = @('Engine/RenderEngine/Texture.cpp'),
    # touch-header: Uuid.h 는 정적 분석상 302 TU 에 닿는 가장 넓은 공용 헤더,
    # ProfileScope.h 는 모듈 파일럿(EngineDiagnostics)의 대표 헤더(99 TU)다.
    [string[]]$TouchHeader = @('Engine/Utility_Framework/Uuid.h', 'Engine/EngineDiagnostics/ProfileScope.h'),
    # 0 이면 /m(논리 코어 전부).
    [ValidateRange(0, 256)][int]$MaxCpuCount = 0,
    # 측정 전 준비 빌드를 건너뛴다. 이미 같은 구성으로 막 빌드한 상태일 때만 쓴다.
    [switch]$SkipPrime,
    # 회차마다 .binlog 를 남긴다(MSBuild Structured Log Viewer 로 연다).
    [switch]$BinaryLog,
    [string]$MSBuild = '',
    # 비우면 <이 스크립트의 저장소>/Build/Timing/build-time.tsv 에 덧붙인다.
    [string]$OutFile = ''
)

# 워크트리 사이의 빌드 시간 비교.
#
# 같은 기계에서 기준선 워크트리와 모듈 워크트리를 **차례로** 재고, 결과를 한 TSV 에
# 쌓아 레이블로 견준다. 이 스크립트는 C++ 를 고치지 않는다 — 시나리오가 건드리는
# 것은 파일의 수정 시각뿐이고 내용은 그대로다(ReflectionRedesignPlan CT0 의
# "헤더 터치 재빌드" 와 같은 정의).
#
# ── 시나리오 ──
#
#   clean         msbuild /t:Clean 뒤 /t:Build 를 잰다(Clean 시간은 빼고).
#   noop          아무것도 안 바꾸고 다시 빌드한다. MSBuild 의 최신 판정 비용 —
#                 모듈은 의존성 스캔이 붙으므로 이 값이 늘 수 있다. 꼭 같이 본다.
#   touch-cpp     .cpp 하나의 수정 시각만 바꾼다. 유니티 빌드에서는 그 파일이 든
#                 블롭(10~15 개 파일)이 통째로 다시 돈다.
#   touch-header  헤더 하나의 수정 시각만 바꾼다. 그 헤더에 닿는 TU 전부가 다시 돈다.
#
# ── 지켜야 하는 것 ──
#
#   · 두 워크트리를 **동시에** 빌드하지 않는다. CPU 를 나눠 쓰면 둘 다 틀린다. 측정
#     직전에 cl/link/MSBuild 프로세스가 떠 있으면 경고한다.
#   · 첫 빌드는 재지 않는다. vcpkg 매니페스트 설치(바이너리 캐시가 차 있으면 약 26 초,
#     비어 있으면 약 38 분)와 reflgen 의 "Build again (once per environment)" 이 거기서
#     일어난다. 준비 빌드(-SkipPrime 이 아니면 자동)가 그것을 흡수한다.
#   · 노드 재사용을 끈다(/nr:false). 앞 측정의 MSBuild 노드가 남아 다음 측정을
#     데우거나, 다른 워크트리의 노드를 물려받지 않게 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = $scriptRepo
}
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'CreatorEngine.sln') -PathType Leaf)) {
    throw "CreatorEngine.sln 이 없다 — 워크트리 경로가 맞는가: $RepoRoot"
}

function Resolve-RepoPath {
    param([string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

$targetPath = if ([string]::IsNullOrWhiteSpace($Target)) { Join-Path $RepoRoot 'CreatorEngine.sln' } else { Resolve-RepoPath $Target }
if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
    throw "빌드 대상이 없다: $targetPath"
}

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $scriptRepo 'Build/Timing/build-time.tsv'
}
$OutFile = [IO.Path]::GetFullPath($OutFile)
$logDir = Join-Path (Split-Path $OutFile) 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null

# ─────────────────────────────────────────────────────────────────────────────
# MSBuild
# ─────────────────────────────────────────────────────────────────────────────

function Find-MSBuild {
    param([string]$Explicit)

    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (-not (Test-Path -LiteralPath $Explicit -PathType Leaf)) { throw "MSBuild.exe 가 없다: $Explicit" }
        return $Explicit
    }
    $command = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }

    # Directory.Build.props 가 x64 host 도구를 고정한다(PreferredToolArchitecture) —
    # MSBuild 도 amd64 판을 쓴다(verify-msbuild-tool-architecture.ps1 과 같은 선택).
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $found = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\amd64\MSBuild.exe')
        if ($found.Count -gt 0) { return $found[0] }
    }
    $fallback = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (Test-Path -LiteralPath $fallback -PathType Leaf) { return $fallback }
    throw 'MSBuild.exe 를 찾지 못했다. -MSBuild 로 경로를 준다.'
}

$msbuildPath = Find-MSBuild -Explicit $MSBuild

function Invoke-MSBuild {
    param([string]$Targets, [string]$LogName)

    $arguments = @(
        $targetPath,
        "/t:$Targets",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        '/nr:false',
        '/nologo',
        '/v:minimal',
        '/clp:Summary'
    )
    $arguments += if ($MaxCpuCount -gt 0) { "/m:$MaxCpuCount" } else { '/m' }
    if ($Shipping) {
        $arguments += '/p:EngineShipping=true'
    }
    if ($BinaryLog) {
        $arguments += "/bl:$(Join-Path $logDir "$LogName.binlog")"
    }

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $msbuildPath
    foreach ($argument in $arguments) { [void]$info.ArgumentList.Add($argument) }
    $info.WorkingDirectory = $RepoRoot
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $watch.Stop()

    $logPath = Join-Path $logDir "$LogName.log"
    [IO.File]::WriteAllText($logPath, $stdout.Result + $stderr.Result)
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Seconds  = [math]::Round($watch.Elapsed.TotalSeconds, 2)
        Log      = $logPath
    }
}

function Get-MSBuildProperty {
    param([string]$Project, [string]$Name)
    try {
        $value = & $msbuildPath $Project "/p:Configuration=$Configuration" "/p:Platform=$Platform" "-getProperty:$Name" 2>$null
        if ($LASTEXITCODE -eq 0) { return ([string]($value | Select-Object -First 1)).Trim() }
    } catch {
    }
    return 'unknown'
}

function Assert-Quiet {
    # 다른 빌드가 돌고 있으면 측정이 틀린다. 멈추지는 않고 경고와 함께 기록한다.
    $busy = @(Get-Process -Name 'cl', 'link', 'MSBuild', 'vctip' -ErrorAction SilentlyContinue)
    if ($busy.Count -gt 0) {
        Write-Warning ("측정 직전에 빌드 프로세스가 떠 있다: " + (($busy | ForEach-Object { "$($_.Name)#$($_.Id)" }) -join ', ') +
            " — 다른 워크트리를 동시에 빌드하고 있다면 이 측정은 버린다.")
        return $false
    }
    return $true
}

# ─────────────────────────────────────────────────────────────────────────────
# 기록
# ─────────────────────────────────────────────────────────────────────────────

$gitBranch = try { (& git -C $RepoRoot rev-parse --abbrev-ref HEAD 2>$null) } catch { 'unknown' }
$gitCommit = try { (& git -C $RepoRoot rev-parse --short HEAD 2>$null) } catch { 'unknown' }
$gitDirty = try { @(& git -C $RepoRoot status --porcelain 2>$null).Count } catch { -1 }
$msbuildVersion = try { ([string](& $msbuildPath -version -nologo 2>$null | Select-Object -Last 1)).Trim() } catch { 'unknown' }
$vcTools = Get-MSBuildProperty -Project (Join-Path $RepoRoot 'Engine/EngineDiagnostics/EngineDiagnostics.vcxproj') -Name 'VCToolsVersion'
$cpu = try { (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim() } catch { 'unknown' }
$cores = [Environment]::ProcessorCount
$variantName = if ($Shipping) { 'Shipping' } else { 'Development' }
$targetName = [IO.Path]::GetFileName($targetPath)
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')

$columns = @('time', 'label', 'repo', 'branch', 'commit', 'dirty', 'configuration', 'platform', 'variant',
             'target', 'scenario', 'file', 'iteration', 'seconds', 'exit', 'quiet',
             'msbuild', 'vctools', 'cpu', 'cores', 'maxcpu')
if (-not (Test-Path -LiteralPath $OutFile -PathType Leaf)) {
    New-Item -ItemType Directory -Force (Split-Path $OutFile) | Out-Null
    [IO.File]::WriteAllText($OutFile, ($columns -join "`t") + "`n")
}

$rows = [Collections.Generic.List[object]]::new()

function Add-Row {
    param([string]$ScenarioName, [string]$File, [int]$Iteration, [object]$Result, [bool]$Quiet)

    $row = [ordered]@{
        time = (Get-Date).ToString('s'); label = $Label; repo = $RepoRoot; branch = $gitBranch
        commit = $gitCommit; dirty = $gitDirty; configuration = $Configuration; platform = $Platform
        variant = $variantName; target = $targetName; scenario = $ScenarioName; file = $File
        iteration = $Iteration; seconds = $Result.Seconds; exit = $Result.ExitCode; quiet = $Quiet
        msbuild = $msbuildVersion; vctools = $vcTools; cpu = $cpu; cores = $cores; maxcpu = $MaxCpuCount
    }
    $rows.Add([pscustomobject]$row)
    $line = ($columns | ForEach-Object { [string]$row[$_] }) -join "`t"
    [IO.File]::AppendAllText($OutFile, $line + "`n")

    $color = if ($Result.ExitCode -eq 0) { 'Gray' } else { 'Red' }
    $where = if ($File) { " $File" } else { '' }
    Write-Host ('{0,-13} #{1} {2,9:N2} s  exit {3}{4}' -f $ScenarioName, $Iteration, $Result.Seconds, $Result.ExitCode, $where) -ForegroundColor $color
    if ($Result.ExitCode -ne 0) {
        Write-Host "  log: $($Result.Log)" -ForegroundColor Red
    }
}

function Set-Touched {
    param([string]$Path)
    $full = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "건드릴 파일이 없다: $full"
    }
    (Get-Item -LiteralPath $full).LastWriteTime = Get-Date
}

Write-Host ''
Write-Host "label    : $Label"
Write-Host "repo     : $RepoRoot ($gitBranch @ $gitCommit, 변경 $gitDirty 건)"
Write-Host "target   : $targetPath  [$Configuration|$Platform $variantName]"
Write-Host "msbuild  : $msbuildPath ($msbuildVersion)  VCToolsVersion=$vcTools"
Write-Host "machine  : $cpu, $cores 논리 코어"
Write-Host "out      : $OutFile"
Write-Host ''

# ─────────────────────────────────────────────────────────────────────────────
# 측정
# ─────────────────────────────────────────────────────────────────────────────

if (-not $SkipPrime) {
    Write-Host '준비 빌드(재지 않는다) …'
    $prime = Invoke-MSBuild -Targets 'Build' -LogName "$Label-$stamp-prime"
    if ($prime.ExitCode -ne 0) {
        # reflgen 이 이번 빌드에 설치됐으면 한 번 더 빌드하라고 멈춘다(환경당 한 번).
        Write-Host "  준비 빌드 실패(exit $($prime.ExitCode)) — 한 번 더 시도한다. log: $($prime.Log)"
        $prime = Invoke-MSBuild -Targets 'Build' -LogName "$Label-$stamp-prime2"
        if ($prime.ExitCode -ne 0) {
            throw "준비 빌드가 두 번 실패했다 — 측정할 수 없다. log: $($prime.Log)"
        }
    }
    Write-Host ("  준비 빌드 {0:N2} s" -f $prime.Seconds)
}

foreach ($name in $Scenario) {
    switch ($name) {
        'clean' {
            for ($i = 1; $i -le $CleanRepeat; $i++) {
                $cleaned = Invoke-MSBuild -Targets 'Clean' -LogName "$Label-$stamp-clean-$i-clean"
                if ($cleaned.ExitCode -ne 0) { throw "Clean 실패 — log: $($cleaned.Log)" }
                $quiet = Assert-Quiet
                $result = Invoke-MSBuild -Targets 'Build' -LogName "$Label-$stamp-clean-$i"
                Add-Row -ScenarioName 'clean' -File '' -Iteration $i -Result $result -Quiet $quiet
            }
        }
        'noop' {
            for ($i = 1; $i -le $Repeat; $i++) {
                $quiet = Assert-Quiet
                $result = Invoke-MSBuild -Targets 'Build' -LogName "$Label-$stamp-noop-$i"
                Add-Row -ScenarioName 'noop' -File '' -Iteration $i -Result $result -Quiet $quiet
            }
        }
        { $_ -in @('touch-cpp', 'touch-header') } {
            $files = if ($name -eq 'touch-cpp') { $TouchCpp } else { $TouchHeader }
            foreach ($file in $files) {
                for ($i = 1; $i -le $Repeat; $i++) {
                    Set-Touched -Path $file
                    $quiet = Assert-Quiet
                    $safe = ($file -replace '[^A-Za-z0-9_.-]', '_')
                    $result = Invoke-MSBuild -Targets 'Build' -LogName "$Label-$stamp-$name-$safe-$i"
                    Add-Row -ScenarioName $name -File $file -Iteration $i -Result $result -Quiet $quiet
                }
            }
        }
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# 요약 — 결과 파일 전체를 레이블별로 견준다
# ─────────────────────────────────────────────────────────────────────────────

function Get-Median {
    param([double[]]$Values)
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { return $null }
    $middle = [int][math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return $sorted[$middle] }
    return [math]::Round(($sorted[$middle - 1] + $sorted[$middle]) / 2, 2)
}

# ★ 같은 구성·대상·기계의 성공한 회차만 견준다. 실패한 빌드의 시간이나 다른 구성의
#   시간이 섞이면 비교가 아니라 우연이 된다. 다른 빌드가 떠 있던 회차(quiet=False)도 뺀다.
$all = @(Import-Csv -LiteralPath $OutFile -Delimiter "`t" | Where-Object {
    $_.configuration -eq $Configuration -and $_.platform -eq $Platform -and $_.variant -eq $variantName -and
    $_.target -eq $targetName -and $_.cpu -eq $cpu -and $_.exit -eq '0' -and $_.quiet -eq 'True'
})

Write-Host ''
Write-Host "── 중앙값(초) — $Configuration|$Platform $variantName · $targetName · 성공·조용한 회차만 ──"
$labels = @($all | ForEach-Object label | Sort-Object -Unique)
$keys = @($all | ForEach-Object { "$($_.scenario) $($_.file)".Trim() } | Sort-Object -Unique)
Write-Host (('{0,-58}' -f 'scenario') + (($labels | ForEach-Object { '{0,14}' -f $_ }) -join ''))
foreach ($key in $keys) {
    $cells = foreach ($name in $labels) {
        $values = @($all | Where-Object { $_.label -eq $name -and "$($_.scenario) $($_.file)".Trim() -eq $key } |
            ForEach-Object { [double]$_.seconds })
        $median = Get-Median -Values $values
        if ($null -eq $median) { '{0,14}' -f '-' } else { '{0,14}' -f ('{0:N2} (n={1})' -f $median, $values.Count) }
    }
    Write-Host (('{0,-58}' -f $key) + ($cells -join ''))
}

$failed = @($rows | Where-Object { $_.exit -ne 0 })
if ($failed.Count -gt 0) {
    Write-Host "실패한 회차 $($failed.Count) 건 — 위 표에서 빠졌다." -ForegroundColor Red
    exit 1
}
exit 0

[CmdletBinding()]
param(
    [string]$Stage = '',

    [ValidateRange(1, 60)]
    [int]$StartupWaitSeconds = 8,

    [ValidateRange(10, 300)]
    [int]$ExitTimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stageRoot = Join-Path $repoRoot 'Build\Staging'
if ([string]::IsNullOrWhiteSpace($Stage)) {
    $pointerPath = Join-Path $stageRoot 'Dynamic_CPP.current.json'
    $pointer = Get-Content -LiteralPath $pointerPath -Raw | ConvertFrom-Json
    $Stage = Join-Path $stageRoot ([string]$pointer.releaseDirectory)
}
$stagePath = [IO.Path]::GetFullPath($Stage)
$playerPath = Join-Path $stagePath 'Player.exe'
$pakPath = Join-Path $stagePath 'GameAssets.pak'
foreach ($required in @($playerPath, $pakPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "normal-exit verification input is missing: $required"
    }
}

function Get-StageFileSet {
    param([Parameter(Mandatory)][string]$Root)

    return @(
        Get-ChildItem -LiteralPath $Root -File -Recurse |
            ForEach-Object {
                '{0}|{1}|{2}' -f [IO.Path]::GetRelativePath($Root, $_.FullName),
                    $_.Length, (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            } |
            Sort-Object
    )
}

$beforeFiles = Get-StageFileSet -Root $stagePath
$beforePak = (Get-FileHash -LiteralPath $pakPath -Algorithm SHA256).Hash
$beforePlayer = (Get-FileHash -LiteralPath $playerPath -Algorithm SHA256).Hash

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase `
    ('CreatorEngine-NormalExit-' + [Guid]::NewGuid().ToString('N'))))
$tempPrefix = $tempBase.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
if (-not $testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "normal-exit temp root escaped the system temp directory: $testRoot"
}
New-Item -ItemType Directory -Path $testRoot | Out-Null
$playerRuntimeParent = Join-Path $testRoot 'CreatorEngine\Player'
$siblingRoot = Join-Path $playerRuntimeParent ([string]$PID)
$siblingSentinel = Join-Path $siblingRoot 'sentinel.txt'
$expectedSiblingSentinel = 'CreatorEngine Player sibling ownership sentinel'
New-Item -ItemType Directory -Path $siblingRoot -Force | Out-Null
[IO.File]::WriteAllText($siblingSentinel, $expectedSiblingSentinel,
    [Text.UTF8Encoding]::new($false))
$beforeSiblingHash = (Get-FileHash -LiteralPath $siblingSentinel -Algorithm SHA256).Hash
$beforeRuntimeParentFiles = Get-StageFileSet -Root $playerRuntimeParent

$process = $null
$stdout = ''
$stderr = ''
try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $playerPath
    $startInfo.WorkingDirectory = $stagePath
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Environment['TEMP'] = $testRoot
    $startInfo.Environment['TMP'] = $testRoot
    $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $startInfo.Environment['PATH'] = @(
        $stagePath,
        (Join-Path $windowsRoot 'System32'),
        $windowsRoot
    ) -join [IO.Path]::PathSeparator

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'normal Player failed to start.' }
    $playerPid = $process.Id
    if ($playerPid -eq $PID) { throw 'child Player unexpectedly reused the live test process PID.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    $windowReady = $false
    for ($poll = 0; $poll -lt 240; ++$poll) {
        if ($process.HasExited) { break }
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $windowReady = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $windowReady) {
        throw 'normal Player exited or timed out before its main window became ready.'
    }

    $processRuntimeRoot = Join-Path $playerRuntimeParent ([string]$playerPid)
    $runtimeRootObserved = $false
    for ($poll = 0; $poll -lt 120; ++$poll) {
        if (Test-Path -LiteralPath $processRuntimeRoot -PathType Container) {
            $runtimeRootObserved = $true
            break
        }
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $runtimeRootObserved) {
        throw "normal Player never created its owned runtime root: $processRuntimeRoot"
    }

    Start-Sleep -Seconds $StartupWaitSeconds
    $process.Refresh()
    if (-not $process.HasExited -and -not $process.CloseMainWindow()) {
        throw 'normal Player rejected WM_CLOSE.'
    }
    if (-not $process.WaitForExit($ExitTimeoutSeconds * 1000)) {
        throw "normal Player did not exit within $ExitTimeoutSeconds seconds."
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode

    $runtimeRootRemoved = -not (Test-Path -LiteralPath $processRuntimeRoot)
    $siblingPreserved = (Test-Path -LiteralPath $siblingSentinel -PathType Leaf) -and
        ((Get-FileHash -LiteralPath $siblingSentinel -Algorithm SHA256).Hash -eq
            $beforeSiblingHash) -and
        ([IO.File]::ReadAllText($siblingSentinel) -eq $expectedSiblingSentinel) -and
        (Test-Path -LiteralPath $playerRuntimeParent -PathType Container)
    $afterRuntimeParentFiles = Get-StageFileSet -Root $playerRuntimeParent
    $runtimeParentStable = ($beforeRuntimeParentFiles -join "`n") -eq
        ($afterRuntimeParentFiles -join "`n")
    $afterFiles = Get-StageFileSet -Root $stagePath
    $stageFileSetStable = ($beforeFiles -join "`n") -eq ($afterFiles -join "`n")
    $pakStable = $beforePak -eq
        (Get-FileHash -LiteralPath $pakPath -Algorithm SHA256).Hash
    $playerStable = $beforePlayer -eq
        (Get-FileHash -LiteralPath $playerPath -Algorithm SHA256).Hash

    [pscustomobject]@{
        Pid = $playerPid
        ExitCode = $exitCode
        RuntimeRootObserved = $runtimeRootObserved
        RuntimeRootRemoved = $runtimeRootRemoved
        SiblingPreserved = $siblingPreserved
        RuntimeParentStable = $runtimeParentStable
        StageFileSetStable = $stageFileSetStable
        PakStable = $pakStable
        PlayerStable = $playerStable
        StdoutTail = (($stdout -split "`r?`n" | Select-Object -Last 8) -join ' | ')
        StderrTail = (($stderr -split "`r?`n" | Select-Object -Last 8) -join ' | ')
    } | Format-List

    if ($exitCode -ne 0) { throw "normal Player returned exit code $exitCode." }
    if (-not $runtimeRootObserved) { throw "Player runtime root was never observed: $processRuntimeRoot" }
    if (-not $runtimeRootRemoved) { throw "Player runtime root survived normal exit: $processRuntimeRoot" }
    if (-not $siblingPreserved -or -not $runtimeParentStable) {
        throw "Player cleanup mutated or removed a sibling runtime root: $siblingRoot"
    }
    if (-not $stageFileSetStable -or -not $pakStable -or -not $playerStable) {
        throw 'normal Player mutated its immutable release stage.'
    }
} finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            try { $process.Kill($true); $process.WaitForExit() } catch {}
        }
        $process.Dispose()
    }
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
        if (-not $resolvedTestRoot.StartsWith($tempPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing unsafe normal-exit cleanup path: $resolvedTestRoot"
        }
        $rootItem = Get-Item -LiteralPath $resolvedTestRoot -Force
        if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "refusing cleanup because normal-exit temp root became a reparse point: $resolvedTestRoot"
        }
        $remainingReparse = @(Get-ChildItem -LiteralPath $resolvedTestRoot -Force -Recurse |
            Where-Object {
                ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            })
        if ($remainingReparse.Count -ne 0) {
            throw "refusing recursive cleanup with remaining reparse points: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}

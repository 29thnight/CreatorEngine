[CmdletBinding()]
param(
    [string]$Stage = '',

    [ValidateRange(5, 120)]
    [int]$ExitTimeoutSeconds = 30
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
foreach ($required in @($stagePath, $playerPath, $pakPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "missing-pak verification input is absent: $required"
    }
}

function Assert-NoReparseTree {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Label
    )

    $rootItem = Get-Item -LiteralPath $Root -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label root is a reparse point: $Root"
    }
    $reparseItems = @(Get-ChildItem -LiteralPath $Root -Force -Recurse |
        Where-Object {
            ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        })
    if ($reparseItems.Count -ne 0) {
        throw "$Label contains a reparse point: $($reparseItems[0].FullName)"
    }
}

function Get-StageFileSet {
    param([Parameter(Mandatory)][string]$Root)

    return @(
        Get-ChildItem -LiteralPath $Root -File -Force -Recurse |
            ForEach-Object {
                '{0}|{1}|{2}' -f [IO.Path]::GetRelativePath($Root, $_.FullName),
                    $_.Length, (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            } |
            Sort-Object
    )
}

function Assert-TempChildPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$TempBase,
        [Parameter(Mandatory)][string]$Label
    )

    $absolutePath = [IO.Path]::GetFullPath($Path)
    $absoluteTemp = [IO.Path]::GetFullPath($TempBase)
    $tempPrefix = $absoluteTemp.TrimEnd([char[]]@('\', '/')) +
        [IO.Path]::DirectorySeparatorChar
    if ($absolutePath.Equals($absoluteTemp, [StringComparison]::OrdinalIgnoreCase) -or
        -not $absolutePath.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escaped the system temp directory: $absolutePath"
    }
    return $absolutePath
}

function Copy-StageWithoutPak {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )

    Assert-NoReparseTree -Root $Source -Label 'published Stage'
    New-Item -ItemType Directory -Path $Destination | Out-Null

    $items = @(Get-ChildItem -LiteralPath $Source -Force -Recurse)
    foreach ($directory in @($items | Where-Object { $_.PSIsContainer } |
        Sort-Object { $_.FullName.Length })) {
        $relative = [IO.Path]::GetRelativePath($Source, $directory.FullName)
        New-Item -ItemType Directory -Path (Join-Path $Destination $relative) -Force |
            Out-Null
    }
    foreach ($file in @($items | Where-Object { -not $_.PSIsContainer })) {
        $relative = [IO.Path]::GetRelativePath($Source, $file.FullName)
        if ($relative.Equals('GameAssets.pak', [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $destinationFile = Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationFile) -Force |
            Out-Null
        [IO.File]::Copy($file.FullName, $destinationFile, $false)
    }
}

Assert-NoReparseTree -Root $stagePath -Label 'published Stage'
$beforeFiles = Get-StageFileSet -Root $stagePath
$beforePak = (Get-FileHash -LiteralPath $pakPath -Algorithm SHA256).Hash
$beforePlayer = (Get-FileHash -LiteralPath $playerPath -Algorithm SHA256).Hash

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = Assert-TempChildPath -Path (Join-Path $tempBase `
    ('CreatorEngine-MissingPak-' + [Guid]::NewGuid().ToString('N'))) `
    -TempBase $tempBase -Label 'missing-pak test root'
$clonedStage = Assert-TempChildPath -Path (Join-Path $testRoot 'Stage') `
    -TempBase $tempBase -Label 'missing-pak cloned Stage'
$runtimeTemp = Assert-TempChildPath -Path (Join-Path $testRoot 'RuntimeTemp') `
    -TempBase $tempBase -Label 'missing-pak runtime temp'

$process = $null
try {
    if (Test-Path -LiteralPath $testRoot) {
        throw "missing-pak test root must be unique and absent before setup: $testRoot"
    }
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    Assert-NoReparseTree -Root $testRoot -Label 'missing-pak test root'
    New-Item -ItemType Directory -Path $runtimeTemp | Out-Null
    Assert-NoReparseTree -Root $testRoot -Label 'missing-pak prepared test root'
    Copy-StageWithoutPak -Source $stagePath -Destination $clonedStage
    $clonedPak = Join-Path $clonedStage 'GameAssets.pak'
    if (Test-Path -LiteralPath $clonedPak) {
        throw "missing-pak clone unexpectedly contains GameAssets.pak: $clonedPak"
    }

    $expectedCloneFiles = @($beforeFiles | Where-Object {
        -not $_.StartsWith('GameAssets.pak|', [StringComparison]::OrdinalIgnoreCase)
    })
    $actualCloneFiles = Get-StageFileSet -Root $clonedStage
    if (($expectedCloneFiles -join "`n") -ne ($actualCloneFiles -join "`n")) {
        throw 'missing-pak clone differs from the published Stage beyond GameAssets.pak.'
    }

    $clonedPlayer = Join-Path $clonedStage 'Player.exe'
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $clonedPlayer
    $startInfo.WorkingDirectory = $clonedStage
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Environment['TEMP'] = $runtimeTemp
    $startInfo.Environment['TMP'] = $runtimeTemp
    $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $startInfo.Environment['PATH'] = @(
        $clonedStage,
        (Join-Path $windowsRoot 'System32'),
        $windowsRoot
    ) -join [IO.Path]::PathSeparator

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'missing-pak Player failed to start.' }
    $playerPid = $process.Id
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($ExitTimeoutSeconds * 1000)) {
        try { $process.Kill($true); $process.WaitForExit() } catch {}
        throw "missing-pak Player did not fail closed within $ExitTimeoutSeconds seconds."
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode

    $processRuntimeRoot = Join-Path $runtimeTemp "CreatorEngine\Player\$playerPid"
    $runtimeRootRemoved = -not (Test-Path -LiteralPath $processRuntimeRoot)
    $missingPakReported = ($stdout + "`n" + $stderr) -match
        'Host runtime content preparation failed'

    $afterFiles = Get-StageFileSet -Root $stagePath
    $stageFileSetStable = ($beforeFiles -join "`n") -eq ($afterFiles -join "`n")
    $pakStable = $beforePak -eq
        (Get-FileHash -LiteralPath $pakPath -Algorithm SHA256).Hash
    $playerStable = $beforePlayer -eq
        (Get-FileHash -LiteralPath $playerPath -Algorithm SHA256).Hash

    [pscustomobject]@{
        Pid = $playerPid
        ExitCode = $exitCode
        MissingPakReported = $missingPakReported
        RuntimeRootRemoved = $runtimeRootRemoved
        StageFileSetStable = $stageFileSetStable
        PakStable = $pakStable
        PlayerStable = $playerStable
        StdoutTail = (($stdout -split "`r?`n" | Select-Object -Last 8) -join ' | ')
        StderrTail = (($stderr -split "`r?`n" | Select-Object -Last 8) -join ' | ')
    } | Format-List

    if ($exitCode -ne 2 -or -not $missingPakReported -or -not $runtimeRootRemoved) {
        throw 'Player missing-pak startup did not fail closed with a clean PID runtime root.'
    }
    if (-not $stageFileSetStable -or -not $pakStable -or -not $playerStable) {
        throw 'missing-pak verification mutated the published Stage.'
    }
} finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            try { $process.Kill($true); $process.WaitForExit() } catch {}
        }
        $process.Dispose()
    }
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = Assert-TempChildPath -Path $testRoot -TempBase $tempBase `
            -Label 'missing-pak cleanup root'
        if (-not $resolvedTestRoot.Equals($testRoot,
            [StringComparison]::OrdinalIgnoreCase)) {
            throw "missing-pak cleanup root identity changed: $testRoot -> $resolvedTestRoot"
        }
        Assert-NoReparseTree -Root $resolvedTestRoot -Label 'missing-pak cleanup tree'
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}

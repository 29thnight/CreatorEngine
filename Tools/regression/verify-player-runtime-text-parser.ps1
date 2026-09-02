[CmdletBinding()]
param(
    [string]$Stage = '',

    [ValidateRange(1, 1000)]
    [int]$SmokeFrames = 60,

    [ValidateRange(10, 300)]
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stageRoot = Join-Path $repoRoot 'Build\Staging'
if ([string]::IsNullOrWhiteSpace($Stage)) {
    $pointer = Get-Content -LiteralPath (Join-Path $stageRoot 'Dynamic_CPP.current.json') `
        -Raw | ConvertFrom-Json
    $Stage = Join-Path $stageRoot ([string]$pointer.releaseDirectory)
}
$stagePath = [IO.Path]::GetFullPath($Stage)
$playerPath = Join-Path $stagePath 'Player.exe'
$manifestPath = Join-Path $stagePath 'package-manifest.json'
foreach ($required in @($playerPath, $manifestPath, (Join-Path $stagePath 'GameAssets.pak'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "D6 verification input is missing: $required"
    }
}

# D6 source/API boundary. Utility_Framework still owns ryml until E6/N9, but
# executable runtime modules may not include or name the backend directly.
$runtimeRoots = @(
    'Player',
    'Engine\Core',
    'Engine\Physics',
    'Engine\RenderEngine',
    'Engine\SceneRuntime'
) | ForEach-Object { Join-Path $repoRoot $_ } | Where-Object { Test-Path -LiteralPath $_ }
$runtimeSources = @(Get-ChildItem -LiteralPath $runtimeRoots -Recurse -File -Include *.cpp,*.h)
$rymlReferences = @($runtimeSources | ForEach-Object {
    $sourceText = [IO.File]::ReadAllText($_.FullName)
    $withoutBlockComments = [regex]::Replace($sourceText, '(?s)/\*.*?\*/', '')
    $codeOnly = [regex]::Replace($withoutBlockComments, '(?m)//.*$', '')
    foreach ($match in [regex]::Matches($codeOnly,
        '(?m)^\s*#\s*include\s*[<"](?:ryml/|c4/yml/)|\bryml::')) {
        [pscustomobject]@{ Path = $_.FullName; Symbol = $match.Value.Trim() }
    }
})
if ($rymlReferences.Count -ne 0) {
    $rymlReferences | Format-Table -AutoSize
    throw "Player runtime source에 direct ryml reference $($rymlReferences.Count)건이 있다."
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempPrefix = $tempBase.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase `
    ('CreatorEngine-D6-' + [Guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "D6 temp root escaped system temp: $testRoot"
}
New-Item -ItemType Directory -Path $testRoot | Out-Null

$process = $null
try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $playerPath
    $startInfo.WorkingDirectory = $stagePath
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    [void]$startInfo.ArgumentList.Add('--smoke')
    [void]$startInfo.ArgumentList.Add([string]$SmokeFrames)
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
    if (-not $process.Start()) { throw 'D6 Player failed to start.' }
    $playerPid = $process.Id
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill($true) } catch {}
        $process.WaitForExit()
        throw "D6 Player timed out after $TimeoutSeconds seconds."
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) {
        throw "D6 Player returned $($process.ExitCode): $stderr"
    }

    $markers = @([regex]::Matches($stdout,
        '\[runtime\.text-parser\]\s*calls=(\d+)'))
    $callContexts = @([regex]::Matches($stdout,
        '\[runtime\.text-parser\.call\]\s*source=([^\r\n]+)'))
    if ($markers.Count -ne 1 -or [int]$markers[0].Groups[1].Value -ne 0 -or
        $callContexts.Count -ne 0) {
        throw "D6 text parser contract failed: markers=$($markers.Count), contexts=$($callContexts.Count)"
    }

    $runtimeRoot = Join-Path $testRoot "CreatorEngine\Player\$playerPid\RuntimeContent"
    if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) {
        throw "D6 runtime content root is missing: $runtimeRoot"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $runtimeDocuments = [Collections.Generic.List[IO.FileInfo]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $runtimeRoot 'ProjectSetting') `
        -File -Recurse -Filter '*.asset')) { $runtimeDocuments.Add($file) }
    $extensions = @('.inputmap', '.bt', '.blackboard', '.volume', '.terrain', '.foliage')
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $runtimeRoot 'Assets') `
        -File -Recurse | Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() })) {
        $runtimeDocuments.Add($file)
    }
    $expectedRuntimeDocuments = [int]$manifest.cook.runtimeDocumentCount
    if ($runtimeDocuments.Count -ne $expectedRuntimeDocuments) {
        throw "D6 runtime document count mismatch: $($runtimeDocuments.Count) != $expectedRuntimeDocuments"
    }

    $derivedDocuments = @(
        'Assets\Derived\Scenes',
        'Assets\Derived\Prefabs',
        'Assets\Derived\Materials',
        'Assets\Derived\ShaderMeta'
    ) | ForEach-Object {
        $folder = Join-Path $runtimeRoot $_
        if (Test-Path -LiteralPath $folder -PathType Container) {
            Get-ChildItem -LiteralPath $folder -File -Recurse
        }
    }
    $allCedoDocuments = @($runtimeDocuments) + @($derivedDocuments)
    foreach ($document in $allCedoDocuments) {
        $stream = [IO.File]::OpenRead($document.FullName)
        try {
            $magic = [byte[]]::new(4)
            if ($stream.Read($magic, 0, 4) -ne 4 -or
                [Text.Encoding]::ASCII.GetString($magic) -ne 'CEDO') {
                throw "D6 runtime artifact is not CEDO: $($document.FullName)"
            }
        } finally {
            $stream.Dispose()
        }
    }
    $runtimeJson = @(Get-ChildItem -LiteralPath $runtimeRoot -File -Recurse -Filter '*.json')
    if ($runtimeJson.Count -ne 0) {
        throw "D6 package leaked legacy JSON documents: $($runtimeJson.Count)"
    }

    [pscustomobject]@{
        ExitCode = $process.ExitCode
        TextParserCalls = 0
        DirectRymlReferences = $rymlReferences.Count
        RuntimeDocuments = $runtimeDocuments.Count
        DerivedDocuments = @($derivedDocuments).Count
        LegacyJsonDocuments = $runtimeJson.Count
        UnexpectedStderr = [int](-not [string]::IsNullOrWhiteSpace($stderr))
    } | Format-List
} finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            try { $process.Kill($true); $process.WaitForExit() } catch {}
        }
        $process.Dispose()
    }
    if (Test-Path -LiteralPath $testRoot) {
        $resolved = [IO.Path]::GetFullPath($testRoot)
        if (-not $resolved.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing unsafe D6 cleanup path: $resolved"
        }
        $reparsePoints = @(Get-ChildItem -LiteralPath $resolved -Force -Recurse |
            Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 })
        if ($reparsePoints.Count -ne 0) {
            throw "refusing D6 cleanup with reparse points: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

'전체 통과 — Player runtime text parser 0, direct ryml reference 0, runtime CEDO/legacy JSON 경계 확인'

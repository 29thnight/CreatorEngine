[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$ArgumentList,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [hashtable]$Environment = @{}
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $ArgumentList) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    foreach ($entry in $Environment.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = [string]$entry.Value
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw "Failed to start: $FilePath"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StdOut = $stdoutTask.GetAwaiter().GetResult()
            StdErr = $stderrTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source = Join-Path $PSScriptRoot 'mathematics_contract_probe.cpp'
$vendorInclude = Join-Path $repoRoot 'ThirdParty\Mathematics\include'
$physicsInclude = Join-Path $repoRoot 'Engine\Physics'
$renderEngineInclude = Join-Path $repoRoot 'Engine\RenderEngine'
$sceneRuntimeInclude = Join-Path $repoRoot 'Engine\SceneRuntime'
$provenance = Join-Path $repoRoot 'ThirdParty\Mathematics\PROVENANCE.md'
$targets = Join-Path $repoRoot 'Directory.Build.targets'
$expectedSha = 'd81ca3338ef6f645cc5743625067eece5f1099f0'

foreach ($required in @(
    $source,
    (Join-Path $vendorInclude 'mathematics\mathematics.hpp'),
    (Join-Path $vendorInclude 'mathematics\color.hpp'),
    (Join-Path $vendorInclude 'mathematics\rect.hpp'),
    (Join-Path $vendorInclude 'mathematics\frustum.hpp'),
    (Join-Path $physicsInclude 'PhysicsMathAdapter.h'),
    (Join-Path $renderEngineInclude 'FrameCameraSnapshot.h'),
    (Join-Path $renderEngineInclude 'MathematicsInterop.h'),
    (Join-Path $sceneRuntimeInclude 'TransformStore.h'),
    $provenance,
    $targets
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Mathematics contract input is missing: $required"
    }
}

if ((Get-Content -LiteralPath $provenance -Raw) -notmatch [regex]::Escape($expectedSha)) {
    throw "Mathematics provenance does not pin expected SHA $expectedSha."
}
if ((Get-Content -LiteralPath $targets -Raw) -notmatch
    [regex]::Escape('ThirdParty\Mathematics\include')) {
    throw 'Directory.Build.targets does not expose the vendored Mathematics include path.'
}

$directXIncludeCandidates = @(
    (Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows\include'),
    (Join-Path $repoRoot 'vcpkg_installed\x64-windows\include')
)
$directXInclude = $directXIncludeCandidates |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'DirectXCollision.h') -PathType Leaf } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($directXInclude)) {
    throw 'DirectXCollision.h was not found in the manifest install tree. Restore vcpkg dependencies first.'
}
$directXInclude = [IO.Path]::GetFullPath($directXInclude)
$physXInclude = Join-Path $directXInclude 'physx'
if (-not (Test-Path -LiteralPath (Join-Path $physXInclude 'foundation\PxVec3.h') -PathType Leaf)) {
    throw 'PhysX foundation headers were not found in the manifest install tree.'
}

if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $programFilesX86 = ${env:ProgramFiles(x86)}
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe was not found.'
    }
    $installations = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) {
        throw 'vswhere.exe did not find an installation with the x64 C++ toolchain.'
    }
    $VisualStudioInstallation = $installations[0]
}
$VisualStudioInstallation = [IO.Path]::GetFullPath($VisualStudioInstallation)
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "vcvars64.bat was not found: $vcvars"
}

$contractBuildRoot = Join-Path $repoRoot 'Build\Obj\MathematicsContract'
New-Item -ItemType Directory -Path $contractBuildRoot -Force | Out-Null
$environmentScript = Join-Path $contractBuildRoot 'capture-vc-environment.cmd'
$environmentScriptText = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set
"@
[IO.File]::WriteAllText($environmentScript, $environmentScriptText,
    [Text.UTF8Encoding]::new($false))
$environmentResult = Invoke-CapturedProcess -FilePath $env:ComSpec `
    -ArgumentList @('/d', '/c', $environmentScript) `
    -WorkingDirectory $repoRoot
if ($environmentResult.ExitCode -ne 0) {
    throw "vcvars64.bat failed:`n$($environmentResult.StdErr)"
}
$compilerEnvironment = @{}
foreach ($line in ($environmentResult.StdOut -split "`r?`n")) {
    $separator = $line.IndexOf('=')
    if ($separator -le 0) {
        continue
    }
    $compilerEnvironment[$line.Substring(0, $separator)] =
        $line.Substring($separator + 1)
}
if (-not $compilerEnvironment.ContainsKey('VCToolsInstallDir')) {
    throw 'vcvars64.bat did not report VCToolsInstallDir.'
}
$compiler = Join-Path $compilerEnvironment['VCToolsInstallDir'] `
    'bin\Hostx64\x64\cl.exe'
$compiler = [IO.Path]::GetFullPath($compiler)
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "x64 compiler was not found: $compiler"
}

$configurations = if ($Configuration -eq 'All') {
    @('Debug', 'Release')
} else {
    @($Configuration)
}

foreach ($current in $configurations) {
    $outputDirectory = Join-Path $repoRoot `
        ("Build\Obj\MathematicsContract\x64-{0}" -f $current)
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'mathematics_contract_probe.exe'
    $object = Join-Path $outputDirectory 'mathematics_contract_probe.obj'
    $pdb = Join-Path $outputDirectory 'mathematics_contract_probe.pdb'

    $arguments = @(
        '/nologo',
        '/TP',
        '/std:c++latest',
        '/EHsc',
        '/W4',
        '/WX',
        '/permissive-',
        '/Zc:__cplusplus',
        '/Zc:preprocessor',
        '/utf-8',
        ('/I{0}' -f $vendorInclude),
        ('/I{0}' -f $physicsInclude),
        ('/I{0}' -f $renderEngineInclude),
        ('/I{0}' -f $sceneRuntimeInclude),
        ('/external:I{0}' -f $directXInclude),
        ('/external:I{0}' -f $physXInclude),
        '/external:W0',
        ('/Fo{0}' -f $object),
        ('/Fd{0}' -f $pdb),
        ('/Fe{0}' -f $executable)
    )
    if ($current -eq 'Debug') {
        $arguments += @('/Od', '/RTC1', '/MDd', '/Zi', '/D_DEBUG')
    } else {
        $arguments += @('/O2', '/Ob2', '/MD', '/DNDEBUG')
    }
    $arguments += $source

    Write-Host "[MATHEMATICS CONTRACT] compiling $current with $compiler"
    $compileResult = Invoke-CapturedProcess -FilePath $compiler `
        -ArgumentList $arguments -WorkingDirectory $repoRoot `
        -Environment $compilerEnvironment
    if (-not [string]::IsNullOrWhiteSpace($compileResult.StdOut)) {
        Write-Host $compileResult.StdOut.TrimEnd()
    }
    if (-not [string]::IsNullOrWhiteSpace($compileResult.StdErr)) {
        Write-Host $compileResult.StdErr.TrimEnd()
    }
    if ($compileResult.ExitCode -ne 0) {
        throw "Mathematics $current contract compile failed with exit code $($compileResult.ExitCode)."
    }

    $runResult = Invoke-CapturedProcess -FilePath $executable `
        -ArgumentList @() -WorkingDirectory $outputDirectory `
        -Environment $compilerEnvironment
    if (-not [string]::IsNullOrWhiteSpace($runResult.StdOut)) {
        Write-Host $runResult.StdOut.TrimEnd()
    }
    if (-not [string]::IsNullOrWhiteSpace($runResult.StdErr)) {
        Write-Host $runResult.StdErr.TrimEnd()
    }
    if ($runResult.ExitCode -ne 0) {
        throw "Mathematics $current contract run failed with exit code $($runResult.ExitCode)."
    }
}

Write-Host '[MATHEMATICS CONTRACT] Debug/Release verification passed.' -ForegroundColor Green

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
$utilityFrameworkInclude = Join-Path $repoRoot 'Engine\Utility_Framework'
$tweenManagerHeader = Join-Path $sceneRuntimeInclude 'TweenManager.h'
$mathematicsIntersectHeader = Join-Path $repoRoot `
    'Engine\Utility_Framework\Mathematics.Intersect.h'
$provenance = Join-Path $repoRoot 'ThirdParty\Mathematics\PROVENANCE.md'
$props = Join-Path $repoRoot 'Directory.Build.props'
$targets = Join-Path $repoRoot 'Directory.Build.targets'
$manifest = Join-Path $repoRoot 'vcpkg.json'
$buildScript = Join-Path $repoRoot 'Tools\build.ps1'
$expectedSha = '1f43e080f180db1afbf6e18cb3849b758858a496'

foreach ($required in @(
    $source,
    (Join-Path $vendorInclude 'mathematics\mathematics.hpp'),
    (Join-Path $vendorInclude 'mathematics\color.hpp'),
    (Join-Path $vendorInclude 'mathematics\easing.hpp'),
    (Join-Path $vendorInclude 'mathematics\rect.hpp'),
    (Join-Path $vendorInclude 'mathematics\frustum.hpp'),
    (Join-Path $vendorInclude 'mathematics\tween.hpp'),
    (Join-Path $vendorInclude 'mathematics\tween_views.hpp'),
    (Join-Path $physicsInclude 'PhysicsMathAdapter.h'),
    (Join-Path $renderEngineInclude 'FrameCameraSnapshot.h'),
    (Join-Path $sceneRuntimeInclude 'TransformStore.h'),
    $tweenManagerHeader,
    $mathematicsIntersectHeader,
    $provenance,
    $props,
    $manifest,
    $targets,
    $buildScript
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Mathematics contract input is missing: $required"
    }
}

if ((Get-Content -LiteralPath $provenance -Raw) -notmatch [regex]::Escape($expectedSha)) {
    throw "Mathematics provenance does not pin expected SHA $expectedSha."
}
$tweenManagerText = Get-Content -LiteralPath $tweenManagerHeader -Raw
foreach ($requiredContract in @(
    'math::tween<Value>',
    'EntityHandle',
    'generation',
    'SweepPool',
    'DispatchPool'
)) {
    if ($tweenManagerText -notmatch [regex]::Escape($requiredContract)) {
        throw "TweenManager contract marker is missing: $requiredContract"
    }
}
foreach ($forbiddenOwnership in @(
    'std::function',
    'std::shared_ptr',
    'std::unique_ptr',
    'Entity*',
    'Component*',
    'Scene*'
)) {
    if ($tweenManagerText -match [regex]::Escape($forbiddenOwnership)) {
        throw "TweenManager stores or exposes a forbidden ownership surface: $forbiddenOwnership"
    }
}
if ((Get-Content -LiteralPath $targets -Raw) -notmatch
    [regex]::Escape('ThirdParty\Mathematics\include')) {
    throw 'Directory.Build.targets does not expose the vendored Mathematics include path.'
}

# The repository-owned native surface, including regression tools, must not
# regain the retired DirectX math contracts.
$nativeExtensions = @('.h', '.hpp', '.cpp', '.inl')
$trackedNative = @(& git -C $repoRoot ls-files --cached --others --exclude-standard) |
    Where-Object {
        $nativeExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() -and
        $_ -notlike 'ThirdParty/*'
    }
if ($LASTEXITCODE -ne 0) {
    throw 'git ls-files failed while checking the repository math boundary.'
}
$directXBoundsOffenders = @(foreach ($relativePath in $trackedNative) {
    $absolutePath = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
        continue
    }
    if (Select-String -LiteralPath $absolutePath -Pattern `
            '\bBounding(?:Box|Sphere|Frustum|OrientedBox)\b' -Quiet) {
        $relativePath
    }
})
if ($directXBoundsOffenders.Count -ne 0) {
    throw "Repository DirectX bounding-type references remain:`n$($directXBoundsOffenders -join "`n")"
}

$directXNamespaceUsingOffenders = @(foreach ($relativePath in $trackedNative) {
    if ($relativePath -eq 'Engine/RenderEngine/Texture.cpp') {
        # DirectXTex's codec API is outside this math migration. Its one local
        # namespace import is tolerated, while the token gates above and below
        # still reject every math type/function in the same file.
        continue
    }
    $absolutePath = Join-Path $repoRoot $relativePath
    if ((Test-Path -LiteralPath $absolutePath -PathType Leaf) -and
        (Select-String -LiteralPath $absolutePath -Pattern `
            '(?m)^\s*using\s+namespace\s+DirectX\s*;' -Quiet)) {
        $relativePath
    }
})
if ($directXNamespaceUsingOffenders.Count -ne 0) {
    throw "Repository DirectX namespace imports remain outside the DirectXTex boundary:`n$($directXNamespaceUsingOffenders -join "`n")"
}

$retiredInteropHeader = Join-Path $renderEngineInclude 'MathematicsInterop.h'
if (Test-Path -LiteralPath $retiredInteropHeader) {
    throw 'Retired MathematicsInterop.h was reintroduced.'
}

$interopOffenders = @(foreach ($relativePath in $trackedNative) {
    $absolutePath = Join-Path $repoRoot $relativePath
    if ((Test-Path -LiteralPath $absolutePath -PathType Leaf) -and
        (Select-String -LiteralPath $absolutePath -SimpleMatch `
            'MathematicsInterop' -Quiet)) {
        $relativePath
    }
})
if ($interopOffenders.Count -ne 0) {
    throw "Retired MathematicsInterop references remain:`n$($interopOffenders -join "`n")"
}

$simpleMathOffenders = @(foreach ($relativePath in $trackedNative) {
    $absolutePath = Join-Path $repoRoot $relativePath
    if ((Test-Path -LiteralPath $absolutePath -PathType Leaf) -and
        (Select-String -LiteralPath $absolutePath -Pattern `
            '(?:DirectX::)?SimpleMath::|Mathf::(?:Matrix|Vector2|Vector3|Vector4|Quaternion)|SimpleMath\.h' -Quiet)) {
        $relativePath
    }
})
if ($simpleMathOffenders.Count -ne 0) {
    throw "Repository SimpleMath references remain:`n$($simpleMathOffenders -join "`n")"
}

$retiredMathfHeader = Join-Path $utilityFrameworkInclude 'Core.Mathf.h'
if (Test-Path -LiteralPath $retiredMathfHeader) {
    throw 'Retired Core.Mathf.h was reintroduced.'
}

$rawDirectXMathPattern =
    '\b(?:XMVECTOR(?:F32|I32|U32)?|XMMATRIX|' +
    'XMFLOAT(?:2|3|4|3X3|4X3|4X4)A?|XMINT(?:2|3|4)|XMUINT(?:2|3|4)|' +
    'XMVector\w*|XMMatrix\w*|XMQuaternion\w*|XMPlane\w*|XMColor\w*|' +
    'XMScalar\w*|XMConvert\w*|XMLoad\w*|XMStore\w*|XM_[A-Za-z0-9_]+)\b|' +
    '(?:DirectX::)?Colors::\w+|Mathf::\w+'
$directXMathIncludePattern =
    '#\s*include\s*[<"](?:DirectXMath|DirectXCollision|DirectXColors|' +
    'directxtk12/SimpleMath)\.h[>"]'
$rawDirectXMathOffenders = @(foreach ($relativePath in $trackedNative) {
    $absolutePath = Join-Path $repoRoot $relativePath
    if ((Test-Path -LiteralPath $absolutePath -PathType Leaf) -and
        (Select-String -LiteralPath $absolutePath -Pattern `
            $rawDirectXMathPattern, $directXMathIncludePattern -Quiet)) {
        $relativePath
    }
})
if ($rawDirectXMathOffenders.Count -ne 0) {
    throw "Repository raw DirectXMath surface remains:`n$($rawDirectXMathOffenders -join "`n")"
}

$manifestText = Get-Content -LiteralPath $manifest -Raw
if ($manifestText -match '"(?:directxmath|directxtk12)"') {
    throw 'Retired directxmath/directxtk12 dependency was reintroduced in vcpkg.json.'
}
if ((Get-Content -LiteralPath $props -Raw) -match 'DIRECTX_TOOLKIT') {
    throw 'Retired DirectXTK import configuration was reintroduced.'
}
if ((Get-Content -LiteralPath $buildScript -Raw) -match '(?i)DirectXTK12\.dll') {
    throw 'Retired DirectXTK12 runtime copy entry was reintroduced in Tools/build.ps1.'
}

$manifestIncludeCandidates = @(
    (Join-Path $repoRoot 'vcpkg_installed\x64-windows\x64-windows\include'),
    (Join-Path $repoRoot 'vcpkg_installed\x64-windows\include')
)
$manifestInclude = $manifestIncludeCandidates |
    Where-Object {
        Test-Path -LiteralPath (Join-Path $_ 'physx\foundation\PxVec3.h') -PathType Leaf
    } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($manifestInclude)) {
    throw 'PhysX headers were not found in the manifest install tree. Restore vcpkg dependencies first.'
}
$manifestInclude = [IO.Path]::GetFullPath($manifestInclude)
$physXInclude = Join-Path $manifestInclude 'physx'
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
        ('/external:I{0}' -f $manifestInclude),
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

Write-Host (('[MATHEMATICS CONTRACT] {0} verification passed.' -f
    ($configurations -join '/'))) -ForegroundColor Green

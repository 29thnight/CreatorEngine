[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$header = Join-Path $repoRoot 'Engine\Utility_Framework\AuthoringBase64.h'
$probe = Join-Path $PSScriptRoot 'authoring_base64_contract_probe.cpp'
$dataSystem = Join-Path $repoRoot 'Engine\RenderEngine\DataSystem.cpp'

foreach ($required in @($header, $probe, $dataSystem)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Authoring base64 contract input is missing: $required"
    }
}

if (Select-String -LiteralPath $dataSystem -Pattern 'YAML::(?:Encode|Decode)Base64' -Quiet) {
    throw 'DataSystem still delegates base64 payloads to yaml-cpp.'
}
if (-not (Select-String -LiteralPath $dataSystem `
    -Pattern 'Authoring::Base64::Encode' -Quiet) -or
    -not (Select-String -LiteralPath $dataSystem `
    -Pattern 'Authoring::Base64::Decode' -Quiet)) {
    throw 'DataSystem is not wired to the authoring base64 codec.'
}

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

$configurations = if ($Configuration -eq 'All') {
    @('Debug', 'Release')
} else {
    @($Configuration)
}

foreach ($current in $configurations) {
    $outputDirectory = Join-Path $repoRoot `
        ("Build\Obj\AuthoringBase64Contract\x64-{0}" -f $current)
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'authoring_base64_contract_probe.exe'
    $utilityInclude = Join-Path $repoRoot 'Engine\Utility_Framework'

    $configurationArguments = if ($current -eq 'Debug') {
        '/MDd /Od /RTC1 /Zi /D_DEBUG'
    } else {
        '/MD /O2 /Ob2 /DNDEBUG'
    }
    $command = 'call "' + $vcvars + '" >nul && cl.exe ' +
        '/nologo /EHsc /std:c++latest /permissive- /Zc:__cplusplus ' +
        '/utf-8 /W4 /WX ' + $configurationArguments + ' ' +
        '/I"' + $utilityInclude + '" /Fe:"' + $executable + '" "' +
        $probe + '" && "' + $executable + '"'

    Write-Host "[AUTHORING BASE64] compiling/running $current"
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Authoring base64 $current contract failed with exit code $LASTEXITCODE."
    }
}

Write-Host (('[AUTHORING BASE64] {0} verification passed.' -f
    ($configurations -join '/'))) -ForegroundColor Green

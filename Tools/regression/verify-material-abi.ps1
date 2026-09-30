param(
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installations = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) {
        throw 'MSVC x64 installation was not found'
    }
    $VisualStudioInstallation = $installations[0]
}

$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
$probe = Join-Path $PSScriptRoot 'material_abi_probe.cpp'
$output = Join-Path $repo 'Build\Obj\MaterialAbiProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'material_abi_probe.exe'
$object = Join-Path $output 'material_abi_probe.obj'
$slangInclude = Join-Path $repo 'ThirdParty\Slang\include'
$renderInclude = Join-Path $repo 'Engine\RenderEngine\RHI'
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest ' +
    '/permissive- /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX ' +
    '/I"' + $slangInclude + '" /I"' + $renderInclude + '" ' +
    '/Fo:"' + $object + '" /Fe:"' + $exe + '" "' + $probe + '"'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "Material ABI probe compile failed: exit $LASTEXITCODE"
}

# The probe dynamically loads only ThirdParty/Slang/bin. Installed SDK/PATH
# compilers cannot silently substitute for the product's pinned compiler.
$result = @(& $exe $repo 2>&1)
$result | Set-Content -LiteralPath (Join-Path $output 'compile.log') -Encoding utf8
if ($LASTEXITCODE -ne 0 -or
    @($result | Where-Object { $_ -eq 'MATERIAL_ABI_COMPILE_OK compiled=70 rejected=58' }).Count -ne 1) {
    throw "Material ABI compile gate failed: $($result -join "`n")"
}
$result

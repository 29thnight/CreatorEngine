param(
    [string]$Source = 'C:\Program Files\Blender Foundation\Blender 5.1\5.1\datafiles\studiolights\world\forest.exr',
    [string]$Output = '',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe',
    [switch]$SkipBuild
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Source = (Resolve-Path -LiteralPath $Source).Path
if (!$Output) { $Output = Join-Path $repo 'Resources/Environment/forest.ceibl' }
$Output = [IO.Path]::GetFullPath($Output)
if (!$SkipBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
    & $msbuild (Join-Path $PSScriptRoot 'EnvironmentCooker.vcxproj') /nologo /m "/p:Configuration=$Configuration" /p:Platform=x64 "/p:SolutionDir=$repo\" /v:minimal
    if ($LASTEXITCODE) { throw 'Environment cooker build failed' }
}
$exe = Join-Path $repo "Bin/x64-$Configuration/Tools/EnvironmentCooker/EnvironmentCooker.exe"
function Write-CookMetadata {
    # Refresh the human-readable record on a cook hit as well as a miss. The
    # runtime validates the binary header/checksum and does not read this JSON.
    $header = New-Object byte[] 112
    $stream = [IO.File]::OpenRead($Output)
    try {
        if ($stream.Read($header,0,$header.Length) -ne $header.Length -or
            [Text.Encoding]::ASCII.GetString($header,0,8) -ne 'CEIBL001') { throw 'Invalid environment cook header' }
    } finally { $stream.Dispose() }
    $cube = [BitConverter]::ToUInt32($header,40)
    $brdf = [BitConverter]::ToUInt32($header,44)
    $sourceHash = [BitConverter]::ToString($header,48,32).Replace('-','').ToLowerInvariant()
    $recipeHash = [BitConverter]::ToString($header,80,32).Replace('-','').ToLowerInvariant()
    $isForest = $sourceHash -eq 'bdf2298244affa0f85509380fd130ac6d4dfaa3c856df065998f7f4c1a93dc0d'
    $mips = 1; $mipSize = $cube
    while ($mipSize -gt 1 -and $mips -lt 7) { $mipSize = $mipSize -shr 1; ++$mips }
    $record = [ordered]@{
        schemaVersion=1; artifact=[IO.Path]::GetFileName($Output)
        artifactSha256=(Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant()
        source=$(if($isForest){'Blender 5.1.1 release/datafiles/studiolights/world/forest.exr'}else{[IO.Path]::GetFileName($Source)})
        sourceSha256=$sourceHash; recipeSha256=$recipeHash
        cubeSize=$cube; irradianceSize=[Math]::Min($cube,64); environmentMipLevels=$mips
        prefilterMipLevels=6; brdfSize=$brdf; format='RGBA16Float'; colorSpace='linear Rec.709'
        irradianceConvention='E/pi'; bytes=(Get-Item -LiteralPath $Output).Length
        producer='Tools/AssetCooker/cook-environment.ps1'
    }
    if ($isForest) { $record['license']='CC0; Greg Zaal / Poly Haven, ninomaru_teien' }
    [IO.File]::WriteAllText([IO.Path]::ChangeExtension($Output,'.cook.json'),
        ($record|ConvertTo-Json)+"`n",[Text.UTF8Encoding]::new($false))
}
$oldPath = $env:PATH
try {
    # The standalone authoring tool uses the configured engine/vcpkg DLLs.
    $runtime = @("Bin/x64-$Configuration/Runtime/Common", "Bin/x64-$Configuration/Runtime/Editor",
        "Bin/x64-$Configuration", 'vcpkg_installed/x64-windows/x64-windows/bin', 'vcpkg_installed/x64-windows/bin') |
        ForEach-Object { Join-Path $repo $_ }
    $env:PATH = ($runtime -join ';') + ';' + $oldPath
    & $exe $repo $Source $Output --check-cache
    if ($LASTEXITCODE -eq 0) { Write-CookMetadata; return }
    if ($LASTEXITCODE -ne 3) { throw 'Environment cache validation failed' }
    if ([IO.Path]::GetExtension($Source) -ieq '.exr') {
        if (!(Test-Path -LiteralPath $Blender)) { throw 'Blender is required only to decode an EXR on a cook miss' }
        $decoded = Join-Path $repo ('Build/Obj/EnvironmentCooker/decode-' + [guid]::NewGuid().ToString('N') + '.rgba32f')
        try {
            & $Blender --background --factory-startup --python (Join-Path $repo 'Tools/blender/decode-environment-exr.py') -- $Source $decoded
            if ($LASTEXITCODE) { throw 'EXR authoring decode failed' }
            & $exe $repo $Source $Output $decoded
            if ($LASTEXITCODE) { throw 'Environment cook failed' }
        } finally {
            if (Test-Path -LiteralPath $decoded) { Remove-Item -LiteralPath $decoded }
        }
    } else {
        & $exe $repo $Source $Output
        if ($LASTEXITCODE) { throw 'HDR environment cook failed' }
    }
    Write-CookMetadata
} finally { $env:PATH = $oldPath }

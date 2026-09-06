# CreatorEngine game-package orchestrator (BuildPipelinePlan B2).
#
# Clean-checkout package-input gate:
#   pwsh Tools/build.ps1 -Config Release -InputMode Tracked -BuildNative
#
# Normal project package:
#   pwsh Tools/build.ps1 -Config Release -InputMode Project
[CmdletBinding()]
param(
    [ValidateSet('Game')]
    [string]$Target = 'Game',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [string]$Project = '',

    [ValidateSet('Project', 'Workspace', 'Tracked')]
    [string]$InputMode = 'Project',

    [ValidateScript({
        [string]::IsNullOrWhiteSpace($_) -or
        $_.Equals('dx12', [StringComparison]::OrdinalIgnoreCase) -or
        $_.Equals('vulkan', [StringComparison]::OrdinalIgnoreCase)
    })]
    [string]$RenderBackend = '',

    [string]$StartupScene = '',

    [switch]$BuildNative,
    [switch]$SkipVerify,

    # PHASE 14.5 LC8 — Player 를 Shipping 으로 짓는다(§11.2 · §12.1).
    #
    # ★ 이 스위치가 하는 일은 링크에서 `CommandService` 를 빼는 것이다. 켜면
    #   Player 바이너리에 소켓 import 도 서비스 심볼도 0 이 되고,
    #   `--command-service` 를 줘도 서비스가 없다. `verify-player-shipping-isolation.ps1`
    #   이 두 바이너리를 나란히 놓고 그 차이를 판정한다.
    #
    # ★★ 스모크는 **두 구성 모두** 돈다. Shipping 만 검증하면 개발자가 매일 쓰는
    #   Development 가 검증되지 않고, Development 만 검증하면 실제로 배포되는 것이
    #   검증되지 않는다.
    [switch]$Shipping,

    [ValidateRange(1, 1000000)]
    [int]$SmokeFrames = 120,

    [ValidateRange(10, 3600)]
    [int]$SmokeTimeoutSec = 180,

    [string]$StageRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($Project)) {
    $Project = Join-Path $repoRoot 'Dynamic_CPP'
}
if ([string]::IsNullOrWhiteSpace($StageRoot)) {
    $StageRoot = Join-Path $repoRoot 'Build\Staging'
}

$projectRootArgument = $Project
$stageRootArgument = $StageRoot
$projectRoot = [IO.Path]::GetFullPath($projectRootArgument)
$stageRootPath = [IO.Path]::GetFullPath($stageRootArgument)
$templatePath = Join-Path $repoRoot 'Tools\packaging\templates\EngineSettings.runtime.yml'
$canonicalScenePath = Join-Path $projectRoot 'Assets\Scenes\FT_Primitives.creator'
$solutionPath = Join-Path $repoRoot 'CreatorEngine.sln'
# PHASE 14.5 LC8 — Development/Shipping 은 솔루션 구성이 아니라 스위치다
# (Directory.Build.props 의 EngineShipping). 산출물 키가 갈리므로 이 뿌리도 갈린다.
#
# ★ **Player 만 Shipping 으로 짓는다.** 에디터에는 Shipping 개념이 없고(§12.1),
#   패키지에 들어가는 것은 Player 뿐이다. 도구(AssetPacker·AssetCooker)도
#   Development 그대로다 — 패키지에 실리지 않고, 그것들까지 두 벌로 만들면
#   빌드 시간만 두 배가 된다.
$engineConfigKey = if ($Shipping) { "$Config-Shipping" } else { $Config }
$engineBinaryRoot = Join-Path $repoRoot "Bin\x64-$engineConfigKey"
$toolBinaryRoot = Join-Path $repoRoot "Bin\x64-$Config"
$playerOutput = Join-Path $engineBinaryRoot 'Player'
$packerOutput = Join-Path $toolBinaryRoot 'Tools\AssetPacker'
$cookerOutput = Join-Path $toolBinaryRoot 'Tools\AssetCooker'
$managedOutput = Join-Path $toolBinaryRoot 'Managed'

if (-not ('CreatorEnginePathIdentity' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public static class CreatorEnginePathIdentity
{
    private const uint FileReadAttributes = 0x80;
    private const uint FileShareRead = 0x1;
    private const uint FileShareWrite = 0x2;
    private const uint FileShareDelete = 0x4;
    private const uint OpenExisting = 3;
    private const uint FileFlagBackupSemantics = 0x02000000;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string fileName, uint desiredAccess, uint shareMode, IntPtr securityAttributes,
        uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandleW(
        SafeFileHandle file, StringBuilder path, uint pathLength, uint flags);

    public static string ResolveExistingDirectory(string path)
    {
        using (SafeFileHandle handle = CreateFileW(path, FileReadAttributes,
            FileShareRead | FileShareWrite | FileShareDelete, IntPtr.Zero,
            OpenExisting, FileFlagBackupSemantics, IntPtr.Zero))
        {
            if (handle.IsInvalid)
                throw new IOException("CreateFileW failed for directory path.",
                    new Win32Exception(Marshal.GetLastWin32Error()));

            StringBuilder buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle, buffer,
                (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity)
                throw new IOException("GetFinalPathNameByHandleW failed for directory path.",
                    new Win32Exception(Marshal.GetLastWin32Error()));

            string result = buffer.ToString();
            if (result.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase))
                result = @"\\" + result.Substring(8);
            else if (result.StartsWith(@"\\?\", StringComparison.OrdinalIgnoreCase))
                result = result.Substring(4);
            return Path.GetFullPath(result);
        }
    }
}
'@
}

function Assert-NormalWin32PathSyntax {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label
    )

    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Label path is empty." }
    foreach ($prefix in @('\\?\', '\\.\', '\??\')) {
        if ($Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Label uses a device/extended path namespace: $Path"
        }
    }

    $fullPath = [IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith('\\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must use a local DOS drive path: $fullPath"
    }
    if ($fullPath.Length -gt 2 -and $fullPath.Substring(2).Contains(':')) {
        throw "$Label contains an alternate-stream/device separator: $fullPath"
    }
    $root = [IO.Path]::GetPathRoot($fullPath)
    $relative = [IO.Path]::GetRelativePath($root, $fullPath)
    foreach ($component in $relative.Split(
        [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar),
        [StringSplitOptions]::RemoveEmptyEntries)) {
        if ($component.EndsWith(' ') -or $component.EndsWith('.')) {
            throw "$Label contains a component with a trailing space/dot: $fullPath"
        }
    }
}

function Resolve-CanonicalLocalDirectoryPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label,
        [switch]$MustExist
    )

    Assert-NormalWin32PathSyntax -Path $Path -Label $Label
    $fullPath = [IO.Path]::GetFullPath($Path)
    $probe = $fullPath
    $suffix = [Collections.Generic.List[string]]::new()
    while (-not (Test-Path -LiteralPath $probe)) {
        $leaf = [IO.Path]::GetFileName($probe)
        $parent = [IO.Path]::GetDirectoryName($probe)
        if ([string]::IsNullOrWhiteSpace($leaf) -or
            [string]::IsNullOrWhiteSpace($parent) -or
            $parent.Equals($probe, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Label has no existing directory ancestor: $fullPath"
        }
        $suffix.Insert(0, $leaf)
        $probe = $parent
    }
    if (-not (Test-Path -LiteralPath $probe -PathType Container)) {
        throw "$Label ancestor is not a directory: $probe"
    }
    if ($MustExist -and $suffix.Count -ne 0) {
        throw "$Label directory is missing: $fullPath"
    }

    try {
        $canonical = [CreatorEnginePathIdentity]::ResolveExistingDirectory($probe)
    } catch {
        throw "$Label final-path resolution failed for '$probe': $($_.Exception.Message)"
    }
    foreach ($component in $suffix) { $canonical = Join-Path $canonical $component }
    Assert-NormalWin32PathSyntax -Path $canonical -Label "$Label final path"
    return [IO.Path]::GetFullPath($canonical)
}

function Assert-ScopedPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Label
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root)
    $rootPrefix = $fullRoot.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label path escapes its root: $fullPath (root: $fullRoot)"
    }
    if ($fullPath.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label path must not equal its root: $fullPath"
    }
}

function Assert-DisjointTrees {
    param(
        [Parameter(Mandatory)][string]$Left,
        [Parameter(Mandatory)][string]$Right,
        [Parameter(Mandatory)][string]$Label
    )

    $leftFull = [IO.Path]::GetFullPath($Left).TrimEnd([char[]]@('\', '/'))
    $rightFull = [IO.Path]::GetFullPath($Right).TrimEnd([char[]]@('\', '/'))
    $leftPrefix = $leftFull + [IO.Path]::DirectorySeparatorChar
    $rightPrefix = $rightFull + [IO.Path]::DirectorySeparatorChar
    if ($leftFull.Equals($rightFull, [StringComparison]::OrdinalIgnoreCase) -or
        $leftFull.StartsWith($rightPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $rightFull.StartsWith($leftPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label trees overlap: $leftFull <-> $rightFull"
    }
}

function Assert-NoReparseAncestors {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw "$Label has no filesystem root: $fullPath"
    }

    $current = $root
    $relative = [IO.Path]::GetRelativePath($root, $fullPath)
    foreach ($component in $relative.Split(
        [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar),
        [StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $component
        if (-not (Test-Path -LiteralPath $current)) { break }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label crosses a symbolic/reparse point: $current"
        }
    }
}

function Assert-NoReparseTree {
    param([Parameter(Mandatory)][string]$Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push([IO.Path]::GetFullPath($Root))
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        $directoryItem = Get-Item -LiteralPath $directory -Force
        if (($directoryItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "symbolic/reparse package input is not allowed: $directory"
        }
        foreach ($child in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "symbolic/reparse package input is not allowed: $($child.FullName)"
            }
            if ($child.PSIsContainer) { $pending.Push($child.FullName) }
        }
    }
}

function Remove-ScopedTree {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Label
    )

    Assert-ScopedPath -Path $Path -Root $Root -Label $Label
    if (Test-Path -LiteralPath $Path) {
        Assert-NoReparseAncestors -Path $Path -Label $Label
        Assert-NoReparseTree -Root $Path
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Remove-ScopedFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Label
    )

    Assert-ScopedPath -Path $Path -Root $Root -Label $Label
    if (Test-Path -LiteralPath $Path) {
        Assert-NoReparseAncestors -Path $Path -Label $Label
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "$Label is not a file: $Path"
        }
        $item = Get-Item -LiteralPath $Path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label is a symbolic/reparse file: $Path"
        }
        Remove-Item -LiteralPath $Path -Force
    }
}

function Assert-DistributionStageClosure {
    param(
        [Parameter(Mandatory)][string]$Stage,
        [Parameter(Mandatory)][string[]]$ExpectedRootFiles
    )

    $allowedFiles = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $ExpectedRootFiles) {
        [void]$allowedFiles.Add($name)
        $expectedPath = Join-Path $Stage $name
        if (-not (Test-Path -LiteralPath $expectedPath -PathType Leaf)) {
            throw "required distribution file is missing: $name"
        }
    }

    foreach ($item in @(Get-ChildItem -LiteralPath $Stage -Force)) {
        if ($item.PSIsContainer) {
            if (-not $item.Name.Equals('Managed', [StringComparison]::OrdinalIgnoreCase)) {
                throw "unexpected distribution directory: $($item.Name)"
            }
            continue
        }
        if (-not $allowedFiles.Contains($item.Name)) {
            throw "unexpected distribution file: $($item.Name)"
        }
    }
}

function Copy-OneFile {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "package source file is missing: $Source"
    }
    $sourceItem = Get-Item -LiteralPath $Source -Force
    if (($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "symbolic/reparse package source is not allowed: $Source"
    }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-RelativeFile {
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][string]$DestinationRoot,
        [Parameter(Mandatory)][string]$RelativePath
    )

    $relativeNative = $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    Copy-OneFile -Source (Join-Path $SourceRoot $relativeNative) `
        -Destination (Join-Path $DestinationRoot $relativeNative)
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$Label
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Find-MSBuild {
    $fromPath = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue
    if ($null -ne $fromPath) {
        $pathDirectory = Split-Path -Parent $fromPath.Source
        if ((Split-Path -Leaf $pathDirectory) -ieq 'amd64') {
            return $fromPath.Source
        }

        # setup-msbuild와 Developer Command Prompt의 PATH는 x86 Bin을 가리킬 수
        # 있다. 같은 VS 설치의 x64 host를 우선해 Release LTCG .ipdb/.iobj가
        # 호출 경로에 따라 서로 다른 host 상태로 교대 갱신되지 않게 한다.
        $amd64Sibling = Join-Path $pathDirectory 'amd64\MSBuild.exe'
        if (Test-Path -LiteralPath $amd64Sibling -PathType Leaf) {
            return $amd64Sibling
        }
    }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $found = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
                -find 'MSBuild\**\Bin\amd64\MSBuild.exe')
            if ($LASTEXITCODE -eq 0 -and $found.Count -gt 0) { return $found[0] }
        }
    }
    if ($null -ne $fromPath) { return $fromPath.Source }
    throw 'MSBuild.exe를 찾지 못했다. Visual Studio C++ workload를 확인하라.'
}

function Find-SlangRuntime {
    $candidate = Join-Path $repoRoot 'ThirdParty\Slang\bin\slang-compiler.dll'
    foreach ($name in @('slang-compiler.dll', 'dxcompiler.dll', 'dxil.dll')) {
        $path = Join-Path (Split-Path -Parent $candidate) $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "고정 ThirdParty/Slang 번들의 $name 을 찾지 못했다."
        }
    }
    return [IO.Path]::GetFullPath($candidate)
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Sort-EntriesOrdinal {
    param([Parameter(Mandatory)][object[]]$Entries)

    $ordered = [Collections.Generic.List[object]]::new()
    foreach ($entry in $Entries) { [void]$ordered.Add($entry) }
    $ordered.Sort([Comparison[object]]{
        param($left, $right)
        return [StringComparer]::Ordinal.Compare([string]$left.path, [string]$right.path)
    })
    return $ordered.ToArray()
}

function Get-RuntimeDllNames {
    if ($Config -eq 'Debug') {
        return @(
            'DirectXTex.dll',
            'fmodL.dll', 'fmtd.dll', 'kubazip.dll', 'meshoptimizer.dll',
            'minizipd.dll', 'nethost.dll', 'PhysX_64.dll', 'PhysXCommon_64.dll',
            'PhysXDevice64.dll',
            'PhysXCooking_64.dll', 'PhysXFoundation_64.dll', 'poly2tri.dll',
            'pugixml.dll', 'vulkan-1.dll', 'zd.dll'
        )
    }
    return @(
        'DirectXTex.dll',
        'fmod.dll', 'fmt.dll', 'kubazip.dll', 'meshoptimizer.dll',
        'minizip.dll', 'nethost.dll', 'PhysX_64.dll', 'PhysXCommon_64.dll',
        'PhysXDevice64.dll',
        'PhysXCooking_64.dll', 'PhysXFoundation_64.dll', 'poly2tri.dll',
        'pugixml.dll', 'vulkan-1.dll', 'z.dll'
    )
}

function Find-MatchingPhysXGpuRuntime {
    $outputGpu = Join-Path $playerOutput 'PhysXGpu_64.dll'
    if (Test-Path -LiteralPath $outputGpu -PathType Leaf) {
        return [IO.Path]::GetFullPath($outputGpu)
    }

    $outputCore = Join-Path $playerOutput 'PhysX_64.dll'
    if (-not (Test-Path -LiteralPath $outputCore -PathType Leaf)) {
        throw "PhysX core runtime is missing: $outputCore"
    }
    $coreLength = (Get-Item -LiteralPath $outputCore).Length
    $coreHash = Get-Sha256 -Path $outputCore
    $installedRoot = Join-Path $repoRoot 'vcpkg_installed'
    if (Test-Path -LiteralPath $installedRoot -PathType Container) {
        foreach ($candidateCore in @(Get-ChildItem -LiteralPath $installedRoot -Recurse `
            -Filter 'PhysX_64.dll' -File | Sort-Object FullName)) {
            if ($candidateCore.Length -ne $coreLength -or
                (Get-Sha256 -Path $candidateCore.FullName) -ne $coreHash) {
                continue
            }
            $candidateGpu = Join-Path $candidateCore.DirectoryName 'PhysXGpu_64.dll'
            if (Test-Path -LiteralPath $candidateGpu -PathType Leaf) {
                return [IO.Path]::GetFullPath($candidateGpu)
            }
        }
    }
    throw 'linked PhysX runtime과 일치하는 PhysXGpu_64.dll을 찾지 못했다.'
}

function Copy-ManagedRuntime {
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][string]$DestinationRoot
    )

    $managedFiles = @(
        'ScriptCore.deps.json',
        'ScriptCore.dll',
        'ScriptCore.runtimeconfig.json',
        'Scripts/GameScripts.deps.json',
        'Scripts/GameScripts.dll'
    )
    foreach ($relative in $managedFiles) {
        Copy-RelativeFile -SourceRoot $SourceRoot -DestinationRoot $DestinationRoot `
            -RelativePath $relative
    }
    return $managedFiles
}

function Get-RuntimePayloadEntries {
    param(
        [Parameter(Mandatory)][string]$Stage,
        [Parameter(Mandatory)][string[]]$RootFiles
    )

    $entries = [Collections.Generic.List[object]]::new()
    foreach ($name in $RootFiles) {
        $path = Join-Path $Stage $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "runtime payload file is missing: $name"
        }
        $item = Get-Item -LiteralPath $path
        [void]$entries.Add([pscustomobject][ordered]@{
            path = $name.Replace('\', '/')
            bytes = $item.Length
            sha256 = Get-Sha256 -Path $item.FullName
        })
    }
    $managedRoot = Join-Path $Stage 'Managed'
    foreach ($item in @(Get-ChildItem -LiteralPath $managedRoot -Recurse -File)) {
        $relative = [IO.Path]::GetRelativePath($Stage, $item.FullName).Replace('\', '/')
        [void]$entries.Add([pscustomobject][ordered]@{
            path = $relative
            bytes = $item.Length
            sha256 = Get-Sha256 -Path $item.FullName
        })
    }
    return @(Sort-EntriesOrdinal -Entries $entries.ToArray())
}

function Get-ContentDigest {
    param([Parameter(Mandatory)][object[]]$Entries)
    $lines = @($Entries | ForEach-Object { "$($_.path)`0$($_.sha256)" })
    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
    $digest = [Security.Cryptography.SHA256]::HashData($bytes)
    return [Convert]::ToHexString($digest).ToLowerInvariant()
}

function Test-IsAssetPackerExcludedFile {
    param([Parameter(Mandatory)][string]$Path)

    # Keep this projection explicit and verify it against AssetPacker's reopened
    # pak listing below. CEMF v2 owns source identity, so .meta is authoring-only.
    # D4 removed every JSON reader; those files are obsolete authoring remnants,
    # not a compatibility surface, and must not leak back into Player content.
    $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
    return $extension -in @('.cpp', '.h', '.hpp', '.meta', '.json')
}

function Get-PackageEntries {
    param([Parameter(Mandatory)][string]$MergedRoot)

    $entries = @(Get-ChildItem -LiteralPath $MergedRoot -File -Recurse |
        Where-Object { -not (Test-IsAssetPackerExcludedFile -Path $_.FullName) } |
        ForEach-Object {
            $relative = [IO.Path]::GetRelativePath($MergedRoot, $_.FullName).Replace('\', '/')
            [pscustomobject][ordered]@{
                path = $relative
                bytes = $_.Length
                sha256 = Get-Sha256 -Path $_.FullName
            }
        })
    return @(Sort-EntriesOrdinal -Entries $entries)
}

function Get-ModelCookSources {
    param([Parameter(Mandatory)][string]$AssetsRoot)

    $models = @(Get-ChildItem -LiteralPath $AssetsRoot -File -Recurse |
        Where-Object {
            $_.Extension.ToLowerInvariant() -in @('.fbx', '.glb', '.gltf')
        } |
        Sort-Object FullName)
    if ($models.Count -eq 0) {
        throw "package Assets에 cook할 model source가 없다: $AssetsRoot"
    }
    return $models
}

# Derived 하위 폴더마다의 GUID-addressed 규약. 쿠커가 만드는 경로와 **독립적으로**
# 한 번 더 확인한다 — 경로를 만드는 지점(CookedAssetManifest.h)과 검사하는 지점이
# 같은 코드면 규약이 바뀌어도 아무도 모른다.
$script:derivedPathRules = @(
    # MBC11 — 모델은 generation 디렉터리(<id>/<gen>/generation.asset·model.cemc·
    # sidecar.meta·textures/<TextureId>.png)로 내보낸다. Directory=$true 규약.
    [pscustomobject]@{ Folder = 'Models';     Pattern = 'asset|cemc|meta|png'; Directory = $true }
    [pscustomobject]@{ Folder = 'Textures';   Pattern = 'png|hdr|dds|jpg' }
    [pscustomobject]@{ Folder = 'ShaderMeta'; Pattern = 'shadermeta' }
    [pscustomobject]@{ Folder = 'Materials';  Pattern = 'asset' }
    [pscustomobject]@{ Folder = 'Scenes';     Pattern = 'creator' }
    [pscustomobject]@{ Folder = 'Prefabs';    Pattern = 'prefab' }
)

function Assert-CookOutput {
    param(
        [Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][int]$ExpectedArtifactPathCount
    )

    $derivedRoot = Join-Path $OutputRoot 'Derived'
    $manifestPath = Join-Path $derivedRoot 'asset-manifest.cemf'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "cook manifest가 없다: $manifestPath"
    }

    $guid = '([0-9a-f]{8}-[0-9a-f]{4}-[48][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})'
    $derivedFiles = @(Get-ChildItem -LiteralPath $derivedRoot -File -Recurse)
    $artifacts = @($derivedFiles | Where-Object {
        $_.FullName -ne (Get-Item -LiteralPath $manifestPath).FullName
    })

    $byFolder = @{}
    foreach ($artifact in $artifacts) {
        $relative = [IO.Path]::GetRelativePath($derivedRoot, $artifact.FullName).Replace('\', '/')
        $folder = ($relative -split '/')[0]
        $rule = $script:derivedPathRules | Where-Object Folder -eq $folder
        if ($null -eq $rule) {
            throw "Derived 하위에 규약 밖 폴더가 있다: $relative"
        }
        # ★ `-cnotmatch` 다. `-notmatch` 는 대소문자를 무시해서 대문자 GUID 가
        #   통과한다(guid 게이트가 같은 함정에 빠져 있었다).
        $isDirectoryRule = ($rule.PSObject.Properties.Name -contains 'Directory') -and $rule.Directory
        $expected = if ($isDirectoryRule) {
            ('^{0}/([0-9a-f]{{2}})/{1}/[0-9]+/(generation\.asset|model\.cemc|sidecar\.meta|textures/{1}\.png)$' -f $folder, $guid)
        } else {
            ('^{0}/([0-9a-f]{{2}})/{1}\.({2})$' -f $folder, $guid, $rule.Pattern)
        }
        if ($relative -cnotmatch $expected -or
            $Matches[1] -ne $Matches[2].Substring(0, 2)) {
            throw "cook artifact 경로가 GUID-addressed 규약과 다르다: $relative"
        }
        if (-not $byFolder.ContainsKey($folder)) { $byFolder[$folder] = 0 }
        $byFolder[$folder] += 1
    }

    # 쿠커가 보고한 서로 다른 artifact 경로 수와 디스크가 맞아야 한다.
    # material 처럼 model artifact 를 공유하는 subasset 은 파일이 없으므로
    # entry 수가 아니라 **경로 수**로 맞춘다.
    if ($artifacts.Count -ne $ExpectedArtifactPathCount) {
        throw ("Derived artifact 파일 수가 쿠커 보고와 다르다: {0} != {1}" -f
            $artifacts.Count, $ExpectedArtifactPathCount)
    }

    return [pscustomobject][ordered]@{
        ArtifactCount = $artifacts.Count
        ArtifactBytes = ($artifacts | Measure-Object Length -Sum).Sum
        ManifestBytes = (Get-Item -LiteralPath $manifestPath).Length
        ManifestSha256 = Get-Sha256 -Path $manifestPath
        DerivedFileCount = $derivedFiles.Count
        ByFolder = $byFolder
    }
}

# 쿠커 option 별 source 확장자. producer 의 allowlist 와 같은 범위여야 한다.
$script:cookSourceRules = @(
    [pscustomobject]@{ Option = '--model';      Extensions = @('.fbx', '.glb', '.gltf') }
    [pscustomobject]@{ Option = '--texture';    Extensions = @('.png', '.hdr', '.dds') }
    [pscustomobject]@{ Option = '--shadermeta'; Extensions = @('.shadermeta') }
    [pscustomobject]@{ Option = '--material';   Extensions = @('.asset') }
    [pscustomobject]@{ Option = '--scene';      Extensions = @('.creator', '.prefab') }
)

function Invoke-AssetCook {
    param(
        [Parameter(Mandatory)][string]$AssetCooker,
        [Parameter(Mandatory)][string]$AssetsRoot,
        [Parameter(Mandatory)][string]$OutputRoot
    )

    if (Test-Path -LiteralPath $OutputRoot) {
        throw "cook output은 존재하지 않는 새 디렉터리여야 한다: $OutputRoot"
    }

    $models = @(Get-ModelCookSources -AssetsRoot $AssetsRoot)

    # ★ `Assets/Models/*.asset` 은 재질이 아니라 **legacy 쿠킹 캐시**다.
    #   `--material` 로 넘기면 `m_shaderMetaGuid` 가 없어 fail-closed 로 터지므로
    #   material 열거에서 제외한다.
    #
    #   이것들이 여기 있다는 사실 자체가 §3.6.1 이 죽이려는 "파생물이 콘텐츠
    #   서브트리 안에 있는" 형태다(InputMode Project 는 gitignore 된 이 파일들까지
    #   그대로 복사한다). **다만 여기서 패키지 구성을 바꾸지는 않는다** — legacy
    #   로더가 아직 그 캐시를 읽고, 그 경로를 끊는 것은 I5/I6 의 일이다.
    #   숨기지 않고 세어서 보고만 한다.
    $staleCookCache = @(Get-ChildItem -LiteralPath $AssetsRoot -File -Recurse -Filter '*.asset' |
        Where-Object {
            [IO.Path]::GetRelativePath($AssetsRoot, $_.FullName).Replace('\', '/') -like 'Models/*'
        })
    $staleCookCacheNames = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@($staleCookCache | ForEach-Object { $_.FullName }),
        [StringComparer]::OrdinalIgnoreCase)

    $arguments = [Collections.Generic.List[string]]::new()
    $arguments.Add('--asset-root')
    $arguments.Add($AssetsRoot)
    $arguments.Add('--output')
    $arguments.Add($OutputRoot)

    $sourceCounts = [ordered]@{}
    foreach ($rule in $script:cookSourceRules) {
        $matches = @(Get-ChildItem -LiteralPath $AssetsRoot -File -Recurse |
            Where-Object { $rule.Extensions -contains $_.Extension.ToLowerInvariant() } |
            Where-Object { -not $staleCookCacheNames.Contains($_.FullName) } |
            Sort-Object FullName)
        $sourceCounts[$rule.Option] = $matches.Count
        foreach ($source in $matches) {
            $arguments.Add($rule.Option)
            $arguments.Add($source.FullName)
        }
    }
    if ($sourceCounts['--model'] -ne $models.Count) {
        throw ("model source 열거가 어긋난다: {0} != {1}" -f
            $sourceCounts['--model'], $models.Count)
    }

    $cookLog = @(Invoke-NativeChecked -FilePath $AssetCooker `
        -Label 'AssetCooker corpus' -Arguments $arguments.ToArray())
    foreach ($line in $cookLog) { Write-Host $line }

    # ★ 파일 수의 기대값을 여기서 다시 계산하지 않는다. 쿠커가 폐포 스윕에서
    #   실제로 센 값(`artifactPaths`)을 읽어 디스크와 맞춘다 — 같은 규칙을 두
    #   곳에서 유도하면 둘 다 틀렸을 때 서로를 확인해 준다.
    $summary = $cookLog | Where-Object { $_ -match '^asset-cooker models=' } | Select-Object -First 1
    if (-not $summary -or $summary -notmatch 'artifactPaths=(\d+)') {
        throw 'AssetCooker 요약에서 artifactPaths를 읽지 못했다.'
    }
    $artifactPathCount = [int]$Matches[1]
    if ($summary -notmatch 'legacyTextureNameRefs=(\d+)') {
        throw 'AssetCooker 요약에서 legacyTextureNameRefs를 읽지 못했다.'
    }
    $legacyTextureNameRefs = [int]$Matches[1]

    $result = Assert-CookOutput -OutputRoot $OutputRoot `
        -ExpectedArtifactPathCount $artifactPathCount
    Add-Member -InputObject $result -NotePropertyName SourceCounts -NotePropertyValue $sourceCounts
    Add-Member -InputObject $result -NotePropertyName ModelCount -NotePropertyValue $models.Count
    Add-Member -InputObject $result -NotePropertyName LegacyTextureNameRefs `
        -NotePropertyValue $legacyTextureNameRefs
    Add-Member -InputObject $result -NotePropertyName LegacyModelCookCaches `
        -NotePropertyValue $staleCookCache.Count
    return $result
}

function Invoke-RuntimeDocumentCook {
    param(
        [Parameter(Mandatory)][string]$AssetCooker,
        [Parameter(Mandatory)][string]$RuntimeRoot
    )

    $cookLog = @(Invoke-NativeChecked -FilePath $AssetCooker `
        -Label 'AssetCooker runtime documents' -Arguments @(
            '--compile-runtime-documents', '--runtime-root', $RuntimeRoot))
    foreach ($line in $cookLog) { Write-Host $line }
    $summary = $cookLog | Where-Object {
        $_ -match '^asset-cooker runtime-documents=' } | Select-Object -First 1
    if (-not $summary -or
        $summary -notmatch '^asset-cooker runtime-documents=(\d+) bytes=(\d+) format=CEDO1$') {
        throw 'AssetCooker runtime document 요약을 읽지 못했다.'
    }
    $documentCount = [int]$Matches[1]
    $documentBytes = [uint64]$Matches[2]

    $documents = [Collections.Generic.List[IO.FileInfo]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $RuntimeRoot 'ProjectSetting') `
        -File -Recurse -Filter '*.asset')) {
        $documents.Add($file)
    }
    $runtimeExtensions = @('.inputmap', '.bt', '.blackboard', '.volume', '.terrain', '.foliage')
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $RuntimeRoot 'Assets') `
        -File -Recurse | Where-Object { $runtimeExtensions -contains $_.Extension.ToLowerInvariant() })) {
        $documents.Add($file)
    }
    if ($documents.Count -ne $documentCount) {
        throw "runtime document 수가 tool 요약과 다르다: $($documents.Count) != $documentCount"
    }
    foreach ($document in $documents) {
        $stream = [IO.File]::OpenRead($document.FullName)
        try {
            $magic = [byte[]]::new(4)
            if ($stream.Read($magic, 0, 4) -ne 4 -or
                [Text.Encoding]::ASCII.GetString($magic) -ne 'CEDO') {
                throw "runtime document가 CEDO가 아니다: $($document.FullName)"
            }
        } finally {
            $stream.Dispose()
        }
    }

    return [pscustomobject]@{
        DocumentCount = $documentCount
        DocumentBytes = $documentBytes
        Format = 'CEDO1'
    }
}

function Copy-WorkspaceInputs {
    param(
        [Parameter(Mandatory)][string]$BaseRoot,
        [Parameter(Mandatory)][string]$ProjectRoot
    )

    $expectedProject = [IO.Path]::GetFullPath((Join-Path $repoRoot 'Dynamic_CPP'))
    if (-not $ProjectRoot.Equals($expectedProject, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Workspace input mode currently supports only this repository Dynamic_CPP project.'
    }

    Assert-NoReparseTree -Root (Join-Path $ProjectRoot 'Assets')
    Assert-NoReparseTree -Root (Join-Path $ProjectRoot 'ProjectSetting')

    $gitPaths = @(git -C $repoRoot ls-files -- 'Dynamic_CPP/Assets/**' 'Dynamic_CPP/ProjectSetting/**')
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed while preparing tracked package input.' }

    $copied = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($gitPath in $gitPaths) {
        $normalized = $gitPath.Replace('\', '/')
        if ($normalized.StartsWith('Dynamic_CPP/Assets/', [StringComparison]::Ordinal)) {
            $relative = $normalized.Substring('Dynamic_CPP/Assets/'.Length)
            Copy-RelativeFile -SourceRoot (Join-Path $ProjectRoot 'Assets') `
                -DestinationRoot (Join-Path $BaseRoot 'Assets') -RelativePath $relative
            [void]$copied.Add("Assets/$relative")
        } elseif ($normalized.StartsWith('Dynamic_CPP/ProjectSetting/', [StringComparison]::Ordinal)) {
            $relative = $normalized.Substring('Dynamic_CPP/ProjectSetting/'.Length)
            if (Test-IsGeneratedRuntimeSetting -RelativePath $relative) {
                continue
            }
            Copy-RelativeFile -SourceRoot (Join-Path $ProjectRoot 'ProjectSetting') `
                -DestinationRoot (Join-Path $BaseRoot 'ProjectSetting') -RelativePath $relative
            [void]$copied.Add("ProjectSetting/$relative")
        }
    }

    # Workspace mode intentionally uses working-tree bytes. The canonical fixture is
    # included even before its first commit; Tracked mode never has this fallback.
    $sceneVirtualPath = 'Assets/Scenes/FT_Primitives.creator'
    if (-not $copied.Contains($sceneVirtualPath)) {
        Copy-OneFile -Source $canonicalScenePath `
            -Destination (Join-Path $BaseRoot $sceneVirtualPath.Replace('/', '\'))
        [void]$copied.Add($sceneVirtualPath)
    }
    return $copied.Count
}

function New-TrackedSnapshot {
    param(
        [Parameter(Mandatory)][string]$DestinationRoot,
        [Parameter(Mandatory)][string]$GitCommit
    )

    $expectedProject = [IO.Path]::GetFullPath((Join-Path $repoRoot 'Dynamic_CPP'))
    if (-not $projectRoot.Equals($expectedProject, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Tracked input mode currently supports only this repository Dynamic_CPP project.'
    }

    $requiredPaths = @(
        'Dynamic_CPP/Assets/Scenes/FT_Primitives.creator',
        'Tools/packaging/templates/EngineSettings.runtime.yml'
    )
    foreach ($required in $requiredPaths) {
        $objectSpec = "${GitCommit}:$required"
        & git -C $repoRoot cat-file -e $objectSpec
        if ($LASTEXITCODE -ne 0) {
            throw "Tracked input is absent from commit $GitCommit`: $required"
        }
    }

    $archivePath = "$DestinationRoot.zip"
    Assert-ScopedPath -Path $DestinationRoot -Root (Split-Path -Parent $DestinationRoot) `
        -Label 'tracked snapshot'
    Assert-ScopedPath -Path $archivePath -Root (Split-Path -Parent $archivePath) `
        -Label 'tracked snapshot archive'
    & git -C $repoRoot archive --format=zip "--output=$archivePath" $GitCommit -- `
        'Dynamic_CPP/Assets' 'Dynamic_CPP/ProjectSetting' `
        'Tools/packaging/templates/EngineSettings.runtime.yml'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "git archive failed for commit $GitCommit"
    }
    try {
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $DestinationRoot)
    } finally {
        Remove-ScopedFile -Path $archivePath -Root (Split-Path -Parent $archivePath) `
            -Label 'tracked snapshot archive'
    }
    return [IO.Path]::GetFullPath($DestinationRoot)
}

function Copy-ProjectInputs {
    param(
        [Parameter(Mandatory)][string]$BaseRoot,
        [Parameter(Mandatory)][string]$ProjectRoot
    )

    $assetRoot = Join-Path $ProjectRoot 'Assets'
    $settingsRoot = Join-Path $ProjectRoot 'ProjectSetting'
    if (-not (Test-Path -LiteralPath $assetRoot -PathType Container)) {
        throw "Assets directory is missing: $assetRoot"
    }
    Assert-NoReparseTree -Root $assetRoot
    Assert-NoReparseTree -Root $settingsRoot

    $count = 0
    foreach ($file in @(Get-ChildItem -LiteralPath $assetRoot -File -Recurse | Sort-Object FullName)) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "symbolic/reparse package input is not allowed: $($file.FullName)"
        }
        $relative = [IO.Path]::GetRelativePath($assetRoot, $file.FullName)
        Copy-OneFile -Source $file.FullName -Destination (Join-Path $BaseRoot "Assets\$relative")
        ++$count
    }

    if (Test-Path -LiteralPath $settingsRoot -PathType Container) {
        foreach ($file in @(Get-ChildItem -LiteralPath $settingsRoot -File -Recurse | Sort-Object FullName)) {
            $relative = [IO.Path]::GetRelativePath($settingsRoot, $file.FullName)
            if (Test-IsGeneratedRuntimeSetting -RelativePath $relative) {
                continue
            }
            Copy-OneFile -Source $file.FullName `
                -Destination (Join-Path $BaseRoot "ProjectSetting\$relative")
            ++$count
        }
    }
    return $count
}

function Test-IsGeneratedRuntimeSetting {
    param([Parameter(Mandatory)][string]$RelativePath)

    return $RelativePath.Equals('EngineSettings.asset', [StringComparison]::OrdinalIgnoreCase) -or
        $RelativePath.EndsWith('.runtime.yml', [StringComparison]::OrdinalIgnoreCase)
}

function Write-MaterializedRuntimeSettings {
    param(
        [Parameter(Mandatory)][string]$Template,
        [Parameter(Mandatory)][string]$Destination,
        [string]$RequestedStartupScene,
        [string]$RequestedRenderBackend
    )

    Copy-OneFile -Source $Template -Destination $Destination
    $settingsText = [IO.File]::ReadAllText($Destination)

    if (-not [string]::IsNullOrWhiteSpace($RequestedStartupScene)) {
        if ([IO.Path]::GetFileName($RequestedStartupScene) -ne $RequestedStartupScene -or
            -not [IO.Path]::GetExtension($RequestedStartupScene).Equals(
                '.creator', [StringComparison]::OrdinalIgnoreCase)) {
            throw "-StartupScene must be a plain .creator file name: $RequestedStartupScene"
        }
        if ($RequestedStartupScene.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
            throw "-StartupScene contains an invalid file-name character: $RequestedStartupScene"
        }

        $startupPattern = '(?m)^(startupSceneName:\s*)[^\r\n]+?\s*$'
        $startupMatches = [regex]::Matches($settingsText, $startupPattern)
        if ($startupMatches.Count -ne 1) {
            throw "runtime settings template must contain exactly one startupSceneName (found $($startupMatches.Count))."
        }
        $settingsText = [regex]::Replace($settingsText, $startupPattern,
            { param($match) $match.Groups[1].Value + '"' + $RequestedStartupScene + '"' })
    }

    $backendPatterns = @(
        '(?ms)(^render:\s*\r?\n\s+backend:\s*)(dx12|vulkan)(\s*$)',
        '(?ms)(^build:\s*\r?\n\s+render:\s*\r?\n\s+backend:\s*)(dx12|vulkan)(\s*$)'
    )
    foreach ($backendPattern in $backendPatterns) {
        $backendMatches = [regex]::Matches($settingsText, $backendPattern)
        if ($backendMatches.Count -ne 1) {
            throw "runtime settings template backend field is ambiguous (found $($backendMatches.Count))."
        }
    }

    # build.render.backend is authoring input only. Every packaged Player reads the
    # projected runtime render.backend, even when the caller omits -RenderBackend.
    $buildBackendMatch = [regex]::Match($settingsText, $backendPatterns[1])
    $backend = if ([string]::IsNullOrWhiteSpace($RequestedRenderBackend)) {
        $buildBackendMatch.Groups[2].Value.ToLowerInvariant()
    } else {
        $RequestedRenderBackend.ToLowerInvariant()
    }
    foreach ($backendPattern in $backendPatterns) {
        $settingsText = [regex]::Replace($settingsText, $backendPattern,
            { param($match) $match.Groups[1].Value + $backend + $match.Groups[3].Value })
    }

    [IO.File]::WriteAllText($Destination, $settingsText, [Text.UTF8Encoding]::new($false))
}

function Merge-PackageInput {
    param(
        [Parameter(Mandatory)][string]$BaseRoot,
        [Parameter(Mandatory)][string]$GeneratedRoot,
        [Parameter(Mandatory)][string]$MergedRoot
    )

    foreach ($file in @(Get-ChildItem -LiteralPath $BaseRoot -File -Recurse | Sort-Object FullName)) {
        $relative = [IO.Path]::GetRelativePath($BaseRoot, $file.FullName)
        Copy-OneFile -Source $file.FullName -Destination (Join-Path $MergedRoot $relative)
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $GeneratedRoot -File -Recurse | Sort-Object FullName)) {
        $relative = [IO.Path]::GetRelativePath($GeneratedRoot, $file.FullName)
        $destination = Join-Path $MergedRoot $relative
        if (Test-Path -LiteralPath $destination) {
            throw "generated overlay collides with base input: $relative"
        }
        Copy-OneFile -Source $file.FullName -Destination $destination
    }
}

function Assert-PackagePreflight {
    param(
        [Parameter(Mandatory)][string]$MergedRoot,
        [Parameter(Mandatory)][string]$SettingsPath
    )

    $settingsText = Get-Content -LiteralPath $SettingsPath -Raw
    if ($settingsText -notmatch '(?ms)^lastWindowSize:\s*\r?\n\s+x:\s*[-+0-9.]+\s*\r?\n\s+y:\s*[-+0-9.]+') {
        throw 'runtime settings template lacks lastWindowSize.x/y.'
    }
    if ($settingsText -notmatch '(?m)^renderPassSettings:\s*$') {
        throw 'runtime settings template lacks renderPassSettings.'
    }
    $startupMatches = [regex]::Matches($settingsText,
        '(?m)^startupSceneName:\s*([^\r\n]+?)\s*$')
    if ($startupMatches.Count -ne 1) {
        throw "runtime settings must contain exactly one startupSceneName (found $($startupMatches.Count))."
    }
    $startupScene = $startupMatches[0].Groups[1].Value.Trim()
    if (($startupScene.StartsWith('"') -and $startupScene.EndsWith('"')) -or
        ($startupScene.StartsWith("'") -and $startupScene.EndsWith("'"))) {
        $startupScene = $startupScene.Substring(1, $startupScene.Length - 2)
    }
    if ([IO.Path]::GetFileName($startupScene) -ne $startupScene -or
        -not [IO.Path]::GetExtension($startupScene).Equals(
            '.creator', [StringComparison]::OrdinalIgnoreCase)) {
        throw "startupSceneName must be a plain .creator file name: $startupScene"
    }

    $runtimeBackend = [regex]::Match($settingsText,
        '(?ms)^render:\s*\r?\n\s+backend:\s*(dx12|vulkan)\s*$')
    $buildBackend = [regex]::Match($settingsText,
        '(?ms)^build:\s*\r?\n\s+render:\s*\r?\n\s+backend:\s*(dx12|vulkan)\s*$')
    if (-not $runtimeBackend.Success -or -not $buildBackend.Success) {
        throw 'runtime settings template must declare render.backend and build.render.backend.'
    }
    if (-not $runtimeBackend.Groups[1].Value.Equals(
        $buildBackend.Groups[1].Value, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'packaged runtime render.backend does not match the projected build backend.'
    }

    $scenePath = Join-Path $MergedRoot "Assets\Scenes\$startupScene"
    if (-not (Test-Path -LiteralPath $scenePath -PathType Leaf)) {
        throw "startup scene is absent from package input: $scenePath"
    }
    $sceneText = Get-Content -LiteralPath $scenePath -Raw
    $sceneCounts = [ordered]@{
        Entity = [regex]::Matches($sceneText, '(?m)^\s{2}- Entity:\s+\d+\s*$').Count
        Transform = [regex]::Matches($sceneText, '(?m)^\s{6}- Transform:\s+\d+\s*$').Count
        Mesh = [regex]::Matches($sceneText, '(?m)^\s{6}- MeshRenderer:\s+\d+\s*$').Count
        Camera = [regex]::Matches($sceneText, '(?m)^\s{6}- CameraComponent:\s+\d+\s*$').Count
        Light = [regex]::Matches($sceneText, '(?m)^\s{6}- LightComponent:\s+\d+\s*$').Count
        Script = [regex]::Matches($sceneText, '(?m)^\s{6}- ScriptComponent:\s+\d+\s*$').Count
    }
    if ($sceneText -match '(?m)^m_SceneObjects:\s*$|^\s+m_transform:\s*$|^\s+m_gameObjectType:\s*') {
        throw "$startupScene contains a legacy GameObject schema key."
    }

    $requiresManagedLifecycle = $startupScene.Equals(
        'FT_Primitives.creator', [StringComparison]::OrdinalIgnoreCase)
    if ($requiresManagedLifecycle) {
        if ($sceneCounts.Entity -ne 11 -or $sceneCounts.Transform -ne 11 -or
            $sceneCounts.Mesh -ne 8 -or $sceneCounts.Camera -ne 1 -or
            $sceneCounts.Light -ne 1 -or $sceneCounts.Script -ne 1) {
            throw "FT_Primitives schema/count mismatch: $($sceneCounts | ConvertTo-Json -Compress)"
        }
        $probeTypes = [regex]::Matches($sceneText,
            '(?m)^\s+m_scriptType:\s*PackageSmokeProbe\s*$')
        if ($probeTypes.Count -ne 1) {
            throw "FT_Primitives must contain exactly one PackageSmokeProbe (found $($probeTypes.Count))."
        }
    }

    $requiredShader = Join-Path $MergedRoot 'Assets\Shaders\DefaultPassShader\WorldSprite.hlsl'
    if (-not (Test-Path -LiteralPath $requiredShader -PathType Leaf)) {
        throw 'tracked shader closure is stale: WorldSprite.hlsl is missing.'
    }
    $templateLeak = Join-Path $MergedRoot 'ProjectSetting\EngineSettings.runtime.yml'
    if (Test-Path -LiteralPath $templateLeak) {
        throw 'runtime settings template leaked into package input.'
    }

    return [pscustomobject]@{
        StartupScene = $startupScene
        SceneCounts = $sceneCounts
        RuntimeBackend = $runtimeBackend.Groups[1].Value
        BuildBackend = $buildBackend.Groups[1].Value
        RequiresManagedLifecycle = $requiresManagedLifecycle
    }
}

function Invoke-PlayerSmoke {
    param(
        [Parameter(Mandatory)][string]$CandidateStage,
        [Parameter(Mandatory)][string]$ManifestPath,
        [Parameter(Mandatory)][object]$Preflight
    )

    $verifyTemp = Join-Path $CandidateStage '.verify-temp'
    Assert-ScopedPath -Path $verifyTemp -Root $CandidateStage -Label 'verify temp'
    if (Test-Path -LiteralPath $verifyTemp) {
        throw "verify temp must be unique and absent before launch: $verifyTemp"
    }
    New-Item -ItemType Directory -Path $verifyTemp | Out-Null

    $stdoutPath = Join-Path $CandidateStage 'verify.stdout.log'
    $stderrPath = Join-Path $CandidateStage 'verify.stderr.log'
    $playerPath = Join-Path $CandidateStage 'Player.exe'

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $playerPath
    $startInfo.WorkingDirectory = $CandidateStage
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    [void]$startInfo.ArgumentList.Add('--smoke')
    [void]$startInfo.ArgumentList.Add([string]$SmokeFrames)
    $startInfo.Environment['TEMP'] = $verifyTemp
    $startInfo.Environment['TMP'] = $verifyTemp
    $systemRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $startInfo.Environment['PATH'] = @(
        $CandidateStage,
        (Join-Path $systemRoot 'System32'),
        $systemRoot
    ) -join [IO.Path]::PathSeparator

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $processId = 0
    $stdout = ''
    $stderr = ''
    $exitCode = -1
    try {
        if (-not $process.Start()) { throw 'Player process failed to start.' }
        $processId = $process.Id
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $completed = $process.WaitForExit($SmokeTimeoutSec * 1000)
        if (-not $completed) {
            try { $process.Kill($true) } catch {}
            $process.WaitForExit()
        } else {
            $process.WaitForExit()
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($stdoutPath, $stdout, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($stderrPath, $stderr, [Text.UTF8Encoding]::new($false))
        if (-not $completed) { throw "Player smoke timed out after $SmokeTimeoutSec seconds." }
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }

    $logTexts = [Collections.Generic.List[string]]::new()
    [void]$logTexts.Add($stdout)
    [void]$logTexts.Add($stderr)
    $runtimeLogRoot = Join-Path $verifyTemp `
        "CreatorEngine\Player\$processId\RuntimeData\Log"
    foreach ($logRoot in @((Join-Path $CandidateStage 'Log'), $runtimeLogRoot)) {
        if (Test-Path -LiteralPath $logRoot -PathType Container) {
            foreach ($logFile in @(Get-ChildItem -LiteralPath $logRoot -File |
                Where-Object { $_.Extension -in @('.html', '.log', '.txt') })) {
                [void]$logTexts.Add([IO.File]::ReadAllText($logFile.FullName))
            }
        }
    }
    $combined = $logTexts -join "`n"

    if ($exitCode -ne 0) { throw "Player smoke failed with exit code $exitCode." }
    $assetCatalogMarker = [regex]::Match($stdout,
        '\[asset\.catalog\]\s*source=cemf\s+identities=([1-9]\d*)\s+metaParsed=0')
    if (-not $assetCatalogMarker.Success) {
        throw 'Player smoke lacks the CEMF-only source catalog marker (metaParsed=0).'
    }
    $cookedCatalogMarker = [regex]::Match($stdout,
        '\[cooked\.catalog\]\s*mount\s+[^\r\n]*\s+entries=([1-9]\d*)\s+sources=([1-9]\d*)\s+stale=(\d+)')
    if (-not $cookedCatalogMarker.Success) {
        throw 'Player smoke lacks the mounted cooked catalog entry/source counts.'
    }
    $sourceIdentities = [int]$assetCatalogMarker.Groups[1].Value
    $mountedEntries = [int]$cookedCatalogMarker.Groups[1].Value
    $mountedSources = [int]$cookedCatalogMarker.Groups[2].Value
    if ($sourceIdentities -ne $mountedSources) {
        throw "Player catalog source count mismatch: identities=$sourceIdentities, mountedSources=$mountedSources"
    }
	$cookedSceneDocuments = ([regex]::Matches($stdout,
		'\[scene\.document\]\s*source=cooked\s+guid=[0-9a-f-]{36}')).Count
	$authoringSceneDocuments = ([regex]::Matches($stdout,
		'\[scene\.document\]\s*source=authoring\s+guid=[0-9a-f-]{36}')).Count
	if ($cookedSceneDocuments -lt 1 -or $authoringSceneDocuments -ne 0) {
		throw "Player scene document route mismatch: cooked=$cookedSceneDocuments, authoring=$authoringSceneDocuments"
	}
    # ── PHASE 14.5 LC8: 스모크가 **어느 구성을 돌렸는지** 스스로 말하게 한다 ──
    #
    # ★ §13 LC8 완료 기준이 이 검사를 이름으로 요구한다: "구성 이름만 바꾸고
    #   스크립트를 두면 빈 출력이 통과한다."
    #
    #   -Shipping 을 주고도 Development 바이너리가 돌면 아래 text-parser·씬 마커는
    #   전부 통과한다 — 그 마커들은 구성과 무관하기 때문이다. 그러면 격리를
    #   검증했다고 적어 놓고 실제로는 아무것도 검증하지 않은 게 된다. Player 가
    #   자기 빌드에 서비스가 컴파일돼 있는지 찍고, 여기서 요청한 구성과 맞대 본다.
    $serviceMarker = [regex]::Match($stdout,
        '\[player\.service\]\s*compiled=(yes|no)\s+enabled=(yes|no)')
    if (-not $serviceMarker.Success) {
        throw 'Player smoke lacks the [player.service] configuration marker (LC8).'
    }
    $serviceCompiled = ($serviceMarker.Groups[1].Value -eq 'yes')
    $expectCompiled = -not $Shipping
    if ($serviceCompiled -ne $expectCompiled) {
        throw ("Player smoke ran the wrong configuration: requested " +
            "$(if ($Shipping) { 'Shipping' } else { 'Development' }) but the binary reports " +
            "compiled=$($serviceMarker.Groups[1].Value).")
    }

    $textParserMarker = [regex]::Match($stdout,
        '\[runtime\.text-parser\]\s*calls=(\d+)')
    if (-not $textParserMarker.Success) {
        throw 'Player smoke lacks the runtime text-parser counter.'
    }
    $textParserCalls = [int]$textParserMarker.Groups[1].Value
    if ($textParserCalls -ne 0) {
        $contexts = @([regex]::Matches($stdout,
            '\[runtime\.text-parser\.call\]\s*source=([^\r\n]+)') |
            ForEach-Object { $_.Groups[1].Value })
        throw "Player runtime text parser was called $textParserCalls times: $($contexts -join ', ')"
    }
    $sceneMarkerPattern = 'Scene loaded:[^\r\n]*' +
        [regex]::Escape([string]$Preflight.StartupScene)
    if ($combined -notmatch $sceneMarkerPattern) {
        throw 'Player smoke lacks the startup scene success marker.'
    }
    $smokeRows = @($stdout -split "`r?`n" | Where-Object { $_.StartsWith('[player.smoke] ') } | ForEach-Object { $_.Substring(15) | ConvertFrom-Json })
    if ($smokeRows.Count -ne 1 -or $smokeRows[0].schemaVersion -ne 1 -or -not $smokeRows[0].ready -or $smokeRows[0].registeredScriptTypes -le 0) {
        throw 'Player smoke lacks a valid managed runtime readiness result.'
    }
    $managedLifecycle = $false
    if ($Preflight.RequiresManagedLifecycle) {
        $managedInitialized = [regex]::Matches($stdout,
            '\[SMOKE\]\s*managed OnInitialized:\s*PackageSmokeProbe')
        $managedSimulation = [regex]::Matches($stdout,
            '\[SMOKE\]\s*managed OnBeginSimulation:\s*PackageSmokeProbe')
        if ($managedInitialized.Count -ne 1 -or $managedSimulation.Count -ne 1) {
            throw "managed lifecycle marker count mismatch: initialized=$($managedInitialized.Count), simulation=$($managedSimulation.Count)"
        }
        if ($managedInitialized[0].Index -ge $managedSimulation[0].Index) {
            throw 'managed lifecycle order is invalid: OnBeginSimulation preceded OnInitialized.'
        }
        $managedLifecycle = $true
    }
    $smokeMarker = [regex]::Match($combined,
        '\[SMOKE\]\s*frame limit reached[^\r\n]*\((\d+)\s+GT frames,\s*display frame\s+(\d+),\s*promotions\s+(\d+)\)')
    if (-not $smokeMarker.Success) { throw 'Player smoke lacks the clean-exit marker.' }
    $gameThreadFrames = [int]$smokeMarker.Groups[1].Value
    $displayFrame = [int]$smokeMarker.Groups[2].Value
    $promotions = [int]$smokeMarker.Groups[3].Value
    if ($gameThreadFrames -lt $SmokeFrames) {
        throw "Player game-thread frame count is too low: $gameThreadFrames < $SmokeFrames"
    }
    if ($displayFrame -lt 2) { throw "Player display frame is too low: $displayFrame" }
    if ($promotions -lt 2) { throw "Player display promotion count is too low: $promotions" }

    $failurePatterns = @(
        '\[SMOKE\]\s*startup scene load FAILED',
        '\[SMOKE\]\s*render pipeline FAILED',
        '\[CRASH\]',
        '\[CLR\].*실패',
        '\[RenderBackend\].*(실패|오류)',
        'Failed to load PhysXGpu_64\.dll',
        'GPU solver/Bp pipeline failed',
        'typed ops 미등록 타입',
        'PxScene::simulate\(\) called with a zero elapsedTime'
    )
    foreach ($pattern in $failurePatterns) {
        if ($combined -match $pattern) { throw "Player smoke matched failure pattern: $pattern" }
    }

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $playerRuntimeBase = Join-Path $verifyTemp 'CreatorEngine\Player'
    $runtimeOwners = @(if (Test-Path -LiteralPath $playerRuntimeBase -PathType Container) {
        Get-ChildItem -LiteralPath $playerRuntimeBase -Directory
    })
    if ($runtimeOwners.Count -ne 1 -or $runtimeOwners[0].Name -ne [string]$processId) {
        throw "Player runtime owner must be exactly the launched PID $processId."
    }
    $unpackedRoot = Join-Path $runtimeOwners[0].FullName 'RuntimeContent'
    if (-not (Test-Path -LiteralPath $unpackedRoot -PathType Container)) {
        throw "Player did not create its exact runtime content root: $unpackedRoot"
    }
    $unpackedFiles = @(Get-ChildItem -LiteralPath $unpackedRoot -File -Recurse)
    if ($unpackedFiles.Count -ne $manifest.entries.Count) {
        throw "unpacked entry count mismatch: $($unpackedFiles.Count) != $($manifest.entries.Count)"
    }
    $unpackedMeta = @($unpackedFiles | Where-Object {
        $_.Extension.Equals('.meta', [StringComparison]::OrdinalIgnoreCase)
    })
    if ($unpackedMeta.Count -ne 0) {
        throw "Player runtime content leaked $($unpackedMeta.Count) authoring .meta files."
    }
    $unpackedLegacyJson = @($unpackedFiles | Where-Object {
        $_.Extension.Equals('.json', [StringComparison]::OrdinalIgnoreCase)
    })
    if ($unpackedLegacyJson.Count -ne 0) {
        throw "Player runtime content leaked $($unpackedLegacyJson.Count) retired JSON files."
    }
    $unpackedCemf = Join-Path $unpackedRoot 'Assets\Derived\asset-manifest.cemf'
    if (-not (Test-Path -LiteralPath $unpackedCemf -PathType Leaf)) {
        throw 'Player runtime content lacks Assets/Derived/asset-manifest.cemf.'
    }
    foreach ($entry in $manifest.entries) {
        $relativeNative = ([string]$entry.path).Replace('/', [IO.Path]::DirectorySeparatorChar)
        $unpackedPath = Join-Path $unpackedRoot $relativeNative
        if (-not (Test-Path -LiteralPath $unpackedPath -PathType Leaf)) {
            throw "unpacked entry is missing: $($entry.path)"
        }
        $actualHash = Get-Sha256 -Path $unpackedPath
        if ($actualHash -ne [string]$entry.sha256) {
            throw "unpacked content hash mismatch: $($entry.path)"
        }
    }

    Remove-ScopedTree -Path $verifyTemp -Root $CandidateStage -Label 'verify temp'
    return [pscustomobject]@{
        ExitCode = $exitCode
        GameThreadFrames = $gameThreadFrames
        DisplayFrame = $displayFrame
        Promotions = $promotions
        RegisteredScripts = [int]$smokeRows[0].registeredScriptTypes
        ManagedLifecycle = $managedLifecycle
        CatalogSource = 'cemf'
        MetaParsed = 0
        SourceIdentities = $sourceIdentities
        CookedEntries = $mountedEntries
		CookedSceneDocuments = $cookedSceneDocuments
        TextParserCalls = $textParserCalls
    }
}

function Publish-Stage {
    param(
        [Parameter(Mandatory)][string]$Candidate,
        [Parameter(Mandatory)][string]$Release,
        [Parameter(Mandatory)][string]$CurrentPointer,
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][object]$Metadata
    )

    Assert-ScopedPath -Path $Candidate -Root $Root -Label 'candidate stage'
    Assert-ScopedPath -Path $Release -Root $Root -Label 'immutable release stage'
    Assert-ScopedPath -Path $CurrentPointer -Root $Root -Label 'current pointer'
    if (Test-Path -LiteralPath $Release) {
        throw "immutable release already exists: $Release"
    }

    # The candidate is unpublished, so one same-volume directory rename creates an
    # immutable release without ever opening a gap in the previously published one.
    # Windows can retain an image/scan handle briefly after Player exits; retry only
    # this atomic rename and never delete or overwrite either side.
    for ($attempt = 1; $attempt -le 20; $attempt++) {
        try {
            [IO.Directory]::Move($Candidate, $Release)
            break
        } catch {
            if ($attempt -eq 20) { throw }
            Start-Sleep -Milliseconds ([Math]::Min(1000, 100 * $attempt))
        }
    }

    $pointerTemp = Join-Path $Root ('.' + (Split-Path -Leaf $CurrentPointer) + ".tmp.$PID")
    $pointerBackup = Join-Path $Root ('.' + (Split-Path -Leaf $CurrentPointer) + ".rollback.$PID")
    Assert-ScopedPath -Path $pointerTemp -Root $Root -Label 'current pointer temp'
    Assert-ScopedPath -Path $pointerBackup -Root $Root -Label 'current pointer rollback'
    if ((Test-Path -LiteralPath $pointerTemp) -or
        (Test-Path -LiteralPath $pointerBackup)) {
        throw 'current pointer temp/rollback path already exists.'
    }

    [IO.File]::WriteAllText($pointerTemp, ($Metadata | ConvertTo-Json -Depth 4),
        [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $CurrentPointer) {
        [IO.File]::Replace($pointerTemp, $CurrentPointer, $pointerBackup, $true)
        try { Remove-Item -LiteralPath $pointerBackup -Force }
        catch { Write-Warning "current pointer rollback cleanup failed: $pointerBackup" }
    } else {
        [IO.File]::Move($pointerTemp, $CurrentPointer)
    }
}

if ($Target -ne 'Game') { throw "unsupported target: $Target" }
Assert-NormalWin32PathSyntax -Path $projectRootArgument -Label 'project root'
Assert-NormalWin32PathSyntax -Path $stageRootArgument -Label 'stage root'
$projectRootLexical = [IO.Path]::GetFullPath($projectRootArgument)
$stageRootLexical = [IO.Path]::GetFullPath($stageRootArgument)
if (-not (Test-Path -LiteralPath $projectRootLexical -PathType Container)) {
    throw "project root is missing: $projectRootLexical"
}
Assert-NoReparseAncestors -Path $projectRootLexical -Label 'project root'
Assert-NoReparseAncestors -Path $stageRootLexical -Label 'stage root'
$projectRoot = Resolve-CanonicalLocalDirectoryPath -Path $projectRootLexical `
    -Label 'project root' -MustExist
$stageRootPath = Resolve-CanonicalLocalDirectoryPath -Path $stageRootLexical `
    -Label 'stage root'
$assetsRoot = Join-Path $projectRoot 'Assets'
$settingsRoot = Join-Path $projectRoot 'ProjectSetting'
Assert-NoReparseAncestors -Path $assetsRoot -Label 'Assets input'
Assert-NoReparseAncestors -Path $settingsRoot -Label 'ProjectSetting input'
$assetsRoot = Resolve-CanonicalLocalDirectoryPath -Path $assetsRoot `
    -Label 'Assets input' -MustExist
$settingsRoot = Resolve-CanonicalLocalDirectoryPath -Path $settingsRoot `
    -Label 'ProjectSetting input' -MustExist
$canonicalScenePath = Join-Path $assetsRoot 'Scenes\FT_Primitives.creator'
if ($InputMode -ne 'Tracked') {
    if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf)) {
        throw "runtime settings template is missing: $templatePath"
    }
    if ($InputMode -eq 'Workspace' -and
        [string]::IsNullOrWhiteSpace($StartupScene) -and
        -not (Test-Path -LiteralPath $canonicalScenePath -PathType Leaf)) {
        throw "canonical startup scene is missing: $canonicalScenePath"
    }
}

$gitCommit = @(& git -C $repoRoot rev-parse HEAD)[0]
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitCommit)) {
    throw 'git rev-parse failed while preparing package provenance.'
}
$workspaceStatus = @(& git -C $repoRoot status --porcelain=v1 --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'git status failed while preparing package provenance.' }
$workspaceDirty = $workspaceStatus.Count -gt 0

Assert-DisjointTrees -Left $stageRootPath -Right $assetsRoot `
    -Label 'stage/assets'
Assert-DisjointTrees -Left $stageRootPath -Right $settingsRoot `
    -Label 'stage/settings'
Assert-NoReparseAncestors -Path $stageRootPath -Label 'stage root'

$projectName = (Split-Path -Leaf $projectRoot) -replace '[<>:"/\\|?*\x00-\x1F]', '_'
if ([string]::IsNullOrWhiteSpace($projectName)) { throw 'project name is empty after sanitization.' }
New-Item -ItemType Directory -Force -Path $stageRootPath | Out-Null
Assert-NoReparseAncestors -Path $stageRootPath -Label 'stage root'
$createdStageRoot = Resolve-CanonicalLocalDirectoryPath -Path $stageRootPath `
    -Label 'stage root' -MustExist
if (-not $createdStageRoot.Equals($stageRootPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "stage root identity changed after creation: $stageRootPath -> $createdStageRoot"
}
$stageRootPath = $createdStageRoot
Assert-DisjointTrees -Left $stageRootPath -Right $assetsRoot -Label 'stage/assets'
Assert-DisjointTrees -Left $stageRootPath -Right $settingsRoot -Label 'stage/settings'
$buildId = [Guid]::NewGuid().ToString('N')
$candidateStage = Join-Path $stageRootPath (".$projectName.$buildId.candidate")
$releaseStage = Join-Path $stageRootPath ("$projectName-$buildId")
$currentPointer = Join-Path $stageRootPath ("$projectName.current.json")
$buildLockPath = Join-Path $stageRootPath (".$projectName.build.lock")
Assert-NoReparseAncestors -Path $candidateStage -Label 'candidate stage'
Assert-NoReparseAncestors -Path $releaseStage -Label 'release stage'
Assert-NoReparseAncestors -Path $currentPointer -Label 'current pointer'
Assert-NoReparseAncestors -Path $buildLockPath -Label 'build lock'
try {
    $buildLock = [IO.File]::Open($buildLockPath, [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch {
    throw "another package build owns the project lock: $buildLockPath"
}
Assert-ScopedPath -Path $candidateStage -Root $stageRootPath -Label 'candidate stage'
if (Test-Path -LiteralPath $candidateStage) {
    $buildLock.Dispose()
    throw "candidate stage already exists; refusing to overwrite it: $candidateStage"
}
New-Item -ItemType Directory -Path $candidateStage | Out-Null
Assert-NoReparseAncestors -Path $candidateStage -Label 'candidate stage'

try {
    Write-Host '[1/6 BuildNative]' -ForegroundColor Cyan
    if ($BuildNative) {
        $msbuild = Find-MSBuild

        # 도구는 항상 Development 다 — 패키지에 실리지 않는다(위 $toolBinaryRoot).
        Invoke-NativeChecked -FilePath $msbuild -Label 'native tool build' -Arguments @(
            $solutionPath, '/m', '/t:Tools\AssetPacker;Tools\AssetCooker', "/p:Configuration=$Config", '/p:Platform=x64',
            '/nologo', '/verbosity:minimal')

        # Player 만 요청된 구성으로 짓는다. -Shipping 이면 CommandService 가
        # ProjectReference 단계에서 빠지므로 소켓 심볼이 링크되지 않는다(§12.1).
        Invoke-NativeChecked -FilePath $msbuild -Label "native Player build ($engineConfigKey)" -Arguments @(
            $solutionPath, '/m', '/t:Player', "/p:Configuration=$Config", '/p:Platform=x64',
            "/p:EngineShipping=$(if ($Shipping) { 'true' } else { 'false' })",
            '/nologo', '/verbosity:minimal')
    } else {
        Write-Host '  skipped (use -BuildNative for a clean/CI native build)'
    }

    $playerSource = Join-Path $playerOutput 'Player.exe'
    $packerSource = Join-Path $packerOutput 'AssetPacker.exe'
    $cookerSource = Join-Path $cookerOutput 'AssetCooker.exe'
    foreach ($requiredBinary in @($playerSource, $packerSource, $cookerSource)) {
        if (-not (Test-Path -LiteralPath $requiredBinary -PathType Leaf)) {
            throw "required native output is missing: $requiredBinary (run with -BuildNative)"
        }
    }

    Write-Host '[2/6 BuildManaged]' -ForegroundColor Cyan
    $dotnet = (Get-Command 'dotnet' -ErrorAction Stop).Source
    Invoke-NativeChecked -FilePath $dotnet -Label 'ScriptCore build' -Arguments @(
        'build', (Join-Path $repoRoot 'ScriptCore\ScriptCore.csproj'), '-c', $Config, '--nologo')
    Invoke-NativeChecked -FilePath $dotnet -Label 'GameScripts build' -Arguments @(
        'build', (Join-Path $repoRoot 'GameScripts\GameScripts.csproj'), '-c', $Config, '--nologo')

    Write-Host '[3/6 Cook]' -ForegroundColor Cyan
    $packageWorkRoot = Join-Path $candidateStage '.package-input'
    $baseRoot = Join-Path $packageWorkRoot 'Base'
    $generatedRoot = Join-Path $packageWorkRoot 'Generated'
    $mergedRoot = Join-Path $packageWorkRoot 'Merged'
    New-Item -ItemType Directory -Force -Path $baseRoot, $generatedRoot, $mergedRoot | Out-Null

    # Cook은 live project가 아니라 실제 package base snapshot을 입력으로 쓴다.
    # 따라서 Tracked/Workspace/Project 모드의 byte closure와 cooked 결과가 어긋나지 않는다.
    $runtimeTemplateSource = $templatePath
    $packageInputRevision = 'WORKTREE'
    $baseCount = if ($InputMode -eq 'Tracked') {
        $snapshotRoot = New-TrackedSnapshot `
            -DestinationRoot (Join-Path $packageWorkRoot 'TrackedSnapshot') `
            -GitCommit $gitCommit
        $snapshotProject = Join-Path $snapshotRoot 'Dynamic_CPP'
        $runtimeTemplateSource = Join-Path $snapshotRoot `
            'Tools\packaging\templates\EngineSettings.runtime.yml'
        $packageInputRevision = $gitCommit
        Copy-ProjectInputs -BaseRoot $baseRoot -ProjectRoot $snapshotProject
    } elseif ($InputMode -eq 'Workspace') {
        Copy-WorkspaceInputs -BaseRoot $baseRoot -ProjectRoot $projectRoot
    } else {
        Copy-ProjectInputs -BaseRoot $baseRoot -ProjectRoot $projectRoot
    }
    $packageAssets = Join-Path $baseRoot 'Assets'
    $authoredDerived = Join-Path $packageAssets 'Derived'
    if (Test-Path -LiteralPath $authoredDerived) {
        throw "package source에 stale/authored Derived tree를 둘 수 없다: $authoredDerived"
    }
    $modelCook = Invoke-AssetCook -AssetCooker $cookerSource `
        -AssetsRoot $packageAssets -OutputRoot (Join-Path $generatedRoot 'Assets')
    $folderSummary = ($script:derivedPathRules | ForEach-Object {
        $count = if ($modelCook.ByFolder.ContainsKey($_.Folder)) { $modelCook.ByFolder[$_.Folder] } else { 0 }
        '{0}={1}' -f $_.Folder, $count
    }) -join ' '
    Write-Host "  Derived: $folderSummary"
    Write-Host "  artifacts=$($modelCook.ArtifactCount), bytes=$($modelCook.ArtifactBytes), CEMF bytes=$($modelCook.ManifestBytes)"
    Write-Host '  B3 전까지 shader는 source HLSL을 pak에 포함하며 precompiled shader cook은 아직 없다.'
    if ($modelCook.LegacyModelCookCaches -gt 0) {
        # ★ §3.6.1: 파생물이 콘텐츠 서브트리 안에 있다. legacy 로더가 아직 이
        #   캐시를 읽으므로 여기서 빼지 않는다 — 끊는 것은 I5/I6의 일이다.
        Write-Host ("  §3.6.1: Assets/Models 안에 legacy 쿠킹 캐시 {0}개가 콘텐츠와 섞여 있다(패키지에 그대로 실린다)." -f
            $modelCook.LegacyModelCookCaches) -ForegroundColor Yellow
    }
    if ($modelCook.LegacyTextureNameRefs -gt 0) {
        # ★ 숨기지 않는다. 이 수가 0이 되어야 D5-c의 "source path 탐색 없이"가
        #   성립한다. 씬 인라인 재질이 아직 texture를 파일명으로 가리킨다.
        Write-Host ("  D5-c 선행: 씬의 legacy texture 이름 참조 {0}건 — GUID 이주 전까지 런타임 이름 폴백에 의존한다." -f
            $modelCook.LegacyTextureNameRefs) -ForegroundColor Yellow
    }

    Write-Host '[4/6 Stage]' -ForegroundColor Cyan
    $runtimeRootFiles = [Collections.Generic.List[string]]::new()
    Copy-OneFile -Source $playerSource -Destination (Join-Path $candidateStage 'Player.exe')
    [void]$runtimeRootFiles.Add('Player.exe')
    foreach ($dllName in @(Get-RuntimeDllNames)) {
        Copy-OneFile -Source (Join-Path $playerOutput $dllName) `
            -Destination (Join-Path $candidateStage $dllName)
        [void]$runtimeRootFiles.Add($dllName)
    }

    $physxGpu = Find-MatchingPhysXGpuRuntime
    Copy-OneFile -Source $physxGpu -Destination (Join-Path $candidateStage 'PhysXGpu_64.dll')
    [void]$runtimeRootFiles.Add('PhysXGpu_64.dll')

    $slangCompiler = Find-SlangRuntime
    Copy-OneFile -Source $slangCompiler `
        -Destination (Join-Path $candidateStage 'slang-compiler.dll')
    [void]$runtimeRootFiles.Add('slang-compiler.dll')
    foreach ($companion in @('dxcompiler.dll', 'dxil.dll')) {
        $source = Join-Path (Split-Path -Parent $slangCompiler) $companion
        Copy-OneFile -Source $source -Destination (Join-Path $candidateStage $companion)
        [void]$runtimeRootFiles.Add($companion)
    }

    $managedSource = $managedOutput
    if (-not (Test-Path -LiteralPath $managedSource -PathType Container)) {
        throw "Managed output is missing: $managedSource"
    }
    $managedFiles = @(Copy-ManagedRuntime -SourceRoot $managedSource `
        -DestinationRoot (Join-Path $candidateStage 'Managed'))
    $runtimeEntries = @(Get-RuntimePayloadEntries -Stage $candidateStage `
        -RootFiles $runtimeRootFiles.ToArray())
    $runtimeDigest = Get-ContentDigest -Entries $runtimeEntries

    Write-Host '[5/6 Pak]' -ForegroundColor Cyan
    $materializedSettings = Join-Path $generatedRoot 'ProjectSetting\EngineSettings.asset'
    Write-MaterializedRuntimeSettings -Template $runtimeTemplateSource `
        -Destination $materializedSettings -RequestedStartupScene $StartupScene `
        -RequestedRenderBackend $RenderBackend
    Merge-PackageInput -BaseRoot $baseRoot -GeneratedRoot $generatedRoot -MergedRoot $mergedRoot

    $mergedCook = Assert-CookOutput -OutputRoot (Join-Path $mergedRoot 'Assets') `
        -ExpectedArtifactPathCount $modelCook.ArtifactCount
    if ($mergedCook.ManifestSha256 -ne $modelCook.ManifestSha256 -or
        $mergedCook.ArtifactBytes -ne $modelCook.ArtifactBytes) {
        throw 'merged package의 Derived tree가 Cook 게시 결과와 다르다.'
    }

    $mergedSettings = Join-Path $mergedRoot 'ProjectSetting\EngineSettings.asset'
    $preflight = Assert-PackagePreflight -MergedRoot $mergedRoot -SettingsPath $mergedSettings
    $runtimeDocumentCook = Invoke-RuntimeDocumentCook -AssetCooker $cookerSource `
        -RuntimeRoot $mergedRoot
    $entries = Get-PackageEntries -MergedRoot $mergedRoot
    if ($entries.Count -eq 0) { throw 'merged package input is empty.' }
    $contentDigest = Get-ContentDigest -Entries $entries
    $logicalDistributionEntries = @($runtimeEntries) + @(
        [pscustomobject][ordered]@{
            path = 'GameAssets.logical'
            bytes = 0
            sha256 = $contentDigest
        })
    $distributionDigest = Get-ContentDigest -Entries `
        (Sort-EntriesOrdinal -Entries $logicalDistributionEntries)
    $manifest = [ordered]@{
        schemaVersion = 2
        workspaceHead = $gitCommit
        workspaceDirty = $workspaceDirty
        packageInputRevision = $packageInputRevision
        nativeSource = 'WORKSPACE'
        nativeBuildRequested = [bool]$BuildNative
        config = $Config
        inputMode = $InputMode
        baseFileCount = $baseCount
        generatedFileCount = 1 + $modelCook.DerivedFileCount
        entryCount = $entries.Count
        contentDigest = $contentDigest
        settingsTemplateSha256 = Get-Sha256 -Path $runtimeTemplateSource
        runtimeSettingsSha256 = Get-Sha256 -Path $mergedSettings
        startupScene = $preflight.StartupScene
        renderBackend = $preflight.RuntimeBackend
        startupSceneSha256 = Get-Sha256 -Path (Join-Path $mergedRoot "Assets\Scenes\$($preflight.StartupScene)")
        startupSceneScriptComponentCount = $preflight.SceneCounts.Script
        managedLifecycleRequired = $preflight.RequiresManagedLifecycle
        distributionPolicy = if ($Config -eq 'Release') {
            'external-runtime-prerequisites'
        } else {
            'development-only'
        }
        runtimePrerequisites = @(
            [ordered]@{ name = '.NET 10 x64 runtime'; bundled = $false },
            [ordered]@{
                name = if ($Config -eq 'Release') {
                    'Microsoft Visual C++ Redistributable x64'
                } else {
                    'Microsoft Visual C++ Debug Runtime x64'
                }
                bundled = $false
            }
        )
        runtimeEntryCount = $runtimeEntries.Count
        runtimeDigest = $runtimeDigest
        distributionDigest = $distributionDigest
        runtimeEntries = $runtimeEntries
        cook = [ordered]@{
            schemaVersion = 1
            producer = 'AssetCooker'
            source = 'package-base/Assets'
            artifactRoot = 'Assets/Derived'
            modelCount = $modelCook.ModelCount
            artifactCount = $modelCook.ArtifactCount
            artifactBytes = $modelCook.ArtifactBytes
            manifestBytes = $modelCook.ManifestBytes
            manifestSha256 = $modelCook.ManifestSha256
            derivedFileCount = $modelCook.DerivedFileCount
            # D5-b2c-5: 종류별 artifact 수와 아직 GUID로 못 그린 참조를 함께
            # 남긴다. 후자는 D5-c 선행 조건의 기계적 판정 근거다.
            byFolder = [ordered]@{
                Models = $modelCook.ByFolder['Models']
                Textures = $modelCook.ByFolder['Textures']
                ShaderMeta = $modelCook.ByFolder['ShaderMeta']
                Materials = $modelCook.ByFolder['Materials']
                Scenes = $modelCook.ByFolder['Scenes']
                Prefabs = $modelCook.ByFolder['Prefabs']
            }
            sourceCounts = $modelCook.SourceCounts
            legacyTextureNameRefs = $modelCook.LegacyTextureNameRefs
            legacyModelCookCaches = $modelCook.LegacyModelCookCaches
            runtimeDocumentCount = $runtimeDocumentCook.DocumentCount
            runtimeDocumentBytes = $runtimeDocumentCook.DocumentBytes
            runtimeDocumentFormat = $runtimeDocumentCook.Format
        }
        verification = 'pending'
        entries = $entries
    }
    $manifestPath = Join-Path $candidateStage 'package-manifest.json'
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6),
        [Text.UTF8Encoding]::new($false))

    $pakPath = Join-Path $candidateStage 'GameAssets.pak'
    $packLog = @(Invoke-NativeChecked -FilePath $packerSource -Label 'AssetPacker' -Arguments @(
        '--assets', (Join-Path $mergedRoot 'Assets'),
        '--settings', (Join-Path $mergedRoot 'ProjectSetting'),
        '--output', $pakPath,
        '--list-entries'))
    foreach ($line in $packLog | Where-Object { $_ -notmatch '^\[PAK-ENTRY\] ' }) {
        Write-Host $line
    }
    if (-not (Test-Path -LiteralPath $pakPath -PathType Leaf) -or
        (Get-Item -LiteralPath $pakPath).Length -le 0) {
        throw 'AssetPacker reported success without a non-empty GameAssets.pak.'
    }
    $packedPaths = @($packLog | Where-Object { $_ -match '^\[PAK-ENTRY\] ' } |
        ForEach-Object { $_.Substring('[PAK-ENTRY] '.Length) })
    if ($packedPaths.Count -ne $entries.Count) {
        throw "AssetPacker entry count mismatch: $($packedPaths.Count) != $($entries.Count)"
    }
    $expectedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $entries) { [void]$expectedPaths.Add([string]$entry.path) }
    $actualPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $packedPaths) { [void]$actualPaths.Add([string]$path) }
    if ($expectedPaths.Count -ne $entries.Count -or
        $actualPaths.Count -ne $packedPaths.Count -or
        -not $expectedPaths.SetEquals($actualPaths)) {
        throw 'AssetPacker reopened entry list differs from the package manifest projection.'
    }
    Write-Host "  pak entries=$($packedPaths.Count), excluded authoring/source files=$((Get-ChildItem -LiteralPath $mergedRoot -File -Recurse).Count - $packedPaths.Count)"
    $manifest['pakFileSha256'] = Get-Sha256 -Path $pakPath
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false))

    Write-Host '[6/6 Verify]' -ForegroundColor Cyan
    if ($SkipVerify) {
        $manifest['verification'] = 'skipped'
        Write-Warning '-SkipVerify candidate는 publish하지 않는다.'
    } else {
        $smoke = Invoke-PlayerSmoke -CandidateStage $candidateStage `
            -ManifestPath $manifestPath -Preflight $preflight
        $manifest['verification'] = 'passed'
        $manifest['smoke'] = [ordered]@{
            exitCode = $smoke.ExitCode
            gameThreadFrames = $smoke.GameThreadFrames
            displayFrame = $smoke.DisplayFrame
            promotions = $smoke.Promotions
            registeredScripts = $smoke.RegisteredScripts
            managedLifecycle = $smoke.ManagedLifecycle
            catalogSource = $smoke.CatalogSource
            metaParsed = $smoke.MetaParsed
            sourceIdentities = $smoke.SourceIdentities
            cookedEntries = $smoke.CookedEntries
			cookedSceneDocuments = $smoke.CookedSceneDocuments
            textParserCalls = $smoke.TextParserCalls
        }
        Write-Host "  scene=$($preflight.StartupScene), cooked scene docs=$($smoke.CookedSceneDocuments), text parser calls=$($smoke.TextParserCalls), promotions=$($smoke.Promotions), managed types=$($smoke.RegisteredScripts), catalog=$($smoke.CatalogSource), identities=$($smoke.SourceIdentities), metaParsed=$($smoke.MetaParsed)"
    }
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false))

    Remove-ScopedTree -Path $packageWorkRoot -Root $candidateStage -Label 'package input'
    Remove-ScopedTree -Path (Join-Path $candidateStage 'Log') `
        -Root $candidateStage -Label 'smoke log directory'
    foreach ($smokeArtifact in @('imgui.ini', 'verify.stdout.log', 'verify.stderr.log')) {
        Remove-ScopedFile -Path (Join-Path $candidateStage $smokeArtifact) `
            -Root $candidateStage -Label 'smoke artifact'
    }
    $postVerifyRuntimeDigest = Get-ContentDigest -Entries `
        (Get-RuntimePayloadEntries -Stage $candidateStage `
            -RootFiles $runtimeRootFiles.ToArray())
    if ($postVerifyRuntimeDigest -ne $runtimeDigest) {
        throw 'Player verification modified the staged runtime payload.'
    }
    if ((Get-Sha256 -Path $pakPath) -ne [string]$manifest.pakFileSha256) {
        throw 'Player verification modified the staged GameAssets.pak.'
    }
    $expectedRootFiles = @($runtimeRootFiles.ToArray()) + @(
        'GameAssets.pak', 'package-manifest.json')
    Assert-DistributionStageClosure -Stage $candidateStage -ExpectedRootFiles $expectedRootFiles
    if ($SkipVerify) {
        $buildLock.Dispose()
        Write-Host "[BUILD] 검증되지 않은 candidate 보존: $candidateStage" -ForegroundColor Yellow
        return
    }
    $publishMetadata = [ordered]@{
        schemaVersion = 2
        releaseDirectory = Split-Path -Leaf $releaseStage
        buildId = $buildId
        workspaceHead = $gitCommit
        workspaceDirty = $workspaceDirty
        packageInputRevision = $packageInputRevision
        config = $Config
        contentDigest = $manifest.contentDigest
        runtimeDigest = $runtimeDigest
        distributionDigest = $distributionDigest
        pakFileSha256 = $manifest.pakFileSha256
        verification = $manifest.verification
    }
    Publish-Stage -Candidate $candidateStage -Release $releaseStage `
        -CurrentPointer $currentPointer -Root $stageRootPath -Metadata $publishMetadata
    $buildLock.Dispose()
    Write-Host "[BUILD] 완료: $releaseStage" -ForegroundColor Green
    Write-Host "[BUILD] current pointer: $currentPointer"
    Write-Host "[BUILD] content digest: $($manifest.contentDigest)"
} catch {
    if ($null -ne $buildLock) { $buildLock.Dispose() }
    Write-Error "[BUILD] 실패; 기존 stage는 보존됨. candidate: $candidateStage`n$($_.Exception.Message)"
    exit 1
}

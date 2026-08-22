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
$engineOutput = if ($Config -eq 'Debug') {
    Join-Path $repoRoot 'x64\Debug'
} else {
    Join-Path $repoRoot 'Bin\Editor'
}

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

function Find-DxcRuntime {
    $candidates = [Collections.Generic.List[string]]::new()
    [void]$candidates.Add((Join-Path $engineOutput 'dxcompiler.dll'))
    [void]$candidates.Add((Join-Path $repoRoot 'ThirdParty\DXC\bin\x64\dxcompiler.dll'))
    if (-not [string]::IsNullOrWhiteSpace($env:VULKAN_SDK)) {
        [void]$candidates.Add((Join-Path $env:VULKAN_SDK 'Bin\dxcompiler.dll'))
    }
    foreach ($directory in ($env:PATH -split [IO.Path]::PathSeparator)) {
        if (-not [string]::IsNullOrWhiteSpace($directory)) {
            [void]$candidates.Add((Join-Path $directory 'dxcompiler.dll'))
        }
    }
    if (Test-Path -LiteralPath 'C:\VulkanSDK' -PathType Container) {
        $sdkCandidates = @(Get-ChildItem -LiteralPath 'C:\VulkanSDK' -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'Bin\dxcompiler.dll' })
        foreach ($candidate in $sdkCandidates) { [void]$candidates.Add($candidate) }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'dxcompiler.dll을 찾지 못했다. SDK-free stage를 만들 수 없다.'
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
            'assimp-vc145-mtd.dll', 'DirectXTex.dll', 'DirectXTK.dll',
            'fmodL.dll', 'fmtd.dll', 'kubazip.dll', 'meshoptimizer.dll',
            'minizipd.dll', 'nethost.dll', 'PhysX_64.dll', 'PhysXCommon_64.dll',
            'PhysXCooking_64.dll', 'PhysXFoundation_64.dll', 'poly2tri.dll',
            'pugixml.dll', 'vulkan-1.dll', 'yaml-cppd.dll', 'zd.dll'
        )
    }
    return @(
        'assimp-vc145-mt.dll', 'DirectXTex.dll', 'DirectXTK.dll',
        'fmodL.dll', 'fmt.dll', 'kubazip.dll', 'meshoptimizer.dll',
        'minizip.dll', 'nethost.dll', 'PhysX_64.dll', 'PhysXCommon_64.dll',
        'PhysXCooking_64.dll', 'PhysXFoundation_64.dll', 'poly2tri.dll',
        'pugixml.dll', 'vulkan-1.dll', 'yaml-cpp.dll', 'z.dll'
    )
}

function Find-MatchingPhysXGpuRuntime {
    $outputGpu = Join-Path $engineOutput 'PhysXGpu_64.dll'
    if (Test-Path -LiteralPath $outputGpu -PathType Leaf) {
        return [IO.Path]::GetFullPath($outputGpu)
    }

    $outputCore = Join-Path $engineOutput 'PhysX_64.dll'
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

function Get-PackageEntries {
    param([Parameter(Mandatory)][string]$MergedRoot)

    $entries = @(Get-ChildItem -LiteralPath $MergedRoot -File -Recurse |
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
    $sceneMarkerPattern = 'Scene loaded:[^\r\n]*' +
        [regex]::Escape([string]$Preflight.StartupScene)
    if ($combined -notmatch $sceneMarkerPattern) {
        throw 'Player smoke lacks the startup scene success marker.'
    }
    if ($combined -notmatch '\[ScriptCore\]\s*초기화 완료') {
        throw 'Player smoke lacks the ScriptCore initialization marker.'
    }
    $scriptRegistration = [regex]::Match($combined, '\[ScriptCore\]\s*스크립트\s+([1-9]\d*)종\s+등록')
    if (-not $scriptRegistration.Success) {
        throw 'Player smoke did not register any managed script types.'
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
        RegisteredScripts = [int]$scriptRegistration.Groups[1].Value
        ManagedLifecycle = $managedLifecycle
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
        Invoke-NativeChecked -FilePath $msbuild -Label 'native Player/tool build' -Arguments @(
            $solutionPath, '/m', '/t:AssetPacker;Player', "/p:Configuration=$Config", '/p:Platform=x64',
            '/nologo', '/verbosity:minimal')
    } else {
        Write-Host '  skipped (use -BuildNative for a clean/CI native build)'
    }

    $playerSource = Join-Path $engineOutput 'Player.exe'
    $packerSource = Join-Path $engineOutput 'AssetPacker.exe'
    foreach ($requiredBinary in @($playerSource, $packerSource)) {
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
    Write-Host '  B3 전까지 사전 컴파일 셰이더 cook은 비어 있으며, HLSL source를 pak에 포함한다.'

    Write-Host '[4/6 Stage]' -ForegroundColor Cyan
    $runtimeRootFiles = [Collections.Generic.List[string]]::new()
    Copy-OneFile -Source $playerSource -Destination (Join-Path $candidateStage 'Player.exe')
    [void]$runtimeRootFiles.Add('Player.exe')
    foreach ($dllName in @(Get-RuntimeDllNames)) {
        Copy-OneFile -Source (Join-Path $engineOutput $dllName) `
            -Destination (Join-Path $candidateStage $dllName)
        [void]$runtimeRootFiles.Add($dllName)
    }

    $physxGpu = Find-MatchingPhysXGpuRuntime
    Copy-OneFile -Source $physxGpu -Destination (Join-Path $candidateStage 'PhysXGpu_64.dll')
    [void]$runtimeRootFiles.Add('PhysXGpu_64.dll')

    $dxcCompiler = Find-DxcRuntime
    Copy-OneFile -Source $dxcCompiler -Destination (Join-Path $candidateStage 'dxcompiler.dll')
    [void]$runtimeRootFiles.Add('dxcompiler.dll')
    $dxcDxil = Join-Path (Split-Path -Parent $dxcCompiler) 'dxil.dll'
    if (Test-Path -LiteralPath $dxcDxil -PathType Leaf) {
        Copy-OneFile -Source $dxcDxil -Destination (Join-Path $candidateStage 'dxil.dll')
        [void]$runtimeRootFiles.Add('dxil.dll')
    }

    $managedSource = Join-Path $engineOutput 'Managed'
    if (-not (Test-Path -LiteralPath $managedSource -PathType Container)) {
        throw "Managed output is missing: $managedSource"
    }
    $managedFiles = @(Copy-ManagedRuntime -SourceRoot $managedSource `
        -DestinationRoot (Join-Path $candidateStage 'Managed'))
    $runtimeEntries = @(Get-RuntimePayloadEntries -Stage $candidateStage `
        -RootFiles $runtimeRootFiles.ToArray())
    $runtimeDigest = Get-ContentDigest -Entries $runtimeEntries

    Write-Host '[5/6 Pak]' -ForegroundColor Cyan
    $packageWorkRoot = Join-Path $candidateStage '.package-input'
    $baseRoot = Join-Path $packageWorkRoot 'Base'
    $generatedRoot = Join-Path $packageWorkRoot 'Generated'
    $mergedRoot = Join-Path $packageWorkRoot 'Merged'
    New-Item -ItemType Directory -Force -Path $baseRoot, $generatedRoot, $mergedRoot | Out-Null

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
    $materializedSettings = Join-Path $generatedRoot 'ProjectSetting\EngineSettings.asset'
    Write-MaterializedRuntimeSettings -Template $runtimeTemplateSource `
        -Destination $materializedSettings -RequestedStartupScene $StartupScene `
        -RequestedRenderBackend $RenderBackend
    Merge-PackageInput -BaseRoot $baseRoot -GeneratedRoot $generatedRoot -MergedRoot $mergedRoot

    $mergedSettings = Join-Path $mergedRoot 'ProjectSetting\EngineSettings.asset'
    $preflight = Assert-PackagePreflight -MergedRoot $mergedRoot -SettingsPath $mergedSettings
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
        generatedFileCount = 1
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
        verification = 'pending'
        entries = $entries
    }
    $manifestPath = Join-Path $candidateStage 'package-manifest.json'
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6),
        [Text.UTF8Encoding]::new($false))

    $pakPath = Join-Path $candidateStage 'GameAssets.pak'
    Invoke-NativeChecked -FilePath $packerSource -Label 'AssetPacker' -Arguments @(
        '--assets', (Join-Path $mergedRoot 'Assets'),
        '--settings', (Join-Path $mergedRoot 'ProjectSetting'),
        '--output', $pakPath)
    if (-not (Test-Path -LiteralPath $pakPath -PathType Leaf) -or
        (Get-Item -LiteralPath $pakPath).Length -le 0) {
        throw 'AssetPacker reported success without a non-empty GameAssets.pak.'
    }
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
        }
        Write-Host "  scene=$($preflight.StartupScene), promotions=$($smoke.Promotions), managed types=$($smoke.RegisteredScripts)"
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

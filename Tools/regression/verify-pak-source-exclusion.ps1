[CmdletBinding()]
param([string]$AssetPacker = '')

# SerializationPlan D1(Y-4) — 네이티브 스크립트 소스가 배포물에 실리지 않는지.
#
# ★ 이 게이트는 **합성 트리**로만 판정한다. 현재 저장소의 실제 pak 입력 루트
#   (`Dynamic_CPP/Assets`, `Dynamic_CPP/ProjectSetting`)에는 `.cpp/.h/.hpp`가
#   **0개**다(2026-08-30 실측). 실자산으로 재면 "0개를 걸렀다"가 나오고 그것은
#   필터가 있든 없든 참이라 아무것도 증명하지 않는다. 그래서 오염된 트리를
#   일부러 만들어 필터를 밟는다.
#
# ★ 과잉 필터도 함께 잡는다. `.hlsl`/`.hlsli`는 pak에 **실려야** 한다 — 현재 패키지는
#   셰이더 소스와 Slang/DXC DLL을 싣고 Player가 런타임에 컴파일한다
#   (BuildPipelinePlan B3). `.meta`도 D5 매니페스트 전까지 실려야 한다.
#   누락만 보고 과잉을 안 보면, 게임이 아무것도 못 그리는 pak이 초록으로 지나간다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($AssetPacker)) {
    $AssetPacker = Join-Path $repoRoot 'Bin\x64-Release\Tools\AssetPacker\AssetPacker.exe'
}
$packerPath = [IO.Path]::GetFullPath($AssetPacker)
if (-not (Test-Path -LiteralPath $packerPath -PathType Leaf)) {
    "AssetPacker가 없다: $packerPath"
    exit 1
}

function Invoke-AssetPacker {
    param(
        [Parameter(Mandatory)][string]$Assets,
        [Parameter(Mandatory)][string]$Settings,
        [Parameter(Mandatory)][string]$Output
    )
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $packerPath
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('--assets', $Assets, '--settings', $Settings, '--output', $Output,
                            '--list-entries')) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    [pscustomobject]@{ ExitCode = $process.ExitCode; Output = ($stdout + $stderr) }
}

function New-CleanTree {
    param([Parameter(Mandatory)][string]$Path)
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("CE_D1PakFilter_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
try {
    # ── 트리 구성 ────────────────────────────────────────────────────────────
    # cleanAssets/cleanSettings : 실려야 하는 것만
    # dirtyAssets/dirtySettings : 같은 내용 + 실리면 안 되는 소스 파일
    $cleanAssets   = Join-Path $work 'clean\Assets'
    $cleanSettings = Join-Path $work 'clean\ProjectSetting'
    $dirtyAssets   = Join-Path $work 'dirty\Assets'
    $dirtySettings = Join-Path $work 'dirty\ProjectSetting'
    foreach ($d in @($cleanAssets, $cleanSettings, $dirtyAssets, $dirtySettings)) { New-CleanTree $d }

    # 실려야 하는 파일들. 셰이더 소스와 sidecar가 여기 있는 것이 요점이다.
    $keepAssets = @{
        'Scenes/Probe.creator'          = 'scene: probe'
        'Scenes/Probe.creator.meta'     = 'guid: 00000000-0000-4000-8000-000000000001'
        'Shaders/Probe.hlsl'            = 'float4 main() : SV_Target { return 0; }'
        'Shaders/Probe.hlsli'           = '#define PROBE 1'
        'Textures/Probe.png'            = 'not-a-real-png'
    }
    $keepSettings = @{
        'EngineSettings.asset' = 'settings: probe'
    }
    # 실리면 안 되는 것들. 대문자 확장자를 섞어 대소문자 처리를 함께 밟는다.
    $dropFiles = @{
        'Scripts/Probe.cpp'   = 'int main() { return 0; }'
        'Scripts/Probe.h'     = '#pragma once'
        'Scripts/Probe.hpp'   = '#pragma once'
        'Scripts/UPPER.CPP'   = 'int upper() { return 0; }'
    }

    function Write-TreeFiles {
        param([string]$Root, [hashtable]$Files)
        foreach ($relative in $Files.Keys) {
            $full = Join-Path $Root $relative
            $parent = Split-Path -Parent $full
            if (-not (Test-Path -LiteralPath $parent)) {
                New-Item -ItemType Directory -Path $parent -Force | Out-Null
            }
            Set-Content -LiteralPath $full -Value $Files[$relative] -Encoding UTF8 -NoNewline
        }
    }

    Write-TreeFiles -Root $cleanAssets   -Files $keepAssets
    Write-TreeFiles -Root $cleanSettings -Files $keepSettings
    Write-TreeFiles -Root $dirtyAssets   -Files $keepAssets
    Write-TreeFiles -Root $dirtySettings -Files $keepSettings
    # 오염은 두 루트 모두에 넣는다 — CollectFiles는 하나지만, 호출이 둘이라
    # 한쪽에만 필터가 걸리는 실수를 이 게이트가 봐야 한다.
    Write-TreeFiles -Root $dirtyAssets   -Files $dropFiles
    Write-TreeFiles -Root $dirtySettings -Files $dropFiles

    $expectedEntries = $keepAssets.Count + $keepSettings.Count
    $droppedPlanted  = $dropFiles.Count * 2

    $cleanPak = Join-Path $work 'clean.pak'
    $dirtyPak = Join-Path $work 'dirty.pak'

    $cleanRun = Invoke-AssetPacker -Assets $cleanAssets -Settings $cleanSettings -Output $cleanPak
    if ($cleanRun.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $cleanPak -PathType Leaf)) {
        $failures.Add("clean 트리 패킹 실패: $($cleanRun.Output)")
    }
    $dirtyRun = Invoke-AssetPacker -Assets $dirtyAssets -Settings $dirtySettings -Output $dirtyPak
    if ($dirtyRun.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $dirtyPak -PathType Leaf)) {
        $failures.Add("dirty 트리 패킹 실패: $($dirtyRun.Output)")
    }

    if ($failures.Count -eq 0) {
        $cleanCount = [regex]::Match($cleanRun.Output, '\[PAK\] packaged (\d+) sorted entries')
        $dirtyCount = [regex]::Match($dirtyRun.Output, '\[PAK\] packaged (\d+) sorted entries')
        if (-not $cleanCount.Success -or -not $dirtyCount.Success) {
            $failures.Add('entry 수 라인을 읽지 못했다 — AssetPacker 출력 형식이 바뀌었다')
        } else {
            $cleanEntries = [int]$cleanCount.Groups[1].Value
            $dirtyEntries = [int]$dirtyCount.Groups[1].Value

            # ① 실려야 할 것이 실제로 실렸는가. 이 단정이 없으면 "전부 걸러낸" pak도 통과한다.
            if ($cleanEntries -ne $expectedEntries) {
                $failures.Add("clean entry 수가 $cleanEntries — $expectedEntries 를 기대했다 (hlsl/hlsli/meta가 빠졌을 수 있다)")
            }
            # ② 오염된 트리가 같은 수를 낸다 = 소스가 전부 걸러졌다.
            if ($dirtyEntries -ne $cleanEntries) {
                $failures.Add("dirty entry 수가 $dirtyEntries, clean은 $cleanEntries — 소스 $droppedPlanted 개 중 일부가 실렸다")
            }

            # ③ 실제 목록 대조.
            #
            # ★ 처음에는 두 pak의 SHA-256 동일성을 단정했는데, **pak은 결정적이지 않다** —
            #   같은 입력을 두 번 패킹해도 hash가 다르다(실측). 자를 먼저 검증하지 않고
            #   "같아야 한다"고 적었던 것이고, 그래서 게이트가 필터가 아니라 자기 전제
            #   때문에 빨개졌다. 수 비교만으로는 "빠져야 할 것이 들어오고 실려야 할 것이
            #   빠진" 같은 수의 pak을 못 잡으므로, 이제 `--list-entries`가 내보내는
            #   **reopen된 pak의 실제 목록**을 대조한다.
            $cleanList = @([regex]::Matches($cleanRun.Output, '\[PAK-ENTRY\] (.+)') |
                ForEach-Object { $_.Groups[1].Value.Trim() }) | Sort-Object
            $dirtyList = @([regex]::Matches($dirtyRun.Output, '\[PAK-ENTRY\] (.+)') |
                ForEach-Object { $_.Groups[1].Value.Trim() }) | Sort-Object

            if ($cleanList.Count -ne $cleanEntries) {
                $failures.Add("--list-entries 목록이 $($cleanList.Count)건인데 entry 수는 $cleanEntries — 옵션이 반영되지 않은 낡은 exe일 수 있다")
            }
            $listDiff = Compare-Object -ReferenceObject $cleanList -DifferenceObject $dirtyList
            if ($null -ne $listDiff) {
                $failures.Add("clean/dirty 목록이 다르다: " + (($listDiff | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) -join '; '))
            }
            # 목록 자체에 소스 확장자가 없어야 한다 — 위 두 단정과 독립적인 직접 확인이다.
            $leakedSources = @($dirtyList | Where-Object { $_ -match '\.(cpp|h|hpp)$' })
            if ($leakedSources.Count -gt 0) {
                $failures.Add("소스가 pak에 실렸다: " + ($leakedSources -join ', '))
            }
            # 셰이더 소스와 sidecar는 반드시 남아야 한다(과잉 필터 검출).
            foreach ($required in @('Assets/Shaders/Probe.hlsl', 'Assets/Shaders/Probe.hlsli',
                                    'Assets/Scenes/Probe.creator.meta')) {
                if ($dirtyList -notcontains $required) {
                    $failures.Add("실려야 할 파일이 빠졌다: $required")
                }
            }

            "plantedSources=$droppedPlanted cleanEntries=$cleanEntries dirtyEntries=$dirtyEntries expected=$expectedEntries"
            "listedEntries=$($dirtyList.Count) leakedSources=$($leakedSources.Count) listIdentical=$($null -eq $listDiff)"
        }
    }
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($failures.Count -gt 0) {
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — 네이티브 소스 8건이 두 루트 모두에서 배제되고, 셰이더 소스/sidecar는 그대로 실렸다'
exit 0

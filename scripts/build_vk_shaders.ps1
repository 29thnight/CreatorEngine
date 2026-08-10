# Vulkan 골격 셰이더 → SPIR-V → C 헤더.
#
# ★ 왜 미리 뽑아 박는가: DX12 는 D3DCompiler_47.dll 이 Windows 에 있어 HLSL 을
#   런타임에 컴파일하지만, Vulkan 은 OS 가 주는 컴파일러가 없다. 배포하는
#   실행 파일이 SPIR-V 를 들고 있어야 한다. 이 차이가 V5(셰이더 컴파일
#   중립화)가 다뤄야 할 것의 실물이다.
#
# ★ 결과 헤더를 커밋한다. 빌드 단계로 만들면 SDK 가 없는 기계에서 리포가
#   빌드되지 않는다 — 골격 하나 때문에 전체 빌드에 SDK 를 요구할 수 없다.
#   대신 재생성 명령이 이 스크립트로 남는다.
#
# 사용: pwsh scripts/build_vk_shaders.ps1

$ErrorActionPreference = 'Stop'

$sdk = $env:VULKAN_SDK
if (-not $sdk) { $sdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "Machine") }
if (-not $sdk) { throw "VULKAN_SDK 가 없다 — Vulkan SDK 를 설치해야 한다" }

$dxc = Join-Path $sdk "Bin\dxc.exe"
if (-not (Test-Path $dxc)) { throw "dxc 를 찾을 수 없다: $dxc" }

$root   = Split-Path $PSScriptRoot -Parent
$shader = Join-Path $root "RenderEngine\RHI\Vulkan\Shaders\VkTriangle.hlsl"
$output = Join-Path $root "RenderEngine\RHI\Vulkan\Shaders\VkTriangleSpv.h"
$temp   = Join-Path $env:TEMP "vkshaders"
New-Item -ItemType Directory -Force -Path $temp | Out-Null

function Convert-SpvToArray {
    param([string]$Path, [string]$Name)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length % 4 -ne 0) { throw "$Path 의 길이가 4의 배수가 아니다" }

    $words = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $bytes.Length; $i += 4) {
        $w = [System.BitConverter]::ToUInt32($bytes, $i)
        $words.Add(("0x{0:x8}u" -f $w))
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("constexpr uint32_t $Name[] = {")
    for ($i = 0; $i -lt $words.Count; $i += 6) {
        $slice = $words[$i..([Math]::Min($i + 5, $words.Count - 1))]
        [void]$sb.AppendLine("    " + ($slice -join ", ") + ",")
    }
    [void]$sb.AppendLine("};")
    return $sb.ToString()
}

# -fvk-invert-y 를 쓰지 않는다.
#
# ★ Vulkan 은 클립 공간 Y 가 아래로 향하고 D3D 는 위로 향한다. dxc 가
#   -fvk-invert-y 로 그것을 셰이더에서 뒤집어 줄 수 있지만, 그러면 뒤집기가
#   셰이더 바이너리 안에 숨는다. 골격은 뷰포트 높이를 음수로 주어(VK_KHR_
#   maintenance1, 1.1 코어) 백엔드 쪽에서 뒤집는다 — 어느 층이 좌표계를
#   맞추는지가 계약의 문제이지 셰이더의 문제가 아니기 때문이다.
# 인자를 따옴표로 감싼다 — PowerShell 이 vulkan1.3 의 점을 멤버 접근으로 읽어
# vulkan1 까지만 넘긴다.
& $dxc -T vs_6_0 -E VSMain -spirv "-fspv-target-env=vulkan1.3" `
    $shader -Fo (Join-Path $temp "vs.spv")
& $dxc -T ps_6_0 -E PSMain -spirv "-fspv-target-env=vulkan1.3" `
    $shader -Fo (Join-Path $temp "ps.spv")

$header = @"
#pragma once
#include <cstdint>

// 자동 생성 — 손으로 고치지 말 것.
// 재생성: pwsh scripts/build_vk_shaders.ps1
// 원본:   RenderEngine/RHI/Vulkan/Shaders/VkTriangle.hlsl

$(Convert-SpvToArray -Path (Join-Path $temp "vs.spv") -Name "kVkTriangleVsSpv")
$(Convert-SpvToArray -Path (Join-Path $temp "ps.spv") -Name "kVkTrianglePsSpv")
"@

Set-Content -Path $output -Value $header -Encoding UTF8
Write-Host "생성 완료: $output"

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
# ── 레지스터 시프트를 헤더에서 읽어 온다 (V8-b) ──
#
# ★ 여기에 숫자를 적으면 그것이 곧 두 벌이다. HLSL 의 b·t·u·s 는 각각 별개
#   이름공간인데 SPIR-V 는 binding 하나뿐이라, 굽는 쪽(이 스크립트)과 레이아웃을
#   만드는 쪽(VulkanPipelineCache)이 **같은 규약**을 써야 한다. 어긋나면
#   디스크립터가 안 걸린 채로 삼각형이 그려진다 — 조용히 틀리는 부류다.
#   그래서 값은 VulkanBindingModel.h 한 벌만 두고 여기서 읽는다.
$bindingModel = Join-Path $root "RenderEngine\RHI\Vulkan\VulkanBindingModel.h"
if (-not (Test-Path $bindingModel)) { throw "바인딩 규약 헤더가 없다: $bindingModel" }
$modelText = Get-Content -LiteralPath $bindingModel -Raw

function Get-Shift {
    param([string]$Name)
    $m = [regex]::Match($modelText, "constexpr\s+uint32_t\s+$Name\s*=\s*(\d+)\s*;")
    if (-not $m.Success) { throw "$Name 을 VulkanBindingModel.h 에서 못 읽었다" }
    return $m.Groups[1].Value
}

$bShift = Get-Shift "kConstantBufferShift"
$tShift = Get-Shift "kShaderResourceShift"
$uShift = Get-Shift "kUnorderedAccessShift"
$sShift = Get-Shift "kSamplerShift"
Write-Host "레지스터 시프트 — b=$bShift t=$tShift u=$uShift s=$sShift (VulkanBindingModel.h)"

# 공간 0 만 준다. HLSL 의 register space 를 이 리포가 쓰지 않는다(실측: 0건).
$shiftArgs = @(
    "-fvk-b-shift", $bShift, "0",
    "-fvk-t-shift", $tShift, "0",
    "-fvk-u-shift", $uShift, "0",
    "-fvk-s-shift", $sShift, "0"
)

# 인자를 따옴표로 감싼다 — PowerShell 이 vulkan1.3 의 점을 멤버 접근으로 읽어
# vulkan1 까지만 넘긴다.
& $dxc -T vs_6_0 -E VSMain -spirv "-fspv-target-env=vulkan1.3" @shiftArgs `
    $shader -Fo (Join-Path $temp "vs.spv")
if ($LASTEXITCODE -ne 0) { throw "VSMain 컴파일 실패" }
& $dxc -T ps_6_0 -E PSMain -spirv "-fspv-target-env=vulkan1.3" @shiftArgs `
    $shader -Fo (Join-Path $temp "ps.spv")
if ($LASTEXITCODE -ne 0) { throw "PSMain 컴파일 실패" }

# ★ 텍스처를 안 만지는 짝 (5c-4d). 레이아웃이 Cbv(0) 하나뿐인 파이프라인을
#   만들 수 있어야 자가 검증이 상수 버퍼 경로를 실제로 부를 수 있다.
#
# ★ **진입점 이름을 PSMain 으로 되돌려 굽는다.** 여기가 두 API 가 갈리는 자리다:
#   DX12 는 진입점을 **컴파일할 때** 고르고 그 뒤의 블롭은 이름을 모른다.
#   SPIR-V 는 `OpEntryPoint` 로 이름을 들고 다니고 파이프라인 생성이 그 이름을
#   **다시** 요구한다. 그래서 이름이 계약에 없는 것(RHIGraphicsPipelineDesc 에
#   진입점 칸이 없다)이 DX12 모델을 물려받은 결과인데, 계약에 칸을 더하는 대신
#   **굽는 쪽이 이름을 규약으로 맞춘다** — 소비자가 하나뿐인 채로 계약을
#   넓히지 않는다(§1.1). 검증 레이어가 이 자리를 정확히 짚어 줬다.
& $dxc -T ps_6_0 -E PSMainTint -spirv "-fspv-target-env=vulkan1.3" `
    "-fspv-entrypoint-name=PSMain" @shiftArgs `
    $shader -Fo (Join-Path $temp "ps_tint.spv")
if ($LASTEXITCODE -ne 0) { throw "PSMainTint 컴파일 실패" }

$header = @"
#pragma once
#include <cstdint>

// 자동 생성 — 손으로 고치지 말 것.
// 재생성: pwsh scripts/build_vk_shaders.ps1
// 원본:   RenderEngine/RHI/Vulkan/Shaders/VkTriangle.hlsl

$(Convert-SpvToArray -Path (Join-Path $temp "vs.spv") -Name "kVkTriangleVsSpv")
$(Convert-SpvToArray -Path (Join-Path $temp "ps.spv") -Name "kVkTrianglePsSpv")
$(Convert-SpvToArray -Path (Join-Path $temp "ps_tint.spv") -Name "kVkTriangleTintPsSpv")
"@

Set-Content -Path $output -Value $header -Encoding UTF8
Write-Host "생성 완료: $output"

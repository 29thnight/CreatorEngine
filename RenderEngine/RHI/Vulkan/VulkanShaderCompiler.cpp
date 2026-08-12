#ifndef DYNAMICCPP_EXPORTS
#include "VulkanShaderCompiler.h"
#include "VulkanBindingModel.h"
#include "../RHIShaderSource.h"

#include <Windows.h>
#include <d3dcommon.h>   // D3D_SHADER_MACRO 의 정의 — 헤더는 전방 선언만 한다
#include <dxcapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::atomic<bool> g_vkShaderCompilerActive{ false };

    HMODULE               g_vkDxcModule = nullptr;
    DxcCreateInstanceProc g_vkDxcCreate = nullptr;

    /// dxcompiler.dll 을 싣는다. VULKAN_SDK 쪽을 먼저 찾는다 — Windows SDK 의
    /// 것은 SPIR-V CodeGen 이 꺼져 있다(헤더 ★).
    bool VkShaderLoadDxc(std::string& outError)
    {
        if (nullptr != g_vkDxcCreate) return true;

        char sdk[MAX_PATH]{};
        size_t written = 0;
        if (0 != getenv_s(&written, sdk, sizeof(sdk), "VULKAN_SDK") || 0 == written)
        {
            // ★ 프로세스 환경에 없으면 머신 레지스트리를 본다. SDK 설치가
            //   로그인 세션 시작 **뒤**면 돌고 있는 셸과 그 자식들은 낡은
            //   환경을 상속한다 — build_vk_shaders.ps1 이 같은 함정을 밟고
            //   같은 폴백을 적어 두었고, 실행 파일 쪽에서도 실측으로 밟았다.
            DWORD bytes = sizeof(sdk);
            if (ERROR_SUCCESS != RegGetValueA(HKEY_LOCAL_MACHINE,
                "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                "VULKAN_SDK", RRF_RT_REG_SZ, nullptr, sdk, &bytes))
            {
                sdk[0] = '\0';
            }
        }
        if ('\0' != sdk[0])
        {
            const std::string path = std::string(sdk) + "\\Bin\\dxcompiler.dll";
            g_vkDxcModule = LoadLibraryA(path.c_str());
        }
        if (nullptr == g_vkDxcModule)
        {
            // SDK 가 없으면 검색 경로의 것을 쓴다 — SPIR-V 가 꺼진 판이면
            // 아래 컴파일이 -spirv 를 거부하며 그 말이 그대로 나온다.
            g_vkDxcModule = LoadLibraryA("dxcompiler.dll");
        }
        if (nullptr == g_vkDxcModule)
        {
            outError = "dxcompiler.dll 을 찾지 못했다 — Vulkan SDK 가 필요하다";
            return false;
        }

        g_vkDxcCreate = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(g_vkDxcModule, "DxcCreateInstance"));
        if (nullptr == g_vkDxcCreate)
        {
            outError = "DxcCreateInstance 진입점이 없다";
            return false;
        }
        return true;
    }

    std::wstring VkShaderWiden(std::string_view narrow)
    {
        return std::wstring(narrow.begin(), narrow.end());
    }

    /// "vs_5_0" → L"vs_6_0". dxc 는 SM6 부터다 — 단계만 취하고 판은 올린다.
    ///
    /// ★ 판을 올려도 되는 근거: 이 저장소의 패스 셰이더는 SM5 기능 집합만
    ///   쓰고(fxc 로 굽고 있으므로 그럴 수밖에 없다), SM6 은 그 상위 집합이다.
    ///   거꾸로(6→5)는 안 된다.
    std::wstring VkShaderMapTarget(const char* dx12Target, std::string& outError)
    {
        const std::string target(dx12Target ? dx12Target : "");
        if (target.size() < 2)
        {
            outError = "셰이더 타깃이 비었다";
            return {};
        }
        return VkShaderWiden(target.substr(0, 2)) + L"_6_0";
    }
}

void VulkanShaderCompiler::SetActive(bool active)
{
    g_vkShaderCompilerActive.store(active, std::memory_order_relaxed);
}

bool VulkanShaderCompiler::IsActive()
{
    return g_vkShaderCompilerActive.load(std::memory_order_relaxed);
}

bool VulkanShaderCompiler::CompileFile(std::string_view name, const char* entryPoint,
    const char* dx12Target, const _D3D_SHADER_MACRO* defines,
    RHIShaderBlob& outBlob, std::string& outError)
{
    if (!VkShaderLoadDxc(outError)) return false;

    std::string source;
    if (!RHIShaderSource::Load(name, source, outError)) return false;

    const std::wstring target = VkShaderMapTarget(dx12Target, outError);
    if (target.empty()) return false;

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(g_vkDxcCreate(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))
        || FAILED(g_vkDxcCreate(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
    {
        outError = "dxc 인스턴스 생성 실패";
        return false;
    }

    // 인클루드는 소스 파일 위치 기준으로 푼다 — D3DCompile 쪽과 같은 계약.
    ComPtr<IDxcIncludeHandler> includes;
    if (FAILED(utils->CreateDefaultIncludeHandler(&includes)))
    {
        outError = "dxc 인클루드 핸들러 생성 실패";
        return false;
    }

    const std::filesystem::path sourcePath = RHIShaderSource::Resolve(name);
    const std::wstring sourceName = sourcePath.wstring();
    const std::wstring includeDir = sourcePath.parent_path().wstring();
    const std::wstring entry = VkShaderWiden(entryPoint ? entryPoint : "");

    // ── 인자 ──
    //
    // ★ 시프트 값은 `VulkanBindingModel.h` 의 그 한 벌이다. 골격 스크립트
    //   (build_vk_shaders.ps1)가 같은 헤더를 정규식으로 읽는다 — 여기가 세
    //   번째 소비자이고, 숫자를 직접 적으면 그것이 곧 두 벌이다.
    //
    // ★ -fvk-invert-y 를 쓰지 않는다. 좌표계는 백엔드(음수 뷰포트)가 맞춘다 —
    //   스크립트가 적어 둔 판단 그대로다.
    const std::wstring shiftB = std::to_wstring(VulkanBindingModel::kConstantBufferShift);
    const std::wstring shiftT = std::to_wstring(VulkanBindingModel::kShaderResourceShift);
    const std::wstring shiftU = std::to_wstring(VulkanBindingModel::kUnorderedAccessShift);
    const std::wstring shiftS = std::to_wstring(VulkanBindingModel::kSamplerShift);

    std::vector<const wchar_t*> arguments = {
        sourceName.c_str(),                      // 진단에 찍히는 이름
        L"-E", entry.c_str(),
        L"-T", target.c_str(),
        L"-spirv",
        L"-fspv-target-env=vulkan1.3",
        L"-fvk-b-shift", shiftB.c_str(), L"0",
        L"-fvk-t-shift", shiftT.c_str(), L"0",
        L"-fvk-u-shift", shiftU.c_str(), L"0",
        L"-fvk-s-shift", shiftS.c_str(), L"0",
        L"-I", includeDir.c_str(),
    };

    // D3D_SHADER_MACRO 목록을 -D 로 옮긴다. 수명은 이 함수 동안이다.
    std::vector<std::wstring> defineStorage;
    if (nullptr != defines)
    {
        for (const _D3D_SHADER_MACRO* macro = defines;
            nullptr != macro->Name; ++macro)
        {
            std::wstring define = VkShaderWiden(macro->Name);
            if (nullptr != macro->Definition && '\0' != macro->Definition[0])
            {
                define += L"=" + VkShaderWiden(macro->Definition);
            }
            defineStorage.push_back(std::move(define));
        }
        for (const std::wstring& define : defineStorage)
        {
            arguments.push_back(L"-D");
            arguments.push_back(define.c_str());
        }
    }

    DxcBuffer buffer{};
    buffer.Ptr = source.data();
    buffer.Size = source.size();
    buffer.Encoding = DXC_CP_UTF8;

    ComPtr<IDxcResult> result;
    const HRESULT compiled = compiler->Compile(&buffer, arguments.data(),
        static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result));

    // 진단은 성공해도 꺼낸다 — 경고가 실패의 예고인 경우가 많다.
    std::string diagnostics;
    if (nullptr != result)
    {
        ComPtr<IDxcBlobUtf8> errors;
        if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))
            && nullptr != errors && 0 != errors->GetStringLength())
        {
            diagnostics = errors->GetStringPointer();
        }
    }

    HRESULT status = compiled;
    if (SUCCEEDED(compiled) && nullptr != result) result->GetStatus(&status);

    if (FAILED(status) || nullptr == result)
    {
        outError = std::string(name) + " (" + (entryPoint ? entryPoint : "")
            + "/SPIR-V) 컴파일 실패: " + (diagnostics.empty() ? "원인 미상" : diagnostics);
        return false;
    }

    ComPtr<IDxcBlob> object;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr))
        || nullptr == object || 0 == object->GetBufferSize())
    {
        outError = std::string(name) + " — SPIR-V 산출물이 비었다";
        return false;
    }

    // 여기서 백엔드 타입이 끝난다. 밖으로 나가는 것은 바이트뿐이다 —
    // DX12ShaderCompiler 와 같은 문장, 같은 계약이다.
    outBlob.Assign(object->GetBufferPointer(), object->GetBufferSize());
    return true;
}

#endif

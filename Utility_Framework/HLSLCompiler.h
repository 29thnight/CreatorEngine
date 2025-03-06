#pragma once
#include "Core.Minimal.h"

class IncludeHandler : public ID3DInclude
{
public:
    IncludeHandler(const std::string_view& shaderPath) : m_shaderPath(shaderPath) {}
    ~IncludeHandler() = default;

    HRESULT Open(
        THIS_ D3D_INCLUDE_TYPE IncludeType,
        LPCSTR pFileName,
        LPCVOID pParentData,
        LPCVOID* ppData,
        UINT* pBytes
    ) final override
    {
        HRESULT hr = S_OK;
        file::path filePath{};
        switch (IncludeType)
        {
        case D3D_INCLUDE_LOCAL:
            filePath = m_shaderPath / pFileName;
            break;
        case D3D_INCLUDE_SYSTEM:
            filePath = PathFinder::Relative("Shaders/") / pFileName;
            break;
        default:
            hr = E_FAIL;
            break;
        };

        if (file::exists(filePath))
        {
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            auto size = file.tellg();
            char* pData = static_cast<char*>(std::malloc(size));

            file.seekg(0, std::ios::beg);
            file.read(pData, size);
            file.close();

            *pBytes = static_cast<UINT>(size);
            *ppData = pData;

            hr = S_OK;
        }
        else
        {
            hr = E_FAIL;
        }

        return hr;
    }

    HRESULT Close(THIS_ LPCVOID pData) final override
    {
        std::free(const_cast<void*>(pData));
        return S_OK;
    }

    file::path m_shaderPath;
};

class HLSLCompiler
{
public:
    static ComPtr<ID3DBlob> LoadFormFile(const std::string_view& filepath);

private:
    static bool CheckResult(HRESULT hResult, ID3DBlob* shader, ID3DBlob* errorBlob);
    static bool CheckExtension(const std::string_view& shaderExtension);

	static std::unordered_map<std::string, ComPtr<ID3DBlob>> m_shaderCache;
};

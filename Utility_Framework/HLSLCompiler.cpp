#include "HLSLCompiler.h"
#include "FileIO.h"

std::unordered_map<std::string, ComPtr<ID3DBlob>> HLSLCompiler::m_shaderCache;

ComPtr<ID3DBlob> HLSLCompiler::LoadFormFile(const std::string_view& filepath)
{
    file::path filePath{ filepath };
	std::string fileExtension = filePath.extension().string();

	if (m_shaderCache.find(filePath.string()) != m_shaderCache.end())
	{
		return m_shaderCache[filePath.string()];
	}

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    flag compileFlag{};

#if defined(_DEBUG)
    compileFlag |= D3DCOMPILE_DEBUG;
#endif
#if defined(NDEBUG)
    compileFlag |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    compileFlag |= D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;

    file::path filename{ filePath.filename() };
    std::string shaderExtension = filename.replace_extension().extension().string();
    filename.replace_extension();

    if ("" == shaderExtension)
    {
		throw std::runtime_error("Shader file has no extension");
    }

    shaderExtension.erase(0, 1);
    if (!CheckExtension(shaderExtension))
    {
		throw std::runtime_error("Shader file has invalid extension");
    }

	if (fileExtension == ".hlsl")
    {
        HRESULT hResult = S_OK;

        IncludeHandler includeHandler(filePath.parent_path().string());

        hResult = D3DCompileFromFile(
            filePath.c_str(),
            NULL,
            &includeHandler,
            "main",
            std::string(shaderExtension + "_5_0").c_str(),
            compileFlag,
            NULL,
            shaderBlob.ReleaseAndGetAddressOf(),
            errorBlob.ReleaseAndGetAddressOf()
        );
        if (!CheckResult(hResult, shaderBlob.Get(), errorBlob.Get()))
        {
            throw std::runtime_error(std::string("Shader compilation failed\n") + reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
        }

		if (SUCCEEDED(hResult))
		{
			m_shaderCache[filePath.string()] = shaderBlob;
            std::string csoPath = PathFinder::RelativeToPrecompiledShader().string() + filePath.stem().string() + ".cso";
			FileWriter writer{ csoPath };
			writer.write(static_cast<char*>(shaderBlob->GetBufferPointer()), shaderBlob->GetBufferSize());
			writer.flush();
		}

    }
	else if (fileExtension == ".cso")
    {
		HRESULT hResult = S_OK;
		hResult = D3DReadFileToBlob(filePath.c_str(), shaderBlob.ReleaseAndGetAddressOf());
		if (FAILED(hResult))
		{
			throw std::runtime_error("Failed to read compiled shader file");
		}

		m_shaderCache[filePath.string()] = shaderBlob;

    }

    return shaderBlob;
}

bool HLSLCompiler::CheckResult(HRESULT hResult, ID3DBlob* shader, ID3DBlob* errorBlob)
{
    if (FAILED(hResult))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
            errorBlob->Release();
        }

        if (shader)
        {
            shader->Release();
        }

        return false;
    }
    else
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
    }

    return true;
}

bool HLSLCompiler::CheckExtension(const std::string_view& shaderExtension)
{
    if ("vs" == shaderExtension or
        "ps" == shaderExtension or
        "gs" == shaderExtension or
        "hs" == shaderExtension or
        "ds" == shaderExtension or
        "cs" == shaderExtension)
    {
        return true;
    }

	return false;

}

#include "HLSLCompiler.h"
#include "FileIO.h"
#include "Benchmark.hpp"

std::unordered_map<std::string, ComPtr<ID3DBlob>> HLSLCompiler::m_shaderCache;

ComPtr<ID3DBlob> HLSLCompiler::LoadFormFile(const std::string_view& filepath)
{
	Benchmark banch;
    file::path filePath{ filepath };
	std::string fileExtension = filePath.extension().string();

	if (m_shaderCache.find(filePath.string()) != m_shaderCache.end())
	{
		return m_shaderCache[filePath.string()];
	}

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    flag compileFlag{};

	std::cout << "Find Cache shader: " << filePath.string() << std::endl;

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
        return nullptr;
    }

    shaderExtension.erase(0, 1);
    if (!CheckExtension(shaderExtension))
    {
        return nullptr;
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
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
            return nullptr;
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
		Benchmark banch;
		HRESULT hResult = S_OK;
		hResult = D3DReadFileToBlob(filePath.c_str(), shaderBlob.ReleaseAndGetAddressOf());
		if (FAILED(hResult))
		{
			return nullptr;
		}

		m_shaderCache[filePath.string()] = shaderBlob;

		std::cout << "Read cso in " << banch.GetElapsedTime() << "ms" << std::endl;
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

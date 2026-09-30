#pragma once

#include <Windows.h>
#include <slang.h>
#include <slang-com-ptr.h>

#include "Vulkan/VulkanBindingModel.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace MaterialProbe
{
using CreateGlobalSession = SlangResult (*)(const SlangGlobalSessionDesc*, slang::IGlobalSession**);

class CompilerRuntime final
{
  public:
    CompilerRuntime() = default;

    void Initialize(const std::filesystem::path& root)
    {
        const auto directory = root / "ThirdParty" / "Slang" / "bin";
        m_compiler = LoadLibraryW((directory / "slang-compiler.dll").c_str());
        m_dxc = LoadLibraryW((directory / "dxcompiler.dll").c_str());
        m_dxil = LoadLibraryW((directory / "dxil.dll").c_str());
        if (!m_compiler || !m_dxc || !m_dxil)
        {
            throw std::runtime_error("Pinned Slang/DXC bundle could not be loaded");
        }

        const auto create =
            reinterpret_cast<CreateGlobalSession>(GetProcAddress(m_compiler, "slang_createGlobalSession2"));
        SlangGlobalSessionDesc description{};
        if (!create || SLANG_FAILED(create(&description, m_global.writeRef())))
        {
            throw std::runtime_error("Slang global session could not be created");
        }
    }

    ~CompilerRuntime()
    {
        m_global.setNull();
        if (m_compiler)
        {
            FreeLibrary(m_compiler);
        }
        if (m_dxc)
        {
            FreeLibrary(m_dxc);
        }
        if (m_dxil)
        {
            FreeLibrary(m_dxil);
        }
    }

    CompilerRuntime(const CompilerRuntime&) = delete;
    CompilerRuntime& operator=(const CompilerRuntime&) = delete;

    bool Compile(const std::filesystem::path& source, const char* entry, SlangStage stage, bool spirv,
                 const std::vector<std::string>& defines, std::string& error, std::vector<uint8_t>* bytecode = nullptr,
                 const std::vector<std::filesystem::path>& includePaths = {})
    {
        error.clear();
        std::vector<std::string> arguments{"-target",
                                           spirv ? "spirv" : "dxil",
                                           "-profile",
                                           spirv ? "spirv_1_3" : "sm_6_0",
                                           "-matrix-layout-column-major",
                                           "-O3",
                                           "-warnings-as-errors",
                                           "all",
                                           "-I",
                                           source.parent_path().string()};
        for (const auto& path : includePaths)
        {
            arguments.emplace_back("-I");
            arguments.emplace_back(path.string());
        }
        if (spirv)
        {
            arguments.emplace_back("-D__spirv__=1");
            arguments.emplace_back("-fvk-use-entrypoint-name");
            arguments.emplace_back("-fvk-use-dx-layout");
            const auto addShift = [&arguments](const char* option, unsigned shift, unsigned space) {
                arguments.emplace_back(option);
                arguments.emplace_back(std::to_string(shift));
                arguments.emplace_back(std::to_string(space));
            };
            for (unsigned space : {0u, 1u})
            {
                addShift("-fvk-b-shift", VulkanBindingModel::kConstantBufferShift, space);
                addShift("-fvk-t-shift", VulkanBindingModel::kShaderResourceShift, space);
                addShift("-fvk-u-shift", VulkanBindingModel::kUnorderedAccessShift, space);
                addShift("-fvk-s-shift", VulkanBindingModel::kSamplerShift, space);
            }
        }
        for (const std::string& define : defines)
        {
            arguments.emplace_back("-D" + define);
        }
        std::vector<const char*> pointers;
        for (const std::string& argument : arguments)
        {
            pointers.push_back(argument.c_str());
        }

        slang::SessionDesc description{};
        Slang::ComPtr<ISlangUnknown> allocation;
        if (SLANG_FAILED(m_global->parseCommandLineArguments(static_cast<int>(pointers.size()), pointers.data(),
                                                             &description, allocation.writeRef())))
        {
            error = "Slang options could not be parsed";
            return false;
        }

        // Slang 2026.14 also places shifts on the target. Retain one copy,
        // as RHIShaderCompiler does, so reflection and SPIR-V agree.
        std::vector<slang::CompilerOptionEntry> sessionOptions;
        for (unsigned i = 0; i < description.compilerOptionEntryCount; ++i)
        {
            const auto& option = description.compilerOptionEntries[i];
            if (option.name != slang::CompilerOptionName::VulkanBindShift)
            {
                sessionOptions.push_back(option);
            }
        }
        description.compilerOptionEntries = sessionOptions.data();
        description.compilerOptionEntryCount = static_cast<unsigned>(sessionOptions.size());

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(m_global->createSession(description, session.writeRef())))
        {
            error = "Slang session could not be created";
            return false;
        }
        std::ifstream stream(source, std::ios::binary);
        if (!stream)
        {
            error = "Shader source could not be opened: " + source.string();
            return false;
        }
        const std::string text(std::istreambuf_iterator<char>{stream}, {});
        Slang::ComPtr<ISlangBlob> diagnostics;
        const auto diagnosticText = [&diagnostics]() {
            return diagnostics ? std::string(static_cast<const char*>(diagnostics->getBufferPointer()),
                                             diagnostics->getBufferSize())
                               : std::string{};
        };
        const std::string moduleName = source.stem().string();
        Slang::ComPtr<slang::IModule> module;
        module = session->loadModuleFromSourceString(moduleName.c_str(), source.string().c_str(), text.c_str(),
                                                     diagnostics.writeRef());
        if (!module)
        {
            error = diagnosticText();
            return false;
        }
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        if (SLANG_FAILED(module->findAndCheckEntryPoint(entry, stage, entryPoint.writeRef(), diagnostics.writeRef())))
        {
            error = diagnosticText();
            return false;
        }
        slang::IComponentType* components[]{module.get(), entryPoint.get()};
        Slang::ComPtr<slang::IComponentType> composite;
        Slang::ComPtr<slang::IComponentType> linked;
        Slang::ComPtr<ISlangBlob> code;
        if (SLANG_FAILED(
                session->createCompositeComponentType(components, 2, composite.writeRef(), diagnostics.writeRef())) ||
            SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef())) ||
            SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())) || !code ||
            code->getBufferSize() == 0)
        {
            error = diagnosticText();
            return false;
        }
        if (bytecode)
        {
            const auto* begin = static_cast<const uint8_t*>(code->getBufferPointer());
            bytecode->assign(begin, begin + code->getBufferSize());
        }
        return true;
    }

  private:
    HMODULE m_compiler{};
    HMODULE m_dxc{};
    HMODULE m_dxil{};
    Slang::ComPtr<slang::IGlobalSession> m_global;
};
} // namespace MaterialProbe

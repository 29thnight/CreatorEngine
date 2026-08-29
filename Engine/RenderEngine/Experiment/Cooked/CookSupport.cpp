#include "CookSupport.h"

#include "ModelCookIdentity.h"

#include <fstream>

namespace experiment::cooked
{
    bool ReadTextFile(const std::filesystem::path& path, std::string& out)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;

        stream.seekg(0, std::ios::end);
        const std::streamoff bytes = stream.tellg();
        if (bytes < 0) return false;
        stream.seekg(0, std::ios::beg);

        out.resize(static_cast<std::size_t>(bytes));
        if (!out.empty())
            stream.read(out.data(), static_cast<std::streamsize>(out.size()));
        return stream.good() || stream.eof();
    }

    bool ReadBinaryFile(const std::filesystem::path& path,
        std::vector<std::byte>& out)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;

        stream.seekg(0, std::ios::end);
        const std::streamoff bytes = stream.tellg();
        if (bytes < 0) return false;
        stream.seekg(0, std::ios::beg);

        out.resize(static_cast<std::size_t>(bytes));
        if (!out.empty())
        {
            stream.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
        }
        return stream.good() || stream.eof();
    }

    bool IsContainedPath(const std::filesystem::path& root,
        const std::filesystem::path& child)
    {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(child, root, error);
        if (error || relative.empty() || relative.is_absolute()) return false;
        for (const std::filesystem::path& part : relative)
        {
            if (part == "..") return false;
        }
        return true;
    }

    bool ReadMetaAssetId(const std::filesystem::path& path,
        AssetId& outId, std::string& outFailure)
    {
        std::string yaml;
        if (!ReadTextFile(path, yaml))
        {
            outFailure = "meta를 읽을 수 없다: " + path.string();
            return false;
        }

        std::vector<ModelIdentityIssue> issues;
        if (!ReadAssetIdFromMeta(yaml, outId, issues))
        {
            outFailure = "meta GUID를 읽을 수 없다: " + path.string();
            if (!issues.empty()) outFailure += " (" + issues.front().message + ")";
            return false;
        }
        return true;
    }
}

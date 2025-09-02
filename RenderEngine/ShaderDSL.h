#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ShaderPassDesc {
    std::string vs, ps, gs, hs, ds, cs;
    std::vector<std::string> keywords;
    std::string queueTag; // "Opaque"/"Transparent"
};

struct ShaderAssetDesc {
    std::string name;     // Shader "Name"
    std::string tag;      // Shader : Opaque
    ShaderPassDesc pass;
};

bool ParseShaderDSL(std::string_view src, ShaderAssetDesc& out);

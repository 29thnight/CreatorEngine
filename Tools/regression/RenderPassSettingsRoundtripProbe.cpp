#include "../../Build/Obj/RenderEngine/x64-Debug/reflgen/reflgen_RenderEngine.h"
#include "../../Engine/RenderEngine/Interfaces/RenderPassSettings.h"
#include "../../Engine/Utility_Framework/Reflection/ReflectionTypedYml.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringParsedDocument.h"
#include <cstdio>

int main()
{
    RenderPassSettings source;
    source.bloom.applyBloom = false;
    source.bloom.threshold = 3.25f;
    source.bloom.blurRadius = 7;
    source.aa.isApply = false;
    source.aa.bias = 0.25f;
    source.ssgi.radius = 2.5f;
    source.colorGrading.textureFilePath = "Assets/color-profile";
    source.skyboxTextureName = "Assets/sky";
    source.m_isSkyboxEnabled = true;
    source.m_windStrength = 2.5f;
    Authoring::WriteDocument document;
    Meta::Typed::SerializeObjectInto(source, document.Root());
    const std::string emitted = document.Dump();
    std::string error;
    const auto parsed = Authoring::ParsedDocument::ParseText(emitted, error);
    if (!parsed) return 1;
    RenderPassSettings loaded;
    Meta::Typed::DeserializeObjectFrom(loaded, parsed.Root());
    if (loaded.bloom.applyBloom || loaded.bloom.threshold != 3.25f || loaded.bloom.blurRadius != 7 ||
        loaded.aa.isApply || loaded.aa.bias != 0.25f || loaded.ssgi.radius != 2.5f ||
        loaded.colorGrading.textureFilePath != "Assets/color-profile" || loaded.skyboxTextureName != "Assets/sky" ||
        !loaded.m_isSkyboxEnabled || loaded.m_windStrength != 2.5f) return 2;
    Authoring::WriteDocument restored;
    Meta::Typed::SerializeObjectInto(loaded, restored.Root());
    if (restored.Dump() != emitted) return 3;
    std::printf("%s", emitted.c_str());
    return 0;
}

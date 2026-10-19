// A checked-in constant-material fixture supplies the material interface only.
// This compiles the complete production host; it is not an asset-cook test.
#include "fixtures/material-codegen/constant.slang"

LXMaterialParameters LXBoundMaterialParameters()
{
    return LXDefaultMaterialParameters();
}

LXMaterialParameters LXBoundMaterialParameters(uint uniformBase, uint descriptorBase)
{
    return LXDefaultMaterialParameters();
}

LXGeneratedMaterial LXGeneratePacketMaterial(LXMaterialContext context, LXMaterialParameters parameters)
{
    return LXGenerateMaterial(context, parameters);
}

#include "Includes/MaterialGraphSceneHost.slang"

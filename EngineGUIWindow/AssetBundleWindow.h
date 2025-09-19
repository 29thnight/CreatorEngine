#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include "AssetEntry.h"

class AssetBundleWindow
{
public:
    AssetBundleWindow();
    ~AssetBundleWindow() = default;

    AssetEntry entry{};
};
#endif // !DYNAMICCPP_EXPORTS
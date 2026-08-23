#pragma once
#include "ImGuiRegister.h"
#include "AssetEntry.h"

class AssetBundleWindow
{
public:
    AssetBundleWindow();
    ~AssetBundleWindow() = default;

    AssetEntry entry{};
};

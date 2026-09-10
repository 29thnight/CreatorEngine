#pragma once
#include "ImGui.h"
#include "AssetEntry.h"

class AssetBundleWindow
{
public:
    AssetBundleWindow();
    ~AssetBundleWindow() = default;

    AssetEntry entry{};
};

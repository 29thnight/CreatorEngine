#pragma once
#include "ImGui.h"
#include "AssetEntry.h"

class AssetBundleWindow
{
public:
    void Draw();
    ~AssetBundleWindow() = default;

    AssetEntry entry{};
};

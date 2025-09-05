#include "AssetBundleWindow.h"
#ifndef DYNAMICCPP_EXPORTS
#include "DataSystem.h"
#include "SceneManager.h"
#include "Scene.h"
#include "ReflectionImGuiHelper.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include <algorithm>

AssetBundleWindow::AssetBundleWindow()
{
    ImGui::ContextRegister(ICON_FA_DIAGRAM_PROJECT "  AssetBundle", [&]()
        {
            auto* activeScene = SceneManagers->GetActiveScene();
            if (!activeScene)
            {
                ImGui::TextUnformatted("No active scene");
                return;
            }

            auto& bundle = activeScene->m_requiredLoadAssetsBundle;

            ImGui::Text("Loaded Assets");
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Models"))
            {
                for (const auto& [name, ptr] : DataSystems->Models)
                {
                    AssetEntry entry{ ManagedAssetType::Model, file::path{name} };
                    ImGui::Selectable(name.c_str());
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
            if (ImGui::CollapsingHeader("Materials"))
            {
                for (const auto& [name, ptr] : DataSystems->Materials)
                {
                    AssetEntry entry{ ManagedAssetType::Material, file::path{name} };
                    ImGui::Selectable(name.c_str());
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
            if (ImGui::CollapsingHeader("Textures"))
            {
                for (const auto& [name, ptr] : DataSystems->Textures)
                {
                    AssetEntry entry{ ManagedAssetType::Texture, file::path{name} };
                    ImGui::Selectable(name.c_str());
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
            if (ImGui::CollapsingHeader("SpriteFonts"))
            {
                for (const auto& [name, ptr] : DataSystems->SFonts)
                {
                    AssetEntry entry{ ManagedAssetType::SpriteFont, file::path{name} };
                    ImGui::Selectable(name.c_str());
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }

            ImGui::Separator();
            ImGui::Text("Asset Bundle");

			std::string bundleName = bundle.name.empty() ? "(Unnamed)" : bundle.name;

            if (ImGui::InputText("Name", &bundleName[0], 
                bundleName.capacity() + 1, 
                ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
                Meta::InputTextCallback,
                static_cast<void*>(&bundleName)))
            {
				bundle.name = bundleName;
			}

            ImGui::BeginChild("##BundleAssets", ImVec2(0, 150), true);
            for (std::size_t i = 0; i < bundle.assets.size();)
            {
                const auto& entry = bundle.assets[i];
                std::string label = entry.assetName.string();
                ImGui::Selectable(label.c_str());
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Remove"))
                    {
                        std::erase(bundle.assets, entry);
                        ImGui::EndPopup();
                        continue;
                    }
                    ImGui::EndPopup();
                }
                ++i;
            }
            /* 2) 남은 공간을 드롭존으로 (0 크기 방지) */
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 0.0f && avail.y > 0.0f)
            {
                // 최소 1px 보장 (둘 다!)
                ImVec2 size(ImMax(1.0f, avail.x), ImMax(1.0f, avail.y));
                ImGui::InvisibleButton("##bundle_dropzone", size);

                // (옵션) 시각적 피드백
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                {
                    ImVec2 p0 = ImGui::GetItemRectMin();
                    ImVec2 p1 = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(255, 255, 0, 255));
                    ImGui::SetTooltip("Drop assets here");
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ASSET_ENTRY",
                            ImGuiDragDropFlags_AcceptBeforeDelivery))
                    {
                        const auto* payloadEntry = static_cast<const AssetEntry*>(payload->Data);
                        if (!bundle.ContainsAsset(*payloadEntry))
                            bundle.AddAsset(*payloadEntry);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else
            {
                // 첫 프레임 도킹 사이징 전 등으로 avail이 0인 경우엔 드롭존 생략
                // (원한다면 ImGui::Dummy(ImVec2(1,1))로 최소 아이템만 두어도 됨)
            }

            ImGui::EndChild();
           
            if (ImGui::Button("Load"))
            {
                DataSystems->LoadAssetBundle(bundle);
            }
            ImGui::SameLine();
            if (ImGui::Button("Retain"))
            {
                DataSystems->RetainAssets(bundle);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                bundle.ClearAssets();
            }
        });
}
#endif // !DYNAMICCPP_EXPORTS
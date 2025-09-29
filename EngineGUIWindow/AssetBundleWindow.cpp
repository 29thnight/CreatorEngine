#include "AssetBundleWindow.h"
#ifndef DYNAMICCPP_EXPORTS
#include "DataSystem.h"
#include "SceneManager.h"
#include "Scene.h"
#include "ReflectionImGuiHelper.h"
#include "IconsFontAwesome6.h"
#include "Model.h"
#include "Material.h"
#include "Texture.h"
#include "fa.h"
#include "imgui_stdlib.h"
#include <algorithm>

struct AssetEntryPayload
{
    uint32_t type;            // ManagedAssetType (int�� ����)
    char     path[1024];      // UTF-8 NUL-terminated (�ʿ��ϸ� �� Ű���� ��)
};

inline void PackPathUTF8(const std::filesystem::path& p, char* out, size_t outCap)
{
    if (!out || outCap == 0) return;
#if defined(_WIN32)
    // MSVC: path.u8string() -> std::u8string
    std::u8string u8 = p.u8string();
    const char* src = reinterpret_cast<const char*>(u8.c_str());
    size_t n = std::min(strlen(src), outCap - 1);
    std::memcpy(out, src, n);
    out[n] = '\0';
#else
    std::string s = p.string(); // �Ϲ������� UTF-8
    size_t n = std::min(s.size(), outCap - 1);
    std::memcpy(out, s.data(), n);
    out[n] = '\0';
#endif
}

inline std::filesystem::path PathFromUTF8(const char* utf8)
{
#if defined(_WIN32)
    // C++20: path(u8string) ������
    auto as_u8 = reinterpret_cast<const char8_t*>(utf8 ? utf8 : "");
    return std::filesystem::path(std::u8string(as_u8));
#else
    return std::filesystem::path(utf8 ? utf8 : "");
#endif
}

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
        static ImGuiTextFilter filter;
        filter.Draw("Assets Search", ImGui::GetContentRegionAvail().x - 90);

        AssetEntryPayload entry{};

        // �巡��-�ҽ��� ����� �ӽ� ��Ʈ�� ���� (ImGui�� payload�� �����ϹǷ� ���������� ���)
        if (ImGui::CollapsingHeader("Models"))
        {
            for (const auto& [name, ptr] : DataSystems->Models)
            {
                // ��� �ϰ���: ������Ʈ ���/���� �� ���� �Ծ࿡ ���� ���
                // �ʿ��ϸ� .filename() ���� �ٲ㵵 ��
                std::filesystem::path filePath = ptr->path.filename();

                AssetEntry containTest{ ManagedAssetType::Model ,filePath };
                if (bundle.ContainsAsset(containTest))
                {
                    continue;
                }

                const std::string label = name;

                if (filter.IsActive() && !(filter.PassFilter(label.c_str())))
                {
                    continue;
                }

                entry.type = (uint32_t)ManagedAssetType::Model;
                PackPathUTF8(filePath, entry.path, sizeof(entry.path));

                ImGui::Selectable(label.c_str());
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        if (ImGui::CollapsingHeader("Materials"))
        {
            for (const auto& [name, ptr] : DataSystems->Materials)
            {
                AssetEntry containTest{ ManagedAssetType::Material ,name };
                if (bundle.ContainsAsset(containTest))
                {
                    continue;
                }

                entry.type = (uint32_t)ManagedAssetType::Material;
                PackPathUTF8(name, entry.path, sizeof(entry.path));

                const std::string label = name;

                if (filter.IsActive() && !(filter.PassFilter(label.c_str())))
                {
                    continue;
                }

                ImGui::Selectable(label.c_str());
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        if (ImGui::CollapsingHeader("Textures"))
        {
            for (const auto& [name, ptr] : DataSystems->Textures)
            {
                AssetEntry containTest{ ManagedAssetType::Texture ,name };
                if (bundle.ContainsAsset(containTest))
                {
                    continue;
                }

                entry.type = (uint32_t)ManagedAssetType::Texture;
                PackPathUTF8(name, entry.path, sizeof(entry.path));

                const std::string label = name;

                if (filter.IsActive() && !(filter.PassFilter(label.c_str())))
                {
                    continue;
                }

                ImGui::Selectable(label.c_str());
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        if (ImGui::CollapsingHeader("SpriteFonts"))
        {
            for (const auto& [name, ptr] : DataSystems->SFonts)
            {
                AssetEntry containTest{ ManagedAssetType::SpriteFont ,name };
                if (bundle.ContainsAsset(containTest))
                {
                    continue;
                }

                entry.type = (uint32_t)ManagedAssetType::SpriteFont;
                PackPathUTF8(name, entry.path, sizeof(entry.path));

                const std::string label = name;

                if (filter.IsActive() && !(filter.PassFilter(label.c_str())))
                {
                    continue;
                }

                ImGui::Selectable(label.c_str());
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("ASSET_ENTRY", &entry, sizeof(AssetEntry));
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        ImGui::Separator();
        ImGui::Text("Asset Bundle");

        // ���� �̸� �Է�: �ӽ� ���ڿ� ��� bundle.name�� ���� ���ε� (���� CallbackResize ����)
        {
            // capacity ��� ���� ������ ���� �ּ� 1����Ʈ �̻� Ȯ��
            if (bundle.name.capacity() == 0) bundle.name.reserve(32);

            // ImGui::InputText with std::string �ݹ� ���� (�̹� ������Ʈ���� ��� ���� Meta::InputTextCallback Ȱ��)
            // - &bundle.name[0] ���� ���� size�� capacity�� ���� ����� Ȯ���� �ʿ䰡 �־� �ݹ鿡�� ó��.
            std::string& nameRef = bundle.name;
            if (ImGui::InputText(
                "Name",
                &nameRef[0],
                nameRef.capacity() + 1,
                ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
                Meta::InputTextCallback,
                static_cast<void*>(&nameRef)))
            {
                // Enter �Է� �� �� �ʿ� �� �߰� ���� ����
            }
        }

        // ���� �ڻ� ����Ʈ
        ImGui::BeginChild("##BundleAssets", ImVec2(0, 150), true);
        for (std::size_t i = 0; i < bundle.assets.size();)
        {
            const auto& _entry = bundle.assets[i]; // <-- �� ���� �󺧷� ����ؾ� �� (entry �ƴ�)

            std::string label;
            try
            {
                label = _entry.assetName;
            }
            catch (const std::exception&)
            {
                label = "(Invalid path)";
            }

            ImGui::Selectable(label.c_str());
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Remove"))
                {
                    // ������ ����: �ε��� ��� erase (�ݺ���/���� ��ȿȭ �̽� ȸ��)
                    bundle.assets.erase(bundle.assets.begin() + static_cast<std::ptrdiff_t>(i));
                    ImGui::EndPopup();
                    continue; // i ���� ���� ���� ������(���� �� ����� ���� �ε���) �˻�
                }
                ImGui::EndPopup();
            }
            ++i;
        }

        // --- ���� ---
        ImGui::Separator();
        ImGui::TextUnformatted("Asset Bundle");

        // �� ������� ��� Ÿ��(�ؽ�Ʈ ������)�� ���Դϴ�
        {
            // �ؽ�Ʈ �������� �簢�� (���̶���Ʈ ��)
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();

            // �巡�װ� �ؽ�Ʈ ���� �ö���� �׵θ�/���� ǥ��(�ɼ�)
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            {
                ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(255, 255, 0, 255));
                ImGui::SetTooltip("Drop assets here");
            }

            // �ؽ�Ʈ �������� ��� Ÿ������ ���
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ENTRY"))
                {
                    if (payload && payload->IsDelivery())
                    {
                        // ����� POD ���̷ε� ����ü
                        const auto* p = static_cast<const AssetEntryPayload*>(payload->Data);

                        // UTF-8 -> path ����
                        std::filesystem::path dropPath = PathFromUTF8(p->path);

                        // ���� ��Ʈ���� ��ȯ
                        AssetEntry dropEntry{
                            static_cast<ManagedAssetType>(p->type),
                            dropPath
                        };

                        if (!bundle.ContainsAsset(dropEntry))
                            bundle.AddAsset(dropEntry);
                    }
                }
                ImGui::EndDragDropTarget();
            }
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
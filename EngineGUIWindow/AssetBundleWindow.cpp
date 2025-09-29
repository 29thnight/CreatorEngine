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
    uint32_t type;            // ManagedAssetType (int로 전송)
    char     path[1024];      // UTF-8 NUL-terminated (필요하면 더 키워도 됨)
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
    std::string s = p.string(); // 일반적으로 UTF-8
    size_t n = std::min(s.size(), outCap - 1);
    std::memcpy(out, s.data(), n);
    out[n] = '\0';
#endif
}

inline std::filesystem::path PathFromUTF8(const char* utf8)
{
#if defined(_WIN32)
    // C++20: path(u8string) 생성자
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

        // 드래그-소스에 사용할 임시 엔트리 변수 (ImGui가 payload를 복사하므로 지역변수로 충분)
        if (ImGui::CollapsingHeader("Models"))
        {
            for (const auto& [name, ptr] : DataSystems->Models)
            {
                // 경로 일관성: 프로젝트 상대/절대 등 엔진 규약에 맞춰 사용
                // 필요하면 .filename() 으로 바꿔도 됨
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

        // 번들 이름 입력: 임시 문자열 대신 bundle.name을 직접 바인딩 (기존 CallbackResize 유지)
        {
            // capacity 기반 버퍼 접근을 위해 최소 1바이트 이상 확보
            if (bundle.name.capacity() == 0) bundle.name.reserve(32);

            // ImGui::InputText with std::string 콜백 패턴 (이미 프로젝트에서 사용 중인 Meta::InputTextCallback 활용)
            // - &bundle.name[0] 접근 전에 size를 capacity에 맞춰 충분히 확장할 필요가 있어 콜백에서 처리.
            std::string& nameRef = bundle.name;
            if (ImGui::InputText(
                "Name",
                &nameRef[0],
                nameRef.capacity() + 1,
                ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
                Meta::InputTextCallback,
                static_cast<void*>(&nameRef)))
            {
                // Enter 입력 시 등 필요 시 추가 로직 가능
            }
        }

        // 번들 자산 리스트
        ImGui::BeginChild("##BundleAssets", ImVec2(0, 150), true);
        for (std::size_t i = 0; i < bundle.assets.size();)
        {
            const auto& _entry = bundle.assets[i]; // <-- 이 값을 라벨로 사용해야 함 (entry 아님)

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
                    // 안전한 삭제: 인덱스 기반 erase (반복자/참조 무효화 이슈 회피)
                    bundle.assets.erase(bundle.assets.begin() + static_cast<std::ptrdiff_t>(i));
                    ImGui::EndPopup();
                    continue; // i 증가 없이 다음 아이템(삭제 후 당겨진 현재 인덱스) 검사
                }
                ImGui::EndPopup();
            }
            ++i;
        }

        // --- 기존 ---
        ImGui::Separator();
        ImGui::TextUnformatted("Asset Bundle");

        // ▼ 여기부터 드롭 타깃(텍스트 아이템)을 붙입니다
        {
            // 텍스트 아이템의 사각형 (하이라이트 용)
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();

            // 드래그가 텍스트 위에 올라오면 테두리/툴팁 표시(옵션)
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            {
                ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(255, 255, 0, 255));
                ImGui::SetTooltip("Drop assets here");
            }

            // 텍스트 아이템을 드롭 타깃으로 사용
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ENTRY"))
                {
                    if (payload && payload->IsDelivery())
                    {
                        // 당신의 POD 페이로드 구조체
                        const auto* p = static_cast<const AssetEntryPayload*>(payload->Data);

                        // UTF-8 -> path 복원
                        std::filesystem::path dropPath = PathFromUTF8(p->path);

                        // 엔진 엔트리로 변환
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
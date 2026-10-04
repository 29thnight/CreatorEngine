#include "MeshRenderer.h"
#include "ProfileScope.h"
#include "MaterialGraphWindow.h"
#include "EditorTheme.h"
#include "EditorAssetDragPayload.h"
// MeshRenderer.h는 Material을 전방 선언만 한다. 이 파일은 m_ORM_TexName 같은
// 멤버를 직접 읽으므로 완전한 형이 필요하다. 그동안은 같은 유니티 블롭의
// 앞선 파일이 대신 공급했고, 폴더에 파일 하나가 늘어 블롭 구성이 바뀌자
// 드러났다 — 전이 include에 기대면 무관한 파일 추가가 빌드를 깬다.
#include "Material.h"
#include "EditorObjectOperations.h"
#include "Assets/ModelAssetGeneration.h" // PHASE 3.75 MBC8: typed 정본 read-only 표시
#include "MaterialScriptBinding.h"
#include "MaterialPropertyPacker.h"
#include "ShaderMeta.h"
#include "StandardMaterialProperty.h"
#include "ReflectionTypedDraw.h"
#include "EditorImGuiTexture.h"
#include "ReflectionImGuiHelper.h"
#include "DataSystem.h"
#include "EditorAssetDatabase.h"
#include "EditorAssetPresentation.h"
#include "SceneManager.h"
#include "EditorIcons.h"
#include "ExternUI.h"
#include "EditorPropertyRow.h"
#include "imgui_stdlib.h"
#include <cstdio>
#include "PathFinder.h"
#include <filesystem>
#include <d3d11shader.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <span>
#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */

namespace
{
	// 전용 드로어의 줄 배치 상태 (PHASE 21 W2-I4). 인스펙터는 한 스레드에서 그린다.
	editor::widgets::property_layout_state g_meshRendererLayout{};

	// Material 구간과 텍스처 칸(`TextureDropTarget`)이 **같은 열**에 서도록 한 목록에서 잰다.
	editor::widgets::property_sheet MaterialSheet()
	{
		return editor::widgets::property_sheet(g_meshRendererLayout, { "Element", "Bitflag",
			"Base Map", "Normal Map", "ORM Map", "Base Color", "Metallic", "Roughness", "Rendering Mode",
			"Enable LODGroup", "Receive Shadow", "Cast Shadow",
			"Model", "Mesh", "Generation", "Name", "Vertices" });
	}

	// 읽기 전용 값 한 줄. 값이 칸보다 길면 입력칸이 잘라 보이고 tooltip 이 전체를 준다.
	void ReadOnlyLine(const editor::widgets::property_sheet& sheet, const char* label, std::string value)
	{
		ImGui::PushID(label);
		ImGui::SetNextItemWidth(sheet.line(label));
		ImGui::InputText("##Value", &value, ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", value.c_str());
        ImGui::PopID();
    }

    void ApplyGraphParameter(MeshRenderer& renderer, LX::Id id, LX::LXSocketValue value)
    {
        const auto previous = renderer.m_Material;
        const auto baseGuid = renderer.m_materialBaseGuid;
        const auto acceptedBase = renderer.GetMaterialAssetBase();
        const auto referenceState = renderer.GetMaterialAssetReferenceState();
        auto candidate = std::make_shared<Material>(*previous);
        std::string error;
        if (!candidate->TrySetMaterialGraphParameter(id, std::move(value), error))
        {
            Debug::PrintLog(spdlog::level::err, error);
            return;
        }
        const auto apply = [scene = renderer.GetOwner()->GetScene(),
                            handle = renderer.GetOwner()->GetScene()->HandleOf(renderer.GetOwner()->m_index),
                            component = renderer.GetInstanceID(), baseGuid, acceptedBase,
                                referenceState](const std::shared_ptr<Material>& material) {
            if (SceneManagers->GetActiveScene() != scene)
            {
                return;
            }
            auto* object = scene->Resolve(handle);
            auto* target = object ? object->GetComponent<MeshRenderer>() : nullptr;
            if (target && target->GetInstanceID() == component)
            {
                target->SetMaterialAssetReference(material, baseGuid, acceptedBase, referenceState);
            }
        };
        Meta::MakeCustomChangeCommand([apply, previous] { apply(previous); }, [apply, candidate] { apply(candidate); });
    }

    void AssignMaterialAsset(MeshRenderer& renderer, const std::shared_ptr<Material>& asset)
    {
        const auto previous = renderer.m_Material;
        const auto previousBase = renderer.m_materialBaseGuid;
        const auto previousSnapshot = renderer.GetMaterialAssetBase();
        const auto previousState = renderer.GetMaterialAssetReferenceState();
        auto candidate = std::make_shared<Material>(*asset);
        const auto apply = [scene = renderer.GetOwner()->GetScene(),
                            handle = renderer.GetOwner()->GetScene()->HandleOf(renderer.GetOwner()->m_index),
                            component = renderer.GetInstanceID()](const std::shared_ptr<Material>& material,
                                FileGuid base,
                                                                  const std::shared_ptr<const Material>& snapshot,
                                                                  const std::shared_ptr<const MaterialAssetReferenceState>& state) {
            if (SceneManagers->GetActiveScene() != scene)
            {
                return;
            }
            auto* object = scene->Resolve(handle);
            auto* target = object ? object->GetComponent<MeshRenderer>() : nullptr;
            if (!target || target->GetInstanceID() != component)
            {
                return;
            }
            target->SetMaterialAssetReference(material, base, snapshot, state);
        };
        Meta::MakeCustomChangeCommand([apply, previous, previousBase, previousSnapshot, previousState] {
                                          apply(previous, previousBase, previousSnapshot, previousState);
                                      },
                                      [apply, candidate, asset] { apply(candidate, asset->m_fileGuid, asset, {}); });
    }

    void DrawMaterialAssetSelector(MeshRenderer& renderer)
    {
        ImGui::PushID(renderer.GetOwner()->GetScene());
        ImGui::PushID(std::to_string(renderer.GetOwner()->m_index).c_str());
        ImGui::PushID(std::to_string(renderer.GetInstanceID()).c_str());
        static std::vector<std::filesystem::path> assets;
        static ImGuiTextFilter filter;
        static std::string name, error;
        static bool assignSaved = false;
        const auto refresh = [&] {
            assets.clear();
            std::error_code ec;
            const auto root = PathFinder::RelativeToMaterial("");
            for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".asset") assets.push_back(it->path());
            std::ranges::sort(assets);
        };
        const float button = ImGui::GetFrameHeight();
        const float width = std::max(1.f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(width);
        ImGui::SetNextWindowSizeConstraints(ImVec2{width, 0.f}, ImVec2{width, 1000.f});
        const auto label = renderer.m_Material ? renderer.m_Material->m_name : std::string("Select Material");
        if (ImGui::BeginCombo("##MaterialAssets", label.c_str()))
        {
            if (ImGui::IsWindowAppearing()) refresh();
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::InputTextWithHint("##Search", "Search materials", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
                filter.Build();
            if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
            if (ImGui::BeginChild("##AssetList", ImVec2{0.f, editor::ThemePixels(220.f)}))
            {
                for (const auto& path : assets)
                {
                    const auto caption = std::filesystem::relative(path, PathFinder::RelativeToMaterial("")).replace_extension().generic_string();
                    if (!filter.PassFilter(caption.c_str())) continue;
                    const auto assetGuid = DataSystems->GetFileGuid(path);
                    ImGui::PushID(path.string().c_str());
                    if (ImGui::Selectable(caption.c_str(), renderer.m_materialBaseGuid == assetGuid))
                    {
                        try
                        {
                            auto material = DataSystems->LoadMaterialShared(assetGuid);
                            if (material && material->m_fileGuid == assetGuid)
                            {
                                AssignMaterialAsset(renderer, material);
                                ImGui::CloseCurrentPopup();
                            }
                            else
                            {
                                error = "Could not load material: " + caption;
                            }
                        }
                        catch (const std::exception& failure) { error = failure.what(); }
                    }
                    ImGui::PopID();
                }
                if (assets.empty()) ImGui::TextDisabled("No saved materials. Use + to save one.");
            }
            ImGui::EndChild();
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!renderer.m_Material || !PathFinder::IsAssetAuthoringEnabled());
        if (ImGui::Button("+##SaveMaterialAsset", ImVec2{button, button}))
        {
            name = std::filesystem::path(label).filename().string() + " Copy";
            assignSaved = false;
            error.clear();
            ImGui::OpenPopup("Save Material Copy");
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Save a material instance copy; graph-backed copies share their shader graph");
        }
        if (ImGui::BeginPopup("Save Material Copy"))
        {
            ImGui::TextUnformatted(renderer.m_Material && renderer.m_Material->HasMaterialGraph()
                ? "Save Material Copy (shared graph)" : "Save Material Copy");
            ImGui::InputText("Name", &name);
            ImGui::TextWrapped("Current instance values become defaults in the new material asset.");
            if (renderer.m_Material && renderer.m_Material->HasMaterialGraph())
            {
                ImGui::TextWrapped("This copy shares its shader graph. Use Node Editor Save As Copy for an "
                    "independent graph.");
            }
            ImGui::Checkbox("Assign this copy to this mesh", &assignSaved);
            if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
            if (ImGui::Button("Save Copy"))
            {
                const auto path = PathFinder::RelativeToMaterial("") / (name + ".asset");
                const bool valid = !name.empty() && name != "." && name != ".." &&
                    name.find_first_of("<>:\"/\\|?*") == std::string::npos && name.back() != '.' && name.back() != ' ' &&
                    std::ranges::none_of(name, [](unsigned char c) { return c < 32; });
                if (!valid) error = "Enter a valid material name.";
                else if (std::filesystem::exists(path) || DataSystems->FindCachedMaterial(name))
                    error = "That name already exists. Choose another name.";
                else
                {
                    auto material = std::make_shared<Material>(*renderer.m_Material);
                    material->m_name = name;
                    material->m_fileGuid = FileGuid::CreateRandomV4();
                    if (EditorAssetDatabase::Get().SaveMaterial(material.get()))
                    {
                        DataSystems->InsertMaterial(material);
                        if (assignSaved)
                        {
                            AssignMaterialAsset(renderer, material);
                        }
                        refresh();
                        ImGui::CloseCurrentPopup();
                    }
                    else error = "Could not save the material asset.";
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }

    void DrawGraphSurface(MeshRenderer& renderer, const editor::widgets::property_sheet& sheet)
    {
        // Keep the snapshot alive if a widget publishes a replacement instance.
        const auto instance = renderer.m_Material->GetMaterialGraphInstance();
        const auto& program = instance->generation->cooked.product.program;
        if (!ImGui::CollapsingHeader("Instance Overrides", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        ReadOnlyLine(sheet, "Shader", program.surface ? "Surface shader" : "No surface output");
        for (const auto& parameter : program.parameters)
        {
            if (!parameter.exposed || parameter.type == LX::PinType::Sampler)
                continue;
            ImGui::PushID(static_cast<int>(parameter.id));
            LX::LXSocketValue value = parameter.value;
            const auto override = std::ranges::find(instance->description.parameters, parameter.id,
                                                    &material_graph::ParameterOverride::id);
            if (override != instance->description.parameters.end())
                value = override->value;
            const float width = sheet.line(parameter.name.c_str());
            ImGui::SetNextItemWidth(width);
            bool edited{};
            if (parameter.type == LX::PinType::Texture)
            {
                const auto resource = std::ranges::find(instance->generation->cooked.product.layout.textures,
                                                        parameter.id, &LX::LXMaterialResource::parameter);
                const auto owner =
                    resource == instance->generation->cooked.product.layout.textures.end()
                        ? instance->textures.end()
                        : std::ranges::find(instance->textures, resource->slot, &material_graph::InstanceTexture::slot);
                if (owner != instance->textures.end())
                {
                    const auto texture = EditorImGuiTexture::From(owner->owner);
                    const float height = ImGui::GetFrameHeight();
                    if (texture != ImTextureID_Invalid)
                        ImGui::Image(texture, {height, height});
                    else
                        ImGui::Dummy({height, height});
                    ImGui::SameLine();
                    const auto name = owner->owner->m_name + owner->owner->m_extension;
                    ImGui::TextUnformatted(name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Edit texture connections in the Node Editor.");
                }
                else
                    ImGui::TextDisabled("Not used by the active output");
            }
            else if (auto* scalar = std::get_if<double>(&value))
            {
                float number = static_cast<float>(*scalar);
                const bool unit = parameter.identifier == "metallic" || parameter.identifier == "roughness" ||
                                  parameter.identifier == "alpha" || parameter.identifier == "alphaCutoff" ||
                                  parameter.identifier == "occlusionStrength";
                edited = unit ? ImGui::SliderFloat("##Value", &number, 0, 1)
                              : ImGui::DragFloat("##Value", &number, .01f, 0, 100);
                value = double(number);
            }
            else if (auto* color = std::get_if<std::array<double, 4>>(&value))
            {
                float values[]{float((*color)[0]), float((*color)[1]), float((*color)[2]), float((*color)[3])};
                edited = parameter.identifier == "baseColor" ? ImGui::ColorEdit3("##Value", values)
                                                             : ImGui::ColorEdit4("##Value", values);
                value = std::array<double, 4>{values[0], values[1], values[2], values[3]};
            }
            else if (auto* vector = std::get_if<std::array<double, 3>>(&value))
            {
                float values[]{float((*vector)[0]), float((*vector)[1]), float((*vector)[2])};
                edited = ImGui::DragFloat3("##Value", values, .01f);
                value = std::array<double, 3>{values[0], values[1], values[2]};
            }
            else if (auto* boolean = std::get_if<bool>(&value))
                edited = ImGui::Checkbox("##Value", boolean);
            else if (auto* integer = std::get_if<std::int64_t>(&value))
                edited = ImGui::DragScalar("##Value", ImGuiDataType_S64, integer);
            if (edited)
                ApplyGraphParameter(renderer, parameter.id, std::move(value));
            ImGui::PopID();
        }

    }
    } // namespace

    void ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer)
    {
        ce::profile_scope profile{ce::marker<"MaterialInspectorDraw">()};
        meshRenderer->RefreshMaterialAsset();
        const editor::widgets::property_sheet sheet = MaterialSheet();

        // PHASE 3.75 MBC8 — typed 정본의 read-only snapshot. 인스펙터는 generation을
        // 읽기만 하고, 변경은 authoring transaction(재임포트)이 새 generation으로
        // 게시한다. legacy Mesh 필드는 여기서 보이지 않는다(MBC9 은퇴).
        if (ImGui::CollapsingHeader("Model Generation"))
        {
            if (const auto& generation = meshRenderer->m_modelGeneration)
            {
                const assets::ModelAssetGenerationIdentity& identity = generation->Identity();
                ReadOnlyLine(sheet, "Model", Uuid::ToString(identity.modelId));
                ReadOnlyLine(sheet, "Mesh", meshRenderer->m_meshAssetId.ToString());
                ReadOnlyLine(sheet, "Generation",
                             std::to_string(identity.generation) + " · epoch " + identity.identityEpoch);
                if (meshRenderer->m_modelMeshIndex < generation->Meshes().size())
                {
                    const assets::ModelMeshAsset& mesh = generation->Meshes()[meshRenderer->m_modelMeshIndex];
                    ReadOnlyLine(sheet, "Name", mesh.name);
                    char counts[128]{};
                    std::snprintf(
                        counts, sizeof(counts), "%zu · Indices %zu · mask 0x%X",
                        static_cast<std::size_t>(mesh.vertexStride ? mesh.vertexBytes.size() / mesh.vertexStride : 0u),
                        mesh.indices.size(), mesh.vertexAttributeMask);
                    ReadOnlyLine(sheet, "Vertices", counts);
                }
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("typed generation 없음 (legacy/experiment 경로)");
                ImGui::PopStyleColor();
            }
        }

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawMaterialAssetSelector(*meshRenderer);
            if (ImGui::Button("Open Node Editor", ImVec2{-1.f, 0.f}))
            {
                std::string error;
                if (!editor::material_editing::Open(*meshRenderer, error))
                {
                    Debug::PrintLog(spdlog::level::err, error);
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", meshRenderer->m_materialBaseGuid != FileGuid{}
                    ? "Edit the shared material asset. Values below override this mesh only."
                    : "Create a standalone material draft from this mesh. Assignment is a separate action.");
            }
            editor::material_editing::DrawInspectorPreview(*meshRenderer);
            if (meshRenderer->m_Material && meshRenderer->m_Material->HasMaterialGraph())
            {
                DrawGraphSurface(*meshRenderer, sheet);
            }
        }
        const bool latticeMaterial = meshRenderer->m_Material && meshRenderer->m_Material->HasMaterialGraph();
        if (!latticeMaterial && ImGui::CollapsingHeader("MaterialInfo", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& mat_type = Meta::Find(type_guid(Material)); // CT1: 문자열 → typeID 조회
            if (nullptr != meshRenderer->m_Material)
            {
                auto& mat_info = meshRenderer->m_Material->m_materialInfo;
                auto mat = meshRenderer->m_Material.get();
                TextureDropTarget(mat, meshRenderer->GetMaterialInstance());

                // I5-M5 S4 — 편집 정본은 이름 기반 논리 값이다. m_materialInfo는
                // legacy 스칼라 소비자용 사본이라 binding이 함께 동기화한다.
                // meta가 없어 논리 경로가 거부되는 legacy 재질만 사본에 직접 쓴다.
                math::color baseColor = MaterialScriptBinding::GetBaseColor(*mat);
                ImGui::SetNextItemWidth(sheet.line("Base Color"));
                if (ImGui::ColorEdit4("##BaseColor", &baseColor.r))
                {
                    MaterialScriptBinding::SetBaseColor(*mat, baseColor, meshRenderer->GetMaterialInstance());
                }

                float metallic =
                    MaterialScriptBinding::GetFloat(*mat, standard_material::property::Metallic, mat_info.m_metallic);
                ImGui::SetNextItemWidth(sheet.line("Metallic"));
                if (ImGui::SliderFloat("##Metallic", &metallic, 0.f, 1.f) &&
                    !MaterialScriptBinding::SetFloat(*mat, standard_material::property::Metallic, metallic,
                                                     meshRenderer->GetMaterialInstance()))
                {
                    mat_info.m_metallic = metallic;
                }

                float roughness =
                    MaterialScriptBinding::GetFloat(*mat, standard_material::property::Roughness, mat_info.m_roughness);
                ImGui::SetNextItemWidth(sheet.line("Roughness"));
                if (ImGui::SliderFloat("##Roughness", &roughness, 0.f, 1.f) &&
                    !MaterialScriptBinding::SetFloat(*mat, standard_material::property::Roughness, roughness,
                                                     meshRenderer->GetMaterialInstance()))
                {
                    mat_info.m_roughness = roughness;
                }

                // IOR 슬라이더는 은퇴 — 유일 소비자였던 TrySetMaterialInfo가 호출자
                // 0인 죽은 함수였고 PBRMaterial CB는 어떤 셰이더에도 없다. 소비 0인
                // 저작 표면은 데이터만 쌓는다. m_IOR 필드 자체는 S2c/I6에서 제거한다.

                for (const reflgen::field_info& enumProp : mat_type ? mat_type->fields() : std::span<const reflgen::field_info>{})
                {
                    if (enumProp.type() == reflgen::type_id_of<MaterialRenderingMode>())
                    {
                        auto mode = meshRenderer->m_Material->m_renderingMode;
                        ImGui::SetNextItemWidth(sheet.line("Rendering Mode"));
                        Meta::DrawEnumProperty(&mode, enumProp, "##RenderingMode");
                        if (mode != meshRenderer->m_Material->m_renderingMode)
                            EditorObjectOperations::MaterialMode({meshRenderer->m_Material}, mode);
                        break;
                    }
                }
            }
            else
            {
                ImGui::TextUnformatted("No Material assigned.");
            }
        }

        // I5-M5 S4 — ShaderMeta 선언 기반 동적 property 편집기. 편집은 논리 값
        // 경로(MaterialScriptBinding)만 탄다. 표준 3종은 위 MaterialInfo 헤더의
        // 전용 위젯이 담당하므로 건너뛴다.
        if (!latticeMaterial && ImGui::CollapsingHeader("Shader Properties", ImGuiTreeNodeFlags_DefaultOpen))
        {
            Material* mat = meshRenderer->m_Material.get();
            if (nullptr == mat)
            {
                ImGui::TextUnformatted("No Material assigned.");
            }
            else if (FileGuid{} == mat->m_shaderMetaGuid)
            {
                ImGui::TextWrapped("ShaderMeta가 없어 논리 property를 편집할 수 없다");
            }
            else
            {
                std::string metaError;
                const ShaderMetaHandle metaHandle = DataSystems->LoadShaderMetaHandle(mat->m_shaderMetaGuid, metaError);
                const auto meta = DataSystems->ResolveShaderMeta(metaHandle);
                if (!meta)
                {
                    ImGui::TextWrapped("ShaderMeta 로드 실패: %s", metaError.c_str());
                }
                else
                {
                    for (const ShaderPropertyDesc& desc : meta->properties)
                    {
                        if (desc.name == standard_material::property::BaseColor ||
                            desc.name == standard_material::property::Metallic ||
                            desc.name == standard_material::property::Roughness)
                        {
                            continue;
                        }

                        // 현재 논리 값 — 없으면 ShaderMeta 기본값(정본 packer의
                        // ApplyDefault). 0을 보여주면 기본 1.0 저작이 틀리게 보인다.
                        MaterialPropertyValue current;
                        {
                            const auto authored = std::find_if(
                                mat->m_propertyValues.begin(), mat->m_propertyValues.end(),
                                [&](const MaterialPropertyValue& value) { return value.m_name == desc.name; });
                            if (authored != mat->m_propertyValues.end())
                            {
                                current = *authored;
                            }
                            else
                            {
                                std::string defaultError;
                                (void)MaterialPropertyPacker::ApplyDefault(desc, current, defaultError);
                            }
                        }

                        // 이름은 셰이더가 정한다 — 컴파일 시 목록에 없으므로 최소 열에서 잘린다.
                        ImGui::PushID(desc.name.c_str());
                        const float width = sheet.line(desc.name.c_str());
                        switch (desc.type)
                        {
                        case ShaderPropertyType::Float:
                        case ShaderPropertyType::Float2:
                        case ShaderPropertyType::Float3:
                        case ShaderPropertyType::Float4: {
                            const int count = static_cast<int>(MaterialPropertyPacker::NumericElementCount(desc.type));
                            float values[4]{};
                            for (int i = 0; i < count && i < static_cast<int>(current.m_numericValue.size()); ++i)
                            {
                                values[i] = current.m_numericValue[i];
                            }
                            ImGui::SetNextItemWidth(width);
                            if (ImGui::DragScalarN("##Value", ImGuiDataType_Float, values, count, 0.01f))
                            {
                                (void)MaterialScriptBinding::SetFloatVector(
                                    *mat, *meta, desc.name,
                                    std::span<const float>(values, static_cast<std::size_t>(count)),
                                    meshRenderer->GetMaterialInstance());
                            }
                            break;
                        }
                        case ShaderPropertyType::Int: {
                            int value = current.m_integerValue;
                            ImGui::SetNextItemWidth(width);
                            if (ImGui::DragInt("##Value", &value))
                            {
                                (void)MaterialScriptBinding::SetInt(*mat, *meta, desc.name, value,
                                                                    meshRenderer->GetMaterialInstance());
                            }
                            break;
                        }
                        case ShaderPropertyType::Bool: {
                            bool value = current.m_boolValue;
                            if (ImGui::Checkbox("##Value", &value))
                            {
                                (void)MaterialScriptBinding::SetInt(*mat, *meta, desc.name, value ? 1 : 0,
                                                                    meshRenderer->GetMaterialInstance());
                            }
                            break;
                        }
                        case ShaderPropertyType::Texture2D: {
                            std::string guid = FileGuid{} == current.m_textureGuid ? std::string("(none)")
                                                                                   : current.m_textureGuid.ToString();
                            ImGui::SetNextItemWidth(width);
                            ImGui::InputText("##Value", &guid, ImGuiInputTextFlags_ReadOnly);
                            break;
                        }
                        default:
                            ImGui::TextDisabled("(Inspector 미지원 타입)");
                            break;
                        }
                        ImGui::PopID();
                    }
                }
            }
        }

        if (ImGui::CollapsingHeader("LightMapping", ImGuiTreeNodeFlags_DefaultOpen))
        {
            Meta::TypedDraw::DrawOwnMembers(meshRenderer->m_LightMapping);
        }

        // I5-D5b — LOD 임계값 편집 표면을 걷었다. D0b 판정(LOD는 결과를 버리는
        // 죽은 생산 전용 파이프라인 — 렌더 소비 0·SelectLOD 호출자 0·코퍼스
        // 저작분 0건)의 이행이다. 이 UI의 "Apply"는 Mesh::GenerateLODs를 불러
        // MeshOptimizer 산출을 버리고(indexCount만 보관) 아무도 읽지 않는
        // m_LODThresholds를 적었다 — 자산을 실제로 바꾸지 않으면서 바꾼 것처럼
        // 보이는 표면이라 남겨 두는 쪽이 더 나쁘다(같은 이유로 legacy Mesh*
        // 직소비 3지점도 함께 사라진다).
        //
        // m_isEnableLOD는 존치한다 — ProxyCommand→PrimitiveRenderProxy::m_EnableLOD로
        // 흐르는 저작 값이고, 미래 LOD는 렌더 파생 몫이라는 것이 D0b 판정이다.
        if (ImGui::CollapsingHeader("LODGroupShared", ImGuiTreeNodeFlags_DefaultOpen))
        {
            sheet.line("Enable LODGroup");
            ImGui::Checkbox("##EnableLODGroup", &meshRenderer->m_isEnableLOD);
        }

        if (ImGui::CollapsingHeader("ShadowSetting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            sheet.line("Receive Shadow");
            ImGui::Checkbox("##EnableShadowReceive", &meshRenderer->m_shadowRecive);
            sheet.line("Cast Shadow");
            ImGui::Checkbox("##EnableShadowCast", &meshRenderer->m_shadowCast);
        }

        if (ImGui::CollapsingHeader("Advanced"))
        {
            ImGui::SetNextItemWidth(sheet.line("Bitflag"));
            ImGui::DragScalar("##Bitflag", ImGuiDataType_U32, &meshRenderer->m_bitflag);
        }

    }

namespace
{
	// I5-M5 S4 — 드롭의 저작 정본은 texture GUID 논리 값이다. 이름 필드는 더
	// 쓰지 않는다(D5-c가 죽인 이름 참조를 부활시키지 않는다 — 저장 시
	// SynchronizeLegacyMaterialProperties가 GUID에서 이름을 되채운다). delete는
	// GUID와 이름을 함께 비운다 — 이름만 남으면 Finalize의 이름 폴백이
	// 텍스처를 되살린다.
	//
	// 한 줄이다: 라벨, 미리보기(있을 때), 이름 버튼(끌어 놓기 대상), 지우기 버튼.
	void DrawMaterialTextureSlot(const editor::widgets::property_sheet& sheet, Material& mat,
		const char* label, std::string_view propertyName, std::string& legacyNameField,
		const std::shared_ptr<Texture>& current, bool compress,
		const std::function<void(std::shared_ptr<Texture>)>& apply,
		experiment::MaterialInstance* instance) // I5-D5c3
	{
		ImGui::PushID(propertyName.data(),
			propertyName.data() + propertyName.size());

		const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
		const float square = ImGui::GetFrameHeight();
		float width = current ? sheet.line_before_buttons(label, 2) : sheet.line(label);
		if (current)
		{
			ImGui::Image((ImTextureID)EditorImGuiTexture::From(current.get()),
				ImVec2(square, square));
			ImGui::SameLine(0.f, gap);
		}

		const std::string name = current ? current->m_name + current->m_extension : std::string("None");
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
		ImGui::Button((name + "###Slot").c_str(), ImVec2(width, 0.f));
		ImGui::PopStyleVar();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());

		if (!(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload =
				ImGui::AcceptDragDropPayload("Texture"))
			{
				const file::path filepath = editor::asset_drag::path_of(*payload);
				if (filepath.filename().empty())
				{
					Debug::PrintLog(spdlog::level::info, "Empty Texture File Name");
				}
				else if (const FileGuid guid = DataSystems->GetFileGuid(filepath);
					FileGuid{} == guid)
				{
					// GUID 없는 드롭을 받으면 화면에는 보여도 저장이 안 된다 —
					// 조용한 소실보다 거부가 낫다.
					Debug::PrintLog(spdlog::level::warn, "드롭한 텍스처에 .meta GUID가 없다 — "
						"저작을 거부한다: " + filepath.string());
				}
				else
				{
					apply(DataSystems->LoadSharedMaterialTexture(
						filepath.string(), compress));
					MaterialScriptBinding::SetTexture(mat, propertyName, guid,
						instance);
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (current)
		{
			ImGui::SameLine(0.f, gap);
			if (ImGui::Button(EditorIcon::Label<EditorIcon::Close, "##ClearTexture">, ImVec2(square, square)))
			{
				legacyNameField.clear();
				MaterialScriptBinding::SetTexture(mat, propertyName, {},
					instance);
				apply({});
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear texture");
		}

		ImGui::PopID();
	}
}

void TextureDropTarget(Material* mat,
	experiment::MaterialInstance* instance)
{
	const editor::widgets::property_sheet sheet = MaterialSheet();
	ImGui::PushID(mat);
	DrawMaterialTextureSlot(sheet, *mat, "Base Map",
		standard_material::property::BaseColorMap, mat->m_baseColorTexName,
		mat->GetBaseColorMapShared(), true,
		[mat](std::shared_ptr<Texture> texture)
		{
			mat->UseBaseColorMap(std::move(texture));
		}, instance);
	DrawMaterialTextureSlot(sheet, *mat, "Normal Map",
		standard_material::property::NormalMap, mat->m_normalTexName,
		mat->GetNormalMapShared(), false,
		[mat](std::shared_ptr<Texture> texture)
		{
			mat->UseNormalMap(std::move(texture));
		}, instance);
	DrawMaterialTextureSlot(sheet, *mat, "ORM Map",
		standard_material::property::OrmMap, mat->m_ORM_TexName,
		mat->GetOccRoughMetalMapShared(), false,
		[mat](std::shared_ptr<Texture> texture)
		{
			mat->UseOccRoughMetalMap(std::move(texture));
		}, instance);
	ImGui::PopID();
}

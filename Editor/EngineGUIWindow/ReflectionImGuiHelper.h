#pragma once
#include "ReflectionUndo.h" // MakeCustomChangeCommand — 에디터 층 Undo(E1-6 이관)

// 백엔드 중립 ImTextureID 변환(구현은 RenderEngine/EditorImGuiTexture.cpp).
// 이 헤더는 Utility_Framework 소속이라 RenderEngine 헤더를 못 끌어온다 —
// 전방 선언으로 계층을 지키고 링크가 잇는다.
class Texture;
namespace EditorImGuiTexture { unsigned long long From(Texture* texture); }
#include "ReflectionFunction.h"
// ReflectionFunction.h가 imgui를 대신 끌어와 주고 있었다 — 정작 그쪽은
// ImGui 심볼을 하나도 쓰지 않으면서 UF 전체를 imgui에 묶고 있었다.
// 실제로 쓰는 여기서 직접 든다.
#include <imgui.h>
#include "ReflectionRegister.h"
#include "SceneManager.h"
// GetActiveScene()->GetEntity(...) 호출이 Scene 완전 타입을 요구한다.
// 예전에는 Entity.h → Entity.inl → Scene.h 전이로 우연히 왔지만
// 그 간선이 제거되어(EntityAt 우회) 직접 세운다.
#include "Scene.h"
// Texture 드래그드롭 처리(LoadManagedFromPath)가 완전 타입을 요구한다.
#include "Texture.h"
// GameObject의 완전한 정의. 드래그&드롭 처리에서 GameObject::Index와 멤버 함수를
// 쓰는데, 예전에는 유니티 블롭의 다른 파일을 거쳐 전이적으로 딸려 왔다.
// (이 헤더가 코어에 있으면서 ScriptBinder를 참조하는 것 자체는 4-3에서 해소할 부채다)
#include "Entity.h"
#include "TypeTrait.h"
#include "InputManager.h"
#include <algorithm>
#include <any>
#include <string>
#include <unordered_map>
#include <vector>

using namespace TypeTrait;
namespace Meta
{
    // 콜백 함수: 입력 텍스트 버퍼 크기가 부족할 때 std::string을 재조정
    inline int InputTextCallback(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            // UserData에 저장된 std::string 포인터를 가져옴
            std::string* str = static_cast<std::string*>(data->UserData);
            // 새로운 길이에 맞춰 std::string의 크기 재조정
            str->resize(data->BufTextLen);
            data->Buf = const_cast<char*>(str->c_str());
        }
        return 0;
    }

    inline void DrawObject(void* instance, const Type& type);

    // CT6-c typed Draw 레지스트리 — 썽크는 ReflectionTypedDraw.h(템플릿),
    // 등록은 InspectorWindow ctor(전 타입 인스턴스화를 한 TU에 가둔다).
    namespace TypedDraw
    {
        using DrawFn = void(*)(void* instance);

        inline std::unordered_map<size_t, DrawFn>& Registry()
        {
            static std::unordered_map<size_t, DrawFn> s_map;
            return s_map;
        }

        inline DrawFn FindDraw(size_t typeID)
        {
            auto& m = Registry();
            auto it = m.find(typeID);
            return (it != m.end()) ? it->second : nullptr;
        }
    }

    enum class MethodInputKind { Int, Float, Bool, String, Unsupported };

    inline MethodInputKind MethodInputOf(const MethodParameter& param)
    {
        if (param.typeID == GUIDCreator::GetTypeID<int>()) return MethodInputKind::Int;
        if (param.typeID == GUIDCreator::GetTypeID<float>()) return MethodInputKind::Float;
        if (param.typeID == GUIDCreator::GetTypeID<bool>()) return MethodInputKind::Bool;
        if (param.typeID == GUIDCreator::GetTypeID<std::string>()) return MethodInputKind::String;
        return MethodInputKind::Unsupported;
    }

    struct MethodUiState
    {
        std::vector<std::any> arguments;
        std::string feedback;
        bool failed = false;
        int lastSeenFrame = 0;
    };

    inline std::string MethodResultText(const std::any& result)
    {
        if (!result.has_value()) return "Invoked";
        if (const auto* value = std::any_cast<int>(&result)) return "Result: " + std::to_string(*value);
        if (const auto* value = std::any_cast<float>(&result)) return "Result: " + std::to_string(*value);
        if (const auto* value = std::any_cast<bool>(&result)) return *value ? "Result: true" : "Result: false";
        if (const auto* value = std::any_cast<std::string>(&result)) return "Result: " + *value;
        return "Invoked (result cannot be displayed)";
    }

    inline void InvokeInspectorMethod(void* instance, const Method& method, MethodUiState& state)
    {
        try
        {
            state.feedback = MethodResultText(method.invoker(instance, state.arguments));
            state.failed = false;
        }
        catch (const std::exception& e)
        {
            if (!state.failed || state.feedback != e.what())
                Debug::PrintLog(spdlog::level::err, e.what());
            state.feedback = e.what();
            state.failed = true;
        }
    }

    inline void DrawMethodFeedback(const MethodUiState& state)
    {
        if (state.feedback.empty()) return;
        const ImVec4 color = state.failed ? ImVec4(1.f, .4f, .4f, 1.f)
            : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", state.feedback.c_str());
        ImGui::PopStyleColor();
    }

    // CT7: 속성은 typed Draw 경로지만 메서드는 이 공통 UI가 담당한다.
    inline void DrawMethods(void* instance, const Type& type)
    {
        const bool hasVisibleMethod = std::any_of(type.methods.begin(), type.methods.end(),
            [](const Method& method) { return !method.inspectorHidden; });
        if (!hasVisibleMethod) return;

        // ImGui ID는 상위 컴포넌트 ID, 인스턴스 포인터, 메서드 순서를 포함한다.
        // 접은 컴포넌트의 임시 입력도 잠시 보존하고 오래 안 쓴 상태는 회수한다.
        static std::unordered_map<ImGuiID, MethodUiState> states;
        static int lastCleanupFrame = 0;
        const int frame = ImGui::GetFrameCount();
        if (frame - lastCleanupFrame >= 600)
        {
            for (auto it = states.begin(); it != states.end();)
            {
                if (frame - it->second.lastSeenFrame > 3600) it = states.erase(it);
                else ++it;
            }
            lastCleanupFrame = frame;
        }

        ImGui::SeparatorText("Methods");
        ImGui::PushID(instance);
        for (size_t methodIndex = 0; methodIndex < type.methods.size(); ++methodIndex)
        {
            const Method& method = type.methods[methodIndex];
            if (method.inspectorHidden) continue;
            ImGui::PushID(static_cast<int>(methodIndex));
            MethodUiState& state = states[ImGui::GetID("##methodState")];
            state.lastSeenFrame = frame;
            if (state.arguments.size() != method.parameters.size())
                state.arguments.resize(method.parameters.size());

            if (method.inspectorReadOnly && method.parameters.empty())
            {
                InvokeInspectorMethod(instance, method, state);
                ImGui::TextUnformatted(method.name);
                DrawMethodFeedback(state);
            }
            else if (method.parameters.empty())
            {
                if (ImGui::Button(method.name)) InvokeInspectorMethod(instance, method, state);
                DrawMethodFeedback(state);
            }
            else if (ImGui::TreeNode(method.name))
            {
                bool supported = true;
                for (size_t i = 0; i < method.parameters.size(); ++i)
                {
                    const MethodParameter& param = method.parameters[i];
                    std::any& argument = state.arguments[i];
                    ImGui::PushID(static_cast<int>(i));
                    switch (MethodInputOf(param))
                    {
                    case MethodInputKind::Int:
                        if (!std::any_cast<int>(&argument)) argument = 0;
                        ImGui::TextUnformatted(param.name.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::InputInt("##value", std::any_cast<int>(&argument))) state.feedback.clear();
                        break;
                    case MethodInputKind::Float:
                        if (!std::any_cast<float>(&argument)) argument = 0.f;
                        ImGui::TextUnformatted(param.name.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::InputFloat("##value", std::any_cast<float>(&argument))) state.feedback.clear();
                        break;
                    case MethodInputKind::Bool:
                        if (!std::any_cast<bool>(&argument)) argument = false;
                        if (ImGui::Checkbox(param.name.c_str(), std::any_cast<bool>(&argument))) state.feedback.clear();
                        break;
                    case MethodInputKind::String:
                    {
                        if (!std::any_cast<std::string>(&argument)) argument = std::string{};
                        auto* value = std::any_cast<std::string>(&argument);
                        ImGui::TextUnformatted(param.name.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::InputText("##value", value->data(), value->capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize, InputTextCallback, value))
                            state.feedback.clear();
                        break;
                    }
                    case MethodInputKind::Unsupported:
                        supported = false;
                        ImGui::TextDisabled("%s: %s is not supported", param.name.c_str(), param.typeName.c_str());
                        break;
                    }
                    ImGui::PopID();
                }

                ImGui::BeginDisabled(!supported);
                const bool invoke = ImGui::Button("Invoke");
                ImGui::EndDisabled();
                if (invoke && supported) InvokeInspectorMethod(instance, method, state);
                DrawMethodFeedback(state);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // 열거형 점검(8-17): 이름 키 재조회(EnumRegistry->Find — 같은 것을 두 번
    // 찾는 이중 조회였다)를 Property::enumType 직접 참조로 대체. 파라미터로
    // 받던 EnumType*도 prop이 이미 들고 있으므로 시그니처에서 내렸다.
    // `comboLabel` 을 주면 그 라벨로 그린다 — 공통 배치 줄이 라벨을 이미 놓았을 때 "##" 로 넘긴다.
    inline void DrawEnumProperty(int* instance, const Property& prop, const char* comboLabel = nullptr)
    {
        if (const EnumType* enumType = prop.enumType)
        {
            std::vector<const char*> items;
            int prevValue = *instance;
            int current_index = 0;
            for (size_t i = 0; i < enumType->values.size(); i++)
            {
                items.push_back(enumType->values[i].name);
                if (enumType->values[i].value == *instance)
                    current_index = static_cast<int>(i);
            }

            ImGui::PushID(prop.name);
            if (ImGui::Combo(comboLabel ? comboLabel : prop.name, &current_index, items.data(), static_cast<int>(items.size())))
            {
                Meta::MakeCustomChangeCommand(
                    [=]
                    {
                        *instance = prevValue;
                    },
                    [=]
                    {
                        *instance = enumType->values[current_index].value;
                    }
                );
                *instance = enumType->values[current_index].value;

            }
            ImGui::PopID();
        }
    }

    inline void DrawObject(void* instance, const Type& type)
    {
        // CT7: typed Draw 단일 경로 — 레거시 체인·A/B 토글 은퇴.
        if (TypedDraw::DrawFn fn = TypedDraw::FindDraw(type.typeID.m_ID_Data))
        {
            fn(instance);
            return;
        }
        ImGui::Text("%s: typed draw 미등록 (REFLECT_TYPE_LIST 확인)", type.name.c_str());
    }
}

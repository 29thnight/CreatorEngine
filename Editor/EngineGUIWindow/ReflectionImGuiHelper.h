#include "../../Engine/Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../Engine/Containers/ContainerAPI.h"
#pragma once
#include "../EngineEntry/Editing/ReflectionUndo.h" // MakeCustomChangeCommand — 에디터 층 Undo(E1-6 이관)

// 백엔드 중립 ImTextureID 변환(구현은 RenderEngine/EditorImGuiTexture.cpp).
// 이 헤더는 Utility_Framework 소속이라 RenderEngine 헤더를 못 끌어온다 —
// 전방 선언으로 계층을 지키고 링크가 잇는다.
class Texture;
namespace EditorImGuiTexture
{
    unsigned long long From(const Texture* texture);
}
#include "../../Engine/Utility_Framework/Reflection/ReflectionFunction.h"
// ReflectionFunction.h가 imgui를 대신 끌어와 주고 있었다 — 정작 그쪽은
// ImGui 심볼을 하나도 쓰지 않으면서 UF 전체를 imgui에 묶고 있었다.
// 실제로 쓰는 여기서 직접 든다.
#include <imgui.h>
#include "../ImGuiHelper/ReflectedStringInput.h"
#include "../ImGuiHelper/ReflectedMethodText.h"
#include "../../Engine/Utility_Framework/Reflection/ReflectionRegister.h"
#include "../../Engine/Utility_Framework/Reflection/ReflgenRuntime.h"
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
#include "../../Engine/Utility_Framework/Reflection/TypeTrait.h"
#include "InputManager.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

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

    inline void DrawObject(void* instance, const reflgen::type_descriptor& type);

    // CT6-c typed Draw 레지스트리 — 썽크는 ReflectionTypedDraw.h(템플릿),
    // 등록은 InspectorWindow ctor(전 타입 인스턴스화를 한 TU에 가둔다).
    namespace TypedDraw
    {
        using DrawFn = void(*)(void* instance);

        inline ce::engine_node_map<size_t, DrawFn>& Registry()
        {
            static ce::engine_node_map<size_t, DrawFn> s_map;
            return s_map;
        }

        inline DrawFn FindDraw(size_t typeID)
        {
            auto& m = Registry();
            auto it = m.find(typeID);
            return (it != m.end()) ? it->second : nullptr;
        }
    }

    struct MethodUiState
    {
        ce::dynamic_array<std::any> arguments;
        std::string feedback;
        bool failed = false;
        int lastSeenFrame = 0;
    };

    inline void InvokeInspectorMethod(void* instance, const reflgen::method_info& method, MethodUiState& state)
    {
        try
        {
            ce::dynamic_array<void*> arguments;
            arguments.reserve(state.arguments.size());
            for (std::any& argument : state.arguments)
            {
                void* address = MethodArgumentAddress(argument);
                if (nullptr == address) throw std::invalid_argument("method '" + std::string(method.name()) + "': argument is not set");
                arguments.push_back(address);
            }
            state.feedback = InvokeAndDescribe(instance, method, arguments);
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
    // instance 는 메서드를 선언한 타입(methods 의 서술자 타입)의 객체다 — DrawFrame 이 그 타입 포인터로 넘긴다.
    inline void DrawMethods(void* instance, std::span<const reflgen::method_info> methods)
    {
        const bool hasVisibleMethod = std::any_of(methods.begin(), methods.end(),
            [](const reflgen::method_info& method) { return !method.attributes().contains<creator::hide_in_inspector>(); });
        if (!hasVisibleMethod) return;

        // ImGui ID는 상위 컴포넌트 ID, 인스턴스 포인터, 메서드 순서를 포함한다.
        // 접은 컴포넌트의 임시 입력도 잠시 보존하고 오래 안 쓴 상태는 회수한다.
        static ce::engine_node_map<ImGuiID, MethodUiState> states;
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
        for (size_t methodIndex = 0; methodIndex < methods.size(); ++methodIndex)
        {
            const reflgen::method_info& method = methods[methodIndex];
            if (method.attributes().contains<creator::hide_in_inspector>()) continue;
            const std::string methodName(method.name());
            const std::span<const reflgen::parameter_info> parameters = method.parameters();
            ImGui::PushID(static_cast<int>(methodIndex));
            MethodUiState& state = states[ImGui::GetID("##methodState")];
            state.lastSeenFrame = frame;
            if (state.arguments.size() != parameters.size())
                state.arguments.resize(parameters.size());

            if (method.attributes().contains<creator::read_only_in_inspector>() && parameters.empty())
            {
                InvokeInspectorMethod(instance, method, state);
                ImGui::TextUnformatted(methodName.c_str());
                DrawMethodFeedback(state);
            }
            else if (parameters.empty())
            {
                if (ImGui::Button(methodName.c_str())) InvokeInspectorMethod(instance, method, state);
                DrawMethodFeedback(state);
            }
            else if (ImGui::TreeNode(methodName.c_str()))
            {
                bool supported = true;
                for (size_t i = 0; i < parameters.size(); ++i)
                {
                    const reflgen::parameter_info& param = parameters[i];
                    const std::string paramName(param.name());
                    std::any& argument = state.arguments[i];
                    ImGui::PushID(static_cast<int>(i));
                    switch (MethodInputOf(param.type()))
                    {
                    case MethodInputKind::Int:
                        if (!std::any_cast<int>(&argument)) argument = 0;
                        ImGui::TextUnformatted(paramName.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::InputInt("##value", std::any_cast<int>(&argument))) state.feedback.clear();
                        break;
                    case MethodInputKind::Float:
                        if (!std::any_cast<float>(&argument)) argument = 0.f;
                        ImGui::TextUnformatted(paramName.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::InputFloat("##value", std::any_cast<float>(&argument))) state.feedback.clear();
                        break;
                    case MethodInputKind::Bool:
                        if (!std::any_cast<bool>(&argument)) argument = false;
                        if (ImGui::Checkbox(paramName.c_str(), std::any_cast<bool>(&argument))) state.feedback.clear();
                        break;
                    case MethodInputKind::String:
                    {
                        if (!std::any_cast<std::string>(&argument)) argument = std::string{};
                        auto* value = std::any_cast<std::string>(&argument);
                        ImGui::TextUnformatted(paramName.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        if (ImGui::InputText("##value", value->data(), value->capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize, InputTextCallback, value))
                            state.feedback.clear();
                        break;
                    }
                    case MethodInputKind::Utf8String:
                    {
                        if (!std::any_cast<ce::utf8_string>(&argument)) argument = ce::utf8_string{};
                        auto& value = *std::any_cast<ce::utf8_string>(&argument);
                        ImGui::TextUnformatted(paramName.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        auto edit = editor::text::InputReflectedString("##value", value);
                        if (edit.changed)
                        {
                            value = edit.value();
                            edit.accept(value);
                            state.feedback.clear();
                        }
                        supported = supported && bool(edit.status) && !edit.conflict;
                        editor::text::DrawStringInputIssue(edit.status, edit.conflict);
                        break;
                    }
                    case MethodInputKind::Utf16String:
                    {
                        if (!std::any_cast<ce::string>(&argument)) argument = ce::string{};
                        auto& value = *std::any_cast<ce::string>(&argument);
                        ImGui::TextUnformatted(paramName.c_str());
                        ImGui::SetNextItemWidth(-1.f);
                        auto edit = editor::text::InputReflectedString("##value", value);
                        if (edit.changed)
                        {
                            value = edit.value();
                            edit.accept(value);
                            state.feedback.clear();
                        }
                        supported = supported && bool(edit.status) && !edit.conflict;
                        editor::text::DrawStringInputIssue(edit.status, edit.conflict);
                        break;
                    }
                    case MethodInputKind::Unsupported:
                        supported = false;
                        ImGui::TextDisabled("%s: %s is not supported", paramName.c_str(), std::string(param.type_name()).c_str());
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

    // 열거형 점검(8-17): 필드가 자기 enum 표를 직접 든다(field_info::enumeration()) — 이름 키 재조회가 없다.
    // value 는 그 필드의 enum 객체다. 기반 타입의 폭은 서술자가 맞춰 읽고 쓴다(옛 경로는 int* 로 받았다).
    // `comboLabel` 을 주면 그 라벨로 그린다 — 공통 배치 줄이 라벨을 이미 놓았을 때 "##" 로 넘긴다.
    inline void DrawEnumProperty(void* value, const reflgen::field_info& field, const char* comboLabel = nullptr)
    {
        if (const reflgen::enum_descriptor* enumeration = field.enumeration())
        {
            ce::dynamic_array<const char*> items;
            const long long prevValue = enumeration->read(value);
            int current_index = 0;
            const std::span<const reflgen::enum_descriptor::entry> entries = enumeration->entries();
            for (size_t i = 0; i < entries.size(); i++)
            {
                items.push_back(entries[i].name.data()); // 열거자 이름 표는 NUL 로 끝난다(reflgen 이 보장)
                if (entries[i].value == prevValue)
                    current_index = static_cast<int>(i);
            }

            const std::string fieldName(field.name());
            ImGui::PushID(fieldName.c_str());
            if (ImGui::Combo(comboLabel ? comboLabel : fieldName.c_str(), &current_index, items.data(), static_cast<int>(items.size())))
            {
                const long long nextValue = entries[current_index].value;
                Meta::MakeCustomChangeCommand(
                    [=]
                    {
                        enumeration->write(value, prevValue);
                    },
                    [=]
                    {
                        enumeration->write(value, nextValue);
                    }
                );
                enumeration->write(value, nextValue);

            }
            ImGui::PopID();
        }
    }

    inline void DrawObject(void* instance, const reflgen::type_descriptor& type)
    {
        // CT7: typed Draw 단일 경로 — 레거시 체인·A/B 토글 은퇴.
        if (TypedDraw::DrawFn fn = TypedDraw::FindDraw(TypeIDOf(type).m_ID_Data))
        {
            fn(instance);
            return;
        }
        ImGui::Text("%s: typed draw 미등록 (REFLECT_TYPE_LIST 확인)", std::string(type.name()).c_str());
    }
}

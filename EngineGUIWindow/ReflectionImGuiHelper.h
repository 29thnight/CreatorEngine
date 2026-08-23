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

    // CT7: 레거시 프로퍼티 위젯 체인(DrawProperties)은 은퇴했다 — typed Draw
    // (ReflectionTypedDraw.h)가 단일 경로다. 파리티는 CT6-c A/B 캡처로 증명됐다.
    inline void DrawMethods(void* instance, const Type& type)
    {
        // 하나의 정적 컨테이너로 모든 매개변수를 관리합니다.
        static std::unordered_map<std::string, std::any> paramValues;

        for (const auto& method : type.methods)
        {
            if (method.parameters.empty())
            {
                ImGui::Text("Function: ");
                ImGui::SameLine();
                if (ImGui::Button(method.name))
                {
                    try
                    {
                        method.invoker(instance, {});
                    }
                    catch (const std::exception& e)
                    {
                        Debug->LogError(e.what());
                    }
                }
            }
            else
            {
                if (ImGui::TreeNode(method.name))
                {
                    // CT1: 키에 타입명을 포함한다 — paramValues는 전 컴포넌트가
                    // 공유하는 static 맵이라, 동명 메서드(같은 인덱스)의 입력값이
                    // 타입 경계를 넘어 서로 새어 들어갔다. 조립도 메서드당 1회로.
                    const std::string keyBase = type.name + "_" + std::string(method.name) + "_param_";

                    // 각 매개변수에 대해 고유한 키 생성
                    for (size_t i = 0; i < method.parameters.size(); i++)
                    {
                        const auto& param = method.parameters[i];
                        std::string key = keyBase + std::to_string(i);

                        // 해당 키가 컨테이너에 없다면, 기본값을 설정
                        if (paramValues.find(key) == paramValues.end())
                        {
                            if (std::string(param.typeName) == "int")
                                paramValues[key] = 0;
                            else if (std::string(param.typeName) == "float")
                                paramValues[key] = 0.0f;
                            else if (std::string(param.typeName) == "bool")
                                paramValues[key] = false;
                            else if (param.typeID == GUIDCreator::GetTypeID<std::string>())
                                paramValues[key] = std::string();
                            // 여기서 다른 지원 타입에 대한 기본값을 추가할 수 있음
                        }

                        // 각 타입별로 UI 위젯을 출력합니다.
                        if (std::string(param.typeName) == "int")
                        {
                            int value = std::any_cast<int>(paramValues[key]);
                            ImGui::InputInt(param.name.c_str(), &value);
                            paramValues[key] = value;
                        }
                        else if (std::string(param.typeName) == "float")
                        {
                            float value = std::any_cast<float>(paramValues[key]);
                            ImGui::InputFloat(param.name.c_str(), &value);
                            paramValues[key] = value;
                        }
                        else if (std::string(param.typeName) == "bool")
                        {
                            bool value = std::any_cast<bool>(paramValues[key]);
                            ImGui::Checkbox(param.name.c_str(), &value);
                            paramValues[key] = value;
                        }
                        else if (param.typeID == GUIDCreator::GetTypeID<std::string>())
                        {
                            std::string value = std::any_cast<std::string>(paramValues[key]);
                            // C 스타일 버퍼가 필요하므로 임시 버퍼 사용
                            char buf[128];
                            // strncpy_s를 사용하여 안전하게 문자열 복사 (_TRUNCATE: 출력 버퍼 크기를 넘어가면 잘라냄)
                            strncpy_s(buf, sizeof(buf), value.c_str(), _TRUNCATE);
                            buf[sizeof(buf) - 1] = '\0';
                            if (ImGui::InputText(param.name.c_str(), buf, sizeof(buf)))
                            {
                                paramValues[key] = std::string(buf);
                            }
                        }
                        else
                        {
                            // std::string을 varargs(%s)에 그대로 넘기던 UB도 함께 수정
                            ImGui::Text("Parameter %s of type %s is not supported.",
                                param.name.c_str(), param.typeName.c_str());
                        }
                    }

                    if (ImGui::Button("Invoke"))
                    {
                        std::vector<std::any> args;
                        for (size_t i = 0; i < method.parameters.size(); i++)
                        {
                            std::string key = keyBase + std::to_string(i);
                            args.push_back(paramValues[key]);
                        }
                        try
                        {
                            method.invoker(instance, args);
                        }
                        catch (const std::exception& e)
                        {
                            Debug->LogError(e.what());
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
    }

    // 열거형 점검(8-17): 이름 키 재조회(EnumRegistry->Find — 같은 것을 두 번
    // 찾는 이중 조회였다)를 Property::enumType 직접 참조로 대체. 파라미터로
    // 받던 EnumType*도 prop이 이미 들고 있으므로 시그니처에서 내렸다.
    inline void DrawEnumProperty(int* instance, const Property& prop)
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
            if (ImGui::Combo(prop.name, &current_index, items.data(), static_cast<int>(items.size())))
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

#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
#include "SceneManager.h"
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

    inline void DrawProperties(void* instance, const Type& type)
    {
        for (const auto& prop : type.properties)
        {
            if (prop.name == "m_isEnabled" || (prop.typeID == type_guid(Object) && prop.name == "m_name"))
                continue;

            const HashedGuid hash = prop.typeID;
            if (hash == GUIDCreator::GetTypeID<int>())
            {
                int value = std::any_cast<int>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragInt(prop.name, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<unsigned int>() || prop.typeName == "UINT")
            {
                unsigned int value = std::any_cast<unsigned int>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragScalar(prop.name, ImGuiDataType_U32, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<long long>())
            {
                long long value = std::any_cast<long long>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragScalar(prop.name, ImGuiDataType_S64, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<float>())
            {
                float value = std::any_cast<float>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat(prop.name, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<bool>() || prop.typeName == "bool32")
            {
                bool value = std::any_cast<bool>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::Checkbox(prop.name, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<std::string>())
            {
                std::string value = std::any_cast<std::string>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::InputText(prop.name,
                    value.data(),
                    value.size() + 1,
                    ImGuiInputTextFlags_CallbackResize,
                    Meta::InputTextCallback,
                    static_cast<void*>(&value)))
                {
                    if (InputManagement->IsKeyPressed(VK_RETURN))
                    {
                        MakePropChangeCommand(instance, prop, value);
                        prop.setter(instance, value);
                    }
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<const char*>())
            {
                const char* value = std::any_cast<const char*>(prop.getter(instance));
                ImGui::PushID(prop.name);
                ImGui::Text(value);
                ImGui::PopID();
            }//[OverWatching]
            else if (hash == GUIDCreator::GetTypeID<std::vector<std::string>>())
            {
                auto iter = prop.createVectorIterator(instance);
                ImGui::PushID(prop.name);
                std::vector<std::string> temp;
                while (iter->IsValid())
                {
                    std::string str = *static_cast<std::string*>(iter->Get());
                    temp.push_back(str);
                    iter->Next();
                }
                if (ImGui::CollapsingHeader(prop.name)) {
                    if (ImGui::Button("Add")) {
                        temp.push_back("");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        if (!temp.empty())
						    temp.pop_back();
                    }

                    char buf[128];
                    int size = temp.size();
                    for (int i = 0; i < temp.size(); i++) {
                        ImGui::PushID(i);

                        // strncpy_s를 사용하여 안전하게 문자열 복사 (_TRUNCATE: 출력 버퍼 크기를 넘어가면 잘라냄)
                        strncpy_s(buf, sizeof(buf), temp[i].c_str(), _TRUNCATE);
                        buf[sizeof(buf) - 1] = '\0';
                        if (ImGui::InputText(("##" + std::to_string(i)).c_str(), buf, sizeof(buf)))
                        {
                            temp[i] = std::string(buf);
                        }

                        if (size > 0) {
                            ImGui::SameLine();
                            if (ImGui::Button("^") && i > 0) {
                                std::string t = temp[i];
                                temp[i] = temp[i - 1];
                                temp[i - 1] = t;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("v") && i < size - 1) {
                                std::string t = temp[i];
                                temp[i] = temp[i + 1];
                                temp[i + 1] = t;
                            }
                        }
                        ImGui::PopID();
                    }

                    auto castInstance = reinterpret_cast<std::vector<std::string>*>(reinterpret_cast<char*>(instance) + prop.offset);
                    castInstance->clear(); // Clear existing elements
                    for (const auto& elem : temp)
                    {
                        castInstance->push_back(elem);
                    }
                }

                ImGui::PopID();

     //           auto iter = prop.createVectorIterator(instance);

     //           std::vector<std::string> value;
     //           int size = 0;
     //           while (iter->IsValid())
     //           {
     //               std::string str = *static_cast<std::string*>(iter->Get());
     //               value.push_back(str);
     //               iter->Next();
     //           }

     //           if (ImGui::Button("Add")) {
					//value.push_back("");
     //           }

     //           /* std::vector<std::string> value = std::any_cast<std::vector<std::string>>(prop.getter(instance));*/
     //           if (value.empty()) return;

     //           int selectedIndex = 0; // 안전하게 관리할 방법이 있다면 외부에서 가져와도 됨

     //           if (selectedIndex >= value.size())
     //               selectedIndex = 0;

     //           const char* currentLabel = value[selectedIndex].c_str();

     //           ImGui::PushID(prop.name);
     //           if (ImGui::BeginCombo("Ani list", currentLabel))
     //           {
     //               for (int i = 0; i < value.size(); ++i)
     //               {
     //                   const bool isSelected = (selectedIndex == i);
     //                   if (ImGui::Selectable(value[i].c_str(), isSelected))
     //                   {
     //                       selectedIndex = i;
     //                       if (prop.setter)
     //                           prop.setter(instance, value[i]);
     //                   }

     //                   if (isSelected)
     //                   {

     //                   }
     //                   //ImGui::SetItemDefaultFocus();
     //               }

     //               ImGui::EndCombo();
     //           }
     //           ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<std::vector<int>>()) {
                auto iter = prop.createVectorIterator(instance);
                ImGui::PushID(prop.name);
                std::vector<int> temp;
                while (iter->IsValid())
                {
                    int i = *static_cast<int*>(iter->Get());
                    temp.push_back(i);
                    iter->Next();
                }
                if (ImGui::CollapsingHeader(prop.name)) {
                    if (ImGui::Button("Add")) {
                        temp.push_back(0);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        if (!temp.empty())
                            temp.pop_back();
                    }

                    int buf;
                    int size = temp.size();
                    for (int i = 0; i < temp.size(); i++) {
                        ImGui::PushID(i);

                        buf = temp[i];
                        if (ImGui::InputInt(("##" + std::to_string(i)).c_str(), &buf))
                        {
                            temp[i] = buf;
                        }

                        if (size > 0) {
                            ImGui::SameLine();
                            if (ImGui::Button("^") && i > 0) {
                                int t = temp[i];
                                temp[i] = temp[i - 1];
                                temp[i - 1] = t;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("v") && i < size - 1) {
                                int t = temp[i];
                                temp[i] = temp[i + 1];
                                temp[i + 1] = t;
                            }
                        }
                        ImGui::PopID();
                    }

                    auto castInstance = reinterpret_cast<std::vector<int>*>(reinterpret_cast<char*>(instance) + prop.offset);
                    castInstance->clear(); // Clear existing elements
                    for (const auto& elem : temp)
                    {
                        castInstance->push_back(elem);
                    }
                }

                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<std::vector<int>>()) {
                auto iter = prop.createVectorIterator(instance);
                ImGui::PushID(prop.name);
                std::vector<int> temp;
                while (iter->IsValid())
                {
                    int i = *static_cast<int*>(iter->Get());
                    temp.push_back(i);
                    iter->Next();
                }
                if (ImGui::CollapsingHeader(prop.name)) {
                    if (ImGui::Button("Add")) {
                        temp.push_back(0);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        if (!temp.empty())
                            temp.pop_back();
                    }

                    int buf;
                    int size = temp.size();
                    for (int i = 0; i < temp.size(); i++) {
                        ImGui::PushID(i);

                        buf = temp[i];
                        if (ImGui::InputInt(("##" + std::to_string(i)).c_str(), &buf))
                        {
                            temp[i] = buf;
                        }

                        if (size > 0) {
                            ImGui::SameLine();
                            if (ImGui::Button("^") && i > 0) {
                                int t = temp[i];
                                temp[i] = temp[i - 1];
                                temp[i - 1] = t;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("v") && i < size - 1) {
                                int t = temp[i];
                                temp[i] = temp[i + 1];
                                temp[i + 1] = t;
                            }
                        }
                        ImGui::PopID();
                    }

                    auto castInstance = reinterpret_cast<std::vector<int>*>(reinterpret_cast<char*>(instance) + prop.offset);
                    castInstance->clear(); // Clear existing elements
                    for (const auto& elem : temp)
                    {
                        castInstance->push_back(elem);
                    }
                }

                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<std::vector<float>>()) {
                auto iter = prop.createVectorIterator(instance);
                ImGui::PushID(prop.name);
                std::vector<float> temp;
                while (iter->IsValid())
                {
                    float i = *static_cast<float*>(iter->Get());
                    temp.push_back(i);
                    iter->Next();
                }
                if (ImGui::CollapsingHeader(prop.name)) {
                    if (ImGui::Button("Add")) {
                        temp.push_back(0.f);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        if (!temp.empty())
                            temp.pop_back();
                    }

                    float buf;
                    int size = temp.size();
                    for (int i = 0; i < temp.size(); i++) {
                        ImGui::PushID(i);

                        buf = temp[i];
                        if (ImGui::InputFloat(("##" + std::to_string(i)).c_str(), &buf))
                        {
                            temp[i] = buf;
                        }

                        if (size > 0) {
                            ImGui::SameLine();
                            if (ImGui::Button("^") && i > 0) {
                                float t = temp[i];
                                temp[i] = temp[i - 1];
                                temp[i - 1] = t;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("v") && i < size - 1) {
                                float t = temp[i];
                                temp[i] = temp[i + 1];
                                temp[i + 1] = t;
                            }
                        }
                        ImGui::PopID();
                    }

                    auto castInstance = reinterpret_cast<std::vector<float>*>(reinterpret_cast<char*>(instance) + prop.offset);
                    castInstance->clear(); // Clear existing elements
                    for (const auto& elem : temp)
                    {
                        castInstance->push_back(elem);
                    }
                }

                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<std::vector<Mathf::Vector2>>()) {
                auto iter = prop.createVectorIterator(instance);
                ImGui::PushID(prop.name);
                std::vector<Mathf::Vector2> temp;
                while (iter->IsValid())
                {
                    Mathf::Vector2 i = *static_cast<Mathf::Vector2*>(iter->Get());
                    temp.push_back(i);
                    iter->Next();
                }
                if (ImGui::CollapsingHeader(prop.name)) {
                    if (ImGui::Button("Add")) {
                        temp.push_back({0.f,0.f});
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        if (!temp.empty())
                            temp.pop_back();
                    }

                    Mathf::Vector2 buf;
                    int size = temp.size();
                    for (int i = 0; i < temp.size(); i++) {
                        ImGui::PushID(i);

                        buf = temp[i];
                        if (ImGui::InputFloat2(("##" + std::to_string(i)).c_str(), &buf.x))
                        {
                            temp[i] = buf;
                        }

                        if (size > 0) {
                            ImGui::SameLine();
                            if (ImGui::Button("^") && i > 0) {
                                Mathf::Vector2 t = temp[i];
                                temp[i] = temp[i - 1];
                                temp[i - 1] = t;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("v") && i < size - 1) {
                                Mathf::Vector2 t = temp[i];
                                temp[i] = temp[i + 1];
                                temp[i + 1] = t;
                            }
                        }
                        ImGui::PopID();
                    }

                    auto castInstance = reinterpret_cast<std::vector<Mathf::Vector2>*>(reinterpret_cast<char*>(instance) + prop.offset);
                    castInstance->clear(); // Clear existing elements
                    for (const auto& elem : temp)
                    {
                        castInstance->push_back(elem);
                    }
                }

                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<std::vector<Mathf::Vector3>>()) {
                auto iter = prop.createVectorIterator(instance);
                ImGui::PushID(prop.name);
                std::vector<Mathf::Vector3> temp;
                while (iter->IsValid())
                {
                    Mathf::Vector3 i = *static_cast<Mathf::Vector3*>(iter->Get());
                    temp.push_back(i);
                    iter->Next();
                }
                if (ImGui::CollapsingHeader(prop.name)) {
                    if (ImGui::Button("Add")) {
                        temp.push_back({ 0.f,0.f,0.f });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        if (!temp.empty())
                            temp.pop_back();
                    }

                    Mathf::Vector3 buf;
                    int size = temp.size();
                    for (int i = 0; i < temp.size(); i++) {
                        ImGui::PushID(i);

                        buf = temp[i];
                        if (ImGui::InputFloat3(("##" + std::to_string(i)).c_str(), &buf.x))
                        {
                            temp[i] = buf;
                        }

                        if (size > 0) {
                            ImGui::SameLine();
                            if (ImGui::Button("^") && i > 0) {
                                Mathf::Vector3 t = temp[i];
                                temp[i] = temp[i - 1];
                                temp[i - 1] = t;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("v") && i < size - 1) {
                                Mathf::Vector3 t = temp[i];
                                temp[i] = temp[i + 1];
                                temp[i + 1] = t;
                            }
                        }
                        ImGui::PopID();
                    }

                    auto castInstance = reinterpret_cast<std::vector<Mathf::Vector3>*>(reinterpret_cast<char*>(instance) + prop.offset);
                    castInstance->clear(); // Clear existing elements
                    for (const auto& elem : temp)
                    {
                        castInstance->push_back(elem);
                    }
                }

                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<HashingString>())
            {
                HashingString value = std::any_cast<HashingString>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::InputText(prop.name, value.data(), value.size() + 1))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Vector2>())
            {
                auto value = std::any_cast<Mathf::Vector2>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat2(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Vector3>())
            {
                auto value = std::any_cast<Mathf::Vector3>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat3(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Vector4>())
            {
                auto value = std::any_cast<Mathf::Vector4>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat4(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Quaternion>())
            {
                auto value = std::any_cast<Mathf::Quaternion>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat4(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Color4>())
            {
                auto value = std::any_cast<Mathf::Color4>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::ColorEdit4(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<float2>())
            {
                auto value = std::any_cast<float2>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat2(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<float3>())
            {
                auto value = std::any_cast<float3>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat3(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<float4>())
            {
                auto value = std::any_cast<float4>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat4(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<int2>())
            {
                auto value = std::any_cast<int2>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragInt2(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<int3>())
            {
                auto value = std::any_cast<int3>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragInt3(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
                ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Rect>())
            {
                auto value = std::any_cast<Mathf::Rect>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::DragFloat4(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
				}
                ImGui::PopID();
            }// 다른 타입 추가 가능
            else if (const EnumType* enumType = MetaEnumRegistry->Find(prop.typeName))
            {
                // 현재 enum 값을 정수로 얻어옵니다.
                int value = std::any_cast<int>(prop.getter(instance));
                // enum의 모든 이름을 배열에 저장합니다.
                std::vector<const char*> items;
                int current_index = 0;
                for (size_t i = 0; i < enumType->values.size(); i++)
                {
                    items.push_back(enumType->values[i].name);
                    if (enumType->values[i].value == value)
                        current_index = static_cast<int>(i);
                }
                // 콤보 박스로 enum 값을 선택할 수 있도록 합니다.
                ImGui::PushID(prop.name);
                if (ImGui::Combo(prop.name, &current_index, items.data(), static_cast<int>(items.size())))
                {
                    // 선택된 인덱스에 해당하는 enum 값으로 업데이트합니다.
                    prop.setter(instance, enumType->values[current_index].value);
                }
                ImGui::PopID();
            }
            else if (prop.isPointer)
            {
                void* ptr = TypeCast->ToVoidPtr(prop.typeInfo, prop.getter(instance));
                if (ptr)
                {
                    std::string_view view = prop.typeName.c_str();
                    size_t endPos = view.rfind("*");
                    std::string name = view.data();

                    if (prop.typeID == GUIDCreator::GetTypeID<GameObject>()) {

                        std::string copyname = name.substr(0, endPos);
                        if (const Type* subType = MetaDataRegistry->Find(copyname))
                        {
                            ImGui::PushID(prop.name);
                            if (ImGui::CollapsingHeader(prop.name, ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::Button("#script", ImVec2(150, 20));

                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
                                    {
                                        GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
                                        auto gameObject = SceneManagers->GetActiveScene()->GetGameObject(draggedIndex).get();
                                        if (gameObject->GetTypeID() == subType->typeID) {
                                            MakePropChangeCommand(instance, prop, gameObject);
                                            prop.setter(instance, gameObject);
                                        }
                                        else {
                                            auto it = gameObject->m_componentIds.find(subType->typeID);
                                            if (it != gameObject->m_componentIds.end())
                                            {
                                                size_t index = it->second;
                                                auto component = gameObject->m_components[index];
                                                if (component) {
                                                    //MakePropChangeCommand(instance, prop, component.get());
                                                    prop.setter(instance, component.get());
                                                }
                                            }
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                DrawObject(ptr, *subType);
                            }
                            ImGui::PopID();
                        }
                        else
                        {
                            //TODO : 테스트 후 제거
                            std::string_view view = prop.typeName.c_str();
                            size_t endPos = view.rfind("*");
                            std::string name = view.data();
                            std::string copyname = name.substr(0, endPos);
                            if (const Type* subType = MetaDataRegistry->Find(copyname)) {
                                ImGui::PushID(prop.name);
                                ImGui::Text("%s: [Unregistered Type For GUI Debug]", prop.name);
                                ImGui::Button("#Missing", ImVec2(150, 20));
                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
                                    {
                                        GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
                                        auto gameObject = SceneManagers->GetActiveScene()->GetGameObject(draggedIndex).get();
                                        if (gameObject->GetTypeID() == subType->typeID) {
                                            MakePropChangeCommand(instance, prop, gameObject);
                                            prop.setter(instance, gameObject);
                                        }
                                        else {
                                            auto it = gameObject->m_componentIds.find(subType->typeID);
                                            if (it != gameObject->m_componentIds.end())
                                            {
                                                size_t index = it->second;
                                                auto component = gameObject->m_components[index];
                                                if (component) {
                                                    //MakePropChangeCommand(instance, prop, component.get());
                                                    prop.setter(instance, component.get());
                                                }
                                            }
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                ImGui::PopID();
                            }
                        }
                    }
                    else if (prop.typeID == GUIDCreator::GetTypeID<Texture>()) {
                        auto texture = std::any_cast<Texture*>(prop.getter(instance));

                        if (texture) {
                            if (texture->m_pSRV)
                                ImGui::Image((ImTextureID)texture->m_pSRV, ImVec2(30, 30));
                            else {
                                ImGui::Button("None Texture", ImVec2(150, 20));
                            }
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Textures"))
                            {
                                const char* droppedFilePath = (const char*)payload->Data;
                                file::path filename = droppedFilePath;
                                file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
                                HashingString path = filepath.string();
                                if (!filename.filename().empty()) {
                                    prop.setter(instance, Texture::LoadManagedFromPath(filepath.string()).get());
                                }
                                else {
                                    Debug->Log("Empty Texture File Name");
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                }
                else
                {
                    //TODO : 테스트 후 제거
                    std::string_view view = prop.typeName.c_str();
                    size_t endPos = view.rfind("*");
                    std::string name = view.data();

                    if (prop.typeID == GUIDCreator::GetTypeID<GameObject>()) {
                        std::string copyname = name.substr(0, endPos);
                        if (const Type* subType = MetaDataRegistry->Find(copyname)) {
                            ImGui::PushID(prop.name);
                            ImGui::Text("%s: nullptr [For GUI Debug]", prop.name);
                            ImGui::Button("#Missing", ImVec2(150, 20));
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
                                {
                                    GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
                                    auto gameObject = SceneManagers->GetActiveScene()->GetGameObject(draggedIndex).get();
                                    if (gameObject->GetTypeID() == subType->typeID) {
                                        MakePropChangeCommand(instance, prop, gameObject);
                                        prop.setter(instance, gameObject);
                                    }
                                    else {
                                        auto it = gameObject->m_componentIds.find(subType->typeID);
                                        if (it != gameObject->m_componentIds.end())
                                        {
                                            size_t index = it->second;
                                            auto component = gameObject->m_components[index];
                                            if (component) {
                                                //MakePropChangeCommand(instance, prop, component.get());
                                                prop.setter(instance, component.get());
                                            }
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                            ImGui::PopID();
                        }
                    }
                    else if (prop.typeID == GUIDCreator::GetTypeID<Texture>()) {
                        ImGui::Button("None Texture", ImVec2(150, 20));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Textures"))
                            {
                                const char* droppedFilePath = (const char*)payload->Data;
                                file::path filename = droppedFilePath;
                                file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
                                HashingString path = filepath.string();
                                if (!filename.filename().empty()) {
                                    prop.setter(instance, Texture::LoadManagedFromPath(filepath.string()).get());
                                }
                                else {
                                    Debug->Log("Empty Texture File Name");
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                }
            }
            else if (nullptr != MetaDataRegistry->Find(prop.typeName))
            {
                // 기존 인스턴스의 주소에서 해당 오프셋을 더합니다.
                void* subInstance = reinterpret_cast<void*>(reinterpret_cast<char*>(instance) + prop.offset);

                if (const Meta::Type* subType = MetaDataRegistry->Find(prop.typeName))
                {
                    ImGui::PushID(prop.name);
                    if (ImGui::CollapsingHeader(prop.name, ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawObject(subInstance, *subType);
                    }
                    ImGui::PopID();
                }

            }
        }
    }

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
                    // 각 매개변수에 대해 고유한 키 생성
                    for (size_t i = 0; i < method.parameters.size(); i++)
                    {
                        const auto& param = method.parameters[i];
                        std::string key = std::string(method.name) + "_param_" + std::to_string(i);

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
                            ImGui::Text("Parameter %s of type %s is not supported.", param.name, param.typeName);
                        }
                    }

                    if (ImGui::Button("Invoke"))
                    {
                        std::vector<std::any> args;
                        for (size_t i = 0; i < method.parameters.size(); i++)
                        {
                            std::string key = std::string(method.name) + "_param_" + std::to_string(i);
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

    inline void DrawEnumProperty(int* instance, const EnumType* _enumType, const Property& prop)
    {
        if (const EnumType* enumType = MetaEnumRegistry->Find(_enumType->name))
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
        ImGui::PushID(type.name.data());
        if (type.parent)
        {
            DrawObject(instance, *type.parent);
        }

        //ImGui::Indent();
        DrawProperties(instance, type);
        DrawMethods(instance, type);
        ImGui::PopID();
        //ImGui::Unindent();
    }
}
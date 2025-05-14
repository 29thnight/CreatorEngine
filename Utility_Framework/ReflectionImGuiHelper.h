#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
#include "SceneManager.h"
#include "TypeTrait.h"

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
			if (prop.name == "m_isEnabled")
				continue;

            const HashedGuid hash = prop.typeID;
            if (hash == GUIDCreator::GetTypeID<int>())
            {
                int value = std::any_cast<int>(prop.getter(instance));
                ImGui::PushID(prop.name);
                if (ImGui::InputInt(prop.name, &value))
                {
					MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
				ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<unsigned int>())
            {
                unsigned int value = std::any_cast<unsigned int>(prop.getter(instance));
				ImGui::PushID(prop.name);
                if (ImGui::InputScalar(prop.name, ImGuiDataType_S32, &value))
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
                if (ImGui::InputScalar(prop.name, ImGuiDataType_S64, &value))
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
                if (ImGui::InputFloat(prop.name, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
				ImGui::PopID();
            }
            else if (hash == GUIDCreator::GetTypeID<bool>()|| prop.typeName == "bool32")
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
                    ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
					Meta::InputTextCallback,
					static_cast<void*>(&value)))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
				ImGui::PopID();
            }//[OverWatching]
            else if (hash == GUIDCreator::GetTypeID<std::vector<std::string>>())
            {
                auto iter = prop.createVectorIterator(instance);
                std::vector<std::string> value;
                int size = 0;
                while (iter->IsValid())
                {
                    std::string str = *static_cast<std::string*>(iter->Get());
                    value.push_back(str);
                    iter->Next();
                }

               /* std::vector<std::string> value = std::any_cast<std::vector<std::string>>(prop.getter(instance));*/
                if (value.empty()) return;

                int selectedIndex = 0; // 안전하게 관리할 방법이 있다면 외부에서 가져와도 됨

                if (selectedIndex >= value.size())
                    selectedIndex = 0;

                const char* currentLabel = value[selectedIndex].c_str();

                ImGui::PushID(prop.name);
                if (ImGui::BeginCombo("Ani list", currentLabel))
                {
                    for (int i = 0; i < value.size(); ++i)
                    {
                        const bool isSelected = (selectedIndex == i);
                        if (ImGui::Selectable(value[i].c_str(), isSelected))
                        {
                            selectedIndex = i;
                            if (prop.setter)
                                prop.setter(instance, value[i]);
                        }

                        if (isSelected)
                        {

                        }
                            //ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
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
                if (ImGui::InputInt2(prop.name, &value.x))
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
                if (ImGui::InputInt3(prop.name, &value.x))
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
                    std::string copyname = name.substr(0, endPos);
                    if (const Type* subType = MetaDataRegistry->Find(copyname))
                    {
                        ImGui::PushID(prop.name);
                        if (ImGui::CollapsingHeader(prop.name, ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            DrawObject(ptr, *subType);
                        }
                        ImGui::PopID();
                    }
                    else
                    {
                        //TODO : 테스트 후 제거
                        ImGui::Text("%s: [Unregistered Type For GUI Debug]", prop.name);
                    }
                }
                else
                {
                    //TODO : 테스트 후 제거
                    ImGui::Text("%s: nullptr [For GUI Debug]", prop.name);
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
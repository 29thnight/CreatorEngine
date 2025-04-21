#pragma once
#include "ReflectionFunction.h"

namespace Meta
{
	// �ݹ� �Լ�: �Է� �ؽ�Ʈ ���� ũ�Ⱑ ������ �� std::string�� ������
	inline int InputTextCallback(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			// UserData�� ����� std::string �����͸� ������
			std::string* str = static_cast<std::string*>(data->UserData);
			// ���ο� ���̿� ���� std::string�� ũ�� ������
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
            const HashedGuid hash = prop.typeID;
            if (hash == GUIDCreator::GetTypeID<int>())
            {
                int value = std::any_cast<int>(prop.getter(instance));
                if (ImGui::InputInt(prop.name, &value))
                {
                   /* int prevValue = std::any_cast<int>(prop.getter(instance));
                    UndoCommandManager->Execute(
                        std::make_unique<PropertyChangeCommand<int>>(instance, prop, prevValue, value)
                    );*/
					MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<unsigned int>())
            {
                unsigned int value = std::any_cast<unsigned int>(prop.getter(instance));
                if (ImGui::InputScalar(prop.name, ImGuiDataType_S32, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<long long>())
            {
                long long value = std::any_cast<long long>(prop.getter(instance));
                if (ImGui::InputScalar(prop.name, ImGuiDataType_S64, &value))
                {
					MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<float>())
            {
                float value = std::any_cast<float>(prop.getter(instance));
                if (ImGui::InputFloat(prop.name, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<bool>()|| prop.typeName == "bool32")
            {
                bool value = std::any_cast<bool>(prop.getter(instance));
                if (ImGui::Checkbox(prop.name, &value))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<std::string>())
            {
                std::string value = std::any_cast<std::string>(prop.getter(instance));
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
            }//[OverWatching]
            else if (hash == GUIDCreator::GetTypeID<HashingString>())
            {
                HashingString value = std::any_cast<HashingString>(prop.getter(instance));
                if (ImGui::InputText(prop.name, value.data(), value.size() + 1))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Vector2>())
            {
                auto value = std::any_cast<Mathf::Vector2>(prop.getter(instance));
                if (ImGui::DragFloat2(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Vector3>())
            {
                auto value = std::any_cast<Mathf::Vector3>(prop.getter(instance));
                if (ImGui::DragFloat3(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Vector4>()
                || hash == GUIDCreator::GetTypeID<Mathf::Quaternion>())
            {
                auto value = std::any_cast<Mathf::Vector4>(prop.getter(instance));
                if (ImGui::DragFloat4(prop.name, &value.x, 0.1f))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<Mathf::Color4>())
            {
                auto value = std::any_cast<Mathf::Color4>(prop.getter(instance));
                if (ImGui::ColorEdit4(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<float2>())
            {
                auto value = std::any_cast<float2>(prop.getter(instance));
                if (ImGui::DragFloat2(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<float3>())
            {
                auto value = std::any_cast<float3>(prop.getter(instance));
                if (ImGui::DragFloat3(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<float4>())
            {
                auto value = std::any_cast<float4>(prop.getter(instance));
                if (ImGui::DragFloat4(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<int2>())
            {
                auto value = std::any_cast<int2>(prop.getter(instance));
                if (ImGui::InputInt2(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }
            else if (hash == GUIDCreator::GetTypeID<int3>())
            {
                auto value = std::any_cast<int3>(prop.getter(instance));
                if (ImGui::InputInt3(prop.name, &value.x))
                {
                    MakePropChangeCommand(instance, prop, value);
                    prop.setter(instance, value);
                }
            }// �ٸ� Ÿ�� �߰� ����
            else if (const EnumType* enumType = MetaEnumRegistry->Find(prop.typeName))
            {
                // ���� enum ���� ������ ���ɴϴ�.
                int value = std::any_cast<int>(prop.getter(instance));
                // enum�� ��� �̸��� �迭�� �����մϴ�.
                std::vector<const char*> items;
                int current_index = 0;
                for (size_t i = 0; i < enumType->values.size(); i++)
                {
                    items.push_back(enumType->values[i].name);
                    if (enumType->values[i].value == value)
                        current_index = static_cast<int>(i);
                }
                // �޺� �ڽ��� enum ���� ������ �� �ֵ��� �մϴ�.
                if (ImGui::Combo(prop.name, &current_index, items.data(), static_cast<int>(items.size())))
                {
                    // ���õ� �ε����� �ش��ϴ� enum ������ ������Ʈ�մϴ�.
                    prop.setter(instance, enumType->values[current_index].value);
                }
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
                        //TODO : �׽�Ʈ �� ����
                        ImGui::Text("%s: [Unregistered Type For GUI Debug]", prop.name);
                    }
                }
                else
                {
                    //TODO : �׽�Ʈ �� ����
                    ImGui::Text("%s: nullptr [For GUI Debug]", prop.name);
                }
            }
            else if (nullptr != MetaDataRegistry->Find(prop.typeName))
            {
                // ���� �ν��Ͻ��� �ּҿ��� �ش� �������� ���մϴ�.
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
        // �ϳ��� ���� �����̳ʷ� ��� �Ű������� �����մϴ�.
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
                    // �� �Ű������� ���� ������ Ű ����
                    for (size_t i = 0; i < method.parameters.size(); i++)
                    {
                        const auto& param = method.parameters[i];
                        std::string key = std::string(method.name) + "_param_" + std::to_string(i);

                        // �ش� Ű�� �����̳ʿ� ���ٸ�, �⺻���� ����
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
                            // ���⼭ �ٸ� ���� Ÿ�Կ� ���� �⺻���� �߰��� �� ����
                        }

                        // �� Ÿ�Ժ��� UI ������ ����մϴ�.
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
                            // C ��Ÿ�� ���۰� �ʿ��ϹǷ� �ӽ� ���� ���
                            char buf[128];
                            // strncpy_s�� ����Ͽ� �����ϰ� ���ڿ� ���� (_TRUNCATE: ��� ���� ũ�⸦ �Ѿ�� �߶�)
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

    inline void DrawObject(void* instance, const Type& type)
    {
        if (type.parent)
        {
            DrawObject(instance, *type.parent);
        }

        //ImGui::Indent();
        DrawProperties(instance, type);
        DrawMethods(instance, type);
        //ImGui::Unindent();
    }
}
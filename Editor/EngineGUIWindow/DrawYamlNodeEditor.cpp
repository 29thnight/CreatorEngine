#include "Core.Minimal.h"
// Core.Minimal.h가 Reflection 사슬로 대신 끌어와 주던 것을 직접 든다.
#include <imgui.h>
#include "AuthoringWriteNode.h"
#include "ExternUI.h"
#include <unordered_set>

static const std::unordered_set<std::string> ignoredKeys = {
	"guid",
	"importSettings"
};

void DrawYamlNodeEditor(Authoring::WriteNode node, const std::string& label)
{
	const Authoring::ReadNode read = node.Read();
	if (!read || read.IsNull()) return;

	if (read.IsMap())
	{
		for (const Authoring::MapEntry pair : read.Map())
		{
			const std::string key = pair.key.AsString();
			if (ignoredKeys.count(key)) continue;

			const Authoring::ReadNode readValue = pair.value;
			const Authoring::WriteNode value = node.Child(key);

			if (readValue.IsMap() || readValue.IsSequence())
			{
				if (ImGui::TreeNode(key.c_str()))
				{
					DrawYamlNodeEditor(value, key);
					ImGui::TreePop();
				}
			}
			else
			{
				// 자동 분기 처리
				if (readValue.IsScalar())
				{
					const std::string val = readValue.AsString();
					std::istringstream iss(val);
					float f;
					int i;
					bool b;

					std::string uniqueID = key + "##" + label;

					// bool
					if (val == "true" || val == "false")
					{
						b = (val == "true");
						if (ImGui::Checkbox(uniqueID.c_str(), &b))
							value.SetScalar(b);
					}
					// int
					else if ((iss >> i) && iss.eof())
					{
						if (ImGui::InputInt(uniqueID.c_str(), &i))
							value.SetScalar(i);
					}
					// float
					else
					{
						std::istringstream iss2(val);
						if ((iss2 >> f) && iss2.eof())
						{
							if (ImGui::InputFloat(uniqueID.c_str(), &f))
								value.SetScalar(f);
						}
						else
						{
							// fallback to string
							char buffer[256];
							strcpy_s(buffer, val.c_str());
							if (ImGui::InputText(uniqueID.c_str(), buffer, sizeof(buffer)))
								value.SetScalar(std::string(buffer));
						}
					}
				}
			}
		}
	}
	else if (read.IsSequence())
	{
		for (std::size_t i = 0; i < node.Size(); ++i)
		{
			const Authoring::WriteNode element = node.At(i);
			const Authoring::ReadNode readElement = element.Read();
			std::string indexLabel = label + "[" + std::to_string(i) + "]";

			if (readElement.IsMap() || readElement.IsSequence())
			{
				if (ImGui::TreeNode(indexLabel.c_str()))
				{
					DrawYamlNodeEditor(element, indexLabel);
					ImGui::TreePop();
				}
			}
			else
			{
				const std::string val = readElement.AsString();
				std::istringstream iss(val);
				float f;
				int i;
				bool b;

				std::string uniqueID = indexLabel;

				// bool
				if (val == "true" || val == "false")
				{
					b = (val == "true");
					if (ImGui::Checkbox(uniqueID.c_str(), &b))
						element.SetScalar(b);
				}
				// int
				else if ((iss >> i) && iss.eof())
				{
					if (ImGui::InputInt(uniqueID.c_str(), &i))
						element.SetScalar(i);
				}
				// float
				else
				{
					std::istringstream iss2(val);
					if ((iss2 >> f) && iss2.eof())
					{
						if (ImGui::InputFloat(uniqueID.c_str(), &f))
							element.SetScalar(f);
					}
					else
					{
						char buffer[256];
						strcpy_s(buffer, val.c_str());
						if (ImGui::InputText(uniqueID.c_str(), buffer, sizeof(buffer)))
							element.SetScalar(std::string(buffer));
					}
				}
			}
		}
	}
}

#include "Core.Minimal.h"
#include "yaml-cpp/yaml.h"
#include "ExternUI.h"

static const std::unordered_set<std::string> ignoredKeys = {
	"guid",
	"importSettings"
};

void DrawYamlNodeEditor(YAML::Node& node, const std::string& label)
{
	if (node.IsNull()) return;

	if (node.IsMap())
	{
		for (auto it = node.begin(); it != node.end(); ++it)
		{
			std::string key = it->first.as<std::string>();
			if (ignoredKeys.count(key)) continue;

			YAML::Node& value = it->second;

			if (value.IsMap() || value.IsSequence())
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
				if (value.IsScalar())
				{
					std::string val = value.as<std::string>();
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
							value = b ? "true" : "false";
					}
					// int
					else if ((iss >> i) && iss.eof())
					{
						if (ImGui::InputInt(uniqueID.c_str(), &i))
							value = std::to_string(i);
					}
					// float
					else
					{
						std::istringstream iss2(val);
						if ((iss2 >> f) && iss2.eof())
						{
							if (ImGui::InputFloat(uniqueID.c_str(), &f))
								value = std::to_string(f);
						}
						else
						{
							// fallback to string
							char buffer[256];
							strcpy_s(buffer, val.c_str());
							if (ImGui::InputText(uniqueID.c_str(), buffer, sizeof(buffer)))
								value = std::string(buffer);
						}
					}
				}
			}
		}
	}
	else if (node.IsSequence())
	{
		for (std::size_t i = 0; i < node.size(); ++i)
		{
			YAML::Node element = node[i];
			std::string indexLabel = label + "[" + std::to_string(i) + "]";

			if (element.IsMap() || element.IsSequence())
			{
				if (ImGui::TreeNode(indexLabel.c_str()))
				{
					DrawYamlNodeEditor(element, indexLabel);
					ImGui::TreePop();
				}
			}
			else
			{
				std::string val = element.as<std::string>();
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
						element = b ? "true" : "false";
				}
				// int
				else if ((iss >> i) && iss.eof())
				{
					if (ImGui::InputInt(uniqueID.c_str(), &i))
						element = std::to_string(i);
				}
				// float
				else
				{
					std::istringstream iss2(val);
					if ((iss2 >> f) && iss2.eof())
					{
						if (ImGui::InputFloat(uniqueID.c_str(), &f))
							element = std::to_string(f);
					}
					else
					{
						char buffer[256];
						strcpy_s(buffer, val.c_str());
						if (ImGui::InputText(uniqueID.c_str(), buffer, sizeof(buffer)))
							element = std::string(buffer);
					}
				}
			}
		}
	}
}
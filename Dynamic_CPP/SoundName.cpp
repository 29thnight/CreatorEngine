#include "SoundName.h"
#include "pch.h"
#include "Core.Random.h"


SoundNames::SoundNames()
{
}

SoundNames::~SoundNames()
{
}

void SoundNames::LoadSoundNameFromCSV()
{
	soundMap.clear();
	auto path = PathFinder::Relative("CSV\\SoundName.csv");
	CSVReader rdrNew(path.string());
	for (auto& row : rdrNew)
	{
		std::string soundTag = row[0].as<std::string>();
		if (!soundTag.empty() && soundTag.back() == '\0')
			soundTag.pop_back();
		std::vector<std::string> sounds;
		
		try
		{
			for (int i = 1; i < row.GetSize(); ++i)
			{
				std::string cell = row[i].as<std::string>();
				if (!cell.empty() && cell.back() == '\0')
					cell.pop_back();
				if (cell.empty()) continue;
				sounds.push_back(cell);
			}
		}
		catch(...)
		{

		}



		//for (int i = 1; i < row.GetSize(); ++i)
		//{
		//	std::string cell = row[i].as<std::string>();

		//	// ���ڿ��� ����ְų�, ��� ���ڰ� ����(�����̽�/��/���� ��)�� ��� �ǳʶ�
		//	bool onlySpaces = std::all_of(cell.begin(), cell.end(),
		//		[](unsigned char c) { return std::isspace(c); });

		//	if (cell.empty() || onlySpaces)
		//		continue; // ��ĭ �Ǵ� ���鸸 �ִ� ĭ�̸� skip

		//	sounds.push_back(cell);
		//}

		soundMap[soundTag] = sounds;
	}
	
}

std::string SoundNames::GetSoudNameRandom(const std::string& soundTag)
{
	auto it = soundMap.find(soundTag);
	if (it == soundMap.end() || it->second.empty()) return "";
	auto& sounds = it->second;
	if (sounds.size() == 1)
		return sounds[0];
	int index = Random<int>(0, static_cast<int>(sounds.size()) - 1).Generate();
	return sounds[index];
}

std::vector<std::string> SoundNames::GetSoundNames(const std::string& soundTag)
{
	auto it = soundMap.find(soundTag);
	if (it == soundMap.end() || it->second.empty())
		return {}; 

	return it->second; 
}

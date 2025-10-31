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

		//	// 문자열이 비어있거나, 모든 문자가 공백(스페이스/탭/개행 등)인 경우 건너뜀
		//	bool onlySpaces = std::all_of(cell.begin(), cell.end(),
		//		[](unsigned char c) { return std::isspace(c); });

		//	if (cell.empty() || onlySpaces)
		//		continue; // 빈칸 또는 공백만 있는 칸이면 skip

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

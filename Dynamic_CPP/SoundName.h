#pragma once
#include "Core.Minimal.h"



//csv로 사운드이름들 읽어서 저장해두는용 
class SoundNames
{
public:
	SoundNames();
	~SoundNames();
	void LoadSoundNameFromCSV();
	//해당 태그에 있는 사운드이름중 하나
	std::string GetSoudNameRandom(const std::string& soundTag); 
	//해당태그에있는 사운드이름vector
	std::vector<std::string> GetSoundNames(const std::string& soundTag); 
	std::unordered_map<std::string, std::vector<std::string>> soundMap;
};

#pragma once
#include "LinkData.h"
#include "ArticulationData.h"

class LinkData;
class ArticulationData;

class ArticulationLoader {
public:
	void Save(const ArticulationData* data, const std::string& path);

	ArticulationData* Load(const std::string& path);
	ArticulationData* Load(const std::string& path,unsigned int& numbuer);

private:
	LinkData* LoadLinkData(const std::string& path, unsigned int& number);
};
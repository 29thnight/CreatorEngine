#include "BoneRegion.h"

#include "../Utility_Framework/StandardLibrary.h"

std::string ToLower(std::string boneName)
{
	std::transform(boneName.begin(), boneName.end(), boneName.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	return boneName;
}

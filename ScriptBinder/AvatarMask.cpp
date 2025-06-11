#include "AvatarMask.h"
#include "Skeleton.h"
bool AvatarMask::IsBoneEnabled(BoneRegion region)
{
    if (useAll)
        return true;

    switch (region)
    {
    case BoneRegion::Root:
        return useLower;
    case BoneRegion::Spine:
        return useLower;
    case BoneRegion::LeftArm:
        return useUpper;
    case BoneRegion::RightArm:
        return useUpper;
    case BoneRegion::Neck:
        return useUpper;
    case BoneRegion::LeftLeg:
        return useLower;
    case BoneRegion::RightLeg:
        return useLower;
    }

    return false;
}

bool AvatarMask::IsBoneEnabled(const std::string& name)
{
    for (const auto& mask : m_BoneMasks)
    {
        if (mask.boneName == name)
            return mask.isEnabled;
    }
    return false; 
    //&&&&& map 고려 성능
}

void AvatarMask::MakeBoneMask(std::vector<Bone*> Bones)
{
    m_BoneMasks.clear();
    for (auto& Bone : Bones)
    {
        BoneMask newMask;
        newMask.boneName = Bone->m_name;
        newMask.isEnabled = true;
        m_BoneMasks.push_back(newMask);
    }
}


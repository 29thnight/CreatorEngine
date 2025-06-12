#include "AvatarMask.h"
#include "Skeleton.h"
AvatarMask::~AvatarMask()
{
    for (auto& mask : m_BoneMasks)
    {
        delete mask;
    }
}
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
        if (mask->boneName == name)
            return mask->isEnabled;
    }
    return false; 
    //&&&&& map 고려 성능
}

BoneMask* AvatarMask::MakeBoneMask(Bone* Bone)
{

    BoneMask* newMask = new BoneMask();
    newMask->boneName = Bone->m_name;
    newMask->isEnabled = true;

    m_BoneMasks.push_back(newMask);
    for (auto& child : Bone->m_children)
    {
        BoneMask* childMask = MakeBoneMask(child);
        newMask->m_children.push_back(childMask);
    }
    return newMask;
}

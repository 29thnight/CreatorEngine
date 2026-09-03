#include "AvatarMask.h"
#include "BoneRegion.h"
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

void AvatarMask::ReCreateMask(AvatarMask* _otherMask)
{
   /* for (auto* boneMask : _otherMask->m_BoneMasks)
    {
        BoneMask* copy = new BoneMask(*boneMask); 
        m_BoneMasks.push_back(copy);
    }*/
    //m_BoneMasks = _otherMask->m_BoneMasks;
    for(int i = 0; i < _otherMask->m_BoneMasks.size(); ++i)
    {
        m_BoneMasks[i]->isEnabled = _otherMask->m_BoneMasks[i]->isEnabled;
    }
    isHumanoid = _otherMask->isHumanoid;
    useAll = _otherMask->useAll;
    useUpper = _otherMask->useUpper;
    useLower = _otherMask->useLower;
}

bool AvatarMask::IsBoneEnabled(const std::string& name)
{
    for (const auto& mask : m_BoneMasks)
    {
        if (mask == nullptr) return false;
        if (mask->boneName == name)
            return mask->isEnabled;
    }
    return false; 
}


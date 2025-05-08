#include "AvatarMask.h"
#include "Skeleton.h"
bool AvatarMask::IsBoneEnabled(BoneRegion region)
{
    if (isUpper)
    {
        return  region == BoneRegion::Neck ||
            region == BoneRegion::LeftArm ||
            region == BoneRegion::RightArm;
    }

    if (isLower)
    {
        return  region == BoneRegion::Root ||
            region == BoneRegion::Spine ||
            region == BoneRegion::LeftLeg ||
            region == BoneRegion::RightLeg;
    }

    return false;
}

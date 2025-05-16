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

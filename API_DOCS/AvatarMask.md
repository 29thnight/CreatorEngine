# AvatarMask

**Header:** `ScriptBinder/AvatarMask.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `AvatarMask() = default;`
- `~AvatarMask();`
- `bool IsBoneEnabled(BoneRegion region);`
- `void ReCreateMask(AvatarMask* _otherMask);`
- `bool IsBoneEnabled(const std::string& name);`
- `BoneMask* MakeBoneMask(Bone* Bone);`

## Public Properties
- `std::vector<BoneMask*> m_BoneMasks;`
- `bool isHumanoid = true;`
- `bool useAll = false;`
- `bool useUpper = true;`
- `bool useLower = true;`

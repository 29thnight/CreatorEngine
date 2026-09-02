#pragma once

#include <string>

namespace RenderTest
{
    // MBC1 — `ce.uuidv8.sha256.v1` 신원 프로필·충돌 registry의 합성 검사.
    //
    // 세 갈래 독립 유도가 한 값을 내는지 본다:
    //   ① 제품 C++(Hash::Sha256 + assets::DeriveIdentity)
    //   ② Python hashlib 생성기(AssetIdentityTestVectors.inl — 입력 바이트열까지 고정)
    //   ③ BCrypt(experiment::cooked::ComputeSha256) — 해시 계층 대조군
    // 그리고 fail-closed(빈 필드·비정형 UTF-8·비NFC·legacy v4/v5 표기)와 registry의
    // 네 판정(Registered/DuplicateTuple/UuidCollision/RecomputeMismatch)을 단정한다.
    //
    // ★ 벡터가 전부 초록이어도 "프로필 문자열을 바꾸면 값이 바뀐다"를 같은
    //   실행에서 단정한다 — 검출기가 발화하는지 보지 않고 통과로 읽지 않는다.
    [[nodiscard]] bool RunAssetIdentitySelfTest(std::string& outLog);
}

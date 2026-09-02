#!/usr/bin/env python3
"""ce.uuidv8.sha256.v1 신원 프로필의 test vector 생성기 (MBC1).

이 스크립트는 제품 C++ 구현과 **독립적으로** 프로필(ModelAssetBigBangCutoverPlan §2)을
재구현해 정답을 낸다. 제품 구현이 제품 구현을 검산하는 동어반복을 피하기 위한 것이다
(같은 (틀린) 규약을 공유한 참조 재구현이 "오차 0"을 낸 전례: experiment.anim D4e-1).

산출물 둘:
  Editor/RenderTests/AssetIdentity/AssetIdentityTestVectors.inl   C++ selftest가 include
  Tools/regression/asset_identity_vectors.json                      PowerShell 게이트가
                                                                    .NET SHA256으로 3차 검산

프로필:
  IdentityInput :=
    Bytes("ce.uuidv8.sha256.v1") || 0x00 ||
    U32BE(len(domain))    || UTF8_NFC(domain) ||
    U32BE(len(namespace)) || namespaceBytes  ||
    U32BE(len(kind))      || UTF8_NFC(kind)   ||
    U32BE(len(stableKey)) || UTF8_NFC(stableKey)
  digest := SHA256(IdentityInput); uuid := digest[0..15]
  uuid[6] := (uuid[6] & 0x0f) | 0x80 ; uuid[8] := (uuid[8] & 0x3f) | 0x80

실행: python Tools/regression/Generate-AssetIdentityVectors.py
      (저장소 어디서 실행해도 경로는 스크립트 위치 기준으로 푼다)
"""
from __future__ import annotations

import hashlib
import json
import struct
import unicodedata
import uuid as uuidlib
from pathlib import Path

PROFILE = "ce.uuidv8.sha256.v1"
REPO = Path(__file__).resolve().parents[2]
OUT_INL = REPO / "Editor/RenderTests/AssetIdentity/AssetIdentityTestVectors.inl"
OUT_JSON = REPO / "Tools/regression/asset_identity_vectors.json"


def nfc_bytes(text: str) -> bytes:
    normalized = unicodedata.normalize("NFC", text)
    if normalized != text:
        raise ValueError(f"입력이 NFC가 아니다 — 생성기는 정규화하지 않는다: {text!r}")
    return normalized.encode("utf-8")


def identity_input(profile: str, domain: str, namespace: bytes, kind: str,
                   stable_key: str) -> bytes:
    def field(b: bytes) -> bytes:
        return struct.pack(">I", len(b)) + b

    return (profile.encode("ascii") + b"\x00"
            + field(nfc_bytes(domain))
            + field(namespace)
            + field(nfc_bytes(kind))
            + field(nfc_bytes(stable_key)))


def derive(profile: str, domain: str, namespace: bytes, kind: str,
           stable_key: str) -> tuple[bytes, bytes, bytes]:
    data = identity_input(profile, domain, namespace, kind, stable_key)
    digest = hashlib.sha256(data).digest()
    u = bytearray(digest[:16])
    u[6] = (u[6] & 0x0F) | 0x80
    u[8] = (u[8] & 0x3F) | 0x80
    return data, digest, bytes(u)


def canonical(u: bytes) -> str:
    return str(uuidlib.UUID(bytes=u))


# ── 벡터 정의 ────────────────────────────────────────────────────────────────
EPOCH_SEED = bytes(range(32))            # 00 01 … 1f — 문서화된 합성 seed
ALT_SEED = bytes(range(0x20, 0x40))      # 다른 epoch — 같은 key가 다른 ModelId를 내야 한다

vectors: list[dict] = []


def add(name: str, domain: str, namespace: bytes, kind: str, stable_key: str,
        *, profile: str = PROFILE, note: str = "") -> bytes:
    data, digest, u = derive(profile, domain, namespace, kind, stable_key)
    vectors.append({
        "name": name,
        "profile": profile,
        "domain": domain,
        "namespaceHex": namespace.hex(),
        "kind": kind,
        "stableKey": stable_key,
        "inputHex": data.hex(),
        "sha256Hex": digest.hex(),
        "uuid": canonical(u),
        "note": note,
    })
    return u


# 계층 §2.3 — model → subasset 사슬
model_alpha = add("model.alpha", "model", EPOCH_SEED, "model", "authoring/alpha",
                  note="epoch 00..1f, 기본 모델 authoring key")
add("model.beta", "model", EPOCH_SEED, "model", "authoring/beta",
    note="같은 epoch, 다른 key")
add("model.alpha.alt-epoch", "model", ALT_SEED, "model", "authoring/alpha",
    note="같은 key, 다른 epoch → 다른 ModelId여야 한다")
add("model.utf8-nfc", "model", EPOCH_SEED, "model", "저작/기본",
    note="다바이트 UTF-8(NFC) key — 길이는 문자 수가 아니라 바이트 수")
add("subasset.material", "subasset", model_alpha, "material",
    "material/MI_Hero_GU_F_Mythic", note="namespace = model.alpha의 16바이트")
add("subasset.texture", "subasset", model_alpha, "texture",
    "image/Hero_GU_F_Mythic_D")
add("subasset.mesh", "subasset", model_alpha, "mesh", "mesh/SK_Hero_GU_F_Mythic/0")
add("subasset.skeleton", "subasset", model_alpha, "skeleton", "skeleton/root")
add("subasset.animation", "subasset", model_alpha, "animation", "animation/Idle")

# 길이 접두가 경계를 갈라야 한다 — 접두가 없으면 아래 둘은 같은 바이트열이다.
add("prefix.ab-c", "subasset", model_alpha, "ab", "c",
    note="prefix.a-bc와 연결 바이트열은 같고 길이 접두만 다르다")
add("prefix.a-bc", "subasset", model_alpha, "a", "bc")

# 공백은 자르지 않는다 — key는 있는 그대로다.
add("space.leading", "subasset", model_alpha, "material", " x")
add("space.trailing", "subasset", model_alpha, "material", "x ")

# namespace 길이가 다르면 다른 신원 — 16바이트 접두가 같은 32바이트 seed
add("namespace.16", "model", EPOCH_SEED[:16], "model", "authoring/alpha",
    note="model.alpha와 namespace 앞 16바이트가 같다")

# 프로필 이름 변이 — 한 글자만 달라도 다른 프로필이다(§2.2 마지막 규칙).
add("mutation.profile-v0", "model", EPOCH_SEED, "model", "authoring/alpha",
    profile="ce.uuidv8.sha256.v0",
    note="model.alpha와 입력이 같고 프로필 문자열만 다르다 → 다른 UUID")

# ── SHA-256 KAT (FIPS 180-4 / NIST CAVP 공표값) ───────────────────────────────
sha_vectors = [
    ("empty", b""),
    ("abc", b"abc"),
    ("two-block-56", b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
    ("four-block-112",
     b"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
     b"ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
]
# NIST가 공표한 정답을 **하드코딩**한다 — hashlib도 참조 구현이라 둘이 어긋나면
# 생성기가 멈춘다(자기 자신을 정답으로 삼지 않기 위해).
NIST_EXPECTED = {
    "empty": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    "abc": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    "two-block-56": "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
    "four-block-112": "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
}
sha_out = []
for name, data in sha_vectors:
    digest = hashlib.sha256(data).hexdigest()
    if digest != NIST_EXPECTED[name]:
        raise SystemExit(f"hashlib이 NIST 공표값과 다르다: {name}")
    sha_out.append({"name": name, "inputHex": data.hex(), "sha256Hex": digest})
million_a = hashlib.sha256(b"a" * 1_000_000).hexdigest()
if million_a != "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0":
    raise SystemExit("hashlib million-a가 NIST 공표값과 다르다")

# ── 자기 검사: 벡터끼리 UUID·입력 바이트가 겹치지 않는다 ─────────────────────
uuids = [v["uuid"] for v in vectors]
inputs = [v["inputHex"] for v in vectors]
assert len(set(uuids)) == len(uuids), "벡터 UUID 중복"
assert len(set(inputs)) == len(inputs), "벡터 입력 바이트 중복"
for v in vectors:
    u = bytes.fromhex(v["uuid"].replace("-", ""))
    assert (u[6] & 0xF0) == 0x80 and (u[8] & 0xC0) == 0x80, v["name"]


# ── 출력 ─────────────────────────────────────────────────────────────────────
def c_str(text: str) -> str:
    # UTF-8 바이트를 \x 이스케이프로 적어 소스 인코딩(CP949 함정)에 독립시킨다.
    out = []
    for b in text.encode("utf-8"):
        if 0x20 <= b < 0x7F and chr(b) not in '"\\?':
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02x}\"\"")  # 뒤따르는 16진 문자와 붙지 않게 끊는다
    return '"' + "".join(out) + '"'


lines = [
    "// 생성 파일 — 손으로 고치지 않는다.",
    "// 생성기: Tools/regression/Generate-AssetIdentityVectors.py (독립 Python 유도)",
    "// 소비자: AssetIdentitySelfTest.cpp. 같은 벡터를 asset_identity_vectors.json이",
    "//         들고 있고 verify-asset-identity.ps1이 .NET SHA256으로 3차 검산한다.",
    "#pragma once",
    "",
    "namespace RenderTest::identity_vectors",
    "{",
    "    struct ProfileVector final",
    "    {",
    "        const char* name;",
    "        const char* profile;",
    "        const char* domain;",
    "        const char* namespaceHex;",
    "        const char* kind;",
    "        const char* stableKey;",
    "        const char* inputHex;",
    "        const char* sha256Hex;",
    "        const char* uuid;",
    "    };",
    "",
    "    inline constexpr ProfileVector kProfileVectors[] = {",
]
for v in vectors:
    lines.append("        { " + ", ".join([
        c_str(v["name"]), c_str(v["profile"]), c_str(v["domain"]),
        c_str(v["namespaceHex"]), c_str(v["kind"]), c_str(v["stableKey"]),
        c_str(v["inputHex"]), c_str(v["sha256Hex"]), c_str(v["uuid"])]) + " },")
lines += [
    "    };",
    "",
    "    struct ShaVector final",
    "    {",
    "        const char* name;",
    "        const char* inputHex;",
    "        const char* sha256Hex;",
    "    };",
    "",
    "    inline constexpr ShaVector kShaVectors[] = {",
]
for s in sha_out:
    lines.append("        { " + ", ".join([
        c_str(s["name"]), c_str(s["inputHex"]), c_str(s["sha256Hex"])]) + " },")
lines += [
    "    };",
    "",
    f"    inline constexpr const char* kMillionASha256 = {c_str(million_a)};",
    "}",
    "",
]
OUT_INL.parent.mkdir(parents=True, exist_ok=True)
OUT_INL.write_text("\n".join(lines), encoding="utf-8", newline="\n")

OUT_JSON.write_text(json.dumps({
    "profile": PROFILE,
    "generator": "Tools/regression/Generate-AssetIdentityVectors.py",
    "profileVectors": vectors,
    "shaVectors": sha_out,
    "millionASha256": million_a,
}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

print(f"profile vectors: {len(vectors)}  sha vectors: {len(sha_out)}")
print(f"  -> {OUT_INL.relative_to(REPO)}")
print(f"  -> {OUT_JSON.relative_to(REPO)}")

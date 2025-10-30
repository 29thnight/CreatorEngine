//--------------------------------------------------------------------------------------
// File: SpriteSheet.h
//
// C++ sprite sheet renderer
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//--------------------------------------------------------------------------------------

#pragma once

#include <exception>
#include <fstream>
#include <locale>
#include <map>
#include <stdexcept>
#include <string>

#include "DirectXTK/SpriteBatch.h"
#include "Navigation.h"

#include <wrl/client.h>

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

class SpriteSheet
{
public:
    SpriteSheet() = default;
    ~SpriteSheet() = default;

    SpriteSheet(SpriteSheet&&) = default;
    SpriteSheet& operator= (SpriteSheet&&) = default;

    SpriteSheet(SpriteSheet const&) = delete;
    SpriteSheet& operator= (SpriteSheet const&) = delete;

    struct SpriteFrame
    {
        RECT                sourceRect;
        DirectX::XMFLOAT2   size;
        DirectX::XMFLOAT2   origin;
        bool                rotated;
    };

    struct SequenceState
    {
        size_t frameIndex = 0;   // 현재 프레임 인덱스 (mSprites의 사전식 순서)
        float  timeAccum = 0.f; // 누적 시간(초)
        bool   loop = true; // true면 끝에서 처음으로 루프
    };

    // 총 프레임 수 반환
    size_t FrameCount() const noexcept { return mSprites.size(); }

    // 사전식 순서 index로 프레임 얻기 (없으면 nullptr)
    const SpriteFrame* GetFrameByIndex(size_t index) const
    {
        if (index >= mSprites.size()) return nullptr;
        auto it = mSprites.cbegin();
        std::advance(it, index);
        return &it->second;
    }

    void Load(ID3D11ShaderResourceView* texture, const wchar_t* szFileName)
    {
        mSprites.clear();

        mTexture = texture;

        if (szFileName)
        {
            //
            // This code parses the 'MonoGame' project txt file that is produced by CodeAndWeb's TexturePacker.
            // https://www.codeandweb.com/texturepacker
            //
            // You can modify it to match whatever sprite-sheet tool you are using
            //

            std::wifstream inFile(szFileName);
            if (!inFile)
                throw std::runtime_error("SpriteSheet failed to load .txt data");

            inFile.imbue(std::locale::classic());

            wchar_t strLine[1024] = {};
            for (;;)
            {
                inFile >> strLine;
                if (!inFile)
                    break;

                if (0 == wcscmp(strLine, L"#"))
                {
                    // Comment
                }
                else
                {
                    // Parse lines of form: Name;rotatedInt;xInt;yInt;widthInt;heightInt;origWidthInt;origHeightInt;offsetXFloat;offsetYFloat
                    static const wchar_t* delim = L";\n";

                    wchar_t* context = nullptr;
                    wchar_t* name = wcstok_s(strLine, delim, &context);
                    if (!name || !*name)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");

                    if (mSprites.find(name) != mSprites.cend())
                        throw std::runtime_error("SpriteSheet encountered duplicate in .txt data");

                    wchar_t* str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");

                    SpriteFrame frame;
                    frame.rotated = (_wtoi(str) == 1);

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    frame.sourceRect.left = _wtol(str);

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    frame.sourceRect.top = _wtol(str);

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    LONG dx = _wtol(str);
                    frame.sourceRect.right = frame.sourceRect.left + dx;

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    LONG dy = +_wtol(str);
                    frame.sourceRect.bottom = frame.sourceRect.top + dy;

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    frame.size.x = static_cast<float>(_wtof(str));

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    frame.size.y = static_cast<float>(_wtof(str));

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    float pivotX = static_cast<float>(_wtof(str));

                    str = wcstok_s(nullptr, delim, &context);
                    if (!str)
                        throw std::runtime_error("SpriteSheet encountered invalid .txt data");
                    float pivotY = static_cast<float>(_wtof(str));

                    if (frame.rotated)
                    {
                        frame.origin.x = float(dx) * (1.f - pivotY);
                        frame.origin.y = float(dy) * pivotX;
                    }
                    else
                    {
                        frame.origin.x = float(dx) * pivotX;
                        frame.origin.y = float(dy) * pivotY;
                    }

                    mSprites.insert(std::pair<std::wstring, SpriteFrame>(std::wstring(name), frame));
                }

                inFile.ignore(1000, '\n');
            }
        }

        mMaxSrcW = 0;
        mMaxSrcH = 0;
        for (auto& kv : mSprites) {
            const auto& fr = kv.second;
            mMaxSrcW = (std::max)(mMaxSrcW, SrcWidth(fr));
            mMaxSrcH = (std::max)(mMaxSrcH, SrcHeight(fr));
        }
    }

    const SpriteFrame* Find(const wchar_t* name) const
    {
        auto it = mSprites.find(name);
        if (it == mSprites.cend())
            return nullptr;

        return &it->second;
    }

    // Draw overloads specifying position and scale as XMFLOAT2.
    void XM_CALLCONV Draw(DirectX::SpriteBatch* batch, const SpriteFrame& frame, DirectX::XMFLOAT2 const& position,
        DirectX::FXMVECTOR color = DirectX::Colors::White, float rotation = 0, float scale = 1,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None, float layerDepth = 0) const
    {
        assert(batch != nullptr);
        using namespace DirectX;

        if (frame.rotated)
        {
            rotation -= XM_PIDIV2;
            switch (effects)
            {
            case SpriteEffects_FlipHorizontally:    effects = SpriteEffects_FlipVertically; break;
            case SpriteEffects_FlipVertically:      effects = SpriteEffects_FlipHorizontally; break;
            default: break;
            }
        }

        XMFLOAT2 origin = frame.origin;
        switch (effects)
        {
        case SpriteEffects_FlipHorizontally:    origin.x = float(frame.sourceRect.right - frame.sourceRect.left) - origin.x; break;
        case SpriteEffects_FlipVertically:      origin.y = float(frame.sourceRect.bottom - frame.sourceRect.top) - origin.y; break;
        default: break;
        }

        batch->Draw(mTexture.Get(), position, &frame.sourceRect, color, rotation, origin, scale, effects, layerDepth);
    }

    void XM_CALLCONV Draw(DirectX::SpriteBatch* batch, const SpriteFrame& frame, DirectX::XMFLOAT2 const& position,
        DirectX::FXMVECTOR color, float rotation, DirectX::XMFLOAT2 const& scale,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None, float layerDepth = 0) const

    {
        assert(batch != nullptr);
        using namespace DirectX;

        if (frame.rotated)
        {
            rotation -= XM_PIDIV2;
            switch (effects)
            {
            case SpriteEffects_FlipHorizontally:    effects = SpriteEffects_FlipVertically; break;
            case SpriteEffects_FlipVertically:      effects = SpriteEffects_FlipHorizontally; break;
            default: break;
            }
        }

        XMFLOAT2 origin = frame.origin;
        switch (effects)
        {
        case SpriteEffects_FlipHorizontally:    origin.x = float(frame.sourceRect.right - frame.sourceRect.left) - origin.x; break;
        case SpriteEffects_FlipVertically:      origin.y = float(frame.sourceRect.bottom - frame.sourceRect.top) - origin.y; break;
        default: break;
        }

        batch->Draw(mTexture.Get(), position, &frame.sourceRect, color, rotation, origin, scale, effects, layerDepth);
    }

    // Draw overloads specifying position and scale via the first two components of an XMVECTOR.
    void XM_CALLCONV Draw(DirectX::SpriteBatch* batch, const SpriteFrame& frame, DirectX::FXMVECTOR position,
        DirectX::FXMVECTOR color = DirectX::Colors::White, float rotation = 0, float scale = 1,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None, float layerDepth = 0) const
    {
        assert(batch != nullptr);
        using namespace DirectX;

        if (frame.rotated)
        {
            rotation -= XM_PIDIV2;
            switch (effects)
            {
            case SpriteEffects_FlipHorizontally:    effects = SpriteEffects_FlipVertically; break;
            case SpriteEffects_FlipVertically:      effects = SpriteEffects_FlipHorizontally; break;
            default: break;
            }
        }

        XMFLOAT2 origin = frame.origin;
        switch (effects)
        {
        case SpriteEffects_FlipHorizontally:    origin.x = float(frame.sourceRect.right - frame.sourceRect.left) - origin.x; break;
        case SpriteEffects_FlipVertically:      origin.y = float(frame.sourceRect.bottom - frame.sourceRect.top) - origin.y; break;
        default: break;
        }
        XMVECTOR vorigin = XMLoadFloat2(&origin);

        batch->Draw(mTexture.Get(), position, &frame.sourceRect, color, rotation, vorigin, scale, effects, layerDepth);
    }

    void XM_CALLCONV Draw(DirectX::SpriteBatch* batch, const SpriteFrame& frame, DirectX::FXMVECTOR position,
        DirectX::FXMVECTOR color, float rotation, DirectX::GXMVECTOR scale,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None, float layerDepth = 0) const
    {
        assert(batch != nullptr);
        using namespace DirectX;

        if (frame.rotated)
        {
            rotation -= XM_PIDIV2;
            switch (effects)
            {
            case SpriteEffects_FlipHorizontally:    effects = SpriteEffects_FlipVertically; break;
            case SpriteEffects_FlipVertically:      effects = SpriteEffects_FlipHorizontally; break;
            default: break;
            }
        }

        XMFLOAT2 origin = frame.origin;
        switch (effects)
        {
        case SpriteEffects_FlipHorizontally:    origin.x = float(frame.sourceRect.right - frame.sourceRect.left) - origin.x; break;
        case SpriteEffects_FlipVertically:      origin.y = float(frame.sourceRect.bottom - frame.sourceRect.top) - origin.y; break;
        default: break;
        }
        XMVECTOR vorigin = XMLoadFloat2(&origin);

        batch->Draw(mTexture.Get(), position, &frame.sourceRect, color, rotation, vorigin, scale, effects, layerDepth);
    }

    // Draw overloads specifying position as a RECT.
    void XM_CALLCONV Draw(DirectX::SpriteBatch* batch, const SpriteFrame& frame, RECT const& destinationRectangle,
        DirectX::FXMVECTOR color = DirectX::Colors::White, float rotation = 0,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None, float layerDepth = 0) const
    {
        assert(batch != nullptr);
        using namespace DirectX;

        if (frame.rotated)
        {
            rotation -= XM_PIDIV2;
            switch (effects)
            {
            case SpriteEffects_FlipHorizontally:    effects = SpriteEffects_FlipVertically; break;
            case SpriteEffects_FlipVertically:      effects = SpriteEffects_FlipHorizontally; break;
            default: break;
            }
        }

        XMFLOAT2 origin = frame.origin;
        switch (effects)
        {
        case SpriteEffects_FlipHorizontally:    origin.x = float(frame.sourceRect.right - frame.sourceRect.left) - origin.x; break;
        case SpriteEffects_FlipVertically:      origin.y = float(frame.sourceRect.bottom - frame.sourceRect.top) - origin.y; break;
        default: break;
        }

        batch->Draw(mTexture.Get(), destinationRectangle, &frame.sourceRect, color, rotation, origin, effects, layerDepth);
    }

    // 시간 기반 자동 재생 + 그리기 (scale: float 오버로드)
    // dt: 경과시간(초), frameDuration: 프레임당 재생시간(초)
    void XM_CALLCONV DrawSequential(
        DirectX::SpriteBatch* batch,
        DirectX::XMFLOAT2 const& position,
        float dt,
        float frameDuration,
        SequenceState& state,
        DirectX::FXMVECTOR color = DirectX::Colors::White,
        float rotation = 0.f,
        float scale = 1.f,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None,
        float layerDepth = 0.f) const
    {
        if (!batch || mSprites.empty() || frameDuration <= 0.f)
            return;

        // 프레임 진행
        state.timeAccum += dt;
        while (state.timeAccum >= frameDuration)
        {
            state.timeAccum -= frameDuration;
            if (state.frameIndex + 1 < mSprites.size())
            {
                ++state.frameIndex;
            }
            else
            {
                if (state.loop) state.frameIndex = 0;
                else            state.frameIndex = mSprites.size() - 1;
            }
        }

        // 현재 프레임 그리기
        if (auto frame = GetFrameByIndex(state.frameIndex))
        {
            Draw(batch, *frame, position, color, rotation, scale, effects, layerDepth);
        }
    }

    // 시간 기반 자동 재생 + 그리기 (scale: XMFLOAT2 오버로드)
    void XM_CALLCONV DrawSequential(
        DirectX::SpriteBatch* batch,
        DirectX::XMFLOAT2 const& position,
        float dt,
        float frameDuration,
        SequenceState& state,
        DirectX::FXMVECTOR color,
        float rotation,
        DirectX::XMFLOAT2 const& scale,
        DirectX::SpriteEffects effects = DirectX::SpriteEffects_None,
        float layerDepth = 0.f) const
    {
        if (!batch || mSprites.empty() || frameDuration <= 0.f)
            return;

        state.timeAccum += dt;
        while (state.timeAccum >= frameDuration)
        {
            state.timeAccum -= frameDuration;
            if (state.frameIndex + 1 < mSprites.size())
            {
                ++state.frameIndex;
            }
            else
            {
                if (state.loop) state.frameIndex = 0;
                else            state.frameIndex = mSprites.size() - 1;
            }
        }

        if (auto frame = GetFrameByIndex(state.frameIndex))
        {
            Draw(batch, *frame, position, color, rotation, scale, effects, layerDepth);
        }
    }

    public:
        // 소프트 클리핑 버전 (rotation==0, effects==None, frame.rotated==false 전제)
        void XM_CALLCONV DrawSequential(
            DirectX::SpriteBatch* batch,
            DirectX::XMFLOAT2 const& position,
            float dt,
            float frameDuration,
            SequenceState& state,
            ClipDirection clipDir,
            float clipPercent,
            DirectX::FXMVECTOR color = DirectX::Colors::White,
            float rotation = 0.f,                 // 소프트 클리핑은 rotation==0 권장
            float scale = 1.f,
            DirectX::SpriteEffects effects = DirectX::SpriteEffects_None,
            float layerDepth = 0.f) const
        {
            using namespace DirectX;
            if (!batch || mSprites.empty() || frameDuration <= 0.f || mMaxSrcW <= 0 || mMaxSrcH <= 0)
                return;

            // 1) 프레임 진행
            state.timeAccum += dt;
            while (state.timeAccum >= frameDuration)
            {
                state.timeAccum -= frameDuration;
                if (state.frameIndex + 1 < mSprites.size()) ++state.frameIndex;
                else { state.frameIndex = state.loop ? 0 : (mSprites.size() - 1); }
            }

            const SpriteFrame* frame = GetFrameByIndex(state.frameIndex);
            if (!frame) return;

            if (clipDir == ClipDirection::None || clipPercent >= 1.f)
            {
                // 클립 없이 그리기 (최대 기준 불필요)
                Draw(batch, *frame, position, color, rotation, scale, effects, layerDepth);
                return;
            }

            // 2) 최대 박스 목적 사각형 (현재 프레임의 origin 비율을 반영해 정렬 일치)
            const RECT maxDst = MakeMaxDestAlignedByFrameOrigin(mMaxSrcW, mMaxSrcH, *frame, position, scale);

            // 3) 현재 프레임 목적 사각형
            const RECT curDst = MakeDestFromPosScale(*frame, position, scale);

            // 4) 최대 박스 내 클립 사각형
            const RECT clipDst = MakeClipRectFromPercent(maxDst, clipDir, clipPercent);

            // 5) 교집합(실제 그릴 화면 영역)
            RECT inter{};
            if (!IntersectRectSafe(inter, curDst, clipDst))
                return; // 보일 영역 없음

            // 6) 교집합을 소스 좌표로 역매핑
            RECT clippedSrc{};
            if (!MapIntersectionToSource(*frame, curDst, inter, clippedSrc))
                return;

            // 7) 그리기 (origin/회전/플립 영향 없이 inter에 딱 맞춰 그림)
            static const XMFLOAT2 zeroOrigin{ 0.f, 0.f };
            batch->Draw(mTexture.Get(), inter, &clippedSrc, color, /*rotation*/0.f, zeroOrigin,
                SpriteEffects_None, layerDepth);
        }

private:
    // frame, position, scale(float) -> 목적 사각형
    static RECT MakeDestFromPosScale(const SpriteFrame& frame, DirectX::XMFLOAT2 pos, float scale)
    {
        const LONG dx = SrcWidth(frame);
        const LONG dy = SrcHeight(frame);

        const float L = std::floor(pos.x - frame.origin.x * scale);
        const float T = std::floor(pos.y - frame.origin.y * scale);
        const float R = L + std::round(dx * scale);
        const float B = T + std::round(dy * scale);

        RECT r{ LONG(L), LONG(T), LONG(R), LONG(B) };
        return r;
    }

    // frame, position, scale(vec2) -> 목적 사각형
    static RECT MakeDestFromPosScale2(const SpriteFrame& frame, DirectX::XMFLOAT2 pos, DirectX::XMFLOAT2 scale)
    {
        const LONG dx = SrcWidth(frame);
        const LONG dy = SrcHeight(frame);

        const float L = std::floor(pos.x - frame.origin.x * scale.x);
        const float T = std::floor(pos.y - frame.origin.y * scale.y);
        const float R = L + std::round(dx * scale.x);
        const float B = T + std::round(dy * scale.y);

        RECT r{ LONG(L), LONG(T), LONG(R), LONG(B) };
        return r;
    }

    // 최대 박스 목적 사각형 (origin 정렬 동일하게 적용)
    RECT MakeMaxDestFromPosScale(DirectX::XMFLOAT2 pos, float scale) const
    {
        // 최대 박스는 "가상의 프레임"처럼 취급: origin은 현재 frame.origin을 써야 정렬 일관성 유지
        // -> 호출 쪽에서 현재 frame의 origin을 넘기도록 인터페이스를 맞춤
        // 여기서는 origin=(0,0)로 가정하고, 실제 호출부에서 위치를 보정하지 않도록
        // '현재 프레임과 같은 origin'으로 만든 별도 헬퍼를 아래에 둔다.
        RECT r{};
        r.left = r.top = r.right = r.bottom = 0;
        return r;
    }

    // 현재 프레임의 origin 기준으로 최대 박스를 생성 (회전/플립 없음 전제)
    static RECT MakeMaxDestAlignedByFrameOrigin(LONG maxW, LONG maxH,
        const SpriteFrame& frame,
        DirectX::XMFLOAT2 pos,
        float scale)
    {
        // 현재 frame.origin이 (frame 크기 비율)로 들어오므로,
        // 동일 비율을 최대 박스에도 적용해 정렬을 맞춘다.
        const float ox_ratio = (SrcWidth(frame) > 0) ? (frame.origin.x / float(SrcWidth(frame))) : 0.f;
        const float oy_ratio = (SrcHeight(frame) > 0) ? (frame.origin.y / float(SrcHeight(frame))) : 0.f;

        const float ox_max = ox_ratio * float(maxW);
        const float oy_max = oy_ratio * float(maxH);

        const float L = std::floor(pos.x - ox_max * scale);
        const float T = std::floor(pos.y - oy_max * scale);
        const float R = L + std::round(maxW * scale);
        const float B = T + std::round(maxH * scale);

        RECT r{ LONG(L), LONG(T), LONG(R), LONG(B) };
        return r;
    }

    // 교집합
    static inline bool IntersectRectSafe(RECT& out, const RECT& A, const RECT& B)
    {
        out.left = (std::max)(A.left, B.left);
        out.top = (std::max)(A.top, B.top);
        out.right = (std::min)(A.right, B.right);
        out.bottom = (std::min)(A.bottom, B.bottom);
        return (out.left < out.right) && (out.top < out.bottom);
    }

    // dst_cur(현재 프레임의 최종 목적 사각형)과 clipDst(최대 박스 내 클립 사각형)의 교집합을
    // 현재 프레임의 sourceRect로 역매핑한다.
    static bool MapIntersectionToSource(const SpriteFrame& frame,
        const RECT& dst_cur,
        const RECT& inter, // 교집합
        RECT& src_out)     // 잘린 소스
    {
        const LONG cw = SrcWidth(frame);
        const LONG ch = SrcHeight(frame);
        const float dw = float(dst_cur.right - dst_cur.left);
        const float dh = float(dst_cur.bottom - dst_cur.top);
        if (cw <= 0 || ch <= 0 || dw <= 0.f || dh <= 0.f) return false;

        const float u0 = (float(inter.left - dst_cur.left)) / dw;
        const float v0 = (float(inter.top - dst_cur.top)) / dh;
        const float u1 = (float(inter.right - dst_cur.left)) / dw;
        const float v1 = (float(inter.bottom - dst_cur.top)) / dh;

        const LONG sl = frame.sourceRect.left + LONG(std::lround(u0 * cw));
        const LONG st = frame.sourceRect.top + LONG(std::lround(v0 * ch));
        const LONG sr = frame.sourceRect.left + LONG(std::lround(u1 * cw));
        const LONG sb = frame.sourceRect.top + LONG(std::lround(v1 * ch));

        src_out = RECT{ sl, st, sr, sb };
        return (src_out.left < src_out.right) && (src_out.top < src_out.bottom);
    }

    // 최대 박스 안에서 방향/퍼센트로 클립 사각형 만들기
    static RECT MakeClipRectFromPercent(const RECT& maxDst, ClipDirection dir, float percent)
    {
        percent = std::clamp(percent, 0.f, 1.f);
        RECT r = maxDst;
        const LONG W = maxDst.right - maxDst.left;
        const LONG H = maxDst.bottom - maxDst.top;

        switch (dir)
        {
        case ClipDirection::LeftToRight:
            r.right = r.left + LONG(std::lround(W * percent));
            break;
        case ClipDirection::RightToLeft:
            r.left = r.right - LONG(std::lround(W * percent));
            break;
        case ClipDirection::TopToBottom:
            r.bottom = r.top + LONG(std::lround(H * percent));
            break;
        case ClipDirection::BottomToTop:
            r.top = r.bottom - LONG(std::lround(H * percent));
            break;
        default:
            // None -> 전체
            break;
        }
        return r;
    }

private:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    mTexture;
    std::map<std::wstring, SpriteFrame>                 mSprites;

    LONG mMaxSrcW = 0;
    LONG mMaxSrcH = 0;

    static inline LONG SrcWidth(const SpriteFrame& f) { return f.sourceRect.right - f.sourceRect.left; }
    static inline LONG SrcHeight(const SpriteFrame& f) { return f.sourceRect.bottom - f.sourceRect.top; }
};
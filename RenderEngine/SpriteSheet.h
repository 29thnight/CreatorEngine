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
        size_t frameIndex = 0;   // ���� ������ �ε��� (mSprites�� ������ ����)
        float  timeAccum = 0.f; // ���� �ð�(��)
        bool   loop = true; // true�� ������ ó������ ����
    };

    // �� ������ �� ��ȯ
    size_t FrameCount() const noexcept { return mSprites.size(); }

    // ������ ���� index�� ������ ��� (������ nullptr)
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

    // �ð� ��� �ڵ� ��� + �׸��� (scale: float �����ε�)
    // dt: ����ð�(��), frameDuration: �����Ӵ� ����ð�(��)
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

        // ������ ����
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

        // ���� ������ �׸���
        if (auto frame = GetFrameByIndex(state.frameIndex))
        {
            Draw(batch, *frame, position, color, rotation, scale, effects, layerDepth);
        }
    }

    // �ð� ��� �ڵ� ��� + �׸��� (scale: XMFLOAT2 �����ε�)
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
        // ����Ʈ Ŭ���� ���� (rotation==0, effects==None, frame.rotated==false ����)
        void XM_CALLCONV DrawSequential(
            DirectX::SpriteBatch* batch,
            DirectX::XMFLOAT2 const& position,
            float dt,
            float frameDuration,
            SequenceState& state,
            ClipDirection clipDir,
            float clipPercent,
            DirectX::FXMVECTOR color = DirectX::Colors::White,
            float rotation = 0.f,                 // ����Ʈ Ŭ������ rotation==0 ����
            float scale = 1.f,
            DirectX::SpriteEffects effects = DirectX::SpriteEffects_None,
            float layerDepth = 0.f) const
        {
            using namespace DirectX;
            if (!batch || mSprites.empty() || frameDuration <= 0.f || mMaxSrcW <= 0 || mMaxSrcH <= 0)
                return;

            // 1) ������ ����
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
                // Ŭ�� ���� �׸��� (�ִ� ���� ���ʿ�)
                Draw(batch, *frame, position, color, rotation, scale, effects, layerDepth);
                return;
            }

            // 2) �ִ� �ڽ� ���� �簢�� (���� �������� origin ������ �ݿ��� ���� ��ġ)
            const RECT maxDst = MakeMaxDestAlignedByFrameOrigin(mMaxSrcW, mMaxSrcH, *frame, position, scale);

            // 3) ���� ������ ���� �簢��
            const RECT curDst = MakeDestFromPosScale(*frame, position, scale);

            // 4) �ִ� �ڽ� �� Ŭ�� �簢��
            const RECT clipDst = MakeClipRectFromPercent(maxDst, clipDir, clipPercent);

            // 5) ������(���� �׸� ȭ�� ����)
            RECT inter{};
            if (!IntersectRectSafe(inter, curDst, clipDst))
                return; // ���� ���� ����

            // 6) �������� �ҽ� ��ǥ�� ������
            RECT clippedSrc{};
            if (!MapIntersectionToSource(*frame, curDst, inter, clippedSrc))
                return;

            // 7) �׸��� (origin/ȸ��/�ø� ���� ���� inter�� �� ���� �׸�)
            static const XMFLOAT2 zeroOrigin{ 0.f, 0.f };
            batch->Draw(mTexture.Get(), inter, &clippedSrc, color, /*rotation*/0.f, zeroOrigin,
                SpriteEffects_None, layerDepth);
        }

private:
    // frame, position, scale(float) -> ���� �簢��
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

    // frame, position, scale(vec2) -> ���� �簢��
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

    // �ִ� �ڽ� ���� �簢�� (origin ���� �����ϰ� ����)
    RECT MakeMaxDestFromPosScale(DirectX::XMFLOAT2 pos, float scale) const
    {
        // �ִ� �ڽ��� "������ ������"ó�� ���: origin�� ���� frame.origin�� ��� ���� �ϰ��� ����
        // -> ȣ�� �ʿ��� ���� frame�� origin�� �ѱ⵵�� �������̽��� ����
        // ���⼭�� origin=(0,0)�� �����ϰ�, ���� ȣ��ο��� ��ġ�� �������� �ʵ���
        // '���� �����Ӱ� ���� origin'���� ���� ���� ���۸� �Ʒ��� �д�.
        RECT r{};
        r.left = r.top = r.right = r.bottom = 0;
        return r;
    }

    // ���� �������� origin �������� �ִ� �ڽ��� ���� (ȸ��/�ø� ���� ����)
    static RECT MakeMaxDestAlignedByFrameOrigin(LONG maxW, LONG maxH,
        const SpriteFrame& frame,
        DirectX::XMFLOAT2 pos,
        float scale)
    {
        // ���� frame.origin�� (frame ũ�� ����)�� �����Ƿ�,
        // ���� ������ �ִ� �ڽ����� ������ ������ �����.
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

    // ������
    static inline bool IntersectRectSafe(RECT& out, const RECT& A, const RECT& B)
    {
        out.left = (std::max)(A.left, B.left);
        out.top = (std::max)(A.top, B.top);
        out.right = (std::min)(A.right, B.right);
        out.bottom = (std::min)(A.bottom, B.bottom);
        return (out.left < out.right) && (out.top < out.bottom);
    }

    // dst_cur(���� �������� ���� ���� �簢��)�� clipDst(�ִ� �ڽ� �� Ŭ�� �簢��)�� ��������
    // ���� �������� sourceRect�� �������Ѵ�.
    static bool MapIntersectionToSource(const SpriteFrame& frame,
        const RECT& dst_cur,
        const RECT& inter, // ������
        RECT& src_out)     // �߸� �ҽ�
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

    // �ִ� �ڽ� �ȿ��� ����/�ۼ�Ʈ�� Ŭ�� �簢�� �����
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
            // None -> ��ü
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
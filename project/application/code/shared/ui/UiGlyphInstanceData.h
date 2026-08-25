#pragma once

#include <Vector2.h>
#include <Vector4.h>

namespace LogGuide {

/// グリフ 1 つ分の描画データ.
/// IStructuredBuffer<T> の要求により、ネストした ConstantBuffer を持つ.
struct UiGlyphInstanceData {
    OriGine::Vec2f posMin{};
    OriGine::Vec2f posMax{};
    OriGine::Vec2f uvMin{};
    OriGine::Vec2f uvMax{};
    OriGine::Vec4f color{};
    /// 親から受け継いだクリップ矩形（画面ピクセル）
    OriGine::Vec2f clipMin{};
    OriGine::Vec2f clipMax{};

    /// HLSL の UiGlyphInstance (UiText.hlsli) と 1:1 対応するレイアウト (64 バイト).
    struct ConstantBuffer {
        float rect[4]{};     ///< (posMin.x, posMin.y, posMax.x, posMax.y)
        float uv[4]{};       ///< (uvMin.x,  uvMin.y,  uvMax.x,  uvMax.y)
        float color[4]{};
        float clipRect[4]{}; ///< (minX, minY, maxX, maxY)

        /// UiGlyphInstanceData から GPU 用レイアウトへ詰め替える.
        /// IStructuredBuffer<T> がテンプレートとして実体化する際に定義が見えている必要があるため、
        /// ヘッダ内 inline 定義とする.
        ConstantBuffer& operator=(const UiGlyphInstanceData& _data) {
            rect[0] = _data.posMin[OriGine::X];
            rect[1] = _data.posMin[OriGine::Y];
            rect[2] = _data.posMax[OriGine::X];
            rect[3] = _data.posMax[OriGine::Y];

            uv[0] = _data.uvMin[OriGine::X];
            uv[1] = _data.uvMin[OriGine::Y];
            uv[2] = _data.uvMax[OriGine::X];
            uv[3] = _data.uvMax[OriGine::Y];

            color[0] = _data.color[OriGine::X];
            color[1] = _data.color[OriGine::Y];
            color[2] = _data.color[OriGine::Z];
            color[3] = _data.color[OriGine::W];

            clipRect[0] = _data.clipMin[OriGine::X];
            clipRect[1] = _data.clipMin[OriGine::Y];
            clipRect[2] = _data.clipMax[OriGine::X];
            clipRect[3] = _data.clipMax[OriGine::Y];

            return *this;
        }
    };
};

static_assert(sizeof(UiGlyphInstanceData::ConstantBuffer) == 64,
    "UiGlyphInstanceData::ConstantBuffer は UiText.hlsli の UiGlyphInstance と 1:1 対応させること (64 バイト).");

} // namespace LogGuide

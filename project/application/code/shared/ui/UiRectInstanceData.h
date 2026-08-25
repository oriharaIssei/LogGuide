#pragma once

#include <Vector2.h>
#include <Vector4.h>

namespace LogGuide {

/// GPU に送る角丸矩形 1 つ分のデータ.
/// IStructuredBuffer<T> の要求により、ネストした ConstantBuffer を持つ.
struct UiRectInstanceData {
    OriGine::Vec2f rectMin      = {0.0f, 0.0f};
    OriGine::Vec2f rectMax      = {0.0f, 0.0f};
    OriGine::Vec4f fillColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    OriGine::Vec4f borderColor  = {0.0f, 0.0f, 0.0f, 1.0f};
    OriGine::Vec4f cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f}; ///< TL, TR, BR, BL
    float borderWidth           = 0.0f;
    /// 親から受け継いだクリップ矩形（画面ピクセル）. UiLayoutSystem が解決する.
    OriGine::Vec2f clipMin = {0.0f, 0.0f};
    OriGine::Vec2f clipMax = {0.0f, 0.0f};

    /// HLSL の UiRectInstance (UiRect.hlsli) と 1:1 対応するレイアウト (96 バイト).
    struct ConstantBuffer {
        float rect[4]{};
        float fillColor[4]{};
        float borderColor[4]{};
        float cornerRadius[4]{};
        float params[4]{};
        float clipRect[4]{}; ///< (minX, minY, maxX, maxY)

        /// UiRectInstanceData から GPU 用レイアウトへ詰め替える.
        /// IStructuredBuffer<T> がテンプレートとして実体化する際に定義が見えている必要があるため、
        /// ヘッダ内 inline 定義とする.
        ConstantBuffer& operator=(const UiRectInstanceData& _data) {
            rect[0] = _data.rectMin[OriGine::X];
            rect[1] = _data.rectMin[OriGine::Y];
            rect[2] = _data.rectMax[OriGine::X];
            rect[3] = _data.rectMax[OriGine::Y];

            fillColor[0] = _data.fillColor[OriGine::X];
            fillColor[1] = _data.fillColor[OriGine::Y];
            fillColor[2] = _data.fillColor[OriGine::Z];
            fillColor[3] = _data.fillColor[OriGine::W];

            borderColor[0] = _data.borderColor[OriGine::X];
            borderColor[1] = _data.borderColor[OriGine::Y];
            borderColor[2] = _data.borderColor[OriGine::Z];
            borderColor[3] = _data.borderColor[OriGine::W];

            cornerRadius[0] = _data.cornerRadius[OriGine::X];
            cornerRadius[1] = _data.cornerRadius[OriGine::Y];
            cornerRadius[2] = _data.cornerRadius[OriGine::Z];
            cornerRadius[3] = _data.cornerRadius[OriGine::W];

            params[0] = _data.borderWidth;
            params[1] = 0.0f;
            params[2] = 0.0f;
            params[3] = 0.0f;

            clipRect[0] = _data.clipMin[OriGine::X];
            clipRect[1] = _data.clipMin[OriGine::Y];
            clipRect[2] = _data.clipMax[OriGine::X];
            clipRect[3] = _data.clipMax[OriGine::Y];

            return *this;
        }
    };
};

static_assert(sizeof(UiRectInstanceData::ConstantBuffer) == 96,
    "UiRectInstanceData::ConstantBuffer は UiRect.hlsli の UiRectInstance と 1:1 対応させること (96 バイト).");

} // namespace LogGuide

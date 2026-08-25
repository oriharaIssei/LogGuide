#pragma once

/// グリフ 1 つ分のインスタンスデータ.
/// すべて float4 境界に揃えてある (4 * 16 = 64 バイト).
/// CPU 側 UiGlyphInstanceData::ConstantBuffer と 1:1 で対応させること.
struct UiGlyphInstance {
    float4 rect;     ///< (minX, minY, maxX, maxY) 画面ピクセル・左上原点
    float4 uv;       ///< (uMin, vMin, uMax, vMax)
    float4 color;
    float4 clipRect; ///< (minX, minY, maxX, maxY) 親から受け継いだクリップ矩形. 画面ピクセル
};

/// ルート定数 (b0). Num32BitValues = 4.
cbuffer UiTextScene : register(b0) {
    float2 screenSize;     ///< 描画先のピクセルサイズ
    uint   instanceOffset; ///< SV_InstanceID は常に 0 始まりなので、開始位置をここで渡す
    uint   _padding;
};

struct VSOutput {
    float4 svpos    : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    nointerpolation float4 clipRect : TEXCOORD1;
};

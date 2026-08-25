#pragma once

/// 角丸矩形 1 つ分のインスタンスデータ.
/// StructuredBuffer の要素なので、パッキングの曖昧さを避けるため
/// すべて float4 境界に揃えてある (6 * 16 = 96 バイト).
/// CPU 側 UiRectInstanceData::ConstantBuffer と 1:1 で対応させること.
struct UiRectInstance {
    float4 rect;         ///< (minX, minY, maxX, maxY) 画面ピクセル・左上原点
    float4 fillColor;    ///< 塗り
    float4 borderColor;  ///< 枠線
    float4 cornerRadius; ///< 各隅の半径(px). CSS の border-radius と同じ順で (TL, TR, BR, BL)
    float4 params;       ///< x = 枠線の太さ(px). y,z,w は未使用
    float4 clipRect;     ///< (minX, minY, maxX, maxY) 親から受け継いだクリップ矩形. 画面ピクセル
};

/// ルート定数 (b0). Num32BitValues = 4.
cbuffer UiRectScene : register(b0) {
    float2 screenSize;     ///< 描画先のピクセルサイズ
    uint   instanceOffset; ///< SV_InstanceID は常に 0 始まりなので、開始位置をここで渡す
    uint   _padding;
};

struct VSOutput {
    float4 svpos       : SV_POSITION;
    float2 localPx     : TEXCOORD0; ///< 矩形中心を原点としたピクセル座標 (+Y は下)
    float2 halfSize    : TEXCOORD1; ///< 矩形の半分のサイズ (px)
    float4 radius      : TEXCOORD2; ///< (TL, TR, BR, BL)
    float  borderWidth : TEXCOORD3;
    float4 fillColor   : COLOR0;
    float4 borderColor : COLOR1;
    /// 親から受け継いだクリップ矩形. インスタンス内で一定の値なので補間しない.
    nointerpolation float4 clipRect : TEXCOORD4; ///< (minX, minY, maxX, maxY) 画面ピクセル
};

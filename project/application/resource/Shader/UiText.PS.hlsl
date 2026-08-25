#include "UiText.hlsli"

/// フォントアトラス. R8_UNORM なので .r がカバレッジ (0=透明, 1=不透明).
Texture2D<float4> gAtlas   : register(t1);
SamplerState      gSampler : register(s0);

float4 main(VSOutput _input) : SV_TARGET {
    // 親から受け継いだクリップ矩形の外は描かない。
    // SV_POSITION の xy はビューポート上のピクセル座標なので、
    // UiTransform の座標系とそのまま比較できる。
    const float2 screenPx = _input.svpos.xy;
    if (screenPx.x < _input.clipRect.x || screenPx.x >= _input.clipRect.z ||
        screenPx.y < _input.clipRect.y || screenPx.y >= _input.clipRect.w) {
        discard;
    }

    float coverage = gAtlas.Sample(gSampler, _input.uv).r;

    float4 color = _input.color;
    color.a *= coverage;
    if (color.a < 0.01f) {
        discard;
    }
    return color;
}

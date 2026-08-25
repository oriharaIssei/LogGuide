#include "UiRect.hlsli"

/// 角丸矩形の符号付き距離を返す (内側が負、外側が正).
/// _p        : 矩形中心を原点としたピクセル座標. +Y は下方向.
/// _halfSize : 矩形の半分のサイズ (px)
/// _radius   : 各隅の半径 (px). (TL, TR, BR, BL) の順.
float SdRoundedBox(float2 _p, float2 _halfSize, float4 _radius) {
    // +Y が下向きなので _p.y < 0 が上側
    float top    = (_p.x < 0.0f) ? _radius.x : _radius.y; // TL : TR
    float bottom = (_p.x < 0.0f) ? _radius.w : _radius.z; // BL : BR
    float r      = (_p.y < 0.0f) ? top : bottom;

    // 半径は短辺の半分を超えられない
    r = min(r, min(_halfSize.x, _halfSize.y));

    float2 q = abs(_p) - _halfSize + r;
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

float4 main(VSOutput _input) : SV_TARGET {
    float d = SdRoundedBox(_input.localPx, _input.halfSize, _input.radius);

    // fwidth で画面上の 1 ピクセル相当の幅を得る (解像度に依らないアンチエイリアス)
    float aa = max(fwidth(d), 1e-5f);

    // 親から受け継いだクリップ矩形の外は描かない。必ず fwidth() の後に置くこと
    // (fwidth は 2x2 のピクセルクアッドで微分を取るため、先に discard すると
    //  隣接ピクセルの微分が不正確になり、角のアンチエイリアスが壊れる)。
    // SV_POSITION の xy はビューポート上のピクセル座標なので、
    // UiTransform の座標系とそのまま比較できる。
    const float2 screenPx = _input.svpos.xy;
    if (screenPx.x < _input.clipRect.x || screenPx.x >= _input.clipRect.z ||
        screenPx.y < _input.clipRect.y || screenPx.y >= _input.clipRect.w) {
        discard;
    }

    float fill = 1.0f - smoothstep(-aa * 0.5f, aa * 0.5f, d);
    if (fill <= 0.0f) {
        discard;
    }

    float4 color = _input.fillColor;
    if (_input.borderWidth > 0.0f) {
        // d が (-borderWidth, 0) の帯が枠線
        float inner = 1.0f - smoothstep(-aa * 0.5f, aa * 0.5f, d + _input.borderWidth);
        color = lerp(_input.borderColor, _input.fillColor, inner);
    }

    color.a *= fill;
    return color;
}

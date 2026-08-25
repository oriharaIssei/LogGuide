#include "UiText.hlsli"

StructuredBuffer<UiGlyphInstance> gInstances : register(t0);

/// 頂点バッファを使わず、SV_VertexID から矩形を組み立てる.
/// トポロジは TRIANGLELIST、グリフ 1 つあたり 6 頂点.
VSOutput main(uint _vertexId : SV_VertexID, uint _instanceId : SV_InstanceID) {
    UiGlyphInstance inst = gInstances[instanceOffset + _instanceId];

    const float2 kCorners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f)
    };
    float2 corner = kCorners[_vertexId];

    float2 posPx = lerp(inst.rect.xy, inst.rect.zw, corner);
    float2 uv    = lerp(inst.uv.xy,   inst.uv.zw,   corner);

    // 左上原点のピクセル座標 → NDC
    float2 ndc = float2(
         (posPx.x / screenSize.x) * 2.0f - 1.0f,
        -((posPx.y / screenSize.y) * 2.0f - 1.0f));

    VSOutput output;
    output.svpos    = float4(ndc, 0.0f, 1.0f);
    output.uv       = uv;
    output.color    = inst.color;
    output.clipRect = inst.clipRect;
    return output;
}

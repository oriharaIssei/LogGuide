#include "UiRect.hlsli"

StructuredBuffer<UiRectInstance> gInstances : register(t0);

/// 頂点バッファを使わず、SV_VertexID から矩形を組み立てる.
/// トポロジは TRIANGLELIST、1 矩形あたり 6 頂点.
VSOutput main(uint _vertexId : SV_VertexID, uint _instanceId : SV_InstanceID) {
    UiRectInstance inst = gInstances[_instanceId];

    // (0,0) (1,0) (0,1) / (0,1) (1,0) (1,1)
    const float2 kCorners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f)
    };
    float2 corner = kCorners[_vertexId];

    float2 rectMin = inst.rect.xy;
    float2 rectMax = inst.rect.zw;
    float2 posPx   = lerp(rectMin, rectMax, corner);

    // 左上原点のピクセル座標 → NDC
    float2 ndc = float2(
         (posPx.x / screenSize.x) * 2.0f - 1.0f,
        -((posPx.y / screenSize.y) * 2.0f - 1.0f));

    float2 center = (rectMax + rectMin) * 0.5f;

    VSOutput output;
    output.svpos       = float4(ndc, 0.0f, 1.0f);
    output.localPx     = posPx - center;
    output.halfSize    = (rectMax - rectMin) * 0.5f;
    output.radius      = inst.cornerRadius;
    output.borderWidth = inst.params.x;
    output.fillColor   = inst.fillColor;
    output.borderColor = inst.borderColor;
    output.clipRect    = inst.clipRect;
    return output;
}

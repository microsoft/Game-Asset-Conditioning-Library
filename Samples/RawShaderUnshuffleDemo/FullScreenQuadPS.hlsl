//--------------------------------------------------------------------------------------
// FullScreenQuadPS.hlsl
//
// A simple pixel shader to render a texture 
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//--------------------------------------------------------------------------------------

#include "FullScreenQuad.hlsli"

struct Constants
{
    uint32_t width;
    uint32_t height;
    uint32_t scrollX;
    uint32_t scrollY;
};
ConstantBuffer<Constants> cts   : register(b0);

Texture2D<float4> Texture       : register(t0);

[RootSignature(FullScreenQuadRS)]
float4 main(Interpolators In) : SV_Target0
{
    
    float4 color = 0;
    uint2 screenCoords = uint2(floor(In.Position.x - 0.5f), floor(In.Position.y - 0.5f));
    uint2 texCoords = screenCoords + uint2(cts.scrollX, cts.scrollY);

    if (texCoords.x < cts.width && texCoords.y < cts.height)
    {
        int3 param = int3(texCoords.x, texCoords.y, 0u);
        color = Texture.Load(param);
    }
    
    return color;
}
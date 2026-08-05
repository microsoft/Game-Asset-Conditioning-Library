//--------------------------------------------------------------------------------------
// FullScreenQuad.hlsli
//
// Common shader code to draw a full-screen quad
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//--------------------------------------------------------------------------------------

#pragma once

#define FullScreenQuadRS \
    "RootConstants ( num32BitConstants = 4, b0 )," \
    "DescriptorTable ( SRV(t0), visibility = SHADER_VISIBILITY_PIXEL )"\

struct Interpolators
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};
//*********************************************************
//
// Copyright 2020 Intel Corporation 
//
// Permission is hereby granted, free of charge, to any 
// person obtaining a copy of this software and associated 
// documentation files(the "Software"), to deal in the Software 
// without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions :
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
// DEALINGS IN THE SOFTWARE.
//
//*********************************************************

#include "GetLodVisualizationColor.h"

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

cbuffer cb0
{
    float4 g_viewPosition;
    int g_bufferWidth;
    int g_bufferHeight;
    int g_rowPitch;
    int g_offset;
};

Buffer<uint> g_buffer2D : register(t0);
SamplerState g_sampler : register(s0);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
VS_OUT vs(uint vertexID : SV_VertexID)
{
    VS_OUT output;

    vertexID &= 3;
    // normalized screen space is [-1,-1] [1,1], so everything is doubled
    // this forms a rect 0,0 2,0 0,2 2,2
    float2 grid = float2((vertexID & 1) << 1, vertexID & 2);

    float width = g_viewPosition.z;
    float height = -g_viewPosition.w;

    // scale and shift window to bottom left of screen
    output.pos = float4(grid * float2(width, height) + float2(-1.0f, -1.0), 0.0f, 1.0f);
    output.pos.xy += g_viewPosition.xy;

    // uv from 0,0 to 1,1
    output.uv.xy = 0.5f * grid;
    return output;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float4 ps(VS_OUT input) : SV_Target
{
    int2 uv = input.uv * float2(g_bufferWidth, g_bufferHeight);
    uint index = g_offset + uv.x + (uv.y * g_rowPitch);
    uint mipLevel = g_buffer2D.Load(index);

    float3 diffuse = GetLodVisualizationColor(mipLevel);

    return float4(diffuse, 1.0f);
}

Texture2D<uint> g_texture2D : register(t0);

float4 psTexture(VS_OUT input) : SV_Target
{
    int2 uv = input.uv * float2(g_bufferWidth, g_bufferHeight);

    uint mipLevel = g_texture2D.Load(int3(uv, 0));
    float3 diffuse = GetLodVisualizationColor(mipLevel);

    return float4(diffuse.xyz, 1.0f);
}

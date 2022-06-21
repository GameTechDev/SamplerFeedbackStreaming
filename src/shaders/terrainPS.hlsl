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

#include "terrainVS.hlsl"
#include "GetLodVisualizationColor.h"

Texture2D g_streamingTexture : register(t0);

Buffer<uint> g_minmipmap: register(t1);

SamplerState g_sampler : register(s0);

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
float3 evaluateLight(in float3 normal, in float3 eyeToPoint)
{
    float3 reflected = reflect(eyeToPoint, normal);

    // directional light
    float3 pointToLight = g_lightDir.xyz;

    float diffuse = saturate(dot(pointToLight, normal));

    float specDot = saturate(dot(reflected, pointToLight));
    float specular = pow(specDot, 2 * g_lightColor.a);

    float ambient = 0.1f;

    float3 color = (diffuse * g_lightColor.xyz) + (specular * g_specularColor.xyz) + ambient;

    color = pow(color, 1.0f / 2.0f);

    return color;
}

//-----------------------------------------------------------------------------
// shader
//-----------------------------------------------------------------------------
float4 ps(VS_OUT input) : SV_TARGET0
{
    // the CPU provides a buffer that describes the min mip that has been
    // mapped into the streaming texture.
    int2 uv = input.tex * g_minmipmapDim;
    uint index = g_minmipmapOffset + uv.x + (uv.y * g_minmipmapDim.x);
    uint mipLevel = g_minmipmap.Load(index);

    // clamp the streaming texture to the mip level specified in the min mip map
    float3 color = g_streamingTexture.Sample(g_sampler, input.tex, 0, mipLevel).rgb;

    float3 eyeToPoint = normalize(input.eyeToPoint);
    color *= evaluateLight(input.normal, eyeToPoint);

    // returns 0xff if no associated min mip, that is, no texel was touched last frame
    if ((g_visualizeFeedback) && (mipLevel < 16))
    {
        color = lerp(color, GetLodVisualizationColor(mipLevel), 0.3f);
    }

    return float4(color, 1.0f);
}

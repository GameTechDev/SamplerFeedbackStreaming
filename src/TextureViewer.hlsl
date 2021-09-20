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

struct VS_OUT
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD0;
	nointerpolation float mipLevel: OUTPUT;
};

cbuffer cb0
{
	float4 g_viewPosition;
	float g_gap;
	float g_visBaseMip;
	bool g_vertical;
};

VS_OUT vs(uint vertexID : SV_VertexID)
{
	VS_OUT output;

	float level = vertexID >> 2;
	vertexID &= 3;
	// normalized screen space is [-1,-1] [1,1], so everything is doubled
	// this forms a rect 0,0 2,0 0,2 2,2
	float2 grid = float2((vertexID & 1) << 1, vertexID & 2);

	float width = g_viewPosition.z;
	float height = -g_viewPosition.w;

	// scale and shift window to bottom left of screen
	output.pos = float4(grid * float2(width,height) + float2(-1.0f, -1.0), 0.0f, 1.0f);

	// horizontal or vertical arrangement
	if (g_vertical)
	{
		height -= g_gap;

		output.pos.x += g_viewPosition.x;
		output.pos.y += g_viewPosition.y - (height * level * 2);
	}
	else
	{
		width += g_gap;

		output.pos.x += g_viewPosition.x + (width * level * 2);
		output.pos.y += g_viewPosition.y;
	}

	// uv from 0,0 to 1,1
	output.uv.xy = 0.5f * grid;
	output.mipLevel = g_visBaseMip + level;
	return output;
}

Texture2D g_texture2D : register(t0);
SamplerState g_sampler : register(s0);

float4 ps(VS_OUT input) : SV_Target
{
	float4 diffuse = g_texture2D.SampleLevel(g_sampler, input.uv.xy, input.mipLevel);

    return float4(diffuse.xyz, 1.0f);
}

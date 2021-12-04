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

//-------------------------------------------------------------------------
// Constant buffers
//-------------------------------------------------------------------------
cbuffer ModelConstantData : register(b1)
{
	float4x4    g_combinedTransform;
	float4x4    g_worldTransform;
	float4 g_eyePos;

	int2   g_minmipmapDim;
	int    g_minmipmapOffset;
};

cbuffer FrameConstantData : register(b0)
{
	float4x4 g_view;
	float4 g_lightDir;
	float4 g_lightColor;    // RGB + specular intensity
	float4 g_specularColor;
	bool g_visualizeFeedback;
};

//-------------------------------------------------------------------------
// draw the scene
//-------------------------------------------------------------------------

struct VS_IN
{
	float3 pos        : POS;
	float3 normal     : NORMAL;
	float2 tex        : TEX;
};

struct VS_OUT
{
	float4 pos        : SV_POSITION;
	float3 normal	  : NORMAL;
	float3 eyeToPoint : EYETOPOINT;
	float2 tex        : TEX;
};

VS_OUT vs(VS_IN input)
{
	VS_OUT result;
	result.pos = mul(g_combinedTransform, float4(input.pos, 1.0f));
	
	// rotate normal into light coordinate frame (world)
	result.normal = normalize(mul((float3x3)g_worldTransform, input.normal));

	// transform position into light coordinate frame (world)
	float3 pos = mul(g_worldTransform, float4(input.pos, 1.0f)).xyz;

	// direction from eye to pos
	result.eyeToPoint = normalize(pos - g_eyePos.xyz);

	result.tex = input.tex;
	return result;
}

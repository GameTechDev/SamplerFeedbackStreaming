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

#ifdef __cplusplus
#pragma once
struct float3
{
    float v[3];
    float3(float x, float y, float z) { v[0] = x; v[1] = y; v[2] = z; }
    float operator[](int i) { return v[i]; }
};
inline float3 GetLodVisualizationColor(int in_level)
#else
float3 GetLodVisualizationColor(int in_level)
#endif

//-----------------------------------------------------------------------------
// colors to visualize mip usage
//-----------------------------------------------------------------------------
{
    static float3 lut[] =
    {
        { 1, 1, 1 },             // white
        { 1, 0.25f, 0.25f },     // light red
        { 0.25f, 1, 0.25f },     // light green
        { 0.25f, 0.25f, 1 },     // light blue

        { 1, 0.25f, 1 },         // light magenta
        { 1, 1, 0.25f },         // light yellow
        { 0.25f, 1, 1 },         // light cyan
        { 0.9f, 0.5f, 0.2f },    // orange

        { 0.59f, 0.48f, 0.8f },  // dark magenta
        {0.53f, 0.25f, 0.11f }, 
        {0.8f, 0.48f, 0.53f},
        {0.64f, 0.8f, 0.48f },

        {0.48f, 0.75f, 0.8f },
        { 0.5f, 0.25f, 0.75f },
        { 0.99f, 0.68f, 0.42f },
        { .4f, .5f, .6f },
    };
    if (in_level > 15)
    {
        return float3(.3f, .4f, .2f); // olive-ish green
    }
    else
    {
        return lut[in_level];
    }
}

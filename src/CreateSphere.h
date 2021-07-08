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

#pragma once

#include "pch.h"

#include <vector>
#include <intsafe.h>
#include <DirectXMath.h>

#include "TerrainGenerator.h" // for the shared vertex structure

//===========================================================
// Creates a sphere with u/v such that the image is mirrored in u
// more triangles are spent on the poles for a smooth curve
//===========================================================
namespace SphereGen
{
    using Vertex = TerrainGenerator::Vertex;

    struct Properties
    {
        UINT m_numLong{ 128 }; // must be even, or there will be a discontinuity in texture coordinates
        UINT m_numLat{ 111 }; // must be odd, so there's an equator
        float m_exponent{ 3.14159265358979f }; // exponential function puts more triangles at the poles
        bool m_mirrorU{ true }; // u goes 0..1..0 if true. if false, u = {0..1}
    };

    inline void Create(std::vector<Vertex>& out_vertices, std::vector<UINT32>& out_indices, const Properties* in_pProperties = nullptr)
    {
        Properties sphereProperties;
        if (in_pProperties)
        {
            sphereProperties = *in_pProperties;
            sphereProperties.m_numLong &= ~1; // force even
            sphereProperties.m_numLat |= 1;  // force odd
        }

        std::vector<Vertex> vertices;

        // compute sphere vertices
        // skip top and bottom rows (will just use single points)
        {
            float dLat = DirectX::XM_PI * 2.0f / sphereProperties.m_numLong; // the number of vertical segments tells you how many horizontal steps to take
            float dz = 1.0f / (sphereProperties.m_numLat - 1); // e.g. NumLat = 3, dv = 0.5, v = {0, .5, 1}

            // du is uniform
            float du = 1.0f / float(sphereProperties.m_numLong); // u from 0..1
            if (sphereProperties.m_mirrorU)               // when mirroring, u from 0..2
            {
                du *= 2;
            }

            for (UINT longitude = 1; longitude < (sphereProperties.m_numLat - 1); longitude++)
            {
                float z = dz * longitude;

                // logarithmic step in z produces smoother poles
                if (z < 0.5f)
                {
                    z = -1 + std::powf(2 * z, sphereProperties.m_exponent);
                }
                else
                {
                    z = 1 - std::powf(2 - (2 * z), sphereProperties.m_exponent);
                }

                // radius of this latitude, just in x/y plane
                float r = std::sqrtf(1 - (z * z));

                // v is constant for this latitude
                float v = std::acosf(-z) / DirectX::XM_PI;

                // start with u=0. last column repeats the first but with u = 1.0.
                DirectX::XMFLOAT3 pos0{};
                for (UINT lat = 0; lat <= sphereProperties.m_numLong; lat++)
                {
                    DirectX::XMFLOAT3 pos;
                    DirectX::XMFLOAT2 uv;
                    if (lat == sphereProperties.m_numLong)
                    {
                        // repeat first position exactly
                        pos = pos0;
                        // exactly re-start uv
                        uv = DirectX::XMFLOAT2{ 0, v };
                        if (!sphereProperties.m_mirrorU)
                        {
                            uv = DirectX::XMFLOAT2{ 1.0f, v };
                        }
                    }
                    else
                    {
                        float x = r * std::cosf(lat * dLat);
                        float y = r * std::sinf(lat * dLat);
                        pos = DirectX::XMFLOAT3(x, y, z);

                        // remember so we can repeat first position exactly
                        if (0 == lat) pos0 = pos;

                        // mirror in u? 0..1..0
                        float u = lat * du;
                        if (sphereProperties.m_mirrorU && (u > 1))
                        {
                            u = 2 - u;
                        }

                        uv = DirectX::XMFLOAT2{ u, v };
                    }
                    out_vertices.push_back(Vertex{ pos, pos, uv });
                } // end loops around latitude
            } // end vertical steps
        }

        UINT numRows = sphereProperties.m_numLat - 3; // skip top and bottom
        UINT vertsPerRow = sphereProperties.m_numLong + 1;

        // generate connectivity to form rows of triangles
        {
            for (UINT v = 0; v < numRows; v++)
            {
                UINT base = v * vertsPerRow;
                for (UINT i = 0; i < sphereProperties.m_numLong; i += 1)
                {
                    out_indices.push_back(base + i);
                    out_indices.push_back(base + i + vertsPerRow + 1);
                    out_indices.push_back(base + i + vertsPerRow);

                    out_indices.push_back(base + i);
                    out_indices.push_back(base + i + 1);
                    out_indices.push_back(base + i + vertsPerRow + 1);
                }
            }
        }
        // create top and bottom caps
        // this version creates many little top triangles, each connected to a different "pole" vertex
        // the pole vertices have a u value that equals one of the two base triangles
        // this brings everything to a neat point
        {
            // smooth slight pointiness at poles
            float southZ = (out_vertices[0].pos.z - 1.0f) / 2.f;
            float northZ = (out_vertices[vertsPerRow * numRows].pos.z + 1.0f) / 2.f;

            for (UINT i = 0; i < sphereProperties.m_numLong; i++)
            {
                out_indices.push_back((UINT)out_vertices.size());
                DirectX::XMFLOAT2 uv(out_vertices[i].tex.x, 0);
                out_vertices.push_back(Vertex{ DirectX::XMFLOAT3(0,0,southZ), DirectX::XMFLOAT3(0,0,-1), uv });
                out_indices.push_back(i + 1);
                out_indices.push_back(i);

                out_indices.push_back((UINT)out_vertices.size());
                uv = DirectX::XMFLOAT2(out_vertices[(vertsPerRow * numRows) + i].tex.x, 1);
                out_vertices.push_back(Vertex{ DirectX::XMFLOAT3(0,0,northZ), DirectX::XMFLOAT3(0,0,1), uv });
                out_indices.push_back((vertsPerRow * numRows) + i);
                out_indices.push_back((vertsPerRow * numRows) + i + 1);
            }
        }
    }
};

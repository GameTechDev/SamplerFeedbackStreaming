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

#include "pch.h"

#include <random>
#include "TerrainGenerator.h"

using namespace DirectX;

//-----------------------------------------------------------------------------
// Setup random lattice
//-----------------------------------------------------------------------------
TerrainGenerator::TerrainGenerator(const CommandLineArgs& in_args) :
    m_args(in_args)
{
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    UINT gridSize = m_args.m_terrainSideSize * m_args.m_terrainSideSize;

    m_noiseLattice.resize(gridSize);
    for (UINT i = 0; i < gridSize; i++)
    {
        XMVECTOR vec = XMVectorSet(dis(gen), dis(gen), 0, 0);
        vec = XMVector2Normalize(vec);
        XMStoreFloat2(&m_noiseLattice[i], vec);
    }

    m_vertices.resize(gridSize);
    GenerateVertices();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
DirectX::XMFLOAT2 TerrainGenerator::ReadLattice(int2 location)
{
    UINT sampleX = ((UINT)location.x) % m_args.m_terrainSideSize;
    UINT sampleY = ((UINT)location.y) % m_args.m_terrainSideSize;
    assert(sampleY * m_args.m_terrainSideSize + sampleX < m_args.m_terrainSideSize* m_args.m_terrainSideSize);
    UINT index = (sampleY * m_args.m_terrainSideSize) + sampleX;
    return m_noiseLattice[index];
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float TerrainGenerator::Noise(XMFLOAT2 scaledLocation)
{
    //
    // Determine integer sample locations
    //
    int2 location00 = int2(static_cast<int>(floor(scaledLocation.x)), static_cast<int>(floor(scaledLocation.y)));

    //
    // Sample 4 random vectors
    //
    XMFLOAT2 grad00 = ReadLattice(location00 + int2(0, 0));
    XMFLOAT2 grad01 = ReadLattice(location00 + int2(0, 1));
    XMFLOAT2 grad10 = ReadLattice(location00 + int2(1, 0));
    XMFLOAT2 grad11 = ReadLattice(location00 + int2(1, 1));

    //
    // Compute vectors from 4 corners to sample point
    //
    XMFLOAT2 vec00, vec01, vec10, vec11;

    vec00.x = scaledLocation.x - float(location00.x + 0);
    vec00.y = scaledLocation.y - float(location00.y + 0);

    vec01.x = scaledLocation.x - float(location00.x + 0);
    vec01.y = scaledLocation.y - float(location00.y + 1);

    vec10.x = scaledLocation.x - float(location00.x + 1);
    vec10.y = scaledLocation.y - float(location00.y + 0);

    vec11.x = scaledLocation.x - float(location00.x + 1);
    vec11.y = scaledLocation.y - float(location00.y + 1);

    //
    // Compute dot products
    //
    float g00 = Dot(grad00, vec00);
    float g01 = Dot(grad01, vec01);
    float g10 = Dot(grad10, vec10);
    float g11 = Dot(grad11, vec11);

    //
    // Bilinear interpolation
    //
    float a = Bilinear(g00, g10, scaledLocation.x - float(location00.x));
    float b = Bilinear(g01, g11, scaledLocation.x - float(location00.x));

    float noise = Bilinear(a, b, scaledLocation.y - float(location00.y));

    return noise;
}

//-----------------------------------------------------------------------------
// Compute vertex buffer for terrain that is a 2D grid.
// The computed terrain spans [0,1] in texture coordinates (inclusive on edges).
// The array pointed to by pResult is assumed to have m_numVertices elements, which are filled in by valid data by this function.
//-----------------------------------------------------------------------------
void TerrainGenerator::GenerateVertices()
{
    float minDimension = -100.0f;
    float maxDimension = 100.0f;

    const float frac = 1.0f / static_cast<float>(m_args.m_terrainSideSize - 1);

    concurrency::parallel_for(UINT(0), m_args.m_terrainSideSize, [&](UINT y)
    {
        for (UINT x = 0; x < m_args.m_terrainSideSize; x++)
        {
            Vertex& vtx = m_vertices[y * m_args.m_terrainSideSize + x];

            vtx.pos.x = Lerp(minDimension, maxDimension, (float)x / (m_args.m_terrainSideSize - 1));   // x,y = minDim..maxDim evenly distributed
            vtx.pos.z = Lerp(minDimension, maxDimension, (float)y / (m_args.m_terrainSideSize - 1));

            float height = 0.0f;

            for (UINT i = 0; i < m_args.m_numOctaves; i++)
            {
                float coordModulate = float(1 << i);
                float heightModulate = 1.0f / (coordModulate * coordModulate);

                XMFLOAT2 noiseCoordinates = XMFLOAT2(
                    vtx.pos.x * coordModulate / m_args.m_noiseScale,
                    vtx.pos.z * coordModulate / m_args.m_noiseScale);

                height += Noise(noiseCoordinates) * heightModulate;
            }

            //
            // Modulate height by a Gaussian to create a central mountain
            //
            float distanceFromCenter = sqrtf(vtx.pos.x * vtx.pos.x + vtx.pos.z * vtx.pos.z);

            vtx.pos.y = m_args.m_heightScale * height * Gaussian(distanceFromCenter, m_args.m_mountainSize);

            vtx.tex.x = x * frac;     // u,v = 0.0..1.0 evenly distributed
            vtx.tex.y = y * frac;

            // FIXME? the terrain is shown upside down!
            vtx.tex.x = 1.f - vtx.tex.x;
        }
    });

    //
    // Compute normals
    //

    // Each vertex is surrounded by six triangles (except at edges).
    // Compute smooth normal as the average of these connected triangle's face normals.
    // We do this by looping over quads, and for each of its two triangles,
    // compute face normal and accumulate to each of the triangle's three vertices.
    std::vector<XMVECTOR> normals(
        m_args.m_terrainSideSize * m_args.m_terrainSideSize,
        XMVectorSet(0.f, 0.f, 0.f, 0.f));

    concurrency::parallel_for(UINT(0), m_args.m_terrainSideSize - 1, [&](UINT y)
    {
        for (UINT x = 0; x < m_args.m_terrainSideSize - 1; x++)
        {
            UINT vtx0 = (y * m_args.m_terrainSideSize) + x;

            // Upper triangle (CCW)
            {
                UINT vtx2 = vtx0 + 1;
                UINT vtx1 = vtx0 + 1 + m_args.m_terrainSideSize;
                XMVECTOR n = ComputeNormal(vtx0, vtx2, vtx1);
                normals[vtx0] += n;
                normals[vtx1] += n;
                normals[vtx2] += n;
            }

            // Lower triangle (CCW)
            {
                UINT vtx2 = vtx0 + 1 + m_args.m_terrainSideSize;
                UINT vtx1 = vtx0 + m_args.m_terrainSideSize;
                XMVECTOR n = ComputeNormal(vtx0, vtx2, vtx1);
                normals[vtx0] += n;
                normals[vtx1] += n;
                normals[vtx2] += n;
            }
        }
    });

    for (size_t i = 0; i < normals.size(); i++)
    {
        XMVECTOR n = XMVector3Normalize(normals[i]);
        XMStoreFloat3(&m_vertices[i].normal, n);
    }
}

//-----------------------------------------------------------------------------
// Compute index buffer for terrain that is a 2D grid.
// If (0,0) is the top-left vertex, the generated triangles are CCW.
//-----------------------------------------------------------------------------
void TerrainGenerator::GenerateIndices(UINT* pResult)
{
    UINT outIdx = 0;
    UINT topLeftIdx = 0;

    for (UINT y = 0; y < m_args.m_terrainSideSize - 1; y++)
    {
        // reset to beginning of line
        topLeftIdx = y * m_args.m_terrainSideSize;
        for (UINT x = 0; x < m_args.m_terrainSideSize - 1; x++)
        {
            pResult[outIdx + 0] = topLeftIdx;
            pResult[outIdx + 2] = topLeftIdx + 1;
            pResult[outIdx + 1] = topLeftIdx + 1 + m_args.m_terrainSideSize;

            pResult[outIdx + 3] = topLeftIdx;
            pResult[outIdx + 5] = topLeftIdx + 1 + m_args.m_terrainSideSize;
            pResult[outIdx + 4] = topLeftIdx + m_args.m_terrainSideSize;

            outIdx += 6;
            topLeftIdx++;
        }
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
XMVECTOR TerrainGenerator::ComputeNormal(UINT vtx0, UINT vtx1, UINT vtx2) const
{
    XMVECTOR pos0 = XMLoadFloat3(&m_vertices[vtx0].pos);
    XMVECTOR pos1 = XMLoadFloat3(&m_vertices[vtx1].pos);
    XMVECTOR pos2 = XMLoadFloat3(&m_vertices[vtx2].pos);

    XMVECTOR v10 = XMVectorSubtract(pos1, pos0);
    XMVECTOR v20 = XMVectorSubtract(pos2, pos0);
    return XMVector3Normalize(XMVector3Cross(v20, v10));
}

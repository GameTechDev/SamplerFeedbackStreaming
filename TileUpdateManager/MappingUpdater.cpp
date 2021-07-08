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
#include "MappingUpdater.h"

std::vector<D3D12_TILE_RANGE_FLAGS> Streaming::MappingUpdater::m_rangeFlagsMap;   // all NONE
std::vector<D3D12_TILE_RANGE_FLAGS> Streaming::MappingUpdater::m_rangeFlagsUnMap; // all NULL
std::vector<UINT> Streaming::MappingUpdater::m_rangeTileCounts; // all 1s

//=============================================================================
// Internal class that constructs commands that set
// virtual-to-physical mapping for the reserved resource
//=============================================================================
Streaming::MappingUpdater::MappingUpdater(UINT in_maxTileMappingUpdatesPerApiCall) :
    m_maxTileMappingUpdatesPerApiCall(std::max(UINT(1), in_maxTileMappingUpdatesPerApiCall))
{
    // paranoia: make sure static arrays are sized to the maximum of requested sizes
    UINT size = std::max(m_maxTileMappingUpdatesPerApiCall, (UINT)m_rangeTileCounts.size());

    // these will never change size
    m_rangeFlagsMap.assign(size, D3D12_TILE_RANGE_FLAG_NONE);
    m_rangeFlagsUnMap.assign(size, D3D12_TILE_RANGE_FLAG_NULL);
    m_rangeTileCounts.assign(size, 1);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::MappingUpdater::Map(ID3D12CommandQueue* in_pCommandQueue, ID3D12Resource* in_pResource, ID3D12Heap* in_pHeap,
    const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords,
    const std::vector<UINT>& in_indices)
{
    ASSERT(in_coords.size() == in_indices.size());

    UINT numTotal = (UINT)in_coords.size();
    while (numTotal)
    {
        UINT numRegions = std::min(numTotal, m_maxTileMappingUpdatesPerApiCall);
        numTotal -= numRegions;

        in_pCommandQueue->UpdateTileMappings(
            in_pResource,
            numRegions,
            &in_coords[numTotal],
            nullptr,
            in_pHeap,
            numRegions,
            m_rangeFlagsMap.data(),
            &in_indices[numTotal],
            m_rangeTileCounts.data(),
            D3D12_TILE_MAPPING_FLAG_NONE
        );
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::MappingUpdater::UnMap(ID3D12CommandQueue* in_pCommandQueue, ID3D12Resource* in_pResource,
    const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords)
{
    UINT numTotal = (UINT)in_coords.size();
    while (numTotal)
    {
        UINT numRegions = std::min(numTotal, m_maxTileMappingUpdatesPerApiCall);
        numTotal -= numRegions;

        in_pCommandQueue->UpdateTileMappings(
            in_pResource,
            numRegions,
            &in_coords[numTotal],
            nullptr,
            nullptr,
            numRegions,
            m_rangeFlagsUnMap.data(),
            nullptr,
            m_rangeTileCounts.data(),
            D3D12_TILE_MAPPING_FLAG_NONE
        );
    }
}

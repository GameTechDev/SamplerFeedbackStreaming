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

#include "Streaming.h"

//==================================================
// MappingUpdater updates a reserved resource via UpdateTileMappings
// there are 2 kinds of updates: add and remove
// initialize an updater corresponding to each type
// now, all that is really added is coordinates.
//==================================================
namespace Streaming
{
    class MappingUpdater
    {
    public:
        MappingUpdater(UINT in_maxTileMappingUpdatesPerApiCall);

        void Map(ID3D12CommandQueue* in_pCommandQueue, ID3D12Resource* in_pResource, ID3D12Heap* in_pHeap,
            const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords,
            const std::vector<UINT>& in_indices);

        void UnMap(ID3D12CommandQueue* in_pCommandQueue, ID3D12Resource* in_pResource,
            const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords);

        UINT GetMaxTileMappingUpdatesPerApiCall() const { return m_maxTileMappingUpdatesPerApiCall; }
    private:
        const UINT m_maxTileMappingUpdatesPerApiCall;

        static std::vector<D3D12_TILE_RANGE_FLAGS> m_rangeFlagsMap;   // all NONE
        static std::vector<D3D12_TILE_RANGE_FLAGS> m_rangeFlagsUnMap; // all NULL
        static std::vector<UINT> m_rangeTileCounts; // all 1s
    };
}

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


#include <memory>      // for unique_ptr

#include "Streaming.h" // for ComPtr
#include "HeapAllocator.h"

//==================================================
// Streaming Heap wraps the D3D heap, HeapAllocator, and Atlas
//==================================================
namespace Streaming
{
    // 1 or more aliased write-only resources to cover a heap
    class Atlas
    {
    public:
        Atlas(ID3D12Heap* in_pHeap, ID3D12CommandQueue* in_pQueue, UINT in_numTilesHeap, DXGI_FORMAT in_format);

        // return a resource pointer and a coordinate into that resource from linear tile index
        ID3D12Resource* ComputeCoordFromTileIndex(D3D12_TILED_RESOURCE_COORDINATE& out_coord, UINT in_index);

        DXGI_FORMAT GetFormat() const { return m_format; }
    private:
        const DXGI_FORMAT m_format;

        std::vector<D3D12_SUBRESOURCE_TILING> m_atlasTiling;
        std::vector<Streaming::ComPtr<ID3D12Resource>> m_atlases;
        UINT m_numTilesPerAtlas{ 0 };
        const UINT m_atlasNumTiles;

        // returns number of tiles covered by the atlas
        UINT CreateAtlas(ComPtr<ID3D12Resource>& out_pDst,
            ID3D12Heap* in_pHeap, ID3D12CommandQueue* in_pQueue,
            DXGI_FORMAT in_format, UINT in_maxTiles, UINT in_tileOffset);
    };

    // Heap to hold tiles for 1 or more resources
    // contains atlases for the format(s) of the resources
    class Heap
    {
    public:
        Heap(ID3D12CommandQueue* in_pQueue, UINT in_maxNumTilesHeap);
        ~Heap();

        // allocate atlases for a format. does nothing if format already has an atlas
        void AllocateAtlas(const DXGI_FORMAT in_format);

        ID3D12Resource* ComputeCoordFromTileIndex(D3D12_TILED_RESOURCE_COORDINATE& out_coord, UINT in_index, const DXGI_FORMAT in_format);
        ID3D12Heap* GetHeap() const { return m_tileHeap.Get(); }
        HeapAllocator& GetAllocator() { return m_heapAllocator; }
    private:
        HeapAllocator m_heapAllocator;
        std::vector<Streaming::Atlas*> m_atlases;
        ComPtr<ID3D12Heap> m_tileHeap; // heap to hold tiles resident in GPU memory
        ComPtr<ID3D12CommandQueue> m_queue; // queue for atlases to use to initialize mappings
    };
}
